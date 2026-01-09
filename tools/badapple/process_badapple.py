#!/usr/bin/env python3
"""Bad Apple preprocessing pipeline.

Extracts frames from a video, converts to 128x64 1-bit bitmaps, compresses
with RLE, and emits a C header for firmware use.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path
from typing import Iterable, List, Sequence

import cv2
from PIL import Image

LCD_WIDTH = 128
LCD_HEIGHT = 64
PIXELS_PER_FRAME = LCD_WIDTH * LCD_HEIGHT


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Process Bad Apple video for UV-K5.")
    parser.add_argument("video", type=Path, help="Path to input video (mp4/avi).")
    parser.add_argument(
        "--fps",
        type=float,
        default=15.0,
        help="Target frame rate after extraction (default: 15).",
    )
    parser.add_argument(
        "--frames-dir",
        type=Path,
        default=Path("frames"),
        help="Directory to write extracted frames (default: ./frames).",
    )
    parser.add_argument(
        "--header",
        type=Path,
        default=Path("frames_data.h"),
        help="Output header path (default: ./frames_data.h).",
    )
    parser.add_argument(
        "--keep-frames",
        action="store_true",
        help="Keep extracted PNG frames after header generation.",
    )
    parser.add_argument(
        "--threshold",
        type=int,
        default=127,
        help="Threshold for B/W conversion (0-255, default: 127).",
    )
    parser.add_argument(
        "--end-second",
        type=int,
        default=None,
        help="End of video",
    )
    return parser.parse_args()


def ensure_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)


def extract_frames(
    video_path: Path,
    frames_dir: Path,
    target_fps: float,
    threshold: int,
    end_second: float | None = None,
) -> List[Path]:
    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        raise RuntimeError(f"Unable to open video: {video_path}")

    input_fps = cap.get(cv2.CAP_PROP_FPS) or 0.0
    if input_fps <= 0:
        raise RuntimeError("Unable to determine input FPS from video.")

    input_fps = cap.get(cv2.CAP_PROP_FPS) or 0.0
    if input_fps <= 0:
        raise RuntimeError("Unable to determine input FPS from video.")

    max_frame = None
    if end_second is not None:
        max_frame = int(end_second * input_fps)

    frame_skip = max(int(round(input_fps / target_fps)), 1)

    ensure_dir(frames_dir)
    frame_paths: List[Path] = []
    frame_index = 0
    output_index = 0

    while True:
        success, frame = cap.read()
        if not success:
            break

        if max_frame is not None and frame_index >= max_frame:
            break

        if frame_index % frame_skip == 0:
            resized = cv2.resize(
                frame, (LCD_WIDTH, LCD_HEIGHT), interpolation=cv2.INTER_LANCZOS4
            )
            gray = cv2.cvtColor(resized, cv2.COLOR_BGR2GRAY)
            _, bw = cv2.threshold(gray, threshold, 255, cv2.THRESH_BINARY)

            image = Image.fromarray(bw).convert("1")
            frame_path = frames_dir / f"frame_{output_index:04d}.png"
            image.save(frame_path, format="PNG")
            frame_paths.append(frame_path)
            output_index += 1

        frame_index += 1

    cap.release()
    print(f"Extracted {len(frame_paths)} frames at ~{input_fps / frame_skip:.2f} FPS.")
    return frame_paths


def iter_pixels(image: Image.Image) -> Iterable[int]:
    data = list(image.getdata())
    for value in data:
        yield 1 if value else 0


def compress_frame_rle(image_path: Path) -> bytes:
    image = Image.open(image_path).convert("1")
    pixels = list(iter_pixels(image))
    if not pixels:
        return b""

    output = bytearray()
    current_color = pixels[0]
    count = 1

    for pixel in pixels[1:]:
        if pixel == current_color and count < 255:
            count += 1
        else:
            output.extend(struct.pack("BB", count, current_color))
            current_color = pixel
            count = 1

    output.extend(struct.pack("BB", count, current_color))
    return bytes(output)


def generate_c_header(
    compressed_frames: Sequence[bytes],
    header_path: Path,
    frame_rate: float,
) -> None:
    total_frames = len(compressed_frames)
    offsets: List[int] = []
    running = 0
    for frame_data in compressed_frames:
        offsets.append(running)
        running += len(frame_data)

    header_lines = [
        "#ifndef FRAMES_DATA_H",
        "#define FRAMES_DATA_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define TOTAL_FRAMES {total_frames}",
        f"#define FRAME_RATE {int(round(frame_rate))}",
        "",
        "const uint32_t frame_offsets[TOTAL_FRAMES] = {",
    ]

    for index, offset in enumerate(offsets):
        comma = "," if index + 1 < total_frames else ""
        header_lines.append(f"    {offset}{comma}")

    header_lines.extend(
        [
            "};",
            "",
            "const uint8_t frames_data[] = {",
        ]
    )

    for frame_index, frame_data in enumerate(compressed_frames):
        header_lines.append(f"    // Frame {frame_index}")
        for i in range(0, len(frame_data), 16):
            chunk = ", ".join(str(b) for b in frame_data[i : i + 16])
            header_lines.append(f"    {chunk},")

    header_lines.extend(
        [
            "};",
            "",
            "const uint32_t total_data_size = sizeof(frames_data);",
            "",
            "#endif",
            "",
        ]
    )

    header_path.write_text("\n".join(header_lines), encoding="utf-8")

    original_size = total_frames * (PIXELS_PER_FRAME // 8)
    compressed_size = sum(len(data) for data in compressed_frames)
    ratio = compressed_size / original_size if original_size else 0

    print("Compression statistics:")
    print(f"  Frames: {total_frames}")
    print(f"  Original size: {original_size} bytes")
    print(f"  Compressed size: {compressed_size} bytes")
    print(f"  Compression ratio: {ratio:.2%}")
    if total_frames:
        print(f"  Avg bytes/frame: {compressed_size / total_frames:.1f}")


def main() -> None:
    args = parse_args()
    frames = extract_frames(
        args.video, args.frames_dir, args.fps, args.threshold, args.end_second
    )
    if not frames:
        raise RuntimeError("No frames extracted.")

    compressed_frames = [compress_frame_rle(path) for path in frames]
    generate_c_header(compressed_frames, args.header, args.fps)

    if not args.keep_frames:
        for path in frames:
            path.unlink(missing_ok=True)
        try:
            args.frames_dir.rmdir()
        except OSError:
            pass


if __name__ == "__main__":
    main()
