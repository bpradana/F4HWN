/* Copyright 2025
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "app/aprs.h"

#include <math.h>
#include <string.h>

#include "audio.h"
#include "driver/bk4819.h"
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

#define APRS_SAMPLE_BLOCK_MAX 32
#define APRS_FRAME_MAX 330
#define APRS_AX25_ADDR_LEN 7
#define APRS_BAUD 1200U
#define APRS_MARK_HZ 1200.0f
#define APRS_SPACE_HZ 2200.0f

typedef struct APRS_DemodState_t {
    uint32_t sample_rate;
    uint16_t samples_per_symbol;
    uint16_t sample_index;
    int16_t sample_buffer[APRS_SAMPLE_BLOCK_MAX];
    bool last_tone_mark;
    bool have_last_tone;
    uint8_t ones_count;
    uint8_t bit_shift;
    bool in_frame;
    uint8_t bit_index;
    uint8_t current_byte;
    uint8_t frame[APRS_FRAME_MAX];
    uint16_t frame_len;
    int16_t last_rssi;
} APRS_DemodState_t;

static APRS_State_t gAprsState = APRS_READY;
static APRS_Message_t gAprsMessages[APRS_MAX_MESSAGES];
static uint8_t gAprsMessageHead;
static uint8_t gAprsMessageCount;
static uint32_t gAprsTimestampMs;
static APRS_DemodState_t gAprsDemod;
static uint8_t gAprsSelectedIndex;
static bool gAprsDetailView;
static bool gAprsInputActive;
static BK4819_AF_Type_t gAprsPrevAfMode;
static BK4819_FilterBandwidth_t gAprsPrevBandwidth;
static bool gAprsPrevAudioPathOn;
static bool gAprsPrevSettingsValid;

static BK4819_AF_Type_t APRS_GetCurrentAfMode(void)
{
    if (gCurrentVfo == NULL) {
        return BK4819_AF_FM;
    }

    switch (gCurrentVfo->Modulation) {
        default:
        case MODULATION_FM:
            return BK4819_AF_FM;
        case MODULATION_AM:
            return BK4819_AF_FM;
        case MODULATION_USB:
            return BK4819_AF_BASEBAND2;
#ifdef ENABLE_BYP_RAW_DEMODULATORS
        case MODULATION_BYP:
            return BK4819_AF_UNKNOWN3;
        case MODULATION_RAW:
            return BK4819_AF_BASEBAND1;
#endif
    }
}

static BK4819_FilterBandwidth_t APRS_GetCurrentFilterBandwidth(void)
{
    BK4819_FilterBandwidth_t bandwidth = BK4819_FILTER_BW_WIDE;

    if (gRxVfo != NULL) {
        bandwidth = gRxVfo->CHANNEL_BANDWIDTH;

#ifdef ENABLE_FEAT_F4HWN_NARROWER
        if (bandwidth == BK4819_FILTER_BW_NARROW && gSetting_set_nfm == 1) {
            bandwidth = BK4819_FILTER_BW_NARROWER;
        }
#endif

        if (gRxVfo->Modulation == MODULATION_AM) {
            bandwidth = BK4819_FILTER_BW_AM;
        }
    }

    return bandwidth;
}

static void APRS_ResetDemod(void)
{
    memset(&gAprsDemod, 0, sizeof(gAprsDemod));
    gAprsDemod.samples_per_symbol = 8;
}

static void APRS_AddMessage(const char *source, const char *destination, const char *payload, int16_t rssi)
{
    APRS_Message_t *slot = &gAprsMessages[gAprsMessageHead];

    gAprsTimestampMs += 100;
    slot->timestamp_ms = gAprsTimestampMs;
    strncpy(slot->source, source, sizeof(slot->source) - 1U);
    slot->source[sizeof(slot->source) - 1U] = '\0';
    strncpy(slot->destination, destination, sizeof(slot->destination) - 1U);
    slot->destination[sizeof(slot->destination) - 1U] = '\0';
    strncpy(slot->payload, payload, sizeof(slot->payload) - 1U);
    slot->payload[sizeof(slot->payload) - 1U] = '\0';
    slot->rssi = rssi;

    gAprsMessageHead = (gAprsMessageHead + 1U) % APRS_MAX_MESSAGES;
    if (gAprsMessageCount < APRS_MAX_MESSAGES) {
        gAprsMessageCount++;
    }

    if (gAprsMessageCount == 1U) {
        gAprsSelectedIndex = 0;
    }
}

static float APRS_GoertzelPower(const int16_t *samples, uint16_t count, float target_hz, uint32_t sample_rate)
{
    if (count == 0 || sample_rate == 0) {
        return 0.0f;
    }

    const float k = 0.5f + ((float)count * target_hz / (float)sample_rate);
    const float omega = (2.0f * 3.14159265f * k) / (float)count;
    const float coeff = 2.0f * cosf(omega);
    float q0 = 0.0f;
    float q1 = 0.0f;
    float q2 = 0.0f;

    for (uint16_t i = 0; i < count; i++) {
        q0 = coeff * q1 - q2 + (float)samples[i];
        q2 = q1;
        q1 = q0;
    }

    return (q1 * q1) + (q2 * q2) - (q1 * q2 * coeff);
}

static uint16_t APRS_CRC16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF;

    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x0001U) {
                crc = (crc >> 1) ^ 0x8408U;
            } else {
                crc >>= 1;
            }
        }
    }

    return (uint16_t)~crc;
}

static void APRS_FormatCallsign(const uint8_t *addr, char *out, size_t out_len)
{
    size_t pos = 0;

    for (uint8_t i = 0; i < 6; i++) {
        char c = (char)(addr[i] >> 1);
        if (c == ' ' || c == '\0') {
            break;
        }
        if (pos + 1U < out_len) {
            out[pos++] = c;
        }
    }

    uint8_t ssid = (addr[6] >> 1) & 0x0F;
    if (ssid != 0 && pos + 2U < out_len) {
        out[pos++] = '-';
        if (ssid >= 10U && pos + 1U < out_len) {
            out[pos++] = (char)('0' + (ssid / 10U));
        }
        if (pos + 1U < out_len) {
            out[pos++] = (char)('0' + (ssid % 10U));
        }
    }

    if (out_len > 0) {
        out[(pos < out_len) ? pos : (out_len - 1U)] = '\0';
    }
}

static void APRS_ParseFrame(const uint8_t *frame, uint16_t length, int16_t rssi)
{
    if (length < (APRS_AX25_ADDR_LEN * 2U) + 2U) {
        return;
    }

    uint16_t fcs = (uint16_t)frame[length - 2U] | ((uint16_t)frame[length - 1U] << 8);
    uint16_t crc = APRS_CRC16(frame, length - 2U);

    if (crc != fcs) {
        return;
    }

    char destination[APRS_CALLSIGN_LEN] = {0};
    char source[APRS_CALLSIGN_LEN] = {0};

    APRS_FormatCallsign(frame, destination, sizeof(destination));
    APRS_FormatCallsign(frame + APRS_AX25_ADDR_LEN, source, sizeof(source));

    uint16_t offset = 0;
    while (true) {
        if (offset + APRS_AX25_ADDR_LEN > length) {
            return;
        }
        if (frame[offset + 6U] & 0x01U) {
            offset += APRS_AX25_ADDR_LEN;
            break;
        }
        offset += APRS_AX25_ADDR_LEN;
    }

    if (offset + 2U > length) {
        return;
    }

    uint8_t control = frame[offset];
    uint8_t pid = frame[offset + 1U];
    uint16_t info_offset = offset + 2U;

    if (control != 0x03U || pid != 0xF0U) {
        return;
    }

    if (info_offset >= length - 2U) {
        return;
    }

    uint16_t payload_len = (length - 2U) - info_offset;
    if (payload_len >= APRS_PAYLOAD_LEN) {
        payload_len = APRS_PAYLOAD_LEN - 1U;
    }

    char payload[APRS_PAYLOAD_LEN];
    memcpy(payload, &frame[info_offset], payload_len);
    payload[payload_len] = '\0';

    APRS_AddMessage(source, destination, payload, rssi);
}

static void APRS_HandleFrameComplete(void)
{
    if (gAprsDemod.frame_len >= 2U) {
        APRS_ParseFrame(gAprsDemod.frame, gAprsDemod.frame_len, gAprsDemod.last_rssi);
    }
    gAprsDemod.frame_len = 0;
    gAprsDemod.bit_index = 0;
    gAprsDemod.current_byte = 0;
}

static void APRS_ProcessBit(uint8_t bit)
{
    gAprsDemod.bit_shift = (uint8_t)((gAprsDemod.bit_shift >> 1) | (bit ? 0x80U : 0x00U));

    if (gAprsDemod.bit_shift == 0x7EU) {
        if (gAprsDemod.in_frame && gAprsDemod.frame_len > 0U) {
            APRS_HandleFrameComplete();
        }
        gAprsDemod.in_frame = true;
        gAprsDemod.ones_count = 0;
        gAprsDemod.bit_index = 0;
        gAprsDemod.current_byte = 0;
        return;
    }

    if (!gAprsDemod.in_frame) {
        return;
    }

    if (bit) {
        gAprsDemod.ones_count++;
    } else {
        if (gAprsDemod.ones_count == 5U) {
            gAprsDemod.ones_count = 0;
            return;
        }
        gAprsDemod.ones_count = 0;
    }

    if (bit) {
        gAprsDemod.current_byte |= (uint8_t)(1U << gAprsDemod.bit_index);
    }
    gAprsDemod.bit_index++;

    if (gAprsDemod.bit_index >= 8U) {
        if (gAprsDemod.frame_len < APRS_FRAME_MAX) {
            gAprsDemod.frame[gAprsDemod.frame_len++] = gAprsDemod.current_byte;
        } else {
            gAprsDemod.in_frame = false;
        }
        gAprsDemod.bit_index = 0;
        gAprsDemod.current_byte = 0;
    }
}

static void APRS_ProcessSymbol(bool tone_mark)
{
    if (!gAprsDemod.have_last_tone) {
        gAprsDemod.last_tone_mark = tone_mark;
        gAprsDemod.have_last_tone = true;
        return;
    }

    uint8_t bit = (tone_mark == gAprsDemod.last_tone_mark) ? 1U : 0U;
    gAprsDemod.last_tone_mark = tone_mark;
    APRS_ProcessBit(bit);
}

void APRS_Init(void)
{
    gAprsState = APRS_READY;
    gAprsTimestampMs = 0;
    APRS_ClearMessages();
    APRS_ResetDemod();
    BK4819_SetRxAudioSampleCallback(APRS_OnAudioSamples);
}

void APRS_StartRx(void)
{
    gAprsPrevAfMode = APRS_GetCurrentAfMode();
    gAprsPrevBandwidth = APRS_GetCurrentFilterBandwidth();
    gAprsPrevAudioPathOn = gEnableSpeaker;
    gAprsPrevSettingsValid = true;

    gAprsState = APRS_RECEIVING;
    APRS_ResetDemod();
    BK4819_SetRxAudioSampleCallback(APRS_OnAudioSamples);
    BK4819_SetAF(BK4819_AF_FM);
    BK4819_SetFilterBandwidth(BK4819_FILTER_BW_WIDE, false);
    BK4819_RX_TurnOn();
    AUDIO_AudioPathOff();
}

void APRS_StopRx(void)
{
    gAprsState = APRS_READY;
    BK4819_SetRxAudioSampleCallback(NULL);

    if (gAprsPrevSettingsValid) {
        BK4819_SetAF(gAprsPrevAfMode);
#ifdef ENABLE_AM_FIX
        BK4819_SetFilterBandwidth(gAprsPrevBandwidth, gRxVfo != NULL && gRxVfo->Modulation == MODULATION_AM && gSetting_AM_fix);
#else
        BK4819_SetFilterBandwidth(gAprsPrevBandwidth, false);
#endif

        if (gAprsPrevAudioPathOn) {
            AUDIO_AudioPathOn();
        } else {
            AUDIO_AudioPathOff();
        }

        gAprsPrevSettingsValid = false;
    }
}

void APRS_OnAudioSamples(const int16_t *samples, uint16_t count, uint32_t sample_rate, int16_t rssi)
{
    if (gAprsState != APRS_RECEIVING || samples == NULL || count == 0U) {
        return;
    }

    if (sample_rate == 0U) {
        return;
    }

    gAprsDemod.last_rssi = rssi;

    if (gAprsDemod.sample_rate != sample_rate) {
        gAprsDemod.sample_rate = sample_rate;
        gAprsDemod.samples_per_symbol = (uint16_t)(sample_rate / APRS_BAUD);
        if (gAprsDemod.samples_per_symbol == 0U) {
            gAprsDemod.samples_per_symbol = 1U;
        }
        if (gAprsDemod.samples_per_symbol > APRS_SAMPLE_BLOCK_MAX) {
            gAprsDemod.samples_per_symbol = APRS_SAMPLE_BLOCK_MAX;
        }
        gAprsDemod.sample_index = 0;
    }

    for (uint16_t i = 0; i < count; i++) {
        gAprsDemod.sample_buffer[gAprsDemod.sample_index++] = samples[i];
        if (gAprsDemod.sample_index >= gAprsDemod.samples_per_symbol) {
            float mark_power = APRS_GoertzelPower(gAprsDemod.sample_buffer, gAprsDemod.samples_per_symbol, APRS_MARK_HZ, gAprsDemod.sample_rate);
            float space_power = APRS_GoertzelPower(gAprsDemod.sample_buffer, gAprsDemod.samples_per_symbol, APRS_SPACE_HZ, gAprsDemod.sample_rate);
            bool tone_mark = mark_power >= space_power;
            APRS_ProcessSymbol(tone_mark);
            gAprsDemod.sample_index = 0;
        }
    }
}

uint8_t APRS_GetMessageCount(void)
{
    return gAprsMessageCount;
}

bool APRS_GetMessage(uint8_t index, APRS_Message_t *out_message)
{
    if (index >= gAprsMessageCount || out_message == NULL) {
        return false;
    }

    uint8_t newest_index = (gAprsMessageHead + APRS_MAX_MESSAGES - 1U) % APRS_MAX_MESSAGES;
    uint8_t message_index = (uint8_t)((newest_index + APRS_MAX_MESSAGES - index) % APRS_MAX_MESSAGES);
    *out_message = gAprsMessages[message_index];
    return true;
}

APRS_State_t APRS_GetState(void)
{
    return gAprsState;
}

void APRS_ClearMessages(void)
{
    gAprsMessageHead = 0;
    gAprsMessageCount = 0;
    gAprsSelectedIndex = 0;
    gAprsDetailView = false;
    gAprsInputActive = false;
}

uint8_t APRS_GetSelectedIndex(void)
{
    return gAprsSelectedIndex;
}

bool APRS_IsDetailView(void)
{
    return gAprsDetailView;
}

bool APRS_IsInputActive(void)
{
    return gAprsInputActive;
}

static void APRS_ApplyFrequency(void)
{
    uint32_t frequency = StrToUL(INPUTBOX_GetAscii()) * 100U;

    for (unsigned int i = 0; i < BAND_N_ELEM; i++) {
        if (frequency < frequencyBandTable[i].lower || frequency >= frequencyBandTable[i].upper) {
            continue;
        }

        if (TX_freq_check(frequency)) {
            continue;
        }

        frequency = FREQUENCY_RoundToStep(frequency, gRxVfo->StepFrequency);
        gRxVfo->Band = i;
        gRxVfo->freq_config_RX.Frequency = frequency;
        gRxVfo->freq_config_TX.Frequency = frequency;
        RADIO_ConfigureSquelchAndOutputPower(gRxVfo);
        gCurrentVfo = gRxVfo;
        RADIO_SetupRegisters(true);
        gRequestDisplayScreen = DISPLAY_APRS;
        return;
    }

    gRequestDisplayScreen = DISPLAY_APRS;
}

static void APRS_Key_DIGITS(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (!gAprsInputActive) {
        return;
    }

    if (bKeyHeld || !bKeyPressed) {
        return;
    }

    INPUTBOX_Append(Key);
    gRequestDisplayScreen = DISPLAY_APRS;

    if (gInputBoxIndex < 6) {
        return;
    }

    gInputBoxIndex = 0;
    gAprsInputActive = false;
    APRS_ApplyFrequency();
}

static void APRS_Key_UP_DOWN(bool bKeyPressed, bool bKeyHeld, int8_t direction)
{
    if (bKeyHeld) {
        if (!bKeyPressed) {
            return;
        }
    } else {
        if (!bKeyPressed) {
            return;
        }
    }

    if (gAprsMessageCount == 0U) {
        return;
    }

    if (gAprsSelectedIndex >= gAprsMessageCount) {
        gAprsSelectedIndex = (uint8_t)(gAprsMessageCount - 1U);
    }

    gAprsSelectedIndex = (uint8_t)NUMBER_AddWithWraparound(
        gAprsSelectedIndex,
        direction,
        0,
        gAprsMessageCount - 1U);

    gRequestDisplayScreen = DISPLAY_APRS;
}

static void APRS_Key_MENU(bool bKeyPressed, bool bKeyHeld)
{
    if (bKeyHeld || !bKeyPressed) {
        return;
    }

    if (gAprsMessageCount == 0U) {
        return;
    }

    gAprsDetailView = !gAprsDetailView;
    gRequestDisplayScreen = DISPLAY_APRS;
}

static void APRS_Key_EXIT(bool bKeyPressed, bool bKeyHeld)
{
    if (bKeyHeld || !bKeyPressed) {
        return;
    }

    if (gAprsInputActive && gInputBoxIndex > 0U) {
        gInputBox[--gInputBoxIndex] = 10;
        gRequestDisplayScreen = DISPLAY_APRS;
        return;
    }

    if (gAprsInputActive) {
        gAprsInputActive = false;
        gInputBoxIndex = 0;
        gRequestDisplayScreen = DISPLAY_APRS;
        return;
    }

    gAprsDetailView = false;
    APRS_StopRx();
    gRequestDisplayScreen = DISPLAY_MAIN;
}

static void APRS_Key_FREQ(bool bKeyPressed, bool bKeyHeld)
{
    if (bKeyHeld || !bKeyPressed) {
        return;
    }

    gAprsInputActive = true;
    gInputBoxIndex = 0;
    gRequestDisplayScreen = DISPLAY_APRS;
}

void APRS_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (Key == KEY_7 && bKeyHeld && bKeyPressed) {
        APRS_ClearMessages();
        gRequestDisplayScreen = DISPLAY_APRS;
        return;
    }

    if (Key == KEY_5 && !gAprsInputActive) {
        APRS_Key_FREQ(bKeyPressed, bKeyHeld);
        return;
    }

    switch (Key) {
        case KEY_0...KEY_9:
            APRS_Key_DIGITS(Key, bKeyPressed, bKeyHeld);
            break;
        case KEY_MENU:
            APRS_Key_MENU(bKeyPressed, bKeyHeld);
            break;
        case KEY_UP:
            APRS_Key_UP_DOWN(bKeyPressed, bKeyHeld, -1);
            break;
        case KEY_DOWN:
            APRS_Key_UP_DOWN(bKeyPressed, bKeyHeld, 1);
            break;
        case KEY_EXIT:
            APRS_Key_EXIT(bKeyPressed, bKeyHeld);
            break;
        default:
            break;
    }
}
