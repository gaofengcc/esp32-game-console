#!/usr/bin/env python3
"""采集贪吃蛇扭动动画的多帧 PNG 和 GIF。

脚本每一帧都调用真实的 SDL/LVGL 模拟器，并通过 ``--advance-ms`` 将
动画时钟推进到不同时间点。这样即使模拟器进程每帧重新启动，也不会把
所有截图误采成同一个初始相位。``paused`` 场景用于证明静止时仍会扭动。

用法：
    python3 tools/capture_snake_wiggle.py
    python3 tools/capture_snake_wiggle.py --sim simulator/build/snake_sim

输出：
    output/sim/snake_wiggle_frames.png
    output/sim/snake_move.gif
    output/sim/snake_idle.gif
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Iterable, List, Sequence

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SIM = ROOT / "simulator" / "build" / "snake_sim"
OUTPUT_DIR = ROOT / "output" / "sim"
FRAME_COUNT = 6
FRAME_INTERVAL_MS = 50
GIF_DURATION_MS = 80
GIF_INTERVAL_MS = 80
FRAME_SIZE = (480, 320)
# 蛇初始位置在棋盘中心；裁剪后 2 倍放大，便于看清 1~2px 的横向扭动。
CROP_BOX = (120, 96, 360, 256)
CROP_SCALE = 2


def _font(size: int) -> ImageFont.ImageFont:
    for path in (
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ):
        if Path(path).is_file():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def _run_frame(
    simulator: Path,
    scene: str,
    elapsed_ms: int,
    shot_path: Path,
    score_path: Path,
) -> None:
    env = os.environ.copy()
    env.setdefault("SDL_VIDEODRIVER", "dummy")
    cmd = [
        str(simulator),
        "--scene",
        scene,
        "--steps",
        "1",
        "--advance-ms",
        str(elapsed_ms),
        "--score-file",
        str(score_path),
        "--shot",
        str(shot_path),
    ]
    result = subprocess.run(
        cmd,
        cwd=ROOT,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"模拟器采集失败 scene={scene} t={elapsed_ms}ms rc={result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if not shot_path.is_file():
        raise RuntimeError(f"模拟器没有生成截图：{shot_path}")


def _capture_set(
    simulator: Path,
    scene: str,
    directory: Path,
    score_path: Path,
    interval_ms: int,
) -> List[Image.Image]:
    frames: List[Image.Image] = []
    for index in range(FRAME_COUNT):
        elapsed = index * interval_ms
        shot = directory / f"{scene}_{index:02d}.png"
        _run_frame(simulator, scene, elapsed, shot, score_path)
        image = Image.open(shot).convert("RGB")
        if image.size != FRAME_SIZE:
            raise RuntimeError(
                f"截图尺寸异常：{shot} got={image.size}, expected={FRAME_SIZE}"
            )
        frames.append(image)
    return frames


def _write_labeled_sequence(frames: Sequence[Image.Image], path: Path) -> None:
    cols = 3
    rows = (len(frames) + cols - 1) // cols
    panel_size = (
        (CROP_BOX[2] - CROP_BOX[0]) * CROP_SCALE,
        (CROP_BOX[3] - CROP_BOX[1]) * CROP_SCALE,
    )
    canvas = Image.new(
        "RGB",
        (panel_size[0] * cols, panel_size[1] * rows),
        (8, 12, 17),
    )
    draw = ImageDraw.Draw(canvas)
    label_font = _font(20)
    for index, frame in enumerate(frames):
        x = (index % cols) * panel_size[0]
        y = (index // cols) * panel_size[1]
        canvas.paste(_crop_for_gif(frame), (x, y))
        label = f"frame {index}  t={index * FRAME_INTERVAL_MS}ms"
        # 深色底条保证标注在不同棋盘颜色上都可读。
        draw.rectangle((x + 6, y + 6, x + 210, y + 34), fill=(8, 12, 17))
        draw.text((x + 12, y + 8), label, font=label_font, fill=(245, 248, 250))
    path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(path)


def _crop_for_gif(frame: Image.Image) -> Image.Image:
    crop = frame.crop(CROP_BOX)
    return crop.resize(
        (crop.width * CROP_SCALE, crop.height * CROP_SCALE),
        Image.Resampling.NEAREST,
    )


def _write_gif(frames: Sequence[Image.Image], path: Path) -> None:
    enlarged = [_crop_for_gif(frame) for frame in frames]
    first, rest = enlarged[0], enlarged[1:]
    first.save(
        path,
        save_all=True,
        append_images=rest,
        duration=GIF_DURATION_MS,
        loop=0,
        optimize=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="采集蛇身扭动的 PNG/GIF 证据")
    parser.add_argument(
        "--sim",
        type=Path,
        default=DEFAULT_SIM,
        help="snake_sim 可执行文件路径",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=OUTPUT_DIR,
        help="输出目录",
    )
    args = parser.parse_args()

    simulator = args.sim if args.sim.is_absolute() else ROOT / args.sim
    output_dir = args.output_dir if args.output_dir.is_absolute() else ROOT / args.output_dir
    simulator = simulator.resolve()
    output_dir = output_dir.resolve()
    if not simulator.is_file() or not os.access(simulator, os.X_OK):
        raise SystemExit(f"找不到可执行模拟器：{simulator}")
    output_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="snake_wiggle_", dir=output_dir) as tmp:
        temp_dir = Path(tmp)
        score_path = temp_dir / ".snake_score"
        sequence_frames = _capture_set(
            simulator, "game", temp_dir, score_path, FRAME_INTERVAL_MS
        )
        move_frames = _capture_set(
            simulator, "game", temp_dir, score_path, GIF_INTERVAL_MS
        )
        idle_frames = _capture_set(
            simulator, "paused", temp_dir, score_path, GIF_INTERVAL_MS
        )

        # 序列对比图使用移动中场景，避免暂停提示遮住蛇；静止时的证据
        # 由单独的 snake_idle.gif 提供，二者共享同样的时间采样点。
        _write_labeled_sequence(
            sequence_frames, output_dir / "snake_wiggle_frames.png"
        )
        _write_gif(move_frames, output_dir / "snake_move.gif")
        _write_gif(idle_frames, output_dir / "snake_idle.gif")

    print(f"snake_wiggle_frames: {(output_dir / 'snake_wiggle_frames.png').resolve()}")
    print(f"snake_move_gif: {(output_dir / 'snake_move.gif').resolve()}")
    print(f"snake_idle_gif: {(output_dir / 'snake_idle.gif').resolve()}")
    print(
        "采集参数："
        f"{FRAME_COUNT} 帧，帧间隔 {FRAME_INTERVAL_MS}ms，"
        f"GIF {1000 / GIF_DURATION_MS:.1f}fps，裁剪框 {CROP_BOX}，放大 {CROP_SCALE}x"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
