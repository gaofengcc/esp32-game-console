#!/usr/bin/env python3
"""生成贪吃蛇 16x16 像素资源。

资源全部由纯 Python 像素算法生成，不依赖外部图片：
  python3 tools/generate_snake.py

输出：
  source/idf/game_ui/assets/snake_16x16.h
  output/sim/snake_assets.png

像素格式：
  LVGL 使用 RGB565，ESP32 与 SDL 路径均按 little-endian 写入，
  因此每个像素输出低字节、再输出高字节。
"""

from __future__ import annotations

from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

from PIL import Image, ImageDraw

WIDTH = 16
HEIGHT = 16
BG = (0x0F, 0x17, 0x20)

RGB = Tuple[int, int, int]
PALETTE: Dict[str, RGB] = {
    ".": BG,
    # 身体：0x2E8B57 为基色，上缘略亮、下缘略暗。
    "body_outline": (0x12, 0x3C, 0x2B),
    "body_shadow": (0x22, 0x68, 0x43),
    "body": (0x2E, 0x8B, 0x57),
    "body_light": (0x62, 0xB8, 0x78),
    "body_highlight": (0xA1, 0xD8, 0x9F),
    # 头部：亮黄绿主色，眼睛沿前进方向排布。
    "head_outline": (0x4A, 0x66, 0x13),
    "head_shadow": (0x7D, 0xB7, 0x00),
    "head": (0xAE, 0xEA, 0x00),
    "head_light": (0xD7, 0xFF, 0x4A),
    "eye": (0x18, 0x33, 0x2B),
}

ASSETS: Sequence[Tuple[str, str]] = (
    ("snake_body_up", "body_up"),
    ("snake_body_down", "body_down"),
    ("snake_body_left", "body_left"),
    ("snake_body_right", "body_right"),
    ("snake_turn_up_right", "turn_up_right"),
    ("snake_turn_right_down", "turn_right_down"),
    ("snake_turn_down_left", "turn_down_left"),
    ("snake_turn_left_up", "turn_left_up"),
    ("snake_tail_up", "tail_up"),
    ("snake_tail_down", "tail_down"),
    ("snake_tail_left", "tail_left"),
    ("snake_tail_right", "tail_right"),
    ("snake_head_up", "head_up"),
    ("snake_head_down", "head_down"),
    ("snake_head_left", "head_left"),
    ("snake_head_right", "head_right"),
)


def rgb565(red: int, green: int, blue: int) -> int:
    """将 8 位 RGB 转换为 RGB565 数值。"""
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def dist_to_segment(px: float, py: float, ax: float, ay: float,
                    bx: float, by: float) -> float:
    """计算点到线段的距离，用于圆润的转弯描边。"""
    vx = bx - ax
    vy = by - ay
    wx = px - ax
    wy = py - ay
    length2 = vx * vx + vy * vy
    if length2 <= 0.0:
        return ((px - ax) ** 2 + (py - ay) ** 2) ** 0.5
    t = max(0.0, min(1.0, (wx * vx + wy * vy) / length2))
    cx = ax + t * vx
    cy = ay + t * vy
    return ((px - cx) ** 2 + (py - cy) ** 2) ** 0.5


def rounded_rect_inside(x: int, y: int, width: float, height: float,
                        radius: float) -> bool:
    """像素中心是否位于圆角矩形内。"""
    px = x + 0.5
    py = y + 0.5
    x0 = (WIDTH - width) / 2.0
    y0 = (HEIGHT - height) / 2.0
    x1 = x0 + width
    y1 = y0 + height
    if x0 + radius <= px <= x1 - radius or y0 + radius <= py <= y1 - radius:
        return x0 <= px <= x1 and y0 <= py <= y1
    cx = x0 + radius if px < x0 + radius else x1 - radius
    cy = y0 + radius if py < y0 + radius else y1 - radius
    return (px - cx) ** 2 + (py - cy) ** 2 <= radius * radius


