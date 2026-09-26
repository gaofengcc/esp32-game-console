#!/usr/bin/env python3
"""生成贪吃蛇食物苹果像素图。

脚本只使用 Python 标准库和 Pillow：
  python3 tools/generate_apples.py

输出：
  source/idf/game_ui/assets/apple_16x16_a.h
  source/idf/game_ui/assets/apple_16x16_b.h
  source/idf/game_ui/assets/apple_16x16_c.h
  output/sim/apple_A_classic.png
  output/sim/apple_B_bright.png
  output/sim/apple_C_simple.png
  output/sim/apple_candidates.png

RGB565 字节序：
  LV_COLOR_FORMAT_RGB565 的图像数据按目标平台原生 16 位数写入。
  ESP32/LVGL 与 SDL 模拟器均为小端，因此每个像素输出 low-byte, high-byte。
  头文件使用 RGB565（不是 RGB565_SWAPPED），避免 LV_COLOR_16_SWAP 造成红蓝/红绿颠倒。
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

from PIL import Image, ImageDraw, ImageFont

WIDTH = 16
HEIGHT = 16
SCALE = 6
BG = (0x0F, 0x17, 0x20)

# 颜色名称采用单字符，图案本身保持易读、可手工调整。
PALETTE: Dict[str, Tuple[int, int, int]] = {
    ".": BG,
    # A 经典红苹果：深红描边、柔和高光、棕色果柄、深绿叶片。
    "o": (0x70, 0x1B, 0x2A),  # 深红描边
    "d": (0xA9, 0x24, 0x38),  # 暗红
    "r": (0xD7, 0x38, 0x4A),  # 中红
    "R": (0xEF, 0x4C, 0x59),  # 亮红
    "p": (0xF5, 0x6D, 0x74),  # 柔和红高光
    "h": (0xFF, 0xF3, 0xE0),  # 奶油色高光
    "q": (0x86, 0x20, 0x31),  # 底部阴影
    "s": (0x6C, 0x43, 0x2F),  # 果柄深棕
    "t": (0xA0, 0x63, 0x3F),  # 果柄亮棕
    "l": (0x1E, 0x5B, 0x3A),  # 深绿叶片
    "L": (0x3E, 0x8B, 0x55),  # 亮绿叶片
}

PATTERNS: Dict[str, Sequence[str]] = {
    "classic": (
        "......s.........",
        ".....stL........",
        "....oostLL......",
        "...oooodLL......",
        "..ooo.ooo.......",
        ".ooRRRRRoo......",
        ".oRRRRRRRo......",
        "ooRRhRRRRoo.....",
        "ooRhhRRRRRoo....",
        "ooRRRRRRRRoo....",
        ".oRRRRRRRoo.....",
        ".ooRRRRRoo......",
        "..ooRRRoo.......",
        "...ooRoo........",
        "....oqo.........",
        "................",
    ),
    "bright": (
        ".....sstL.......",
        "....ssstLL......",
        "...oosstLL......",
        "..oooolLL.......",
        ".ooooRRooo......",
        "ooRRRRRRRoo.....",
        "oRRRhhhRRRo.....",
        "oRRhhhhRRRo.....",
        "RRRhhhhRRRR.....",
        "oRRRRRRRRRoo....",
        "oRRRRRRRRRoo....",
        ".oRRRRRRRoo.....",
        "..ooRRRRoo......",
        "...ooRoo........",
        "....oqo.........",
        "................",
    ),
    "simple": (
        ".......s........",
        "......st........",
        ".....ooo........",
        "...ooRRRoo......",
        "..oRRRRRRRo.....",
        ".oRRRhhhRRRo....",
        "oRRRhhhhRRRo....",
        "oRRRRRRRRRRRo...",
        "oRRRRRRRRRRRo...",
        ".oRRRRRRRRRo....",
        "..oRRRRRRRo.....",
        "...oRRRRRo......",
        "....oRRRo.......",
        ".....ooo........",
        "......q.........",
        "................",
    ),
}

# A/B/C 仅在饱和度、叶片大小、轮廓粗细和高光面积上区分。
STYLE_LABELS = (
    ("A", "classic", "经典红苹果"),
    ("B", "bright", "卡通亮眼版"),
    ("C", "simple", "简洁版"),
)

RGB565 = "rgb565"


def validate_patterns() -> None:
    for style, rows in PATTERNS.items():
        if len(rows) != HEIGHT:
            raise ValueError(f"{style}: 需要 {HEIGHT} 行，实际 {len(rows)} 行")
        for row in rows:
            if len(row) != WIDTH:
                raise ValueError(f"{style}: 行宽应为 {WIDTH}，实际 {len(row)}")
            unknown = set(row) - set(PALETTE)
            if unknown:
                raise ValueError(f"{style}: 未知像素字符 {sorted(unknown)}")


def rgb565(red: int, green: int, blue: int) -> int:
    """将 8 位 RGB 转为 RGB565 数值。"""
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def pattern_rgb(style: str) -> List[List[Tuple[int, int, int]]]:
    return [[PALETTE[pixel] for pixel in row] for row in PATTERNS[style]]


def image_for(style: str) -> Image.Image:
    image = Image.new("RGB", (WIDTH, HEIGHT), BG)
    pixels = pattern_rgb(style)
    for y, row in enumerate(pixels):
        for x, color in enumerate(row):
            image.putpixel((x, y), color)
    return image


def rgb565_bytes(style: str) -> bytes:
    output = bytearray()
    for row in pattern_rgb(style):
        for red, green, blue in row:
            value = rgb565(red, green, blue)
            # LV_COLOR_FORMAT_RGB565：小端 16 位像素，低字节在前。
            output.extend((value & 0xFF, (value >> 8) & 0xFF))
    if len(output) != WIDTH * HEIGHT * 2:
        raise AssertionError(f"{style}: RGB565 数据长度错误: {len(output)}")
    return bytes(output)


def format_c_array(data: bytes, columns: int = 16) -> str:
    values = [f"0x{value:02X}" for value in data]
    lines = []
    for offset in range(0, len(values), columns):
        lines.append("    " + ", ".join(values[offset : offset + columns]) + ",")
    return "\n".join(lines)


def write_one_header(path: Path, style: str, symbol: str, style_macro: int) -> None:
    guard = f"GAME_UI_APPLE_16X16_{symbol.upper()}_H"
    data_name = f"{symbol}_data"
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(
            f"""/* 自动生成文件：python3 tools/generate_apples.py */
