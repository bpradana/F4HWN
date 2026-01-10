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

#include "ui/aprs.h"

#include <string.h>

#include "app/aprs.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "radio.h"
#include "ui/helper.h"
#include "ui/inputbox.h"

#define APRS_LIST_LINES 4
#define APRS_PAYLOAD_LINE_LEN 20
#define APRS_PREVIEW_LEN 10

static void APRS_RenderList(const uint8_t message_count)
{
    char line[32] = {0};
    uint8_t selected = APRS_GetSelectedIndex();

    if (message_count == 0U) {
        UI_PrintStringSmallNormal("No APRS frames", 0, 127, 3);
    } else {
        if (selected >= message_count) {
            selected = (uint8_t)(message_count - 1U);
        }

        uint8_t top_index = 0;
        if (selected >= APRS_LIST_LINES) {
            top_index = (uint8_t)(selected - (APRS_LIST_LINES - 1U));
        }

        for (uint8_t i = 0; i < APRS_LIST_LINES; i++) {
            uint8_t msg_index = (uint8_t)(top_index + i);
            if (msg_index >= message_count) {
                break;
            }

            APRS_Message_t message;
            if (!APRS_GetMessage(msg_index, &message)) {
                continue;
            }

            char payload_preview[APRS_PREVIEW_LEN + 1U];
            strncpy(payload_preview, message.payload, APRS_PREVIEW_LEN);
            payload_preview[APRS_PREVIEW_LEN] = '\0';

            snprintf(line, sizeof(line), "%c%-9.9s %-.10s",
                     (msg_index == selected) ? '>' : ' ',
                     message.source,
                     payload_preview);
            UI_PrintStringSmallNormal(line, 0, 0, (uint8_t)(1 + i));
        }
    }

    if (APRS_IsInputActive()) {
        if (gInputBoxIndex == 0) {
            uint32_t frequency = gRxVfo->freq_config_RX.Frequency;
            snprintf(line, sizeof(line), "FREQ:%3u.%05u", frequency / 100000U, frequency % 100000U);
        } else {
            const char *ascii = INPUTBOX_GetAscii();
            snprintf(line, sizeof(line), "FREQ:%.3s.%.3s", ascii, ascii + 3);
        }
        UI_PrintStringSmallNormal(line, 0, 0, 5);
    } else {
        snprintf(line, sizeof(line), "Msgs:%u", message_count);
        UI_PrintStringSmallNormal(line, 0, 0, 5);
    }

    UI_PrintStringSmallNormal("UP/DN Sel MENU H9 Brk", 0, 0, 6);
    UI_PrintStringSmallNormal("EXIT Back 5 Freq 7Clr", 0, 0, 7);
}

static void APRS_RenderDetail(const APRS_Message_t *message)
{
    char line[32] = {0};
    uint32_t seconds = message->timestamp_ms / 1000U;

    UI_PrintStringSmallNormal("APRS DETAIL", 0, 127, 0);

    snprintf(line, sizeof(line), "SRC:%-.9s", message->source);
    UI_PrintStringSmallNormal(line, 0, 0, 1);

    snprintf(line, sizeof(line), "DST:%-.9s", message->destination);
    UI_PrintStringSmallNormal(line, 0, 0, 2);

    snprintf(line, sizeof(line), "T:%lus RSSI:%d", seconds, message->rssi);
    UI_PrintStringSmallNormal(line, 0, 0, 3);

    const char *payload = message->payload;
    for (uint8_t i = 0; i < 3; i++) {
        if (payload[0] == '\0') {
            break;
        }

        strncpy(line, payload, APRS_PAYLOAD_LINE_LEN);
        line[APRS_PAYLOAD_LINE_LEN] = '\0';
        UI_PrintStringSmallNormal(line, 0, 0, (uint8_t)(4 + i));
        payload += APRS_PAYLOAD_LINE_LEN;
    }

    UI_PrintStringSmallNormal("MENU Back EXIT Main", 0, 0, 7);
}

void UI_DisplayAprs(void)
{
    UI_DisplayClear();

    uint8_t message_count = APRS_GetMessageCount();

    if (APRS_IsDetailView() && message_count > 0U) {
        APRS_Message_t message;
        if (APRS_GetMessage(APRS_GetSelectedIndex(), &message)) {
            APRS_RenderDetail(&message);
        } else {
            APRS_RenderList(message_count);
        }
    } else {
        UI_PrintStringSmallNormal("APRS RX", 0, 127, 0);
        APRS_RenderList(message_count);
    }

    ST7565_BlitFullScreen();
}
