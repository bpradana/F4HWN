/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
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

#include <assert.h>
#include <string.h>

#include "app/action.h"
#include "app/app.h"
#include "app/chFrScanner.h"
#include "app/common.h"
#include "app/dtmf.h"
#include "app/flashlight.h"
#include "app/fm.h"
#include "app/scanner.h"
#include "audio.h"
#include "driver/backlight.h"
#include "driver/bk1080.h"
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

static void ACTION_Scan_FM(bool bRestart);

static void ACTION_AlarmOr1750(bool b1750);
inline static void ACTION_Alarm()
{
    ACTION_AlarmOr1750(false);
}
inline static void ACTION_1750()
{
    ACTION_AlarmOr1750(true);
};

inline static void ACTION_ScanRestart()
{
    ACTION_Scan(true);
};

void (*action_opt_table[])(void) = {
    [ACTION_OPT_NONE] = &FUNCTION_NOP,
    [ACTION_OPT_POWER] = &ACTION_Power,
    [ACTION_OPT_MONITOR] = &ACTION_Monitor,
    [ACTION_OPT_SCAN] = &ACTION_ScanRestart,
    [ACTION_OPT_KEYLOCK] = &COMMON_KeypadLockToggle,
    [ACTION_OPT_A_B] = &COMMON_SwitchVFOs,
    [ACTION_OPT_VFO_MR] = &COMMON_SwitchVFOMode,
    [ACTION_OPT_SWITCH_DEMODUL] = &ACTION_SwitchDemodul,

    [ACTION_OPT_FLASHLIGHT] = &ACTION_FlashLight,

    [ACTION_OPT_VOX] = &ACTION_Vox,

    [ACTION_OPT_FM] = &ACTION_FM,

    [ACTION_OPT_ALARM] = &ACTION_Alarm,

    [ACTION_OPT_1750] = &ACTION_1750,

    [ACTION_OPT_BLMIN_TMP_OFF] = &FUNCTION_NOP,

    [ACTION_OPT_RXMODE] = &ACTION_RxMode,
    [ACTION_OPT_MAINONLY] = &ACTION_MainOnly,
    [ACTION_OPT_PTT] = &ACTION_Ptt,
    [ACTION_OPT_WN] = &ACTION_Wn,
    [ACTION_OPT_BACKLIGHT] = &ACTION_BackLight,
    // #if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
    [ACTION_OPT_MUTE] = &ACTION_Mute,
    // #else
    //     [ACTION_OPT_MUTE] = &FUNCTION_NOP,
    // #endif
    [ACTION_OPT_POWER_HIGH] = &ACTION_Power_High,
    [ACTION_OPT_REMOVE_OFFSET] = &ACTION_Remove_Offset,
};

static_assert(ARRAY_SIZE(action_opt_table) == ACTION_OPT_LEN);

void ACTION_Power(void)
{
    if (++gTxVfo->OUTPUT_POWER > OUTPUT_POWER_HIGH)
        gTxVfo->OUTPUT_POWER = OUTPUT_POWER_LOW1;

    gRequestSaveChannel = 1;

    gRequestDisplayScreen = gScreenToDisplay;
}

void ACTION_Monitor(void)
{
    if (gCurrentFunction != FUNCTION_MONITOR) { // enable the monitor
        RADIO_SelectVfos();
        RADIO_SetupRegisters(true);
        APP_StartListening(FUNCTION_MONITOR);
        return;
    }

    gMonitor = false;

    if (gScanStateDir != SCAN_OFF) {
        gScanPauseDelayIn_10ms = scan_pause_delay_in_1_10ms;
        gScheduleScanListen = false;
        gScanPauseMode = true;
    }

    RADIO_SetupRegisters(true);

    if (gFmRadioMode) {
        FM_Start();
        gRequestDisplayScreen = DISPLAY_FM;
    } else
        gRequestDisplayScreen = gScreenToDisplay;
}

void ACTION_Scan(bool bRestart)
{
    (void)bRestart;

    if (gFmRadioMode) {
        ACTION_Scan_FM(bRestart);
        return;
    }

    if (SCANNER_IsScanning()) {
        return;
    }

    // not scanning
    gMonitor = false;

    gDTMF_RX_live_timeout = 0;
    memset(gDTMF_RX_live, 0, sizeof(gDTMF_RX_live));

    RADIO_SelectVfos();

    GUI_SelectNextDisplay(DISPLAY_MAIN);

    if (gScanStateDir != SCAN_OFF) {
        // already scanning

        if (!IS_MR_CHANNEL(gNextMrChannel)) {
            CHFRSCANNER_Stop();
            return;
        }

        // channel mode. Keep scanning but toggle between scan lists
        gEeprom.SCAN_LIST_DEFAULT = (gEeprom.SCAN_LIST_DEFAULT + 1) % 6;
        SETTINGS_WriteCurrentState();

        // jump to the next channel
        CHFRSCANNER_Start(false, gScanStateDir);
        gScanPauseDelayIn_10ms = 1;
        gScheduleScanListen = false;
    } else {
        if (gScanRangeStart == 0) // No ScanRange
        {
            gEeprom.CURRENT_STATE = 1;
        } else // ScanRange
        {
            gEeprom.CURRENT_STATE = 2;
        }
        SETTINGS_WriteCurrentState();
        // start scanning
        CHFRSCANNER_Start(true, SCAN_FWD);

        // clear the other vfo's rssi level (to hide the antenna symbol)
        gVFO_RSSI_bar_level[(gEeprom.RX_VFO + 1) & 1U] = 0;

        // let the user see DW is not active
        gDualWatchActive = false;
    }

    gUpdateStatus = true;
}

