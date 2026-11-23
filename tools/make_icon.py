#!/usr/bin/env python3
"""Compose the app icon: a pixel-art sprite over a soft-focus pattern, in a rounded square.

usage: make_icon.py SPRITE PATTERN OUT.png [--size 256]

SPRITE is pixel art on a flat background colour (taken from its top-left pixel and keyed out);
PATTERN is any image, shown at 2.6x the icon width, blurred, slightly desaturated and veiled.
The sprite is scaled with nearest-neighbour (crisp pixels) to 82% of the icon height, with a
soft drop shadow. Everything is drawn at 4x and downsampled, so edges are smooth at any size.
Needs Pillow (dnf: python3-pillow).
"""
import argparse

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter

PEACH = (255, 204, 168)


def cut_sprite(path):
    rgb = Image.open(path).convert("RGB")
    bg = Image.new("RGB", rgb.size, rgb.getpixel((0, 0)))
    # Opaque wherever any channel differs from the background colour.
    alpha = ImageChops.difference(rgb, bg).convert("L").point(lambda v: 255 if v else 0)
    im = rgb.convert("RGBA")
    im.putalpha(alpha)
    return im.crop(alpha.getbbox())


def compose(sprite, pattern, n):
    s = n * 4  # supersampled canvas
    unit = s / 160  # the preview was designed at 160 px
    canvas = Image.new("RGBA", (s, s), PEACH + (255,))

    # Pattern: 2.6x the icon width, centred, scaled 1.06, blurred, saturation 0.8.
    pw = round(s * 2.6 * 1.06)
    pat = pattern.convert("RGB").resize((pw, round(pattern.height * pw / pattern.width)), Image.LANCZOS)
    left, top = (pat.width - s) // 2, (pat.height - s) // 2
    pat = pat.crop((left, top, left + s, top + s))
    pat = ImageEnhance.Color(pat.filter(ImageFilter.GaussianBlur(1.5 * unit))).enhance(0.8)
    canvas.paste(pat.convert("RGBA"))
    canvas = Image.alpha_composite(canvas, Image.new("RGBA", (s, s), (255, 230, 210, round(0.45 * 255))))

    # Sprite: 82% of the height, nearest-neighbour, centred, with a soft shadow below it.
    h = round(s * 0.82)
    w = round(sprite.width * h / sprite.height)
    spr = sprite.resize((w, h), Image.NEAREST)
    x, y = (s - w) // 2, (s - h) // 2
    shadow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    alpha = spr.getchannel("A").point(lambda a: round(a * 0.35))
    shadow.paste(Image.new("RGBA", spr.size, (0, 0, 0, 255)), (x, y + round(4 * unit)), alpha)
    canvas = Image.alpha_composite(canvas, shadow.filter(ImageFilter.GaussianBlur(2 * unit)))
    layer = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    layer.paste(spr, (x, y), spr)
    canvas = Image.alpha_composite(canvas, layer)

    # Rounded square (radius 18.75%, as the project's other icons).
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, s - 1, s - 1), radius=round(s * 0.1875), fill=255)
    canvas.putalpha(ImageChops.multiply(canvas.getchannel("A"), mask))
    return canvas.resize((n, n), Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sprite")
    ap.add_argument("pattern")
    ap.add_argument("out")
    ap.add_argument("--size", type=int, default=256)
    a = ap.parse_args()
    compose(cut_sprite(a.sprite), Image.open(a.pattern), a.size).save(a.out)


if __name__ == "__main__":
    main()