#ifndef {guard}
#define {guard}

#include <stdint.h>
#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#define APPLE_16X16_WIDTH 16U
#define APPLE_16X16_HEIGHT 16U
#define APPLE_16X16_STRIDE 32U
#define APPLE_16X16_DATA_SIZE 512U

/*
 * RGB565 字节顺序说明：
 * 数据由脚本按 little-endian（低字节、再高字节）写出，适用于
 * LV_COLOR_FORMAT_RGB565。不要改成 RGB565_SWAPPED；否则在未交换的
 * SDL/ESP32 绘制路径上会出现颜色通道颠倒。
 */
static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {data_name}[APPLE_16X16_DATA_SIZE] = {{
"""
        )
        output.write(format_c_array(rgb565_bytes(style)))
        output.write(
            f"""
}};

static const lv_image_dsc_t {symbol} = {{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = APPLE_16X16_WIDTH,
    .header.h = APPLE_16X16_HEIGHT,
    .header.stride = APPLE_16X16_STRIDE,
    .data_size = APPLE_16X16_DATA_SIZE,
    .data = {data_name},
}};

#define APPLE_16X16_STYLE_ID {style_macro}
#endif /* {guard} */
"""
        )


def write_header(path: Path) -> None:
    """兼容旧调用：额外生成一个包含三种图的聚合头文件。"""
    names = {
        "classic": "apple_16x16_classic",
        "bright": "apple_16x16_bright",
        "simple": "apple_16x16_simple",
    }
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(
            """/* 自动生成文件：python3 tools/generate_apples.py */
#ifndef GAME_UI_APPLE_16X16_H
#define GAME_UI_APPLE_16X16_H

#include <stdint.h>
#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#define APPLE_16X16_WIDTH 16U
#define APPLE_16X16_HEIGHT 16U
#define APPLE_16X16_STRIDE 32U
#define APPLE_16X16_DATA_SIZE 512U

#define APPLE_STYLE_CLASSIC 0
#define APPLE_STYLE_BRIGHT  1
#define APPLE_STYLE_SIMPLE  2

/*
 * RGB565 字节顺序说明：
 * 数据由脚本按 little-endian（低字节、再高字节）写出，适用于
 * LV_COLOR_FORMAT_RGB565。不要改成 RGB565_SWAPPED；否则在未交换的
 * SDL/ESP32 绘制路径上会出现颜色通道颠倒。
 */
"""
        )
        for style, _, _ in STYLE_LABELS:
            key = {"A": "classic", "B": "bright", "C": "simple"}[style]
            symbol = names[key]
            data_name = f"{symbol}_data"
            output.write(
                f"static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {data_name}[APPLE_16X16_DATA_SIZE] = {{\n"
            )
            output.write(format_c_array(rgb565_bytes(key)))
            output.write("\n};\n\n")
            output.write(
                f"static const lv_image_dsc_t {symbol} = {{\n"
                "    .header.magic = LV_IMAGE_HEADER_MAGIC,\n"
                "    .header.cf = LV_COLOR_FORMAT_RGB565,\n"
                "    .header.flags = 0,\n"
                f"    .header.w = APPLE_16X16_WIDTH,\n"
                f"    .header.h = APPLE_16X16_HEIGHT,\n"
                f"    .header.stride = APPLE_16X16_STRIDE,\n"
                f"    .data_size = APPLE_16X16_DATA_SIZE,\n"
                f"    .data = {data_name},\n"
                "};\n\n"
            )
        output.write(
            """/*
 * 默认候选只需修改这个宏（0=经典、1=亮眼、2=简洁），调用方使用
 * APPLE_16X16_DEFAULT_DSC 即可。
 */
