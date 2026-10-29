#!/usr/bin/env python3
"""Build the bootstrapper's palette-indexed shell background.

Why the artwork is not embedded as BGRA (or LZMA-compressed BGRA)
------------------------------------------------------------------
HyperHEN.elf embeds ``bootstrapper.elf.lzma``: the bootstrapper is packed with
LZMA by scripts/pack_bootstrapper.sh before the unpacker embeds it. The 8.3 MB
BGRA8888 artwork therefore already reaches the final ELF compressed (3.6 MB)
whatever form it has inside bootstrapper.elf -- pre-compressing the DDS at
build time is a no-op, and nesting LZMA actually costs ~70 KB more.

What actually shrinks the payload is carrying *fewer pixels*: the same artwork
as an 8bpp indexed image (256 colours, default 1280x720) packs to ~0.55 MB
instead of 3.6 MB. bootstrap_assets.c expands it back to the stock
1920x1080 BGRA8888 DDS in memory before converging /system_ex/vsh_asset.

Container (little endian):
    0   char magic[4]       "HYBG"
    4   u16  width          source width
    6   u16  height         source height
    8   u16  out_width      stock width (1920)
    10  u16  out_height     stock height (1080)
    12  u16  colors         palette entries (<= 256)
    14  u16  dds_header_size stock DDS header length (148 with the DX10 block)
    16  u32  palette_bytes  colors * 4
    20  u32  index_bytes    width * height
    24  dds_header          copied verbatim from the master .dds
        palette             BGRA, colors entries
        indices             width * height bytes

Run by bootstrapper/CMakeLists.txt; --size is HYPERHEN_BG_PAL_SIZE.
Requires Pillow (build host only).
"""

import argparse
import struct
import sys

from PIL import Image

MAGIC = b"HYBG"
STOCK_W, STOCK_H = 1920, 1080


def dds_header_size(blob):
    if blob[:4] != b"DDS ":
        raise SystemExit("input is not a DDS file")
    base = 4 + struct.unpack_from("<I", blob, 4)[0]
    if blob[84:88] == b"DX10":
        base += 20
    return base


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input", required=True, help="master .dds (BGRA8888)")
    ap.add_argument("--output", required=True, help="container to write")
    ap.add_argument("--size", default="1280x720", help="source resolution WxH")
    ap.add_argument("--colors", type=int, default=256, help="palette entries")
    args = ap.parse_args()

    try:
        width, height = (int(v) for v in args.size.lower().split("x"))
    except ValueError:
        raise SystemExit(f"bad --size {args.size!r}, expected WxH")
    if not 1 <= args.colors <= 256:
        raise SystemExit("--colors must be 1..256")

    blob = open(args.input, "rb").read()
    hdr_size = dds_header_size(blob)
    src_h, src_w = struct.unpack_from("<II", blob, 12)
    if (src_w, src_h) != (STOCK_W, STOCK_H):
        raise SystemExit(f"unexpected DDS size {src_w}x{src_h}")
    pixels = blob[hdr_size:]
    if len(pixels) != STOCK_W * STOCK_H * 4:
        raise SystemExit("unexpected DDS pixel payload size")
    if len(blob) != hdr_size + STOCK_W * STOCK_H * 4:
        raise SystemExit("trailing bytes after DDS pixels")

    image = Image.frombuffer("RGBA", (STOCK_W, STOCK_H), pixels, "raw", "BGRA", 0, 1)
    if (width, height) != (STOCK_W, STOCK_H):
        image = image.resize((width, height), Image.LANCZOS)

    quantized = image.convert("RGB").quantize(
        colors=args.colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE
    )
    palette = quantized.getpalette()[: args.colors * 3]
    if len(palette) < args.colors * 3:
        raise SystemExit("quantizer returned a short palette")

    bgra = bytearray()
    for i in range(args.colors):
        r, g, b = palette[3 * i : 3 * i + 3]
        bgra += bytes((b, g, r, 0xFF))

    indices = bytes(quantized.tobytes())
    if len(indices) != width * height:
        raise SystemExit("unexpected index buffer size")

    out = bytearray()
    out += MAGIC
    out += struct.pack("<HHHHHH", width, height, STOCK_W, STOCK_H, args.colors, hdr_size)
    out += struct.pack("<II", len(bgra), len(indices))
    out += blob[:hdr_size]
    out += bgra
    out += indices

    with open(args.output, "wb") as f:
        f.write(out)

    print(
        f"{args.output}: {width}x{height} / {args.colors} colours "
        f"-> {len(out)} bytes (master {len(blob)} bytes, header {hdr_size})"
    )


if __name__ == "__main__":
    sys.exit(main())
