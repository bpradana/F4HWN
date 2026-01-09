# Bad Apple on UV-K5 (F4HWN Firmware)

This repository includes tooling and a firmware hook to play a Bad Apple
animation on the UV-K5 128×64 monochrome LCD.

## Quick Start

1. Install Python dependencies:
   ```bash
   pip install opencv-python Pillow
   ```
2. Generate frame data:
   ```bash
   ./tools/badapple/process_badapple.py /path/to/badapple.mp4 \
     --fps 15 \
     --header App/app/bad_apple_frames.h
   ```
3. Enable the feature in your build:
   ```bash
   cmake -B build -DENABLE_BAD_APPLE=ON
   cmake --build build
   ```

## Boot Trigger

Hold **PTT + \*** during boot to start playback. Press **EXIT** or **PTT** to exit.

## Notes

- The provided `App/app/bad_apple_frames.h` is a placeholder containing a single
  blank frame. Replace it with generated output from the Python script above.
- Adjust FPS or frame count to fit flash constraints.
- The frame data is stored as RLE in a linear row-major pixel order.
