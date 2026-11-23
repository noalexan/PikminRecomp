#!/usr/bin/env python3
"""Render a picoCAD model (.txt save file, texture included) to a square RGBA PNG icon.

usage: render_picocad.py MODEL OUT.png [--size 256] [--yaw DEG] [--pitch DEG] [--bg]

picoCAD saves Lua tables: objects with pos, vertices (y points down) and quad/triangle faces with
UVs in 8-pixel units into a 128x120 PICO-8 texture stored after the '%' (the header's last field
is the transparent color). Pure Python (no numpy or
Pillow): rasterized at 4x the output size with nearest texture sampling and a simple Lambert
term, then box-filtered down. --bg draws the project's rounded green icon background behind it.
"""
import argparse
import math
import re
import struct
import zlib

PICO8 = [(0, 0, 0), (29, 43, 83), (126, 37, 83), (0, 135, 81), (171, 82, 54), (95, 87, 79),
         (194, 195, 199), (255, 241, 232), (255, 0, 77), (255, 163, 0), (255, 236, 39),
         (0, 228, 54), (41, 173, 255), (131, 118, 156), (255, 119, 168), (255, 204, 170)]
NUM = r"-?\d+(?:\.\d+)?"


def parse(text):
    model, tex = text.split("%", 1)
    # Header "picocad;NAME;ZOOM;BG;ALPHA": texels of the ALPHA color are transparent (None).
    header = model.split("\n", 1)[0].split(";")
    alpha = int(header[4]) if len(header) > 4 and header[4].strip().isdigit() else -1
    rows = [ln.strip()[:128] for ln in tex.splitlines() if ln.strip()][:120]
    texture = [[None if int(ch, 16) == alpha else PICO8[int(ch, 16)] for ch in row] for row in rows]
    objects = []
    for m in re.finditer(r"name='[^']*',\s*pos=\{(" + NUM + r"),(" + NUM + r"),(" + NUM + r")\}.*?v=\{(.*?)\n \},\s*f=\{(.*?)\n \}",
                         model, re.S):
        pos = [float(m.group(i)) for i in (1, 2, 3)]
        verts = [[float(a) + p for a, p in zip(v, pos)]
                 for v in re.findall(r"\{(" + NUM + r"),(" + NUM + r"),(" + NUM + r")\}", m.group(4))]
        faces = []
        for f in re.finditer(r"\{([\d,]+),\s*(.*?)uv=\{([^}]*)\}", m.group(5)):
            idx = [int(i) - 1 for i in f.group(1).strip(",").split(",")]
            uv = [float(x) * 8 for x in f.group(3).split(",")]
            faces.append({"idx": idx, "uv": list(zip(uv[0::2], uv[1::2])), "dbl": "dbl=1" in f.group(2)})
        objects.append((verts, faces))
    return objects, texture


