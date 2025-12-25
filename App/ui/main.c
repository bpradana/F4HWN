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

#include <stdlib.h> // abs()
#include <string.h>

#include "app/chFrScanner.h"
#include "app/dtmf.h"
#include "audio.h"
#include "bitmaps.h"
#include "board.h"
#include "driver/bk4819.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/main.h"
#include "ui/ui.h"

#include "driver/system.h"

center_line_t center_line = CENTER_LINE_NONE;

static int8_t RxBlink;
static int8_t RxBlinkLed = 0;
static int8_t RxBlinkLedCounter;
static int8_t RxLine;
static uint32_t RxOnVfofrequency;

bool isMainOnlyInputDTMF = false;

static bool isMainOnly() {
  return (gEeprom.DUAL_WATCH == DUAL_WATCH_OFF) &&
         (gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_OFF);
}

const int8_t dBmCorrTable[7] = {
    -15, // band 1
    -25, // band 2
    -20, // band 3
    -4,  // band 4
    -7,  // band 5
    -6,  // band 6
    -1   // band 7
};

const char *VfoStateStr[] = {[VFO_STATE_NORMAL] = "",
                             [VFO_STATE_BUSY] = "BUSY",
                             [VFO_STATE_BAT_LOW] = "BAT LOW",
                             [VFO_STATE_TX_DISABLE] = "TX DISABLE",
                             [VFO_STATE_TIMEOUT] = "TIMEOUT",
                             [VFO_STATE_ALARM] = "ALARM",
                             [VFO_STATE_VOLTAGE_HIGH] = "VOLT HIGH"};

// ***************************************************************************

static void DrawSmallAntennaAndBars(uint8_t *p, unsigned int level) {
  if (level > 6)
    level = 6;

  memcpy(p, BITMAP_Antenna, ARRAY_SIZE(BITMAP_Antenna));

  for (uint8_t i = 1; i <= level; i++) {
    char bar = (0xff << (6 - i)) & 0x7F;
    memset(p + 2 + i * 3, bar, 2);
  }
}

static void DrawLevelBar(uint8_t xpos, uint8_t line, uint8_t level,
                         uint8_t bars) {

  uint8_t *p_line = gFrameBuffer[line];
  level = MIN(level, bars);

  for (uint8_t i = 0; i < level; i++) {
    if (gSetting_set_met) {
      const char hollowBar[] = {0b01111111, 0b01000001, 0b01000001, 0b01111111};

      if (i < bars - 4) {
        for (uint8_t j = 0; j < 4; j++)
          p_line[xpos + i * 5 + j] = (~(0x7F >> (i + 1))) & 0x7F;
      } else {
        memcpy(p_line + (xpos + i * 5), &hollowBar, ARRAY_SIZE(hollowBar));
      }
    } else {
      const char hollowBar[] = {0b00111110, 0b00100010, 0b00100010, 0b00111110};

      const char simpleBar[] = {0b00111110, 0b00111110, 0b00111110, 0b00111110};

      if (i < bars - 4) {
        memcpy(p_line + (xpos + i * 5), &simpleBar, ARRAY_SIZE(simpleBar));
      } else {
        memcpy(p_line + (xpos + i * 5), &hollowBar, ARRAY_SIZE(hollowBar));
      }
    }
  }
}

// Approximation of a logarithmic scale using integer arithmetic
uint8_t log2_approx(unsigned int value) {
  uint8_t log = 0;
  while (value >>= 1) {
    log++;
  }
  return log;
}

void UI_DisplayAudioBar(void) {
  if (gSetting_mic_bar) {
    if (gLowBattery && !gLowBatteryConfirmed)
      return;

    RxBlinkLed = 0;
    RxBlinkLedCounter = 0;
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    unsigned int line;
    if (isMainOnly()) {
      line = 5;
    } else {
      line = 3;
    }

    if (gCurrentFunction != FUNCTION_TRANSMIT ||
        gScreenToDisplay != DISPLAY_MAIN) {
      return; // screen is in use
    }

    if (gAlarmState != ALARM_STATE_OFF)
      return;
    static uint8_t barsOld = 0;
    const uint8_t thresold = 18; // arbitrary thresold
    // const uint8_t barsList[] = {0, 0, 0, 1, 2, 3, 4, 5, 6, 8, 10, 13, 16, 20,
    // 25, 25};
    const uint8_t barsList[] = {0, 0,  0,  1,  2,  3,  5,  7,
                                9, 12, 15, 18, 21, 25, 25, 25};
    uint8_t logLevel;
    uint8_t bars;

    unsigned int voiceLevel = BK4819_GetVoiceAmplitudeOut(); // 15:0

    voiceLevel = (voiceLevel >= thresold) ? (voiceLevel - thresold) : 0;
    logLevel = log2_approx(MIN(voiceLevel * 16, 32768u) + 1);
    bars = barsList[logLevel];
    barsOld = (barsOld - bars > 1) ? (barsOld - 1) : bars;

    uint8_t *p_line = gFrameBuffer[line];
    memset(p_line, 0, LCD_WIDTH);

    DrawLevelBar(2, line, barsOld, 25);

    if (gCurrentFunction == FUNCTION_TRANSMIT)
      ST7565_BlitFullScreen();
  }
}

