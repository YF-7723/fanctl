#!/usr/bin/env python3
"""生成 FanCtl 图标：assets/icon_1024.png 与 assets/FanCtl.icns（不依赖 iconutil）"""
import math, struct, os
from PIL import Image, ImageDraw, ImageFilter

SS, SIZE = 4, 1024
C = SIZE * SS
TOP, BOT = (255, 186, 122), (214, 92, 34)
WHITE = (255, 255, 255)


def rounded_mask(size, radius_ratio=0.225, inset_ratio=0.06):
    inset = int(size * inset_ratio)
    w = size - inset * 2
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle(
        [inset, inset, inset + w, inset + w], radius=int(w * radius_ratio), fill=255)
    return m


def gradient(size, top, bot):
    g = Image.new("RGB", (1, size))
    for y in range(size):
        t = y / max(1, size - 1)
        g.putpixel((0, y), tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)))
    return g.resize((size, size))


def blade(cx, cy, angle_deg, r0, r1, w0, sweep=52):
    left, right = [], []
    for i in range(45):
        t = i / 44
        r = r0 + (r1 - r0) * t
        a = math.radians(angle_deg + sweep * (t ** 1.35))
        half = w0 * (1 - t) ** 0.75 + 4 * SS
        nx, ny = math.cos(a), math.sin(a)
        tx, ty = -ny, nx
        left.append((cx + nx * r + tx * half, cy + ny * r + ty * half))
        right.append((cx + nx * r - tx * half, cy + ny * r - ty * half))
    return left + right[::-1]


def main():
    mask = rounded_mask(C)
    img = Image.new("RGBA", (C, C), (0, 0, 0, 0))
    img.paste(gradient(C, TOP, BOT), (0, 0), mask)

    # 顶部柔光，压低对比、别做出硬边
    gloss = Image.new("RGBA", (C, C), (0, 0, 0, 0))
    ImageDraw.Draw(gloss).ellipse(
        [-C * 0.3, -C * 0.75, C * 1.3, C * 0.55], fill=(255, 255, 255, 40))
    gloss = gloss.filter(ImageFilter.GaussianBlur(60 * SS))
    gloss.putalpha(Image.composite(gloss.split()[3], Image.new("L", (C, C), 0), mask))
    img = Image.alpha_composite(img, gloss)

    cx = cy = C // 2
    r0, r1, w0 = int(C * 0.085), int(C * 0.315), int(C * 0.085)

    # 叶片外发光
    glow = Image.new("RGBA", (C, C), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    for k in range(3):
        gd.polygon(blade(cx, cy, -90 + k * 120, r0, r1, w0), fill=(255, 255, 255, 60))
    img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(16 * SS)))

    d = ImageDraw.Draw(img)
    for k in range(3):
        d.polygon(blade(cx, cy, -90 + k * 120, r0, r1, w0), fill=WHITE)

    hub = int(C * 0.082)
    d.ellipse([cx - hub, cy - hub, cx + hub, cy + hub], fill=WHITE)
    inner = int(hub * 0.46)
    d.ellipse([cx - inner, cy - inner, cx + inner, cy + inner], fill=BOT)

    out = img.resize((SIZE, SIZE), Image.LANCZOS)
    base = os.path.dirname(os.path.abspath(__file__))
    out.save(os.path.join(base, "icon_1024.png"))

    specs = [(16, "ic11"), (32, "ic12"), (32, "icp5"), (64, "ic13"),
             (128, "ic07"), (256, "ic08"), (256, "ic14"), (512, "ic09"), (1024, "ic10")]
    chunks = []
    for px, tag in specs:
        tmp = os.path.join(base, "_t.png")
        out.resize((px, px), Image.LANCZOS).save(tmp)
        data = open(tmp, "rb").read()
        os.remove(tmp)
        chunks.append(tag.encode() + struct.pack(">I", len(data) + 8) + data)
    body = b"".join(chunks)
    open(os.path.join(base, "FanCtl.icns"), "wb").write(
        b"icns" + struct.pack(">I", len(body) + 8) + body)
    print("icon ok")


if __name__ == "__main__":
    main()
