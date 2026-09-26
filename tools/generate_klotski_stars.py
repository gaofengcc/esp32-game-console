#!/usr/bin/env python3
"""生成华容道胜利页星星像素图.

用法:
  python3 tools/generate_klotski_stars.py

输出:
  source/idf/game_ui/assets/star_24x24_gold.h
  source/idf/game_ui/assets/star_24x24_gray.h
  output/sim/star_preview.png

RGB565 字节序与 generate_apples.py 一致: 小端 16 位, 低字节在前,
头文件使用 LV_COLOR_FORMAT_RGB565.
星星底色直接烘焙成胜利面板底色 KLOTSKI_UI_WIN_PANEL_BG (0xFFF3D6),
klotski_ui.c 修改面板色时必须同步重跑本脚本.
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import List, Tuple

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SIZE = 24
# 与 klotski_ui.c 的 KLOTSKI_UI_WIN_PANEL_BG 保持一致
PANEL_BG = (0xFF, 0xF3, 0xD6)

GOLD_FILL = (0xFF, 0xC9, 0x3C)
GOLD_EDGE = (0xB8, 0x86, 0x0B)
GOLD_GLINT = (0xFF, 0xF6, 0xC9)
GRAY_FILL = (0xB9, 0xC4, 0xCF)
GRAY_EDGE = (0x7A, 0x87, 0x94)


def star_points(cx: float, cy: float, r_out: float, r_in: float) -> List[Tuple[float, float]]:
    """五角星顶点, 从正上方开始顺时针."""
    points: List[Tuple[float, float]] = []
    for i in range(10):
        radius = r_out if i % 2 == 0 else r_in
        angle = -math.pi / 2 + i * math.pi / 5
        points.append((cx + radius * math.cos(angle), cy + radius * math.sin(angle)))
    return points


def render_star(fill: Tuple[int, int, int], edge: Tuple[int, int, int],
                glint: Tuple[int, int, int] | None) -> Image.Image:
    image = Image.new("RGB", (SIZE, SIZE), PANEL_BG)
    draw = ImageDraw.Draw(image)
    points = star_points(SIZE / 2 - 0.5, SIZE / 2 - 0.5, SIZE / 2 - 1.5, SIZE / 4.6)
    draw.polygon(points, fill=fill, outline=edge)
    if glint is not None:
        # 左上小高光, 增加卡通感
        draw.ellipse((7, 5, 10, 8), fill=glint)
    return image


def rgb565_bytes(image: Image.Image) -> bytes:
    output = bytearray()
    for y in range(SIZE):
        for x in range(SIZE):
            red, green, blue = image.getpixel((x, y))
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            # LV_COLOR_FORMAT_RGB565: 小端 16 位像素, 低字节在前
            output.extend((value & 0xFF, (value >> 8) & 0xFF))
    if len(output) != SIZE * SIZE * 2:
        raise AssertionError(f"RGB565 数据长度错误: {len(output)}")
    return bytes(output)


def format_c_array(data: bytes) -> str:
    lines = []
    for offset in range(0, len(data), 16):
        chunk = data[offset : offset + 16]
        lines.append("    " + ",".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return "\n".join(lines)


def write_header(path: Path, symbol: str, image: Image.Image) -> None:
    guard = path.stem.upper() + "_H"
    data_name = symbol + "_data"
    with open(path, "w", encoding="utf-8") as output:
        output.write(
            f"""#ifndef {guard}
#define {guard}

/* 由 tools/generate_klotski_stars.py 生成, 请勿手改. */
#include "lvgl.h"

#define STAR_24X24_WIDTH 24U
#define STAR_24X24_HEIGHT 24U
#define STAR_24X24_STRIDE 48U
#define STAR_24X24_DATA_SIZE 1152U

/*
 * RGB565 字节顺序说明:
 * 数据按 little-endian (低字节, 再高字节) 写出, 适用于
 * LV_COLOR_FORMAT_RGB565. 底色已烘焙为胜利面板色 0xFFF3D6.
 */
static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {data_name}[STAR_24X24_DATA_SIZE] = {{
"""
        )
        output.write(format_c_array(rgb565_bytes(image)))
        output.write(
            f"""
}};

static const lv_image_dsc_t {symbol} = {{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = STAR_24X24_WIDTH,
    .header.h = STAR_24X24_HEIGHT,
    .header.stride = STAR_24X24_STRIDE,
    .data_size = STAR_24X24_DATA_SIZE,
    .data = {data_name},
}};
#endif /* {guard} */
"""
        )


def main() -> None:
    assets = ROOT / "source" / "idf" / "game_ui" / "assets"
    assets.mkdir(parents=True, exist_ok=True)
    gold = render_star(GOLD_FILL, GOLD_EDGE, GOLD_GLINT)
    gray = render_star(GRAY_FILL, GRAY_EDGE, None)
    write_header(assets / "star_24x24_gold.h", "star_24x24_gold", gold)
    write_header(assets / "star_24x24_gray.h", "star_24x24_gray", gray)

    preview_dir = ROOT / "output" / "sim"
    preview_dir.mkdir(parents=True, exist_ok=True)
    preview = Image.new("RGB", (SIZE * 2 * 4, SIZE * 4), PANEL_BG)
    preview.paste(gold.resize((SIZE * 4, SIZE * 4), Image.NEAREST), (0, 0))
    preview.paste(gray.resize((SIZE * 4, SIZE * 4), Image.NEAREST), (SIZE * 4, 0))
    preview.save(preview_dir / "star_preview.png")
    print("已生成 star_24x24_gold.h / star_24x24_gray.h / star_preview.png")


if __name__ == "__main__":
    main()
