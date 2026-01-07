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

#include "ui/aprs.h"

#ifdef ENABLE_FEAT_F4HWN_APRS

#include <string.h>

#include "app/aprs.h"
#include "bitmaps.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "helper/battery.h"
#include "radio.h"
#include "ui/battery.h"
#include "ui/helper.h"

#define APRS_LIST_ROWS 3
#define APRS_PATH_DISPLAY_LEN 24

static void UI_DisplayAprsDetail(const AprsPacket_t *packet);
static void UI_DisplayAprsList(uint8_t count);
static void APRS_DrawHeader(uint32_t frequency);

void UI_DisplayAprs(void)
{
    UI_DisplayClear();

    const uint32_t frequency = gRxVfo->freq_config_RX.Frequency;
    APRS_DrawHeader(frequency);

    const uint8_t count = APRS_GetPacketCount();
    if (count == 0) {
        UI_PrintString("Waiting for packets...", 2, 127, 4, 8);
        ST7565_BlitFullScreen();
        return;
    }

    if (APRS_IsDetailView()) {
        const AprsPacket_t *packet = APRS_GetPacket(APRS_GetSelection());
        if (packet != NULL) {
            UI_DisplayAprsDetail(packet);
        }
    } else {
        UI_DisplayAprsList(count);
    }

    ST7565_BlitFullScreen();
}

static void UI_DisplayAprsList(uint8_t count)
{
    char line[32];
    const uint8_t selection = APRS_GetSelection();
    uint8_t start = 0;

    if (selection >= APRS_LIST_ROWS)
        start = selection - (APRS_LIST_ROWS - 1);

    if (start + APRS_LIST_ROWS > count)
        start = (count > APRS_LIST_ROWS) ? (count - APRS_LIST_ROWS) : 0;

    for (uint8_t row = 0; row < APRS_LIST_ROWS; row++) {
        const uint8_t index = start + row;
        if (index >= count)
            break;

        const AprsPacket_t *packet = APRS_GetPacket(index);
        if (packet == NULL)
            break;

        snprintf(line, sizeof(line), "%c%s>%s",
                 (index == selection) ? '>' : ' ',
                 packet->source,
                 packet->destination);
        UI_PrintStringSmallNormal(line, 0, 127, 4 + (row * 2));

        if (packet->path[0] != 0) {
            snprintf(line, sizeof(line), "via %.*s", APRS_PATH_DISPLAY_LEN, packet->path);
            UI_PrintStringSmallNormal(line, 0, 127, 5 + (row * 2));
        } else if (packet->info[0] != 0) {
            strncpy(line, packet->info, sizeof(line) - 1);
            line[sizeof(line) - 1] = 0;
            UI_PrintStringSmallNormal(line, 0, 127, 5 + (row * 2));
        }
    }
}

static void UI_DisplayAprsDetail(const AprsPacket_t *packet)
{
    char line[32];
    snprintf(line, sizeof(line), "%s>%s", packet->source, packet->destination);
    UI_PrintStringSmallNormal(line, 0, 127, 4);

    if (packet->path[0] != 0) {
        snprintf(line, sizeof(line), "via %.*s", APRS_PATH_DISPLAY_LEN, packet->path);
        UI_PrintStringSmallNormal(line, 0, 127, 5);
    } else {
        snprintf(line, sizeof(line), "RSSI %d dBm", packet->rssi);
        UI_PrintStringSmallNormal(line, 0, 127, 5);
    }

    const char *info = packet->info;
    uint8_t lineIndex = 6;
    while (*info != 0 && lineIndex < 10) {
        char snippet[22];
        uint8_t len = 0;
        while (info[len] != 0 && info[len] != '\n' && len < (sizeof(snippet) - 1)) {
            snippet[len] = info[len];
            len++;
        }
        snippet[len] = 0;

        UI_PrintStringSmallNormal(snippet, 0, 127, lineIndex++);

        if (info[len] == '\n')
            len++;
        info += len;
    }
}

static void APRS_DrawHeader(uint32_t frequency)
{
    char freqString[16];

    memset(gStatusLine, 0, sizeof(gStatusLine));
    GUI_DisplaySmallest("APRS MON", 0, 1, true, true);

    uint8_t battery[sizeof(BITMAP_BatteryLevel1)];
    UI_DrawBattery(battery, gBatteryDisplayLevel, gLowBatteryBlink);
    memcpy(gStatusLine + (LCD_WIDTH - sizeof(battery)), battery, sizeof(battery));

    ST7565_BlitStatusLine();

    snprintf(freqString, sizeof(freqString), "%3lu.%05lu",
             (unsigned long)(frequency / 100000UL),
             (unsigned long)(frequency % 100000UL));
    UI_PrintStringSmallNormal(freqString, 8, 127, 0);
}

#endif
