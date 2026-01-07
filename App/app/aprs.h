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

#ifndef APP_APRS_H
#define APP_APRS_H

#ifdef ENABLE_FEAT_F4HWN_APRS

#include <stdbool.h>
#include <stdint.h>

#include "driver/keyboard.h"

#define APRS_MAX_INFO_LEN 96
#define APRS_LOG_CAPACITY 8

typedef struct {
    char     source[12];
    char     destination[12];
    char     path[48];
    char     info[APRS_MAX_INFO_LEN];
    uint32_t sequence;
    int16_t  rssi;
} AprsPacket_t;

void        APRS_Enter(void);
void        APRS_Leave(void);
bool        APRS_IsActive(void);
void        APRS_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);
void        APRS_HandleFskBurst(void);
uint8_t     APRS_GetPacketCount(void);
const AprsPacket_t *APRS_GetPacket(uint8_t index);
uint8_t     APRS_GetSelection(void);
void        APRS_MoveSelection(int8_t delta);
void        APRS_ClearLog(void);
bool        APRS_IsDetailView(void);
void        APRS_ToggleDetail(void);

#endif

#endif
