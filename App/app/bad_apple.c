/* Copyright 2024
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

#include "app/bad_apple.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app/bad_apple_frames.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "driver/system.h"

#define BAD_APPLE_PIXELS (128U * 64U)

static bool BAD_APPLE_ShouldExit(void)
{
    if (GPIO_IsPttPressed())
        return true;

    return KEYBOARD_Poll() == KEY_EXIT;
}

static void BAD_APPLE_DecodeFrame(const uint8_t *compressed_data, uint32_t data_size)
{
    uint32_t comp_pos = 0;
    uint32_t pixel_pos = 0;

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));

    while (comp_pos + 1 < data_size && pixel_pos < BAD_APPLE_PIXELS)
    {
        uint8_t count = compressed_data[comp_pos++];
        uint8_t color = compressed_data[comp_pos++];

        for (uint8_t i = 0; i < count && pixel_pos < BAD_APPLE_PIXELS; i++)
        {
            if (color)
            {
                uint8_t x = pixel_pos % 128U;
                uint8_t y = pixel_pos / 128U;
                gFrameBuffer[y / 8][x] |= (uint8_t)(1U << (y % 8U));
            }
            pixel_pos++;
        }
    }
}

void BAD_APPLE_Play(void)
{
    const uint32_t frame_duration_ms = 1000U / FRAME_RATE;

    for (uint32_t frame = 0; frame < TOTAL_FRAMES; frame++)
    {
        if (BAD_APPLE_ShouldExit())
            break;

        uint32_t offset = frame_offsets[frame];
        uint32_t next_offset = (frame + 1 < TOTAL_FRAMES)
            ? frame_offsets[frame + 1]
            : total_data_size;
        uint32_t frame_size = next_offset - offset;

        BAD_APPLE_DecodeFrame(&frames_data[offset], frame_size);
        ST7565_BlitFullScreen();
        SYSTEM_DelayMs(frame_duration_ms);
    }
}
