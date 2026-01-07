/* Copyright 2025
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "app/aprs.h"

#ifdef ENABLE_FEAT_F4HWN_APRS

#include <string.h>

#include "external/printf/printf.h"

#include "driver/bk4819-regs.h"
#include "driver/bk4819.h"
#include "driver/keyboard.h"
#include "misc.h"
#include "radio.h"
#include "ui/ui.h"

#ifndef ARRAY_SIZE
    #define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#define APRS_MAX_FRAME_LEN 330
#define APRS_MIN_FRAME_LEN 18
#define APRS_MAX_DIGIS     4
#define APRS_WORDS_PER_INTERRUPT 8

typedef struct {
    char    text[7];
    uint8_t ssid;
} APRS_Address;

typedef struct {
    uint8_t data[APRS_MAX_FRAME_LEN];
    uint16_t length;
    bool     inFrame;
} APRS_FrameBuffer;

static struct {
    bool              active;
    bool              detailView;
    APRS_FrameBuffer  frame;
    uint32_t          sequence;
} gAprsState;

static AprsPacket_t gAprsLog[APRS_LOG_CAPACITY];
static uint8_t      gAprsLogHead;
static uint8_t      gAprsLogCount;
static uint8_t      gAprsSelection;
static int16_t      gAprsFrameRssi;

extern bool gWasFKeyPressed;

static void     APRS_ResetState(void);
static void     APRS_HandleByte(uint8_t byte);
static bool     APRS_DecodeFrame(const uint8_t *frame, uint16_t length);
static uint16_t APRS_CalculateCrc(const uint8_t *data, uint16_t length);
static bool     APRS_ParseAddress(const uint8_t *frame, uint16_t length, uint16_t *offset, APRS_Address *address, bool *isLast);
static void     APRS_FormatAddress(const APRS_Address *address, char *dest, size_t destLen);
static void     APRS_AppendPath(char *path, size_t pathLen, const char *fragment);
static void     APRS_StorePacket(const AprsPacket_t *packet);
static void     APRS_PrepareRadio(void);
static inline uint8_t APRS_ReverseByte(uint8_t value);

void APRS_Enter(void)
{
    if (gAprsState.active)
        return;

    gAprsState.active     = true;
    gAprsState.detailView = false;
    gAprsState.sequence   = 0;
    APRS_ResetState();
    APRS_ClearLog();
    APRS_PrepareRadio();
    BK4819_PrepareFSKReceive();

    gRequestDisplayScreen = DISPLAY_APRS;
    gUpdateDisplay        = true;
    gUpdateStatus         = true;
}

void APRS_Leave(void)
{
    if (!gAprsState.active)
        return;

    gAprsState.active = false;
    APRS_ResetState();
    BK4819_ResetFSK();
    gRequestDisplayScreen = DISPLAY_MAIN;
    gUpdateDisplay        = true;
}

bool APRS_IsActive(void)
{
    return gAprsState.active;
}

void APRS_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (!bKeyPressed)
        return;

    switch (Key) {
    case KEY_EXIT:
        if (!bKeyHeld) {
            APRS_Leave();
        }
        break;

    case KEY_UP:
        if (!bKeyHeld) {
            APRS_MoveSelection(-1);
        }
        break;

    case KEY_DOWN:
        if (!bKeyHeld) {
            APRS_MoveSelection(1);
        }
        break;

    case KEY_MENU:
        if (!bKeyHeld) {
            APRS_ToggleDetail();
        }
        break;

    case KEY_7:
        if (!bKeyHeld && gWasFKeyPressed) {
            gWasFKeyPressed = false;
            APRS_ClearLog();
        }
        break;

    default:
        break;
    }
}

void APRS_HandleFskBurst(void)
{
    if (!gAprsState.active)
        return;

    for (unsigned int i = 0; i < APRS_WORDS_PER_INTERRUPT; i++) {
        const uint16_t word = BK4819_ReadRegister(BK4819_REG_5F);
        APRS_HandleByte((uint8_t)(word & 0x00FF));
        APRS_HandleByte((uint8_t)(word >> 8));
    }
}

uint8_t APRS_GetPacketCount(void)
{
    return gAprsLogCount;
}

const AprsPacket_t *APRS_GetPacket(uint8_t index)
{
    if (index >= gAprsLogCount)
        return NULL;

    const uint8_t logical = (gAprsLogHead + APRS_LOG_CAPACITY - index) % APRS_LOG_CAPACITY;
    return &gAprsLog[logical];
}

uint8_t APRS_GetSelection(void)
{
    return gAprsSelection;
}

void APRS_MoveSelection(int8_t delta)
{
    if (gAprsLogCount == 0)
        return;

    int16_t next = (int16_t)gAprsSelection + delta;
    if (next < 0) {
        next = 0;
    } else if (next >= gAprsLogCount) {
        next = gAprsLogCount - 1;
    }

    if ((uint8_t)next != gAprsSelection) {
        gAprsSelection = (uint8_t)next;
        gUpdateDisplay = true;
    }
}

void APRS_ClearLog(void)
{
    gAprsLogCount   = 0;
    gAprsSelection  = 0;
    gAprsLogHead    = APRS_LOG_CAPACITY - 1;
    gUpdateDisplay  = true;
}

bool APRS_IsDetailView(void)
{
    return gAprsState.detailView;
}

void APRS_ToggleDetail(void)
{
    gAprsState.detailView = !gAprsState.detailView;
    gUpdateDisplay        = true;
}

static void APRS_ResetState(void)
{
    gAprsState.frame.length = 0;
    gAprsState.frame.inFrame = false;
    gAprsFrameRssi = 0;
}

static void APRS_HandleByte(uint8_t byte)
{
    if (byte == 0x7E) {
        if (gAprsState.frame.inFrame && gAprsState.frame.length >= APRS_MIN_FRAME_LEN) {
            APRS_DecodeFrame(gAprsState.frame.data, gAprsState.frame.length);
        }
        gAprsState.frame.inFrame = true;
        gAprsState.frame.length  = 0;
        gAprsFrameRssi           = BK4819_GetRSSI_dBm();
        return;
    }

    if (!gAprsState.frame.inFrame)
        return;

    if (gAprsState.frame.length >= APRS_MAX_FRAME_LEN) {
        gAprsState.frame.inFrame = false;
        gAprsState.frame.length  = 0;
        return;
    }

    gAprsState.frame.data[gAprsState.frame.length++] = APRS_ReverseByte(byte);
}

static bool APRS_DecodeFrame(const uint8_t *frame, uint16_t length)
{
    if (length < APRS_MIN_FRAME_LEN)
        return false;

    if (length < 3)
        return false;

    const uint16_t payloadLength = length - 2;
    const uint16_t expectedCrc   = APRS_CalculateCrc(frame, payloadLength);
    const uint16_t receivedCrc   = (uint16_t)frame[payloadLength] | ((uint16_t)frame[payloadLength + 1] << 8);
    if (expectedCrc != receivedCrc)
        return false;

    APRS_Address addresses[2 + APRS_MAX_DIGIS];
    uint8_t      addressCount = 0;
    uint16_t     offset       = 0;
    bool         isLast       = false;

    while (!isLast && addressCount < ARRAY_SIZE(addresses) && offset + 7 <= payloadLength) {
        if (!APRS_ParseAddress(frame, payloadLength, &offset, &addresses[addressCount], &isLast))
            return false;
        addressCount++;
    }

    if (addressCount < 2 || offset + 2 > payloadLength)
        return false;

    uint8_t control = frame[offset++];
    uint8_t pid     = frame[offset++];
    (void)control;
    (void)pid;

    uint16_t infoLength = payloadLength - offset;
    if (infoLength >= APRS_MAX_INFO_LEN) {
        infoLength = APRS_MAX_INFO_LEN - 1;
    }

    AprsPacket_t packet = {0};
    APRS_FormatAddress(&addresses[1], packet.source, sizeof(packet.source));
    APRS_FormatAddress(&addresses[0], packet.destination, sizeof(packet.destination));

    for (uint8_t i = 2; i < addressCount; i++) {
        char fragment[12];
        APRS_FormatAddress(&addresses[i], fragment, sizeof(fragment));
        APRS_AppendPath(packet.path, sizeof(packet.path), fragment);
    }

    for (uint16_t i = 0; i < infoLength; i++) {
        char c = (char)frame[offset + i];
        if (c == '\r' || c == '\n') {
            c = ' ';
        } else if ((c < 0x20) || (c > 0x7E)) {
            c = '.';
        }
        packet.info[i] = c;
    }
    packet.info[infoLength] = 0;

    packet.sequence = ++gAprsState.sequence;
    packet.rssi     = gAprsFrameRssi;

    APRS_StorePacket(&packet);
    return true;
}

static uint16_t APRS_CalculateCrc(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF;

    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (unsigned int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0x8408;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

static bool APRS_ParseAddress(const uint8_t *frame, uint16_t length, uint16_t *offset, APRS_Address *address, bool *isLast)
{
    if (*offset + 7 > length)
        return false;

    for (unsigned int i = 0; i < 6; i++) {
        char c = (char)((frame[*offset + i] >> 1) & 0x7F);
        if (c == 0x20)
            c = ' ';
        address->text[i] = c;
    }
    address->text[6] = 0;

    uint8_t ssidField = frame[*offset + 6];
    address->ssid     = (ssidField >> 1) & 0x0F;
    *isLast           = (ssidField & 0x01) ? true : false;

    *offset += 7;
    return true;
}

static void APRS_FormatAddress(const APRS_Address *address, char *dest, size_t destLen)
{
    char buffer[8] = {0};
    memcpy(buffer, address->text, sizeof(buffer) - 1);

    size_t length = strlen(buffer);
    while (length > 0 && buffer[length - 1] == ' ')
        buffer[--length] = 0;

    if (length == 0) {
        buffer[0] = '?';
        buffer[1] = 0;
        length    = 1;
    }

    snprintf(dest, destLen, (address->ssid > 0) ? "%s-%u" : "%s", buffer, address->ssid);
}

static void APRS_AppendPath(char *path, size_t pathLen, const char *fragment)
{
    if (fragment[0] == 0)
        return;

    size_t used = strlen(path);
    if (used > 0) {
        if (used < pathLen - 1) {
            path[used++] = ',';
            path[used]   = 0;
        } else {
            return;
        }
    }

    size_t remaining = pathLen - used;
    if (remaining == 0)
        return;

    strncpy(&path[used], fragment, remaining - 1);
    path[pathLen - 1] = 0;
}

static void APRS_StorePacket(const AprsPacket_t *packet)
{
    gAprsLogHead = (gAprsLogHead + 1) % APRS_LOG_CAPACITY;
    gAprsLog[gAprsLogHead] = *packet;

    if (gAprsLogCount < APRS_LOG_CAPACITY)
        gAprsLogCount++;

    gAprsSelection = 0;
    gUpdateDisplay = true;
}

static void APRS_PrepareRadio(void)
{
    BK4819_SetupAprsFsk();
}

static inline uint8_t APRS_ReverseByte(uint8_t value)
{
    value = (value & 0xF0) >> 4 | (value & 0x0F) << 4;
    value = (value & 0xCC) >> 2 | (value & 0x33) << 2;
    value = (value & 0xAA) >> 1 | (value & 0x55) << 1;
    return value;
}

#endif