def render(objects, texture, n, yaw, pitch):
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)

    def view(v):
        x, y, z = v[0], -v[1], v[2]  # picoCAD's y points down
        x, z = x * cy + z * sy, -x * sy + z * cy
        y, z = y * cp - z * sp, y * sp + z * cp
        return (x, y, z)

    tris = []
    for verts, faces in objects:
        vv = [view(v) for v in verts]
        for f in faces:
            p = [vv[i] for i in f["idx"]]
            for k in range(1, len(p) - 1):
                tris.append(((p[0], p[k], p[k + 1]), (f["uv"][0], f["uv"][k], f["uv"][k + 1]), f["dbl"]))
    pts = [q for t in tris for q in t[0]]
    xs, ys = [q[0] for q in pts], [q[1] for q in pts]
    cx, cyy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    scale = 0.86 * n / max(max(xs) - min(xs), max(ys) - min(ys))
    light = (-0.45, 0.6, 0.66)
    color = [[None] * n for _ in range(n)]
    depth = [[-1e9] * n for _ in range(n)]
    for (a, b, c), (ta, tb, tc), dbl in tris:
        s = [((q[0] - cx) * scale + n / 2, (cyy - q[1]) * scale + n / 2, q[2]) for q in (a, b, c)]
        area = (s[1][0] - s[0][0]) * (s[2][1] - s[0][1]) - (s[2][0] - s[0][0]) * (s[1][1] - s[0][1])
        if abs(area) < 1e-9:
            continue
        # Lambert on the face normal (flipped toward the viewer for double-sided faces).
        u = [b[i] - a[i] for i in range(3)]
        w = [c[i] - a[i] for i in range(3)]
        nn = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]]
        ln = math.sqrt(sum(x * x for x in nn)) or 1
        nn = [x / ln for x in nn]
        if nn[2] < 0:  # both sides drawn (picoCAD's winding varies); the z-buffer sorts it out
            nn = [-x for x in nn]
        shade = 0.62 + 0.38 * max(0.0, sum(nn[i] * light[i] for i in range(3)))
        x0, x1 = max(0, int(min(q[0] for q in s))), min(n - 1, int(max(q[0] for q in s)) + 1)
        y0, y1 = max(0, int(min(q[1] for q in s))), min(n - 1, int(max(q[1] for q in s)) + 1)
        for py in range(y0, y1 + 1):
            fy = py + 0.5
            for px in range(x0, x1 + 1):
                fx = px + 0.5
                w0 = ((s[1][0] - fx) * (s[2][1] - fy) - (s[2][0] - fx) * (s[1][1] - fy)) / area
                w1 = ((s[2][0] - fx) * (s[0][1] - fy) - (s[0][0] - fx) * (s[2][1] - fy)) / area
                w2 = 1 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                z = w0 * s[0][2] + w1 * s[1][2] + w2 * s[2][2]
                if z <= depth[py][px]:
                    continue
                tu = w0 * ta[0] + w1 * tb[0] + w2 * tc[0]
                tv = w0 * ta[1] + w1 * tb[1] + w2 * tc[1]
                texel = texture[min(119, max(0, int(tv)))][min(127, max(0, int(tu)))]
                if texel is None:
                    continue
                r, g, bb = texel
                depth[py][px] = z
                color[py][px] = (r * shade, g * shade, bb * shade)
    return color


def background(n):
    """The project's icon background: rounded square, radial #3b6b3a -> #1c3320."""
    img = []
    rad = 48 / 256 * n
    for y in range(n):
        row = []
        for x in range(n):
            dx = max(rad - x - 0.5, x + 0.5 - (n - rad), 0)
            dy = max(rad - y - 0.5, y + 0.5 - (n - rad), 0)
            a = max(0.0, min(1.0, rad - math.hypot(dx, dy) + 0.5)) if dx and dy else 1.0
            t = min(1.0, math.hypot(x / n - 0.5, y / n - 0.4) / 0.65)
            c = [59 + (28 - 59) * t, 107 + (51 - 107) * t, 58 + (32 - 58) * t]
            row.append((*c, a))
        img.append(row)
    return img


def write_png(path, rows):
    raw = b"".join(b"\0" + bytes(v for px in row for v in px) for row in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    n = len(rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", len(rows[0]), n, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model")
    ap.add_argument("out")
    ap.add_argument("--size", type=int, default=256)
    ap.add_argument("--yaw", type=float, default=60)
    ap.add_argument("--pitch", type=float, default=15)
    ap.add_argument("--bg", action="store_true")
    a = ap.parse_args()
    objects, texture = parse(open(a.model, encoding="utf-8").read())
    ss, n = 4, a.size
    big = render(objects, texture, n * ss, math.radians(a.yaw), math.radians(a.pitch))
    bg = background(n) if a.bg else None
    rows = []
    for y in range(n):
        row = []
        for x in range(n):
            acc, cov = [0.0, 0.0, 0.0], 0
            for j in range(ss):
                for i in range(ss):
                    c = big[y * ss + j][x * ss + i]
                    if c:
                        cov += 1
                        for k in range(3):
                            acc[k] += c[k]
            fa = cov / (ss * ss)
            fg = [v / cov for v in acc] if cov else [0, 0, 0]
            if bg:
                br, bgc, bb, ba = bg[y][x]
                out = [fg[k] * fa + (br, bgc, bb)[k] * (1 - fa) for k in range(3)]
                alpha = ba
            else:
                out, alpha = fg, fa
            row.append(tuple(int(max(0, min(255, round(v)))) for v in out) + (int(round(alpha * 255)),))
        rows.append(row)
    write_png(a.out, rows)


if __name__ == "__main__":
    main()
