#ifndef BAD_APPLE_FRAMES_H
#define BAD_APPLE_FRAMES_H

#include <stdint.h>

#define TOTAL_FRAMES 1
#define FRAME_RATE 15

const uint32_t frame_offsets[TOTAL_FRAMES] = {
    0
};

const uint8_t frames_data[] = {
    // Frame 0 (all black)
    255, 0, 255, 0, 255, 0, 255, 0,
    255, 0, 255, 0, 255, 0, 255, 0,
    255, 0, 255, 0, 255, 0, 255, 0,
    255, 0, 255, 0, 255, 0, 255, 0,
    32, 0,
};

const uint32_t total_data_size = sizeof(frames_data);

#endif