def rounded_rect_at(px: float, py: float, x0: float, y0: float,
                    x1: float, y1: float, radius: float) -> bool:
    """任意位置的圆角矩形，供转弯体节拼接两条圆润臂。"""
    if not (x0 <= px <= x1 and y0 <= py <= y1):
        return False
    if x0 + radius <= px <= x1 - radius or y0 + radius <= py <= y1 - radius:
        return True
    cx = x0 + radius if px < x0 + radius else x1 - radius
    cy = y0 + radius if py < y0 + radius else y1 - radius
    return (px - cx) ** 2 + (py - cy) ** 2 <= radius * radius


def body_inside(kind: str, x: int, y: int) -> bool:
    vertical = kind.endswith("_up") or kind.endswith("_down")
    return rounded_rect_inside(
        x, y, 12.0 if vertical else 16.0, 16.0 if vertical else 12.0, 4.0
    )


def connection_sides(kind: str) -> Tuple[str, ...]:
    """返回该资源需要和相邻体节无缝相接的边。"""
    direction_to_side = {
        "up": "top",
        "down": "bottom",
        "left": "left",
        "right": "right",
    }
    if kind.startswith("body_"):
        if kind.endswith("_up") or kind.endswith("_down"):
            return ("top", "bottom")
        return ("left", "right")
    if kind.startswith("turn_"):
        return tuple(
            direction_to_side[direction]
            for direction in kind.removeprefix("turn_").split("_")
        )
    if kind.startswith("tail_"):
        return (direction_to_side[kind.removeprefix("tail_")],)
    if kind.startswith("head_"):
        opposite = {
            "up": "bottom",
            "down": "top",
            "left": "right",
            "right": "left",
        }
        return (opposite[kind.removeprefix("head_")],)
    return ()


def on_connection_side(kind: str, x: int, y: int, side: str) -> bool:
    """判断像素是否位于指定连接边，供边界描边抑制使用。"""
    if side == "left":
        return x == 0
    if side == "right":
        return x == WIDTH - 1
    if side == "top":
        return y == 0
    if side == "bottom":
        return y == HEIGHT - 1
    raise ValueError(side)


def turn_inside(kind: str, x: int, y: int) -> bool:
    """用两条带圆角的臂拼成拐角，内侧留出弯曲的背景缺口。"""
    px = x + 0.5
    py = y + 0.5
    # 基准形状是“由下方进入、向右方离开”，其余方向由镜像获得。
    if kind == "turn_up_right":
        tx, ty = px, py
    elif kind == "turn_right_down":
        # 基准图顺时针旋转 90 度。
        tx, ty = py, 15.0 - px
    elif kind == "turn_down_left":
        tx, ty = 15.0 - px, 15.0 - py
    else:
        # 基准图逆时针旋转 90 度。
        tx, ty = 15.0 - py, px
    # 竖臂从上方进入，横臂从右方离开，二者在左下角圆润相接。
    vertical_arm = rounded_rect_at(tx, ty, 1.5, 1.0, 9.5, 12.0, 3.0)
    horizontal_arm = rounded_rect_at(tx, ty, 5.0, 7.0, 15.0, 15.0, 3.0)
    # 清掉拐角内侧，避免退化为实心方块。
    inner_gap = tx > 8.5 and ty < 7.0
    # 连接臂贴到 tile 边界，和相邻直线体节/头尾直接相接。
    top_connection = ty <= 0.5 and 2.5 <= tx <= 9.5
    right_connection = tx >= 15.5 and 6.0 <= ty <= 14.5
    return (vertical_arm or horizontal_arm or top_connection or right_connection) \
        and not inner_gap


def tail_inside(kind: str, x: int, y: int) -> Tuple[bool, float]:
    """返回尾巴像素是否在锥形内，以及沿尾巴轴线的进度。"""
    px = x + 0.5
    py = y + 0.5
    if kind == "tail_up":
        axis, lateral = 15.0 - py, px - 8.0
    elif kind == "tail_down":
        axis, lateral = py, px - 8.0
    elif kind == "tail_left":
        axis, lateral = 15.0 - px, py - 8.0
    else:
        axis, lateral = px, py - 8.0
    if axis < 1.0 or axis > 14.8:
        return False, axis
    # 末端明显收窄，根部保持接近体节宽度。
    width = 1.4 + 4.8 * (axis / 14.0)
    inside = abs(lateral) <= width
    # 尾根贴到相邻体节所在的一侧，避免锥形末端与身体之间出现裂缝。
    if kind == "tail_right" and x == WIDTH - 1:
        inside = abs(lateral) <= 4.5
    elif kind == "tail_left" and x == 0:
        inside = abs(lateral) <= 4.5
    elif kind == "tail_up" and y == 0:
        inside = abs(lateral) <= 4.5
    elif kind == "tail_down" and y == HEIGHT - 1:
        inside = abs(lateral) <= 4.5
    return inside, axis


