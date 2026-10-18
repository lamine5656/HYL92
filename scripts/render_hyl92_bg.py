#!/usr/bin/env python3
"""Render the HYL92 home background as the exact vsh_asset DDS layout.

The console ships its shell backgrounds as 1920x1080 BGRA8888 DDS textures:
8 294 548 bytes = 128-byte DDS header + 20-byte DX10 extension + one
1920x1080x4 surface (verified against the FTP listing of /system_ex/vsh_asset).
This script reproduces that container byte-for-byte so the bootstrapper can
replant the artwork over any stock background of the same size.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from PIL import Image

TARGET_WIDTH = 1920
TARGET_HEIGHT = 1080
HEADER_SIZE = 148  # 128 legacy header + 20-byte DDS_HEADER_DXT10
DXGI_B8G8R8A8_UNORM = 87
DDS_DIMENSION_TEXTURE2D = 2

DDSD_CAPS = 0x1
DDSD_HEIGHT = 0x2
DDSD_WIDTH = 0x4
DDSD_PITCH = 0x8
DDSD_PIXELFORMAT = 0x1000
DDPF_ALPHAPIXELS = 0x1
DDPF_RGB = 0x40
DDSCAPS_TEXTURE = 0x1000


def build_header() -> bytes:
    return b"".join(
        (
            b"DDS ",                            # magic
            struct.pack("<I", 124),             # dwSize
            struct.pack(
                "<I",
                DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH |
                DDSD_PIXELFORMAT,
            ),
            struct.pack("<I", TARGET_HEIGHT),
            struct.pack("<I", TARGET_WIDTH),
            struct.pack("<I", TARGET_WIDTH * 4),  # dwPitchOrLinearSize
            struct.pack("<I", 0),               # dwDepth
            struct.pack("<I", 0),               # dwMipMapCount
            struct.pack("<11I", *([0] * 11)),   # dwReserved1
            struct.pack("<I", 32),              # pf.dwSize
            struct.pack("<I", DDPF_RGB | DDPF_ALPHAPIXELS),
            b"DX10",                            # pf.dwFourCC
            struct.pack("<I", 32),              # pf.dwRGBBitCount
            struct.pack("<I", 0x00FF0000),      # R mask (byte order is BGRA)
            struct.pack("<I", 0x0000FF00),      # G mask
            struct.pack("<I", 0x000000FF),      # B mask
            struct.pack("<I", 0xFF000000),      # A mask
            struct.pack("<I", DDSCAPS_TEXTURE),
            struct.pack("<4I", 0, 0, 0, 0),     # dwCaps2..dwReserved2
            struct.pack("<I", DXGI_B8G8R8A8_UNORM),  # dx10.dxgiFormat
            struct.pack("<I", DDS_DIMENSION_TEXTURE2D),
            struct.pack("<I", 0),               # dx10.miscFlag
            struct.pack("<I", 1),               # dx10.arraySize
            struct.pack("<I", 0),               # dx10.miscFlags2
        )
    )


def build_texture(source: Path) -> bytes:
    image = Image.open(source).convert("RGBA")
    if image.size != (TARGET_WIDTH, TARGET_HEIGHT):
        image = image.resize(
            (TARGET_WIDTH, TARGET_HEIGHT), Image.LANCZOS
        )

    r, g, b, a = image.split()
    # vsh_asset backgrounds are BGRA8888 on disk: reorder R and B.
    bgra = Image.merge("RGBA", (b, g, r, a))
    surface = bgra.tobytes()

    texture = build_header() + surface
    if len(texture) != HEADER_SIZE + TARGET_WIDTH * TARGET_HEIGHT * 4:
        raise SystemExit("unexpected texture payload size")
    return texture


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    texture = build_texture(args.source)
    args.output.write_bytes(texture)
    print(
        f"{args.output} : {len(texture)} bytes "
        f"({TARGET_WIDTH}x{TARGET_HEIGHT} BGRA8888, {HEADER_SIZE}-byte header)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
