#!/usr/bin/env python3
"""生成首页游戏选择方块图标.

只使用 Python 标准库:
  python3 tools/generate_select_icons.py

输出:
  source/idf/game_ui/assets/select_icons.h

RGB565 字节序与 tools/generate_apples.py 相同: 小端, LV_COLOR_FORMAT_RGB565.
"""

from __future__ import annotations

from pathlib import Path
from typing import List, Tuple

SIZE = 48
ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "source/idf/game_ui/assets/select_icons.h"

Color = Tuple[int, int, int]
Image = List[List[Color]]

BG: Color = (0x0F, 0x17, 0x20)
SNAKE_BODY: Color = (0x2E, 0x8B, 0x57)
SNAKE_HEAD: Color = (0xAE, 0xEA, 0x00)
EYE: Color = (0x17, 0x23, 0x2B)
APPLE: Color = (0xEF, 0x53, 0x50)
LEAF: Color = (0x3E, 0x8B, 0x55)
WALL: Color = (0x4D, 0xD0, 0xE1)
PLAYER: Color = (0xFF, 0xEB, 0x3B)
EXIT: Color = (0xEF, 0x53, 0x50)
BOARD: Color = (0x17, 0x23, 0x2B)
CAO: Color = (0xEF, 0x53, 0x50)
GUAN: Color = (0x66, 0xBB, 0x6A)
ZHANG: Color = (0xAB, 0x47, 0xBC)
ZHAO: Color = (0x42, 0xA5, 0xF5)
MA: Color = (0xFF, 0xA7, 0x26)
HUANG: Color = (0x26, 0xC6, 0xDA)
SOLDIER: Color = (0xFF, 0xEE, 0x58)


def blank() -> Image:
    return [[BG for _ in range(SIZE)] for _ in range(SIZE)]


def fill_rect(image: Image, x0: int, y0: int, x1: int, y1: int, color: Color) -> None:
    left = max(0, min(x0, x1))
    right = min(SIZE - 1, max(x0, x1))
    top = max(0, min(y0, y1))
    bottom = min(SIZE - 1, max(y0, y1))
    for y in range(top, bottom + 1):
        row = image[y]
        for x in range(left, right + 1):
            row[x] = color


def fill_ellipse(image: Image, x0: int, y0: int, x1: int, y1: int, color: Color) -> None:
    cx = (x0 + x1) / 2.0
    cy = (y0 + y1) / 2.0
    rx = abs(x1 - x0) / 2.0
    ry = abs(y1 - y0) / 2.0
    if rx <= 0.0 or ry <= 0.0:
        return
    for y in range(SIZE):
        for x in range(SIZE):
            dx = (x + 0.5 - cx) / rx
            dy = (y + 0.5 - cy) / ry
            if dx * dx + dy * dy <= 1.0:
                image[y][x] = color


def draw_snake() -> Image:
    image = blank()
    # 身体从左下折向右, 头朝右, 前方一颗苹果.
    fill_rect(image, 6, 28, 16, 40, SNAKE_BODY)
    fill_rect(image, 6, 16, 16, 30, SNAKE_BODY)
    fill_rect(image, 14, 16, 28, 28, SNAKE_BODY)
    fill_rect(image, 26, 14, 40, 30, SNAKE_HEAD)
    fill_rect(image, 34, 18, 37, 21, EYE)
    fill_ellipse(image, 34, 4, 45, 16, APPLE)
    fill_rect(image, 38, 2, 41, 6, LEAF)
    return image


def draw_maze() -> Image:
    image = blank()
    fill_rect(image, 2, 2, 45, 4, WALL)
    fill_rect(image, 2, 43, 45, 45, WALL)
    fill_rect(image, 2, 2, 4, 45, WALL)
    fill_rect(image, 43, 2, 45, 45, WALL)
    # 出口开在右边框偏下.
    fill_rect(image, 43, 34, 47, 42, BG)
    fill_rect(image, 40, 34, 45, 42, EXIT)
    fill_rect(image, 14, 2, 17, 28, WALL)
    fill_rect(image, 14, 26, 34, 29, WALL)
    fill_rect(image, 31, 26, 34, 45, WALL)
    fill_rect(image, 6, 8, 11, 13, PLAYER)
    return image


def draw_klotski() -> Image:
    image = blank()
    fill_rect(image, 4, 2, 43, 45, BOARD)
    # 曹操 2x2 居中靠上, 左右竖块, 下方横块和小兵, 底部留出口.
    fill_rect(image, 6, 4, 12, 19, GUAN)
    fill_rect(image, 14, 4, 33, 19, CAO)
    fill_rect(image, 35, 4, 41, 19, ZHANG)
    fill_rect(image, 6, 21, 12, 28, ZHAO)
    fill_rect(image, 14, 21, 33, 28, MA)
    fill_rect(image, 35, 21, 41, 28, HUANG)
    fill_rect(image, 6, 30, 12, 36, SOLDIER)
    fill_rect(image, 35, 30, 41, 36, SOLDIER)
    fill_rect(image, 6, 38, 12, 44, SOLDIER)
    fill_rect(image, 35, 38, 41, 44, SOLDIER)
    return image


def rgb565_bytes(image: Image) -> bytes:
    output = bytearray()
    for y in range(SIZE):
        for x in range(SIZE):
            red, green, blue = image[y][x]
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
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


def symbol_block(symbol: str, image: Image) -> str:
    data_name = symbol + "_data"
    data_size = SIZE * SIZE * 2
    return f"""static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {data_name}[{data_size}U] = {{
{format_c_array(rgb565_bytes(image))}
}};

static const lv_image_dsc_t {symbol} = {{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = SELECT_ICON_WIDTH,
    .header.h = SELECT_ICON_HEIGHT,
    .header.stride = SELECT_ICON_STRIDE,
    .data_size = SELECT_ICON_DATA_SIZE,
    .data = {data_name},
}};
"""


def main() -> None:
    data_size = SIZE * SIZE * 2
    stride = SIZE * 2
    header = f"""#ifndef SELECT_ICONS_H
#define SELECT_ICONS_H

/* 由 tools/generate_select_icons.py 生成, 请勿手改. */
#include <stdint.h>
#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#define SELECT_ICON_WIDTH {SIZE}U
#define SELECT_ICON_HEIGHT {SIZE}U
#define SELECT_ICON_STRIDE {stride}U
#define SELECT_ICON_DATA_SIZE {data_size}U

/*
 * RGB565 字节顺序说明:
 * 数据按 little-endian (低字节, 再高字节) 写出, 适用于
 * LV_COLOR_FORMAT_RGB565. 不要改成 RGB565_SWAPPED.
 */

{symbol_block("select_icon_snake", draw_snake())}
{symbol_block("select_icon_maze", draw_maze())}
{symbol_block("select_icon_klotski", draw_klotski())}
#endif /* SELECT_ICONS_H */
"""
    HEADER.parent.mkdir(parents=True, exist_ok=True)
    HEADER.write_text(header, encoding="utf-8")
    print(f"wrote {HEADER}")


if __name__ == "__main__":
    main()