def head_inside(kind: str, x: int, y: int) -> bool:
    inside = rounded_rect_inside(x, y, 14.0, 14.0, 5.0)
    # 头部后缘贴到身体所在的一侧，保持单对象渲染下的无缝衔接。
    if kind == "head_right" and x == 0:
        inside = 4 <= y <= 11
    elif kind == "head_left" and x == WIDTH - 1:
        inside = 4 <= y <= 11
    elif kind == "head_up" and y == HEIGHT - 1:
        inside = 4 <= x <= 11
    elif kind == "head_down" and y == 0:
        inside = 4 <= x <= 11
    return inside


def source_inside(kind: str, x: int, y: int) -> bool:
    if kind.startswith("body_"):
        return body_inside(kind, x, y)
    if kind.startswith("turn_"):
        return turn_inside(kind, x, y)
    if kind.startswith("tail_"):
        return tail_inside(kind, x, y)[0]
    if kind.startswith("head_"):
        return head_inside(kind, x, y)
    raise ValueError(kind)


def classify_pixel(kind: str, x: int, y: int) -> RGB:
    if not source_inside(kind, x, y):
        return BG

    # 轮廓：只要相邻像素有一个落在背景，就绘制 1px 深色描边。
    if kind.startswith("body_"):
        # 身体左右边只保留背景留白，不画深色描边；上下边保留描边，
        # 这样相邻格子的横向拼接不会出现 1px 暗缝。
        neighbors = ((x, y - 1), (x, y + 1))
    else:
        neighbors = ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))
    edge = any(
        nx < 0 or nx >= WIDTH or ny < 0 or ny >= HEIGHT
        or not source_inside(kind, nx, ny)
        for nx, ny in neighbors
    )
    # 连接边直接贴到相邻体节；不画描边，避免格子拼接时出现亮/暗细缝。
    if any(
        on_connection_side(kind, x, y, side)
        for side in connection_sides(kind)
        if side in ("left", "right")
    ):
        edge = False

    if kind.startswith("head_"):
        if edge:
            return PALETTE["head_outline"]
        if y <= 4:
            return PALETTE["head_light"]
        if y >= 12:
            return PALETTE["head_shadow"]
        # 眼睛跟随头部朝向变化，保持两只独立的深色像素块。
        if kind == "head_up" and y in (3, 4) and x in (5, 10):
            return PALETTE["eye"]
        if kind == "head_down" and y in (11, 12) and x in (5, 10):
            return PALETTE["eye"]
        if kind == "head_left" and x in (3, 4) and y in (5, 10):
            return PALETTE["eye"]
        if kind == "head_right" and x in (11, 12) and y in (5, 10):
            return PALETTE["eye"]
        return PALETTE["head"]

    if kind.startswith("tail_"):
        _, axis = tail_inside(kind, x, y)
        if edge:
            return PALETTE["body_outline"]
        if axis < 4.0:
            return PALETTE["body_light"]
        if axis > 12.0:
            return PALETTE["body_shadow"]
        return PALETTE["body"]

    if edge:
        return PALETTE["body_outline"]
    if kind.startswith("turn_"):
        # 转弯内侧稍暗，外侧稍亮，强化弯曲体积感。
        if (x + y) % 5 == 0:
            return PALETTE["body_light"]
        if x > 9 and y > 9:
            return PALETTE["body_shadow"]
        return PALETTE["body"]
    if y <= 3:
        return PALETTE["body_highlight"]
    if y <= 6:
        return PALETTE["body_light"]
    if y >= 12:
        return PALETTE["body_shadow"]
    return PALETTE["body"]


def pixels_for(kind: str) -> List[List[RGB]]:
    return [
        [classify_pixel(kind, x, y) for x in range(WIDTH)]
        for y in range(HEIGHT)
    ]


