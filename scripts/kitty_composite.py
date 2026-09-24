#!/usr/bin/env python3
"""Replay a captured kitty presentation stream and check it against ground truth.

PIL is the authoritative decoder (a hand-rolled PNG decoder once conflated
filters and reported 88% mismatch on a pixel-exact stream). The replay is a
terminal: it follows the cursor (CUP), decodes each a=T transmission, and
blits the image at cell + sub-cell offset — exactly what a kitty terminal
does with the stream the compositor wrote.

usage:
  kitty_composite.py capture.bin ground.png --cell 10 20 [--rect X Y W H]
                     [--seconds N]

Reports bytes, transmissions, presents (home-cursor markers), achieved fps
when --seconds is given, and pixel agreement over --rect (default: whole
canvas). Exit status is 0 only if the compared region is pixel-exact.
"""

import argparse
import base64
import io
import sys

from PIL import Image


def parse_kv(keys):
    out = {}
    for part in keys.split(","):
        if "=" in part:
            k, v = part.split("=", 1)
            out[k] = v
        elif part:
            out[part] = ""
    return out


def replay(data, cell_w, cell_h, canvas_w, canvas_h):
    canvas = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
    col = row = 0
    keys = None
    payload = b""
    images = 0
    homes = 0
    i = 0
    n = len(data)
    stray = 0

    while i < n:
        b = data[i]
        if b == 0x1B and i + 2 < n and data[i + 1] == ord("_") and data[i + 2] == ord("G"):
            st = data.find(b"\x1b\\", i + 3)
            if st < 0:
                raise ValueError(f"APC without ST at {i}")
            semi = data.find(b";", i + 3, st)
            if 0 <= semi < st:
                chunk_keys = data[i + 3:semi].decode("latin1")
                payload += data[semi + 1:st]
            else:
                # keys-only command (e.g. our own a=d); kitty's spec has them
                chunk_keys = data[i + 3:st].decode("latin1")
            if keys is None:
                keys = chunk_keys
            else:
                keys += "," + chunk_keys
            i = st + 2

            kv = parse_kv(chunk_keys)
            if int(kv.get("m", 0)) != 0:
                continue

            a = parse_kv(keys).get("a", "")
            if a == "T":
                img = Image.open(io.BytesIO(base64.b64decode(payload))).convert("RGBA")
                p = parse_kv(keys)
                dx = col * cell_w + int(p.get("X", 0))
                dy = row * cell_h + int(p.get("Y", 0))
                # blit: kitty draws the image at pixel (dx, dy)
                tmp = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
                tmp.paste(img, (dx, dy))
                canvas = Image.alpha_composite(canvas, tmp)
                images += 1
            keys = None
            payload = b""
            continue
        if b == 0x1B and i + 1 < n and data[i + 1] == ord("["):
            j = i + 2
            while j < n and not (0x40 <= data[j] <= 0x7E):
                j += 1
            if j >= n:
                raise ValueError(f"truncated CSI at {i}")
            if data[j] == ord("H"):
                params = data[i + 2:j].decode("latin1")
                if params == "":
                    row = col = 0
                else:
                    parts = params.split(";")
                    r = int(parts[0] or 1)
                    c = int(parts[1]) if len(parts) > 1 and parts[1] else 1
                    row, col = max(r - 1, 0), max(c - 1, 0)
                if data[i:j + 1] == b"\x1b[H":
                    homes += 1
            i = j + 1
            continue
        stray += 1
        i += 1

    if images == 0:
        raise ValueError("no a=T transmission in stream")
    return canvas, images, homes, stray


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("ground")
    ap.add_argument("--cell", nargs=2, type=int, required=True,
                    metavar=("W", "H"))
    ap.add_argument("--rect", nargs=4, type=int, default=None,
                    metavar=("X", "Y", "W", "H"))
    ap.add_argument("--seconds", type=float, default=None)
    args = ap.parse_args()

    data = open(args.capture, "rb").read()
    ground = Image.open(args.ground).convert("RGBA")

    canvas, images, homes, stray = replay(
        data, args.cell[0], args.cell[1], ground.width, ground.height)

    print(f"bytes: {len(data)}")
    print(f"transmissions: {images}")
    print(f"presents (home markers): {homes}")
    print(f"stray bytes outside CSI/APC: {stray}")
    if args.seconds:
        print(f"achieved fps: {homes / args.seconds:.1f} "
              f"({homes} presents / {args.seconds}s)")

    gx, gy, gw, gh = (0, 0, ground.width, ground.height)
    if args.rect:
        gx, gy, gw, gh = args.rect

    matched = 0
    total = 0
    maxdiff = 0
    for y in range(gy, min(gy + gh, ground.height)):
        grow = ground.crop((gx, y, gx + gw, y + 1))
        craw = canvas.crop((gx, y, gx + gw, y + 1))
        gp = grow.load()
        cp = craw.load()
        for x in range(grow.width):
            g = gp[x, 0]
            c = cp[x, 0]
            total += 1
            d = max(abs(g[0] - c[0]), abs(g[1] - c[1]), abs(g[2] - c[2]))
            if d == 0:
                matched += 1
            else:
                maxdiff = max(maxdiff, d)
    pct = 100.0 * matched / total if total else 0.0
    print(f"pixel agreement in rect ({gx},{gy},{gw},{gh}): "
          f"{matched}/{total} = {pct:.2f}%, maxdiff={maxdiff}")

    out = args.capture + ".composite.png"
    canvas.convert("RGB").save(out)
    print(f"composite written: {out}")

    return 0 if matched == total else 1


if __name__ == "__main__":
    sys.exit(main())