#ifndef GAME_UI_APPLE_STYLE
#define GAME_UI_APPLE_STYLE APPLE_STYLE_CLASSIC
#endif

#if GAME_UI_APPLE_STYLE == APPLE_STYLE_BRIGHT
#define APPLE_16X16_DEFAULT_DSC (&apple_16x16_bright)
#elif GAME_UI_APPLE_STYLE == APPLE_STYLE_SIMPLE
#define APPLE_16X16_DEFAULT_DSC (&apple_16x16_simple)
#else
#define APPLE_16X16_DEFAULT_DSC (&apple_16x16_classic)
#endif

#endif /* GAME_UI_APPLE_16X16_H */
"""
        )


def load_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = (
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    )
    for candidate in candidates:
        if Path(candidate).is_file():
            return ImageFont.truetype(candidate, size)
    return ImageFont.load_default()


def load_latin_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidate = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    if Path(candidate).is_file():
        return ImageFont.truetype(candidate, size)
    return ImageFont.load_default()


def draw_mixed_text(
    draw: ImageDraw.ImageDraw,
    xy: Tuple[int, int],
    text: str,
    size: int,
    fill: Tuple[int, int, int],
) -> None:
    """ASCII 使用 DejaVu，中文使用 Droid，避免字体 fallback 把 A/数字画成方框。"""
    latin = load_latin_font(size)
    cjk = load_font(size)
    x, y = xy
    for char in text:
        font = latin if ord(char) < 128 else cjk
        draw.text((x, y), char, font=font, fill=fill)
        advance = draw.textlength(char, font=font)
        x += int(round(advance))


def write_comparison(path: Path, individual_dir: Path) -> None:
    # 每个候选放大 6 倍，并在下方同时展示 1:1 版本。
    panel_w = 160
    # 480x320 满足交付图最小尺寸，预留出放大图、1:1 图和中文标注的呼吸空间。
    panel_h = 320
    canvas = Image.new("RGB", (panel_w * 3, panel_h), (0x08, 0x0C, 0x11))
    draw = ImageDraw.Draw(canvas)
    for index, (letter, style, label) in enumerate(STYLE_LABELS):
        x0 = index * panel_w
        draw_mixed_text(
            draw,
            (x0 + 8, 8),
            f"{letter} {label}",
            18,
            (0xF0, 0xF4, 0xF6),
        )
        image = image_for(style)
        scaled = image.resize((WIDTH * SCALE, HEIGHT * SCALE), Image.Resampling.NEAREST)
        big_x = x0 + (panel_w - scaled.width) // 2
        big_y = 38
        canvas.paste(scaled, (big_x, big_y))
        draw.rectangle(
            (big_x - 1, big_y - 1, big_x + scaled.width, big_y + scaled.height),
            outline=(0x31, 0x3D, 0x47),
        )
        one_x = x0 + (panel_w - WIDTH) // 2
        one_y = 156
        canvas.paste(image, (one_x, one_y))
        draw.rectangle(
            (one_x - 1, one_y - 1, one_x + WIDTH, one_y + HEIGHT),
            outline=(0x31, 0x3D, 0x47),
        )
        draw_mixed_text(
            draw,
            (x0 + 8, 180),
            "1:1 实际大小",
            14,
            (0xA9, 0xB7, 0xC0),
        )
        individual_dir.mkdir(parents=True, exist_ok=True)
        image.save(individual_dir / f"apple_{letter}_{style}_1x.png")
        image.resize((WIDTH * SCALE, HEIGHT * SCALE), Image.Resampling.NEAREST).save(
            individual_dir / f"apple_{letter}_{style}.png"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(path)


def main() -> None:
    parser = argparse.ArgumentParser(description="生成 16x16 RGB565 像素苹果资产")
    parser.add_argument(
        "--header",
        type=Path,
        default=Path("source/idf/game_ui/assets/apple_16x16.h"),
        help="输出 LVGL C 头文件",
    )
    parser.add_argument(
        "--comparison",
        type=Path,
        default=Path("output/sim/apple_candidates.png"),
        help="输出三候选对比图",
    )
    parser.add_argument(
        "--assets-dir",
        type=Path,
        default=Path("output/sim"),
        help="输出各候选放大 PNG 的目录",
    )
    args = parser.parse_args()

    validate_patterns()
    args.header.parent.mkdir(parents=True, exist_ok=True)
    # UI 侧按 A/B/C 分开包含，避免未选中的资产也占用编译单元空间。
    for letter, style, _ in STYLE_LABELS:
        write_one_header(
            args.header.parent / f"apple_16x16_{letter.lower()}.h",
            style,
            f"apple_16x16_{letter.lower()}",
            ord(letter) - ord("A"),
        )
    # 同时保留聚合头文件，方便调试工具或未来一次性预览全部候选。
    write_header(args.header)
    write_comparison(args.comparison, args.assets_dir)
    print(f"已生成 {args.header}")
    print(f"已生成 {args.comparison}")


if __name__ == "__main__":
    main()