def rgb565_bytes(kind: str) -> bytes:
    output = bytearray()
    for row in pixels_for(kind):
        for red, green, blue in row:
            value = rgb565(red, green, blue)
            output.extend((value & 0xFF, (value >> 8) & 0xFF))
    if len(output) != WIDTH * HEIGHT * 2:
        raise AssertionError(f"{kind}: RGB565 数据长度错误")
    return bytes(output)


def format_c_array(data: bytes, columns: int = 16) -> str:
    values = [f"0x{value:02X}" for value in data]
    lines = []
    for offset in range(0, len(values), columns):
        lines.append("    " + ", ".join(values[offset:offset + columns]) + ",")
    return "\n".join(lines)


def write_header(path: Path) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(
            """/* 自动生成文件：python3 tools/generate_snake.py */
#ifndef GAME_UI_SNAKE_16X16_H
#define GAME_UI_SNAKE_16X16_H

#include <stdint.h>
#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#define SNAKE_16X16_WIDTH 16U
#define SNAKE_16X16_HEIGHT 16U
#define SNAKE_16X16_STRIDE 32U
#define SNAKE_16X16_DATA_SIZE 512U

/*
 * 数据按 RGB565 little-endian 写入，适用于 LV_COLOR_FORMAT_RGB565。
 * 每个资源都自带与棋盘一致的背景像素，移动 1~2px 时不会露出黑色。
 */
"""
        )
        for symbol, kind in ASSETS:
            data_name = f"{symbol}_data"
            output.write(
                f"static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {data_name}"
                " [SNAKE_16X16_DATA_SIZE] = {\n"
            )
            output.write(format_c_array(rgb565_bytes(kind)))
            output.write(
                f""" }};

static const lv_image_dsc_t {symbol} = {{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = SNAKE_16X16_WIDTH,
    .header.h = SNAKE_16X16_HEIGHT,
    .header.stride = SNAKE_16X16_STRIDE,
    .data_size = SNAKE_16X16_DATA_SIZE,
    .data = {data_name},
}};

"""
            )
        output.write(
            """/* 顺序与 snake_direction_t 一致：上、下、左、右。 */
static const lv_image_dsc_t *const snake_body_by_direction[4] = {
    &snake_body_up, &snake_body_down, &snake_body_left, &snake_body_right,
};
static const lv_image_dsc_t *const snake_tail_by_direction[4] = {
    &snake_tail_up, &snake_tail_down, &snake_tail_left, &snake_tail_right,
};
static const lv_image_dsc_t *const snake_head_by_direction[4] = {
    &snake_head_up, &snake_head_down, &snake_head_left, &snake_head_right,
};

"""
        )
        output.write("#endif /* GAME_UI_SNAKE_16X16_H */\n")


def write_preview(path: Path) -> None:
    scale = 8
    tile_w = WIDTH * scale
    tile_h = HEIGHT * scale
    cols = 4
    rows = (len(ASSETS) + cols - 1) // cols
    image = Image.new("RGB", (cols * tile_w, rows * (tile_h + 22)), BG)
    draw = ImageDraw.Draw(image)
    for index, (symbol, kind) in enumerate(ASSETS):
        col = index % cols
        row = index // cols
        tile = Image.new("RGB", (WIDTH, HEIGHT), BG)
        for y, pixels in enumerate(pixels_for(kind)):
            for x, color in enumerate(pixels):
                tile.putpixel((x, y), color)
        tile = tile.resize((tile_w, tile_h), Image.Resampling.NEAREST)
        x0 = col * tile_w
        y0 = row * (tile_h + 22)
        image.paste(tile, (x0, y0))
        draw.text((x0 + 2, y0 + tile_h + 2), symbol.replace("snake_", ""),
                  fill=(0xE8, 0xF1, 0xF2))
    image.save(path)


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    header = root / "source/idf/game_ui/assets/snake_16x16.h"
    preview = root / "output/sim/snake_assets.png"
    header.parent.mkdir(parents=True, exist_ok=True)
    preview.parent.mkdir(parents=True, exist_ok=True)
    write_header(header)
    write_preview(preview)
    print(header)
    print(preview)


if __name__ == "__main__":
    main()
