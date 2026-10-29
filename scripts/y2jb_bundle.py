#!/usr/bin/env python3
# Copyright (C) 2026 OnionHEN / LightningMods
"""Boot HyperHEN straight from the Y2JB payload (download0) bundle.

The itsPLK Y2JB chain is:

    Relapse.js (kernel exploit) -> kexp.bin -> elfldr on :9021
      -> main.js streams AUTOLOAD_ELF to elfldr
        -> ps5-unified-autoloader.elf: reads autoload.txt, else starts Payload Manager

Nothing in that chain knows about HyperHEN, so the stock bundle always lands in
the Payload Manager web UI. This script rewires the first stage instead of
fighting the second one:

  * copies HyperHEN.elf into the splash_screen cache directory,
  * points main.js' AUTOLOAD_ELF at it, so elfldr receives HyperHEN directly
    (main.js enforces a hard 16 MiB cap: `size > 0x1000000` throws),
  * drops the now unreachable ps5-unified-autoloader.elf / pldmgr_v*.elf.

The result is a drop-in `download0/` tree for the UFS2 image build. HyperHEN
then boots on its own and the only user feedback is its single success or
failure toast, which matches the quiet boot window in libonion_platform.

Usage:
    scripts/y2jb_bundle.py --download0 <dir> --elf HyperHEN.elf [--out <dir>]
"""

import argparse
import pathlib
import re
import shutil
import sys

# main.js refuses anything larger: `if (size < 4n || size > 0x1000000n)`.
AUTOLOAD_SIZE_LIMIT = 0x1000000
SPLASH_SUFFIX = ("cache", "splash_screen")
BUNDLED_ELF = "HyperHEN.elf"
AUTOLOAD_RE = re.compile(r'(const\s+AUTOLOAD_ELF\s*=\s*")([^"]*)(")')
# Stage-one artefacts that only exist to reach the Payload Manager.
REPLACED_FILES = ("ps5-unified-autoloader.elf",)


def fail(message):
    print("error: " + message, file=sys.stderr)
    raise SystemExit(1)


def find_splash_dir(download0):
    """Return the single cache/splash_screen/<base64 url>/ directory."""
    root = download0.joinpath(*SPLASH_SUFFIX)
    if not root.is_dir():
        fail("%s has no %s/" % (download0, "/".join(SPLASH_SUFFIX)))

    candidates = [d for d in root.iterdir() if d.is_dir() and (d / "main.js").is_file()]
    if len(candidates) != 1:
        fail(
            "expected exactly one splash_screen cache directory with main.js, found %d"
            % len(candidates)
        )
    return candidates[0]


def patch_main_js(main_js_path):
    text = main_js_path.read_text(encoding="utf-8", errors="surrogateescape")
    match = AUTOLOAD_RE.search(text)
    if not match:
        fail("could not find the AUTOLOAD_ELF constant in %s" % main_js_path)

    previous = match.group(2)
    if previous == BUNDLED_ELF:
        return previous, False

    main_js_path.write_text(
        text[: match.start()] + match.group(1) + BUNDLED_ELF + match.group(3) + text[match.end() :],
        encoding="utf-8",
        errors="surrogateescape",
    )
    return previous, True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--download0",
        required=True,
        type=pathlib.Path,
        help="Y2JB tree root (the directory copied to /mnt when building download0.dat)",
    )
    parser.add_argument(
        "--elf", required=True, type=pathlib.Path, help="HyperHEN.elf to embed"
    )
    parser.add_argument(
        "--out",
        type=pathlib.Path,
        help="write the patched tree here instead of modifying --download0 in place",
    )
    args = parser.parse_args()

    source_tree = args.download0.resolve()
    if not source_tree.is_dir():
        fail("%s is not a directory" % source_tree)

    elf = args.elf.resolve()
    if not elf.is_file():
        fail("%s does not exist" % elf)

    payload = elf.read_bytes()
    if payload[:4] != b"\x7fELF":
        fail("%s is not an ELF file" % elf)
    if not 4 <= len(payload) <= AUTOLOAD_SIZE_LIMIT:
        fail(
            "%s is %d bytes; main.js only accepts 4..%d bytes (0x1000000)"
            % (elf, len(payload), AUTOLOAD_SIZE_LIMIT)
        )

    if args.out:
        target = args.out.resolve()
        if target.exists():
            shutil.rmtree(target)
        shutil.copytree(source_tree, target, symlinks=True)
        print("copied %s -> %s" % (source_tree, target))
    else:
        target = source_tree

    splash = find_splash_dir(target)
    previous_elf, patched = patch_main_js(splash / "main.js")

    (splash / BUNDLED_ELF).write_bytes(payload)

    removed = []
    for name in REPLACED_FILES:
        stale = splash / name
        if stale.exists():
            stale.unlink()
            removed.append(name)
    for stale in sorted(splash.glob("pldmgr_v*.elf")):
        stale.unlink()
        removed.append(stale.name)

    print("splash dir : %s" % splash.relative_to(target))
    print("AUTOLOAD_ELF: %s -> %s%s" % (previous_elf, BUNDLED_ELF, "" if patched else " (already set)"))
    print("embedded   : %s (%d bytes, %.2f MiB)" % (BUNDLED_ELF, len(payload), len(payload) / 1048576))
    print("removed    : %s" % (", ".join(removed) if removed else "nothing"))
    total = sum(f.stat().st_size for f in target.rglob("*") if f.is_file())
    print("tree size  : %d bytes (%.1f MiB)" % (total, total / 1048576))
    print()
    print("Next: build the 336789504-byte UFS2 image with")
    print("  newfs -O 2 -b 32768 -f 4096 /dev/<md>  then  cp -R %s/* /mnt/" % target.name)


if __name__ == "__main__":
    main()
