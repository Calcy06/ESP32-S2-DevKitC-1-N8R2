#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
汉字点阵生成工具 —— 为 SSD1306 OLED 生成 16x16 汉字字模。

用法:
    python3 gen_font.py                      # 使用内置字符集
    python3 gen_font.py "你胆子真是肥嘟嘟的"   # 自定义字符集
    python3 gen_font.py --size 24 "你好"      # 生成 24x24

依赖: Pillow (pip3 install Pillow)
字体: 任意 CJK 字体，默认按以下顺序查找
      Noto Sans CJK SC / Source Han Sans / WenQuanYi / 系统中文字体

输出: ../main/font_cn.h
数据格式: 横向取模，字节高位在前(MSB left)，按行扫描。
          width=16 -> 每行 2 字节，共 32 字节/字。
"""

import os
import sys
import subprocess
from PIL import Image, ImageDraw, ImageFont

# ---------------------------------------------------------------- 字符集
DEFAULT_CHARS = (
    "你胆子真是肥嘟嘟的"
    "敢抢我的外卖不打听谁"
    "单片机可好玩"
)

# ---------------------------------------------------------------- 字体查找
CANDIDATE_FONTS = [
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansSC-Regular.otf",
    "/usr/share/fonts/opentype/noto/NotoSansSC-Bold.otf",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/System/Library/Fonts/PingFang.ttc",
]


def find_font():
    for p in CANDIDATE_FONTS:
        if os.path.exists(p):
            return p
    # 退化：用 fontconfig 找
    try:
        out = subprocess.check_output(
            ["fc-match", "-f", "%{file}", "sans-serif:lang=zh"], text=True
        )
        if out.strip():
            return out.strip()
    except Exception:
        pass
    raise SystemExit(
        "找不到中文字体。请安装: sudo apt install fonts-noto-cjk\n"
        "或在 CANDIDATE_FONTS / 命令行中指定字体路径。"
    )


def pick_sc_face(path):
    """NotoSansCJK.ttc 里有多张 face，优先选 Simplified Chinese。"""
    for idx in range(0, 8):
        try:
            f = ImageFont.truetype(path, 32, index=idx)
            name = f.getname()
            if isinstance(name, tuple):
                name = " ".join(name)
            if "SC" in name or "Simplified" in name or "SourceHanSansSC" in name:
                return idx, name
        except Exception:
            break
    return 0, "default"


# ---------------------------------------------------------------- 渲染
def render_glyph(font_path, face_idx, ch, size):
    """把单个汉字渲染成 (width, height, bytes) 点阵。"""
    font = ImageFont.truetype(font_path, size, index=face_idx)
    pad = 4
    W = size + pad * 2
    H = size + pad * 2
    img = Image.new("1", (W, H), 0)
    draw = ImageDraw.Draw(img)

    # 用 textbbox 精确定位，避免不同 Pillow 版本 baseline 差异
    bbox = draw.textbbox((0, 0), ch, font=font)
    x = pad - bbox[0]
    y = pad - bbox[1]
    draw.text((x, y), ch, font=font, fill=1)

    # 裁掉空白边，再居中放进 size x size 的格子
    bbox = img.getbbox()
    if bbox is None:
        return [0] * ((size // 8) * size)

    crop = img.crop(bbox)
    canvas = Image.new("1", (size, size), 0)
    ox = (size - crop.width) // 2
    oy = (size - crop.height) // 2
    canvas.paste(crop, (ox, oy))

    px = canvas.load()
    row_bytes = (size + 7) // 8
    data = []
    for row in range(size):
        line = 0
        for col in range(size):
            if px[col, row]:
                line |= 0x80 >> (col % 8)
            if col % 8 == 7:
                data.append(line & 0xFF)
                line = 0
        if size % 8:
            data.append(line & 0xFF)
    assert len(data) == row_bytes * size, (len(data), row_bytes * size)
    return data


def main():
    args = sys.argv[1:]
    size = 16
    font_path = None

    while args and args[0].startswith("--"):
        opt = args.pop(0)
        if opt == "--size":
            size = int(args.pop(0))
        elif opt == "--font":
            font_path = args.pop(0)

    chars = "".join(args) if args else DEFAULT_CHARS
    # 去重保序
    seen, uniq = set(), []
    for c in chars:
        if c not in seen and c.strip():
            seen.add(c)
            uniq.append(c)

    if font_path is None:
        font_path = find_font()
    face_idx, face_name = pick_sc_face(font_path)
    print(f"字体: {font_path} (face #{face_idx} {face_name})")
    print(f"字号: {size}x{size}, 字符数: {len(uniq)}")

    out_path = os.path.join(os.path.dirname(__file__), "..", "main", "font_cn.h")
    out_path = os.path.abspath(out_path)
    row_bytes = (size + 7) // 8
    bytes_per_glyph = row_bytes * size

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("/* 本文件由 tools/gen_font.py 自动生成，请勿手工修改 */\n")
        f.write("#pragma once\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define CN_FONT_SIZE {size}\n")
        f.write(f"#define CN_FONT_ROW_BYTES {row_bytes}\n")
        f.write(f"#define CN_FONT_BYTES {bytes_per_glyph}\n\n")

        f.write("typedef struct {\n")
        f.write("    const char *utf8;   /* UTF-8 编码，便于源码里直接写汉字 */\n")
        f.write("    const uint8_t *data;\n")
        f.write("} cn_glyph_t;\n\n")

        for i, ch in enumerate(uniq):
            data = render_glyph(font_path, face_idx, ch, size)
            code = "".join(f"U{c:04X}" for c in ch.encode("utf-8"))
            f.write(f"static const uint8_t glyph_{i:03d}_{code}[{bytes_per_glyph}] = {{\n")
            for r in range(size):
                row = data[r * row_bytes:(r + 1) * row_bytes]
                f.write("    " + " ".join(f"0x{b:02X}," for b in row) + "\n")
            f.write("};\n\n")

        f.write("static const cn_glyph_t CN_FONT[] = {\n")
        for i, ch in enumerate(uniq):
            code = "".join(f"U{c:04X}" for c in ch.encode("utf-8"))
            f.write(f'    {{ "{ch}", glyph_{i:03d}_{code} }},\n')
        f.write("};\n\n")
        f.write(f"#define CN_FONT_COUNT {len(uniq)}\n")

    print(f"已生成: {out_path}")
    print(f"占用 Flash: {len(uniq) * bytes_per_glyph} 字节 (const, 存 Flash)")


if __name__ == "__main__":
    main()