void ACTION_SwitchDemodul(void)
{
    gRequestSaveChannel = 1;

    gTxVfo->Modulation++;

    if (gTxVfo->Modulation == MODULATION_UKNOWN)
        gTxVfo->Modulation = MODULATION_FM;
}

void ACTION_Handle(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (gScreenToDisplay == DISPLAY_MAIN && gDTMF_InputMode) {
        // entering DTMF code

        gPttWasReleased = true;

        if (Key != KEY_SIDE1 || bKeyHeld || !bKeyPressed) {
            return;
        }

        // side1 btn pressed

        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
        gRequestDisplayScreen = DISPLAY_MAIN;

        if (gDTMF_InputBox_Index <= 0) {
            // turn off DTMF input box if no codes left
            gDTMF_InputMode = false;
            return;
        }

        // DTMF codes are in the input box
        gDTMF_InputBox[--gDTMF_InputBox_Index] = '-'; // delete one code

        return;
    }

    enum ACTION_OPT_t funcShort = ACTION_OPT_NONE;
    enum ACTION_OPT_t funcLong = ACTION_OPT_NONE;
    switch (Key) {
    case KEY_SIDE1:
        funcShort = gEeprom.KEY_1_SHORT_PRESS_ACTION;
        funcLong = gEeprom.KEY_1_LONG_PRESS_ACTION;
        break;
    case KEY_SIDE2:
        funcShort = gEeprom.KEY_2_SHORT_PRESS_ACTION;
        funcLong = gEeprom.KEY_2_LONG_PRESS_ACTION;
        break;
    case KEY_MENU:
        funcLong = gEeprom.KEY_M_LONG_PRESS_ACTION;
        break;
    default:
        break;
    }

    if (!bKeyHeld && bKeyPressed) // button pushed
    {
        return;
    }

    // held or released beyond this point

    if (!(bKeyHeld && !bKeyPressed)) // don't beep on released after hold
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;

    if (bKeyHeld || bKeyPressed) // held
    {
        funcShort = funcLong;

        if (!bKeyPressed) // ignore release if held
            return;
    }

    // held or released after short press beyond this point

    action_opt_table[funcShort]();
}

void ACTION_FM(void)
{
    if (gCurrentFunction != FUNCTION_TRANSMIT && gCurrentFunction != FUNCTION_MONITOR) {
        gInputBoxIndex = 0;

        if (gFmRadioMode) {
            FM_TurnOff();
            gFlagReconfigureVfos = true;
            gRequestDisplayScreen = DISPLAY_MAIN;

            gVoxResumeCountdown = 80;
            return;
        }

        gMonitor = false;

        RADIO_SelectVfos();
        RADIO_SetupRegisters(true);

        FM_Start();

        gRequestDisplayScreen = DISPLAY_FM;
    }
}

static void ACTION_Scan_FM(bool bRestart)
{
    if (FUNCTION_IsRx())
        return;

    GUI_SelectNextDisplay(DISPLAY_FM);

    gMonitor = false;

    if (gFM_ScanState != FM_SCAN_OFF) {
        FM_PlayAndUpdate();

        return;
    }

    uint16_t freq;

    if (bRestart) {
        gFM_AutoScan = true;
        gFM_ChannelPosition = 0;
        FM_EraseChannels();
        freq = BK1080_GetFreqLoLimit(gEeprom.FM_Band);
    } else {
        gFM_AutoScan = false;
        gFM_ChannelPosition = 0;
        freq = gEeprom.FM_FrequencyPlaying;
    }

    BK1080_GetFrequencyDeviation(freq);
    FM_Tune(freq, 1, bRestart);
}

static void ACTION_AlarmOr1750(const bool b1750)
{
    if (gEeprom.KEY_LOCK && gEeprom.KEY_LOCK_PTT)
        return;

    const AlarmState_t alarm_mode =
        (gEeprom.ALARM_MODE == ALARM_MODE_TONE) ? ALARM_STATE_TXALARM : ALARM_STATE_SITE_ALARM;
    gAlarmRunningCounter = 0;
    gAlarmState = b1750 ? ALARM_STATE_TX1750 : alarm_mode;

    (void)b1750;
    gInputBoxIndex = 0;

    gFlagPrepareTX = gAlarmState != ALARM_STATE_OFF;

    if (gScreenToDisplay != DISPLAY_MENU) // 1of11 .. don't close the menu
        gRequestDisplayScreen = DISPLAY_MAIN;
}