void DisplayRSSIBar(const bool now) {

  const unsigned int txt_width = 7 * 8;         // 8 text chars
  const unsigned int bar_x = 2 + txt_width + 4; // X coord of bar graph

  /*
  const char empty[] = {
      0b00000000,
      0b00000000,
      0b00000000,
      0b00000000,
      0b00000000,
      0b00000000,
      0b00000000,
  };
  */

  unsigned int line;
  if (isMainOnly()) {
    line = 5;
  } else {
    line = 3;
  }

  // char rx[4];
  // sprintf(String, "%d", RxBlink);
  // UI_PrintStringSmallBold(String, 80, 0, RxLine);

  if (RxLine >= 0 && center_line != CENTER_LINE_IN_USE) {
    if (RxBlink == 0 || RxBlink == 1) {
      UI_PrintStringSmallBold("RX", 8, 0, RxLine);
      if (RxBlink == 1)
        RxBlink = 2;
    } else {
      for (uint8_t i = 8; i < 24; i++) {
        gFrameBuffer[RxLine][i] = 0x00;
      }
      RxBlink = 1;
    }
    ST7565_BlitLine(RxLine);
  }
  uint8_t *p_line = gFrameBuffer[line];
  char str[16];

  if ((gEeprom.KEY_LOCK && gKeypadLocked > 0) ||
      center_line != CENTER_LINE_RSSI)
    return; // display is in use

  if (gCurrentFunction == FUNCTION_TRANSMIT || gScreenToDisplay != DISPLAY_MAIN)
    return; // display is in use

  if (now)
    memset(p_line, 0, LCD_WIDTH);

  int16_t rssi_dBm = BK4819_GetRSSI_dBm() + dBmCorrTable[gRxVfo->Band];

  rssi_dBm = -rssi_dBm;

  if (rssi_dBm > 141)
    rssi_dBm = 141;
  if (rssi_dBm < 53)
    rssi_dBm = 53;

  uint8_t s_level = 0;
  uint8_t overS9dBm = 0;
  uint8_t overS9Bars = 0;

  if (rssi_dBm >= 93) {
    s_level = map(rssi_dBm, 141, 93, 1, 9);
  } else {
    s_level = 9;
    overS9dBm = map(rssi_dBm, 93, 53, 0, 40);
    overS9Bars = map(overS9dBm, 0, 40, 0, 4);
  }

  if (gSetting_set_gui) {
    sprintf(str, "%3d", -rssi_dBm);
    UI_PrintStringSmallNormal(str, LCD_WIDTH + 8, 0, line - 1);
  } else {
    sprintf(str, "% 4d %s", -rssi_dBm, "dBm");
    if (isMainOnly())
      GUI_DisplaySmallest(str, 2, 41, false, true);
    else
      GUI_DisplaySmallest(str, 2, 25, false, true);
  }

  if (overS9Bars == 0) {
    sprintf(str, "S%d", s_level);
  } else {
    sprintf(str, "+%02d", overS9dBm);
  }

  UI_PrintStringSmallNormal(str, LCD_WIDTH + 38, 0, line - 1);
  DrawLevelBar(bar_x, line, s_level + overS9Bars, 13);
  if (now)
    ST7565_BlitLine(line);
}

