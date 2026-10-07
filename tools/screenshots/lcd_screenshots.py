#!/usr/bin/env python3
"""Renders the TFT screens on the PC (same lib/ui code as the firmware) and saves them as PNG.

Usage: python3 tools/screenshots/lcd_screenshots.py [--out img] [--scale 2]
Needs a C++17 compiler (g++ or clang++); no other dependencies.
"""
import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
W, H = 240, 135


def rgb565_to_rows(data, scale):
    rows = []
    for y in range(H):
        row = bytearray()
        for x in range(W):
            (v,) = struct.unpack_from("<H", data, 2 * (y * W + x))
            r, g, b = v >> 11, (v >> 5) & 0x3F, v & 0x1F
            px = bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
            row += px * scale
        rows.extend([bytes(row)] * scale)
    return rows


def write_png(path, rows):
    def chunk(tag, payload):
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload))

    height, width = len(rows), len(rows[0]) // 3
    raw = b"".join(b"\x00" + r for r in rows)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=str(ROOT / "img"))
    ap.add_argument("--scale", type=int, default=2, help="integer upscaling (pixel-exact)")
    args = ap.parse_args()

    cxx = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        raise SystemExit("No C++ compiler found (set CXX)")
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        exe = pathlib.Path(tmp) / "render_lcd"
        sources = [ROOT / "tools/screenshots/render_lcd.cpp", *sorted((ROOT / "lib/ui").glob("*.cpp"))]
        subprocess.run([cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", f"-I{ROOT / 'lib/ui'}", "-o", str(exe), *map(str, sources)], check=True)
        names = subprocess.run([str(exe), tmp], check=True, capture_output=True, text=True).stdout.split()
        for name in names:
            data = (pathlib.Path(tmp) / f"{name}.rgb565").read_bytes()
            write_png(out / f"{name}.png", rgb565_to_rows(data, args.scale))
            print(f"{out / name}.png")


if __name__ == "__main__":
    main()
