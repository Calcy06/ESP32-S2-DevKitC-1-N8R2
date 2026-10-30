#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
从 Adafruit GFX 的 glcdfont.c 生成适用于本工程的 ASCII 5x7 字模头文件。

原始字体: Adafruit GFX Library - glcdfont.c
         Copyright (c) 2012 Adafruit Industries, BSD License
         见 https://github.com/adafruit/Adafruit-GFX-Library

用法:
    curl -sL https://cdn.jsdelivr.net/gh/adafruit/Adafruit-GFX-Library@master/glcdfont.c -o glcdfont.c
    python3 gen_ascii.py glcdfont.c

输出: ../main/font_ascii.h
格式: 列优先(每列 1 字节，bit0 在上)，5 字节/字符，覆盖 0x20~0x7E(95 字符)
"""

import os
import re
import sys

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "main", "font_ascii.h")


def main():
    src_path = sys.argv[1] if len(sys.argv) > 1 else "glcdfont.c"
    if not os.path.exists(src_path):
        raise SystemExit(f"找不到 {src_path}，请先下载 glcdfont.c")

    src = open(src_path, encoding="utf-8", errors="ignore").read()
    m = re.search(r"font\[\][^=]*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit("解析 glcdfont.c 失败")
    raw = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1))]
    if len(raw) < 95 * 5:
        raise SystemExit(f"字体数据不足: {len(raw)} 字节")

    start = 0x20 * 5
    glyphs = [raw[start + i * 5: start + i * 5 + 5] for i in range(95)]

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("""/*
 * 5x7 ASCII 点阵字库 (列优先, 每字符 5 字节, bit0 在顶部)
 *
 * 来源: Adafruit GFX Library - glcdfont.c
 *       Copyright (c) 2012 Adafruit Industries.  BSD License.
 *       https://github.com/adafruit/Adafruit-GFX-Library
 *
 * 本文件由 tools/gen_ascii.py 自动生成，请勿手工修改。
 */
#pragma once

#include <stdint.h>

#define ASCII_W   5   /* 字宽(像素) */
#define ASCII_H   7   /* 字高(像素) */
#define ASCII_ADV 6   /* 步进 = 字宽 + 1 像素间距 */

static const uint8_t ASCII_FONT[95][5] = {
""")
        for i, g in enumerate(glyphs):
            ch = chr(0x20 + i)
            shown = "space" if ch == " " else ch
            f.write(f"    {{ " + ", ".join(f"0x{b:02X}" for b in g) + f" }},  /* '{shown}' */\n")
        f.write("};\n")

    print(f"已生成 {OUT}: 95 字符 x 5 字节 = {95*5} 字节")


if __name__ == "__main__":
    main()