void UI_MAIN_TimeSlice500ms(void) {
  if (gScreenToDisplay == DISPLAY_MAIN) {

    if (FUNCTION_IsRx()) {
      DisplayRSSIBar(true);
    } else if (gSetting_set_eot > 0 && RxBlinkLed == 2) {
      if (RxBlinkLedCounter <= 8) {
        if (RxBlinkLedCounter % 2 == 0) {
          if (gSetting_set_eot > 1) {
            BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
          }
        } else {
          if (gSetting_set_eot > 1) {
            BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
          }

          if (gSetting_set_eot == 1 || gSetting_set_eot == 3) {
            switch (RxBlinkLedCounter) {
            case 1:
              AUDIO_PlayBeep(BEEP_400HZ_30MS);
              break;

            case 3:
              AUDIO_PlayBeep(BEEP_400HZ_30MS);
              break;

            case 5:
              AUDIO_PlayBeep(BEEP_500HZ_30MS);
              break;

            case 7:
              AUDIO_PlayBeep(BEEP_600HZ_30MS);
              break;
            }
          }
        }
        RxBlinkLedCounter += 1;
      } else {
        RxBlinkLed = 0;
      }
    }
  }
}

// ***************************************************************************

void UI_DisplayMain(void) {
  char String[22];

  center_line = CENTER_LINE_NONE;

  // clear the screen
  UI_DisplayClear();

  if (gLowBattery && !gLowBatteryConfirmed) {
    UI_DisplayPopup("LOW BATTERY");
    ST7565_BlitFullScreen();
    return;
  }

  if (gEeprom.KEY_LOCK &&
      gKeypadLocked > 0) { // tell user how to unlock the keyboard
    uint8_t shift = 3;

    /*
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
    SYSTEM_DelayMs(50);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    SYSTEM_DelayMs(50);
    */

    if (isMainOnly()) {
      shift = 5;
    }
    // memcpy(gFrameBuffer[shift] + 2, gFontKeyLock, sizeof(gFontKeyLock));
    UI_PrintStringSmallBold("UNLOCK KEYBOARD", 12, 0, shift);
    // memcpy(gFrameBuffer[shift] + 120, gFontKeyLock, sizeof(gFontKeyLock));

    /*
    for (uint8_t i = 12; i < 116; i++)
    {
        gFrameBuffer[shift][i] ^= 0xFF;
    }
    */
  }

  unsigned int activeTxVFO = gRxVfoIsActive ? gEeprom.RX_VFO : gEeprom.TX_VFO;

  for (unsigned int vfo_num = 0; vfo_num < 2; vfo_num++) {
    const unsigned int line0 = 0; // text screen line
    const unsigned int line1 = 4;
    unsigned int line;
    if (isMainOnly()) {
      line = 0;
    } else {
      line = (vfo_num == 0) ? line0 : line1;
    }
    const bool isMainVFO = (vfo_num == gEeprom.TX_VFO);
    uint8_t *p_line0 = gFrameBuffer[line + 0];
    uint8_t *p_line1 = gFrameBuffer[line + 1];
    enum Vfo_txtr_mode mode = VFO_MODE_NONE;

    if (isMainOnly()) {
      if (activeTxVFO != vfo_num) {
        continue;
      }
    }

    if (activeTxVFO != vfo_num || isMainOnly()) {
      if (gScanRangeStart) {

        // if(IS_FREQ_CHANNEL(gEeprom.ScreenChannel[0]) &&
        // IS_FREQ_CHANNEL(gEeprom.ScreenChannel[1])) {
        if (IS_FREQ_CHANNEL(gEeprom.ScreenChannel[activeTxVFO])) {

          uint8_t shift = 0;

          if (isMainOnly()) {
            shift = 3;
          }

          UI_PrintString("ScnRng", 5, 0, line + shift, 8);
          sprintf(String, "%3u.%05u", gScanRangeStart / 100000,
                  gScanRangeStart % 100000);
          UI_PrintStringSmallNormal(String, 56, 0, line + shift);
          sprintf(String, "%3u.%05u", gScanRangeStop / 100000,
                  gScanRangeStop % 100000);
          UI_PrintStringSmallNormal(String, 56, 0, line + shift + 1);

          if (!isMainOnly())
            continue;
        } else {
          gScanRangeStart = 0;
        }
      }

      if (gDTMF_InputMode) {
        char *pPrintStr = "";
        // show DTMF stuff
        {
          sprintf(String, ">%s", gDTMF_InputBox);
          pPrintStr = String;
        }

        if (isMainOnly()) {
          UI_PrintString(pPrintStr, 2, 0, 5, 8);
          isMainOnlyInputDTMF = true;
          center_line = CENTER_LINE_IN_USE;
        } else {
          UI_PrintString(pPrintStr, 2, 0, 0 + (vfo_num * 3), 8);
          isMainOnlyInputDTMF = false;
          center_line = CENTER_LINE_IN_USE;
          continue;
        }
      }

      // highlight the selected/used VFO with a marker
      if (isMainVFO)
        memcpy(p_line0 + 0, BITMAP_VFO_Default, sizeof(BITMAP_VFO_Default));
    } else // active TX VFO
    {      // highlight the selected/used VFO with a marker
      if (isMainVFO)
        memcpy(p_line0 + 0, BITMAP_VFO_Default, sizeof(BITMAP_VFO_Default));
      else
        memcpy(p_line0 + 0, BITMAP_VFO_NotDefault,
               sizeof(BITMAP_VFO_NotDefault));
    }

    uint32_t frequency = gEeprom.VfoInfo[vfo_num].pRX->Frequency;

    if (TX_freq_check(frequency) != 0 &&
        gEeprom.VfoInfo[vfo_num].TX_LOCK == true) {
      if (isMainOnly())
        memcpy(p_line0 + 14, BITMAP_VFO_Lock, sizeof(BITMAP_VFO_Lock));
      else
        memcpy(p_line0 + 24, BITMAP_VFO_Lock, sizeof(BITMAP_VFO_Lock));
    }

    if (gCurrentFunction == FUNCTION_TRANSMIT) { // transmitting

      if (gAlarmState == ALARM_STATE_SITE_ALARM)
        mode = VFO_MODE_RX;
      else {
        if (activeTxVFO == vfo_num) { // show the TX symbol
          mode = VFO_MODE_TX;
          UI_PrintStringSmallBold("TX", 8, 0, line);
        }
      }
    } else { // receiving .. show the RX symbol
      mode = VFO_MODE_RX;
      // if (FUNCTION_IsRx() && gEeprom.RX_VFO == vfo_num) {
      if (FUNCTION_IsRx() && gEeprom.RX_VFO == vfo_num &&
          VfoState[vfo_num] == VFO_STATE_NORMAL) {
        RxBlinkLed = 1;
        RxBlinkLedCounter = 0;
        RxLine = line;
        RxOnVfofrequency = frequency;
        if (!isMainVFO) {
          RxBlink = 1;
        } else {
          RxBlink = 0;
        }
      } else {
        if (RxOnVfofrequency == frequency && !isMainOnly()) {
          UI_PrintStringSmallNormal(">>", 8, 0, line);
          // memcpy(p_line0 + 14, BITMAP_VFO_Default,
          // sizeof(BITMAP_VFO_Default));
        }

        if (RxBlinkLed == 1)
          RxBlinkLed = 2;
      }
    }

    if (IS_MR_CHANNEL(gEeprom.ScreenChannel[vfo_num])) { // channel mode
      const unsigned int x = 2;
      const bool inputting = gInputBoxIndex != 0 && gEeprom.TX_VFO == vfo_num;
      if (!inputting)
        sprintf(String, "M%u", gEeprom.ScreenChannel[vfo_num] + 1);
      else
        sprintf(String, "M%.3s", INPUTBOX_GetAscii()); // show the input text
      UI_PrintStringSmallNormal(String, x, 0, line + 1);
    } else if (IS_FREQ_CHANNEL(
                   gEeprom.ScreenChannel[vfo_num])) { // frequency mode
      // show the frequency band number
      const unsigned int x = 2;
      char *buf =
          gEeprom.VfoInfo[vfo_num].pRX->Frequency < _1GHz_in_KHz ? "" : "+";
      sprintf(String, "F%u%s",
              1 + gEeprom.ScreenChannel[vfo_num] - FREQ_CHANNEL_FIRST, buf);
      UI_PrintStringSmallNormal(String, x, 0, line + 1);
    }

    // ************

    enum VfoState_t state = VfoState[vfo_num];

    if (gCurrentFunction == FUNCTION_TRANSMIT &&
        gAlarmState == ALARM_STATE_SITE_ALARM) {
      if (activeTxVFO == vfo_num)
        state = VFO_STATE_ALARM;
    }
    if (state != VFO_STATE_NORMAL) {
      if (state < ARRAY_SIZE(VfoStateStr))
        UI_PrintString(VfoStateStr[state], 31, 0, line, 8);
    } else if (gInputBoxIndex > 0 &&
               IS_FREQ_CHANNEL(gEeprom.ScreenChannel[vfo_num]) &&
               gEeprom.TX_VFO == vfo_num) { // user entering a frequency
      const char *ascii = INPUTBOX_GetAscii();
      bool isGigaF = frequency >= _1GHz_in_KHz;
      sprintf(String, "%.*s.%.3s", 3 + isGigaF, ascii, ascii + 3 + isGigaF);
      if (!isGigaF) {
        // show the remaining 2 small frequency digits
        UI_PrintStringSmallNormal(String + 7, 113, 0, line + 1);
        String[7] = 0;
        // show the main large frequency digits
        UI_DisplayFrequency(String, 32, line, false);
      } else {
        // show the frequency in the main font
        UI_PrintString(String, 32, 0, line, 8);
      }

      continue;
    } else {
      if (gCurrentFunction == FUNCTION_TRANSMIT) { // transmitting
        if (activeTxVFO == vfo_num)
          frequency = gEeprom.VfoInfo[vfo_num].pTX->Frequency;
      }

      if (IS_MR_CHANNEL(gEeprom.ScreenChannel[vfo_num])) { // it's a channel

        if (gEeprom.MENU_LOCK == false) {
          uint8_t countList = 0;
          uint8_t shiftList = 0;

          if (gMR_ChannelExclude[gEeprom.ScreenChannel[vfo_num]] == false) {
            // show the scan list assigment symbols
            const ChannelAttributes_t att =
                gMR_ChannelAttributes[gEeprom.ScreenChannel[vfo_num]];

            countList = att.scanlist1 + att.scanlist2 + att.scanlist3;

            if (countList == 0) {
              memcpy(p_line0 + 127 - (1 * 6), BITMAP_ScanList0,
                     sizeof(BITMAP_ScanList0));
            } else {
              shiftList = countList;

              if (att.scanlist1) {
                memcpy(p_line0 + 127 - (shiftList * 6), BITMAP_ScanList1,
                       sizeof(BITMAP_ScanList1));
                shiftList--;
              }
              if (att.scanlist2) {
                memcpy(p_line0 + 127 - (shiftList * 6), BITMAP_ScanList2,
                       sizeof(BITMAP_ScanList2));
                shiftList--;
              }
              if (att.scanlist3) {
                memcpy(p_line0 + 127 - (shiftList * 6), BITMAP_ScanList3,
                       sizeof(BITMAP_ScanList3));
              }
            }
          } else {
            memcpy(p_line0 + 127 - (1 * 6), BITMAP_ScanListE,
                   sizeof(BITMAP_ScanListE));
          }

          {
          }
        }

        // compander symbol
        // TODO:  // find somewhere else to put the symbol

        switch (gEeprom.CHANNEL_DISPLAY_MODE) {
        case MDF_FREQUENCY: // show the channel frequency
          sprintf(String, "%3u.%05u", frequency / 100000, frequency % 100000);
          if (frequency < _1GHz_in_KHz) {
            // show the remaining 2 small frequency digits
            UI_PrintStringSmallNormal(String + 7, 113, 0, line + 1);
            String[7] = 0;
            // show the main large frequency digits
            UI_DisplayFrequency(String, 32, line, false);
          } else {
            // show the frequency in the main font
            UI_PrintString(String, 32, 0, line, 8);
          }

          break;

        case MDF_CHANNEL: // show the channel number
          sprintf(String, "CH-%03u", gEeprom.ScreenChannel[vfo_num] + 1);
          UI_PrintString(String, 32, 0, line, 8);
          break;

        case MDF_NAME:      // show the channel name
        case MDF_NAME_FREQ: // show the channel name and frequency

          SETTINGS_FetchChannelName(String, gEeprom.ScreenChannel[vfo_num]);
          if (String[0] ==
              0) { // no channel name, show the channel number instead
            sprintf(String, "CH-%03u", gEeprom.ScreenChannel[vfo_num] + 1);
          }

          if (gEeprom.CHANNEL_DISPLAY_MODE == MDF_NAME) {
            UI_PrintString(String, 32, 0, line, 8);
          } else {
            if (isMainOnly()) {
              UI_PrintString(String, 32, 0, line, 8);
            } else {
              if (activeTxVFO == vfo_num) {
                UI_PrintStringSmallBold(String, 32 + 4, 0, line);
              } else {
                UI_PrintStringSmallNormal(String, 32 + 4, 0, line);
              }
            }

            if (isMainOnly()) {
              sprintf(String, "%3u.%05u", frequency / 100000,
                      frequency % 100000);
              if (frequency < _1GHz_in_KHz) {
                // show the remaining 2 small frequency digits
                UI_PrintStringSmallNormal(String + 7, 113, 0, line + 4);
                String[7] = 0;
                // show the main large frequency digits
                UI_DisplayFrequency(String, 32, line + 3, false);
              } else {
                // show the frequency in the main font
                UI_PrintString(String, 32, 0, line + 3, 8);
              }
            } else {
              sprintf(String, "%03u.%05u", frequency / 100000,
                      frequency % 100000);
              UI_PrintStringSmallNormal(String, 32 + 4, 0, line + 1);
            }
          }

          break;
        }
      } else { // frequency mode
        sprintf(String, "%3u.%05u", frequency / 100000, frequency % 100000);

        if (frequency < _1GHz_in_KHz) {
          // show the remaining 2 small frequency digits
          UI_PrintStringSmallNormal(String + 7, 113, 0, line + 1);
          String[7] = 0;
          // show the main large frequency digits
          UI_DisplayFrequency(String, 32, line, false);
        } else {
          // show the frequency in the main font
          UI_PrintString(String, 32, 0, line, 8);
        }

        // show the channel symbols
        const ChannelAttributes_t att =
            gMR_ChannelAttributes[gEeprom.ScreenChannel[vfo_num]];
        if (att.compander)
          memcpy(p_line0 + 120, BITMAP_compand, sizeof(BITMAP_compand));
      }
    }

    // ************

    { // show the TX/RX level
      int8_t Level = -1;

      if (mode == VFO_MODE_TX) { // TX power level
        /*
        switch (gRxVfo->OUTPUT_POWER)
        {
            case OUTPUT_POWER_LOW1:     Level = 2; break;
            case OUTPUT_POWER_LOW2:     Level = 2; break;
            case OUTPUT_POWER_LOW3:     Level = 2; break;
            case OUTPUT_POWER_LOW4:     Level = 2; break;
            case OUTPUT_POWER_LOW5:     Level = 2; break;
            case OUTPUT_POWER_MID:      Level = 4; break;
            case OUTPUT_POWER_HIGH:     Level = 6; break;
        }

        if (gRxVfo->OUTPUT_POWER == OUTPUT_POWER_MID) {
            Level = 4;
        } else if (gRxVfo->OUTPUT_POWER == OUTPUT_POWER_HIGH) {
            Level = 6;
        } else {
            Level = 2;
        }
        */
        Level = gRxVfo->OUTPUT_POWER - 1;
      } else if (mode == VFO_MODE_RX) { // RX signal level
      }
      if (Level >= 0)
        DrawSmallAntennaAndBars(p_line1 + LCD_WIDTH, Level);
    }

    // ************

    String[0] = '\0';
    const VFO_Info_t *vfoInfo = &gEeprom.VfoInfo[vfo_num];

    // show the modulation symbol
    const char *s = "";
    const ModulationMode_t mod = vfoInfo->Modulation;
    switch (mod) {
    case MODULATION_FM: {
      const FREQ_Config_t *pConfig =
          (mode == VFO_MODE_TX) ? vfoInfo->pTX : vfoInfo->pRX;
      const unsigned int code_type = pConfig->CodeType;
      const char *code_list[] = {"", "CT", "DC", "DC"};
      if (code_type < ARRAY_SIZE(code_list))
        s = code_list[code_type];
      break;
    }
    default:
      break;
    }

#if ENABLE_FEAT_F4HWN
    const FREQ_Config_t *pConfig =
        (mode == VFO_MODE_TX) ? vfoInfo->pTX : vfoInfo->pRX;
    int8_t shift = 0;

    switch ((int)pConfig->CodeType) {
    case 1:
      sprintf(String, "%u.%u", CTCSS_Options[pConfig->Code] / 10,
              CTCSS_Options[pConfig->Code] % 10);
      break;

    case 2:
      sprintf(String, "%03oN", DCS_Options[pConfig->Code]);
      break;

    case 3:
      sprintf(String, "%03oI", DCS_Options[pConfig->Code]);
      break;

    default:
      sprintf(String, "%d.%02uK", vfoInfo->StepFrequency / 100,
              vfoInfo->StepFrequency % 100);
      shift = -10;
    }

    if (gSetting_set_gui) {
      UI_PrintStringSmallNormal(s, LCD_WIDTH + 22, 0, line + 1);
      UI_PrintStringSmallNormal(t, LCD_WIDTH + 2, 0, line + 1);

      if (isMainOnly() && !gDTMF_InputMode) {
        if (shift == 0) {
          UI_PrintStringSmallNormal(String, 2, 0, 6);
        }

        if ((vfoInfo->StepFrequency / 100) < 100) {
          sprintf(String, "%d.%02uK", vfoInfo->StepFrequency / 100,
                  vfoInfo->StepFrequency % 100);
        } else {
          sprintf(String, "%dK", vfoInfo->StepFrequency / 100);
        }
        UI_PrintStringSmallNormal(String, 46, 0, 6);
      }
    } else {
      if ((s != NULL) && (s[0] != '\0')) {
        GUI_DisplaySmallest(s, 58, line == 0 ? 17 : 49, false, true);
      }

      if ((t != NULL) && (t[0] != '\0')) {
        GUI_DisplaySmallest(t, 3, line == 0 ? 17 : 49, false, true);
      }

      GUI_DisplaySmallest(String, 68 + shift, line == 0 ? 17 : 49, false, true);

      // sprintf(String, "%d.%02u", vfoInfo->StepFrequency / 100,
      // vfoInfo->StepFrequency % 100); GUI_DisplaySmallest(String, 91, line ==
      // 0 ? 2 : 34, false, true);
    }
#else
    UI_PrintStringSmallNormal(s, LCD_WIDTH + 24, 0, line + 1);
#endif

    if (state == VFO_STATE_NORMAL ||
        state == VFO_STATE_ALARM) { // show the TX power
      uint8_t currentPower = vfoInfo->OUTPUT_POWER % 8;
      uint8_t arrowPos = 19;
      bool userPower = false;

      if (currentPower == OUTPUT_POWER_USER) {
        currentPower = gSetting_set_pwr;
        userPower = true;
      } else {
        currentPower--;
        userPower = false;
      }

      if (gSetting_set_gui) {
        const char pwr_short[][3] = {"L1", "L2", "L3", "L4", "L5", "M", "H"};
        // sprintf(String, "%s", pwr_short[currentPower]);
        // UI_PrintStringSmallNormal(String, LCD_WIDTH + 42, 0, line + 1);
        UI_PrintStringSmallNormal(pwr_short[currentPower], LCD_WIDTH + 42, 0,
                                  line + 1);

        arrowPos = 38;
      } else {
        const char pwr_long[][5] = {"LOW1", "LOW2", "LOW3", "LOW4",
                                    "LOW5", "MID",  "HIGH"};
        // sprintf(String, "%s", pwr_long[currentPower]);
        // GUI_DisplaySmallest(String, 24, line == 0 ? 17 : 49, false, true);
        GUI_DisplaySmallest(pwr_long[currentPower], 24, line == 0 ? 17 : 49,
                            false, true);
      }

      if (userPower == true) {
        memcpy(p_line0 + 256 + arrowPos, BITMAP_PowerUser,
               sizeof(BITMAP_PowerUser));
      }
    }

    if (vfoInfo->freq_config_RX.Frequency !=
        vfoInfo->freq_config_TX.Frequency) { // show the TX offset symbol
      int i = vfoInfo->TX_OFFSET_FREQUENCY_DIRECTION % 3;

      const char dir_list[][2] = {"", "+", "-", "D"};

      if (gTxVfo->TX_OFFSET_FREQUENCY_DIRECTION != 0 &&
          gTxVfo->pTX == &gTxVfo->freq_config_RX &&
          !vfoInfo->FrequencyReverse) {
        i = 3;
      }

#if ENABLE_FEAT_F4HWN
      if (gSetting_set_gui) {
        UI_PrintStringSmallNormal(dir_list[i], LCD_WIDTH + 60, 0, line + 1);
      } else {
        if (i == 3) {
          GUI_DisplaySmallest(dir_list[i], 43, line == 0 ? 17 : 49, false,
                              true);
        } else {
          UI_PrintStringSmallNormal(dir_list[i], LCD_WIDTH + 41, 0, line + 1);
        }
      }
#else
      UI_PrintStringSmallNormal(dir_list[i], LCD_WIDTH + 54, 0, line + 1);
#endif
    }

    // show the TX/RX reverse symbol
    if (vfoInfo->FrequencyReverse)
#if ENABLE_FEAT_F4HWN
    {
      if (gSetting_set_gui) {
        UI_PrintStringSmallNormal("R", LCD_WIDTH + 68, 0, line + 1);
      } else {
        GUI_DisplaySmallest("R", 51, line == 0 ? 17 : 49, false, true);
      }
    }
#else
      UI_PrintStringSmallNormal("R", LCD_WIDTH + 62, 0, line + 1);
#endif

#if ENABLE_FEAT_F4HWN
    bool narrower = 0;

    if (vfoInfo->CHANNEL_BANDWIDTH == BANDWIDTH_NARROW &&
        gSetting_set_nfm == 1) {
      narrower = 1;
    }

    if (gSetting_set_gui) {
      const char *bandWidthNames[] = {"W", "N", "N+"};
      UI_PrintStringSmallNormal(
          bandWidthNames[vfoInfo->CHANNEL_BANDWIDTH + narrower], LCD_WIDTH + 80,
          0, line + 1);
    } else {
      const char *bandWidthNames[] = {"WIDE", "NAR", "NAR+"};
      GUI_DisplaySmallest(bandWidthNames[vfoInfo->CHANNEL_BANDWIDTH + narrower],
                          91, line == 0 ? 17 : 49, false, true);
    }
#else
    if (vfoInfo->CHANNEL_BANDWIDTH == BANDWIDTH_NARROW)
      UI_PrintStringSmallNormal("N", LCD_WIDTH + 70, 0, line + 1);
#endif

    /*
    if(isMainVFO)
    {
        if(gMonitor)
        {
            sprintf(String, "%s", "MONI");
        }

        if (gSetting_set_gui)
        {
            if(!gMonitor)
            {
                sprintf(String, "SQL%d", gEeprom.SQUELCH_LEVEL);
            }
            UI_PrintStringSmallNormal(String, LCD_WIDTH + 98, 0, line + 1);
        }
        else
        {
            if(!gMonitor)
            {
                sprintf(String, "SQL%d", gEeprom.SQUELCH_LEVEL);
            }
            GUI_DisplaySmallest(String, 110, line == 0 ? 17 : 49, false, true);
        }
    }
    */
    if (isMainVFO) {
      if (gMonitor) {
        strcpy(String, "MONI");
      } else {
        sprintf(String, "SQL%d", gEeprom.SQUELCH_LEVEL);
      }

      if (gSetting_set_gui) {
        UI_PrintStringSmallNormal(String, LCD_WIDTH + 98, 0, line + 1);
      } else {
        GUI_DisplaySmallest(String, 110, line == 0 ? 17 : 49, false, true);
      }
    }
  }

  if (center_line == CENTER_LINE_NONE) { // we're free to use the middle line

    const bool rx = FUNCTION_IsRx();

    if (gSetting_mic_bar && gCurrentFunction == FUNCTION_TRANSMIT) {
      center_line = CENTER_LINE_AUDIO_BAR;
      UI_DisplayAudioBar();
    } else

        if (rx) {
      center_line = CENTER_LINE_RSSI;
      DisplayRSSIBar(false);
    } else if (rx || gCurrentFunction == FUNCTION_FOREGROUND ||
               gCurrentFunction == FUNCTION_POWER_SAVE) {
#if 1
      if (gSetting_live_DTMF_decoder &&
          gDTMF_RX_live[0] != 0) { // show live DTMF decode
        const unsigned int len = strlen(gDTMF_RX_live);
        const unsigned int idx =
            (len > (17 - 5)) ? len - (17 - 5) : 0; // limit to last 'n' chars

        if (gScreenToDisplay != DISPLAY_MAIN)
          return;

        center_line = CENTER_LINE_DTMF_DEC;

        sprintf(String, "DTMF %s", gDTMF_RX_live + idx);
        if (isMainOnly()) {
          UI_PrintStringSmallNormal(String, 2, 0, 5);
        } else {
          UI_PrintStringSmallNormal(String, 2, 0, 3);
        }
      }
#else
      if (gSetting_live_DTMF_decoder &&
          gDTMF_RX_index > 0) { // show live DTMF decode
        const unsigned int len = gDTMF_RX_index;
        const unsigned int idx =
            (len > (17 - 5)) ? len - (17 - 5) : 0; // limit to last 'n' chars

        if (gScreenToDisplay != DISPLAY_MAIN ||
            gDTMF_CallState != DTMF_CALL_STATE_NONE)
          return;

        center_line = CENTER_LINE_DTMF_DEC;

        sprintf(String, "DTMF %s", gDTMF_RX_live + idx);
        UI_PrintStringSmallNormal(String, 2, 0, 3);
      }
#endif
    }
  }

  // #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
  // if(gEeprom.MENU_LOCK == false)
  //{
  // #endif
  if (isMainOnly() && !gDTMF_InputMode) {
    sprintf(String, "VFO %s", activeTxVFO ? "B" : "A");
    UI_PrintStringSmallBold(String, 92, 0, 6);
    for (uint8_t i = 92; i < 128; i++) {
      gFrameBuffer[6][i] ^= 0x7F;
    }
  }
  // #ifdef ENABLE_FEAT_F4HWN_RESCUE_OPS
  // }
  // #endif

  ST7565_BlitFullScreen();
}

// ***************************************************************************
