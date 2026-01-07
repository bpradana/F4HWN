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

#ifndef APP_APRS_H
#define APP_APRS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/keyboard.h"

typedef enum APRS_State_t {
    APRS_READY = 0,
    APRS_RECEIVING,
    APRS_MESSAGE_VIEW,
} APRS_State_t;

#define APRS_MAX_MESSAGES 8
#define APRS_CALLSIGN_LEN 10
#define APRS_PAYLOAD_LEN 256

typedef struct APRS_Message_t {
    uint32_t timestamp_ms;
    char source[APRS_CALLSIGN_LEN];
    char destination[APRS_CALLSIGN_LEN];
    char payload[APRS_PAYLOAD_LEN];
    int16_t rssi;
} APRS_Message_t;

void APRS_Init(void);
void APRS_StartRx(void);
void APRS_StopRx(void);
void APRS_OnAudioSamples(const int16_t *samples, uint16_t count, uint32_t sample_rate, int16_t rssi);
uint8_t APRS_GetMessageCount(void);
bool APRS_GetMessage(uint8_t index, APRS_Message_t *out_message);
APRS_State_t APRS_GetState(void);
void APRS_ClearMessages(void);
uint8_t APRS_GetSelectedIndex(void);
bool APRS_IsDetailView(void);
bool APRS_IsInputActive(void);
void APRS_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

#endif