void ACTION_Vox(void)
{
    gEeprom.VOX_SWITCH = !gEeprom.VOX_SWITCH;
    gRequestSaveSettings = true;
    gFlagReconfigureVfos = true;
    gUpdateStatus = true;
}

void ACTION_Update(void)
{
    gSaveRxMode = false;
    gFlagReconfigureVfos = true;
    gUpdateStatus = true;
}

void ACTION_RxMode(void)
{
    static bool cycle = 0;

    switch (cycle) {
    case 0:
        gEeprom.DUAL_WATCH = !gEeprom.DUAL_WATCH;
        cycle = 1;
        break;
    case 1:
        gEeprom.CROSS_BAND_RX_TX = !gEeprom.CROSS_BAND_RX_TX;
        cycle = 0;
        break;
    }

    ACTION_Update();
}

void ACTION_MainOnly(void)
{
    static bool cycle = 0;
    static uint8_t dw = 0;
    static uint8_t cb = 0;

    switch (cycle) {
    case 0:
        dw = gEeprom.DUAL_WATCH;
        cb = gEeprom.CROSS_BAND_RX_TX;

        gEeprom.DUAL_WATCH = 0;
        gEeprom.CROSS_BAND_RX_TX = 0;
        cycle = 1;
        break;
    case 1:
        gEeprom.DUAL_WATCH = dw;
        gEeprom.CROSS_BAND_RX_TX = cb;
        cycle = 0;
        break;
    }

    ACTION_Update();
}

void ACTION_Ptt(void)
{
    gSetting_set_ptt_session = !gSetting_set_ptt_session;
}

void ACTION_Wn(void)
{
    if (gRxVfo->Modulation == MODULATION_AM) {
        BK4819_SetFilterBandwidth(BK4819_FILTER_BW_AM, true);
        return;
    }
    bool narrower = 0;
    if (FUNCTION_IsRx()) {
        gRxVfo->CHANNEL_BANDWIDTH = (gRxVfo->CHANNEL_BANDWIDTH == 0) ? 1 : 0;
        if (gRxVfo->CHANNEL_BANDWIDTH == BANDWIDTH_NARROW && gSetting_set_nfm == 1) {
            narrower = 1;
        }

        BK4819_SetFilterBandwidth(gRxVfo->CHANNEL_BANDWIDTH + narrower, false);
    } else {
        gTxVfo->CHANNEL_BANDWIDTH = (gTxVfo->CHANNEL_BANDWIDTH == 0) ? 1 : 0;
        if (gTxVfo->CHANNEL_BANDWIDTH == BANDWIDTH_NARROW && gSetting_set_nfm == 1) {
            narrower = 1;
        }

        BK4819_SetFilterBandwidth(gTxVfo->CHANNEL_BANDWIDTH, false);
    }
}

void ACTION_BackLight(void)
{
    if (gBackLight) {
        gEeprom.BACKLIGHT_TIME = gBacklightTimeOriginal;
    }
    gBackLight = false;
    BACKLIGHT_TurnOn();
}

void ACTION_BackLightOnDemand(void)
{
    if (gBackLight == false) {
        gBacklightTimeOriginal = gEeprom.BACKLIGHT_TIME;
        gEeprom.BACKLIGHT_TIME = 61;
        gBackLight = true;
    } else {
        if (gBacklightBrightnessOld == gEeprom.BACKLIGHT_MAX) {
            gEeprom.BACKLIGHT_TIME = 0;
        } else {
            gEeprom.BACKLIGHT_TIME = 61;
        }
    }

    BACKLIGHT_TurnOn();
}

// #if !defined(ENABLE_SPECTRUM) || !defined(ENABLE_FMRADIO)
void ACTION_Mute(void)
{
    // Toggle mute state
    gMute = !gMute;

    // Update the registers
    BK1080_WriteRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2, gMute ? 0x0A10 : 0x0A1F);
    gEeprom.VOLUME_GAIN = gMute ? 0 : gEeprom.VOLUME_GAIN_BACKUP;
    BK4819_WriteRegister(BK4819_REG_48,
                         (11u << 12) |    // ??? .. 0 ~ 15, doesn't seem to make any difference
                             (0u << 10) | // AF Rx Gain-1
                             (gEeprom.VOLUME_GAIN << 4) | // AF Rx Gain-2
                             (gEeprom.DAC_GAIN << 0));    // AF DAC Gain (after Gain-1 and Gain-2)

    gUpdateStatus = true;
}
// #endif

void ACTION_Power_High(void)
{
    gPowerHigh = !gPowerHigh;
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
}

void ACTION_Remove_Offset(void)
{
    gRemoveOffset = !gRemoveOffset;
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
}