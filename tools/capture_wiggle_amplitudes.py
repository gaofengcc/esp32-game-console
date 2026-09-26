#!/usr/bin/env python3
"""采集 2/3/4px 扭动幅度的放大 GIF、静态对比图和重绘统计。

每一帧都从 ``paused`` 场景启动模拟器。暂停只冻结蛇的逻辑移动，游戏页
仍会推进 ``game_ui`` 的扭动时钟；截图裁剪区域避开暂停提示，因此得到的
是固定格子的原地扭动。三档幅度通过临时 CMake 构建注入宏，仓库默认值
仍由 ``game_ui.c`` 保持为 2px。

用法：
    python3 tools/capture_wiggle_amplitudes.py
    python3 tools/capture_wiggle_amplitudes.py --sim simulator/build/snake_sim
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import List, Sequence

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SIM = ROOT / "simulator" / "build" / "snake_sim"
OUTPUT_DIR = ROOT / "output" / "sim"
AMPLITUDES = (2, 3, 4)
FRAME_COUNT = 12
FRAME_INTERVAL_MS = 50
FRAME_SIZE = (480, 320)
# 默认蛇在棋盘中央。裁剪框只保留蛇附近区域，避开 paused 页的顶部提示。
CROP_BOX = (176, 144, 304, 224)
CROP_SCALE = 4
COMPARE_TIME_MS = 300

WIGGLE_RE = re.compile(
    r"WIGGLE_STATS phase_ms=(?P<phase>\d+) "
    r"updated_objects=(?P<objects>\d+) "
    r"offset_1=(?P<offset1>-?\d+) "
    r"offset_2=(?P<offset2>-?\d+) "
    r"offset_3=(?P<offset3>-?\d+)"
)
FLUSH_RE = re.compile(
    r"FLUSH_STATS count=(?P<count>\d+) "
    r"pixels=(?P<pixels>\d+) "
    r"percent=(?P<percent>[0-9.]+) "
    r"max_rect=(?P<max_rect>\d+)"
)


@dataclass(frozen=True)
class FrameStats:
    phase_ms: int
    updated_objects: int
    flush_count: int
    flush_pixels: int
    flush_percent: float
    flush_max_rect: int


@dataclass(frozen=True)
class CaptureSet:
    amplitude: int
    frames: Sequence[Image.Image]
    stats: Sequence[FrameStats]
    simulator: Path


def _font(size: int) -> ImageFont.ImageFont:
    for path in (
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ):
        if Path(path).is_file():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def _build_simulator(amplitude: int, build_dir: Path) -> Path:
    """在临时目录构建指定幅度的模拟器，不污染仓库默认构建。"""
    configure = [
        "cmake",
        "-S",
        str(ROOT / "simulator"),
        "-B",
        str(build_dir),
        f"-DCMAKE_C_FLAGS=-DGAME_UI_WIGGLE_AMPLITUDE_PX={amplitude}",
    ]
    result = subprocess.run(
        configure,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"幅度 {amplitude}px 的模拟器配置失败 rc={result.returncode}\n"
            f"{result.stdout}"
        )
    build = subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", "snake_sim", "-j2"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if build.returncode != 0:
        raise RuntimeError(
            f"幅度 {amplitude}px 的模拟器编译失败 rc={build.returncode}\n"
            f"{build.stdout}"
        )
    simulator = build_dir / "snake_sim"
    if not simulator.is_file() or not os.access(simulator, os.X_OK):
        raise RuntimeError(f"模拟器构建产物不存在：{simulator}")
    return simulator


def _parse_stats(stdout: str, amplitude: int, elapsed_ms: int) -> FrameStats:
    wiggle = WIGGLE_RE.search(stdout)
    flush = FLUSH_RE.search(stdout)
    if not wiggle or not flush:
        raise RuntimeError(
            f"幅度 {amplitude}px t={elapsed_ms}ms 缺少统计输出：\n{stdout}"
        )
    return FrameStats(
        phase_ms=int(wiggle.group("phase")),
        updated_objects=int(wiggle.group("objects")),
        flush_count=int(flush.group("count")),
        flush_pixels=int(flush.group("pixels")),
        flush_percent=float(flush.group("percent")),
        flush_max_rect=int(flush.group("max_rect")),
    )


def _run_frame(
    simulator: Path,
    amplitude: int,
    frame_index: int,
    directory: Path,
) -> tuple[Image.Image, FrameStats]:
    # 用 0 步生成 t=0 初始帧，避免模拟器把 --advance-ms 0 解释为默认移速。
    elapsed_ms = frame_index * FRAME_INTERVAL_MS
    steps = 0 if frame_index == 0 else 1
    shot_path = directory / f"amp{amplitude}_{frame_index:02d}.png"
    score_path = directory / ".snake_score"
    env = os.environ.copy()
    env.setdefault("SDL_VIDEODRIVER", "dummy")
    cmd = [
        str(simulator),
        "--scene",
        "paused",
        "--steps",
        str(steps),
        "--advance-ms",
        str(elapsed_ms),
        "--score-file",
        str(score_path),
        "--shot",
        str(shot_path),
        "--wiggle-stats",
        "--flush-stats",
    ]
    result = subprocess.run(
        cmd,
        cwd=ROOT,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"幅度 {amplitude}px 帧 {frame_index} 采集失败 "
            f"t={elapsed_ms}ms rc={result.returncode}\n{result.stdout}"
        )
    if not shot_path.is_file():
        raise RuntimeError(f"模拟器没有生成截图：{shot_path}")
    image = Image.open(shot_path).convert("RGB")
    if image.size != FRAME_SIZE:
        raise RuntimeError(
            f"截图尺寸异常：{shot_path} got={image.size}, expected={FRAME_SIZE}"
        )
    return image, _parse_stats(result.stdout, amplitude, elapsed_ms)


def _capture_set(
    simulator: Path, amplitude: int, directory: Path
) -> CaptureSet:
    directory.mkdir(parents=True, exist_ok=True)
    frames: List[Image.Image] = []
    stats: List[FrameStats] = []
    for frame_index in range(FRAME_COUNT):
        image, frame_stats = _run_frame(
            simulator, amplitude, frame_index, directory
        )
        frames.append(image)
        stats.append(frame_stats)
    return CaptureSet(amplitude, frames, stats, simulator)


def _crop_for_output(frame: Image.Image) -> Image.Image:
    crop = frame.crop(CROP_BOX)
    return crop.resize(
        (crop.width * CROP_SCALE, crop.height * CROP_SCALE),
        Image.Resampling.NEAREST,
    )


def _write_gif(capture: CaptureSet, path: Path) -> None:
    enlarged = [_crop_for_output(frame) for frame in capture.frames]
    path.parent.mkdir(parents=True, exist_ok=True)
    # Pillow 会把像素完全相同的相邻帧合并，导致“12 帧”被压成更少帧。
    # ImageMagick 的 GIF 写入器会保留这些帧，当前采集环境已有 convert。
    convert = shutil.which("convert") or shutil.which("magick")
    if convert:
        with tempfile.TemporaryDirectory(prefix="wiggle_gif_") as tmp:
            frame_paths: List[str] = []
            for index, frame in enumerate(enlarged):
                frame_path = Path(tmp) / f"frame_{index:02d}.png"
                frame.save(frame_path)
                frame_paths.append(str(frame_path))
            subprocess.run(
                [
                    convert,
                    "-delay",
                    str(FRAME_INTERVAL_MS // 10),
                    "-loop",
                    "0",
                    *frame_paths,
                    str(path),
                ],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        return

    # 没有 ImageMagick 时保留 Pillow 回退路径；非重复帧仍可正常生成。
    first, rest = enlarged[0], enlarged[1:]
    first.save(
        path,
        save_all=True,
        append_images=rest,
        duration=FRAME_INTERVAL_MS,
        loop=0,
        optimize=False,
    )


def _write_compare(
    captures: Sequence[CaptureSet], elapsed_ms: int, path: Path
) -> None:
    frame_index = elapsed_ms // FRAME_INTERVAL_MS
    panels = [_crop_for_output(capture.frames[frame_index]) for capture in captures]
    panel_width, panel_height = panels[0].size
    label_height = 56
    canvas = Image.new(
        "RGB",
        (panel_width * len(panels), panel_height + label_height),
        (8, 12, 17),
    )
    draw = ImageDraw.Draw(canvas)
    label_font = _font(28)
    for index, (capture, panel) in enumerate(zip(captures, panels)):
        x = panel_width * index
        draw.text(
            (x + 14, 12),
            f"amplitude {capture.amplitude}px  t={elapsed_ms}ms",
            font=label_font,
            fill=(245, 248, 250),
        )
        canvas.paste(panel, (x, label_height))
    path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(path)


def _write_stats(captures: Sequence[CaptureSet], path: Path) -> None:
    """输出可复核的逐帧统计，便于报告引用。"""
    lines = [
        "amplitude_px,frame,phase_ms,updated_objects,flush_count,"
        "flush_pixels,flush_percent,flush_max_rect"
    ]
    for capture in captures:
        for index, stats in enumerate(capture.stats):
            lines.append(
                f"{capture.amplitude},{index},{stats.phase_ms},"
                f"{stats.updated_objects},{stats.flush_count},"
                f"{stats.flush_pixels},{stats.flush_percent:.3f},"
                f"{stats.flush_max_rect}"
            )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _summarize(capture: CaptureSet) -> str:
    objects = [item.updated_objects for item in capture.stats]
    pixels = [item.flush_pixels for item in capture.stats]
    max_rect = max(item.flush_max_rect for item in capture.stats)
    return (
        f"amp={capture.amplitude}px "
        f"updated_objects={objects} "
        f"avg_updated_objects={sum(objects) / len(objects):.2f} "
        f"flush_pixels={pixels} "
        f"avg_flush_pixels={sum(pixels) / len(pixels):.2f} "
        f"max_rect={max_rect}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="采集蛇身 2/3/4px 扭动幅度的 GIF、对比图和重绘统计"
    )
    parser.add_argument(
        "--sim",
        type=Path,
        default=DEFAULT_SIM,
        help="默认模拟器路径，仅用于校验；各幅度会临时重新编译",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=OUTPUT_DIR,
        help="输出目录",
    )
    args = parser.parse_args()

    output_dir = (
        args.output_dir if args.output_dir.is_absolute() else ROOT / args.output_dir
    ).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    captures: List[CaptureSet] = []
    with tempfile.TemporaryDirectory(prefix="snake_wiggle_build_") as build_root:
        build_root_path = Path(build_root)
        with tempfile.TemporaryDirectory(
            prefix="snake_wiggle_frames_", dir=output_dir
        ) as frames_root:
            frames_root_path = Path(frames_root)
            for amplitude in AMPLITUDES:
                build_dir = build_root_path / f"amp{amplitude}"
                simulator = _build_simulator(amplitude, build_dir)
                capture = _capture_set(
                    simulator,
                    amplitude,
                    frames_root_path / f"amp{amplitude}",
                )
                captures.append(capture)
                _write_gif(
                    capture,
                    output_dir / f"wiggle_amp{amplitude}_zoom.gif",
                )

            _write_compare(
                captures,
                COMPARE_TIME_MS,
                output_dir / "wiggle_amp_compare.png",
            )
            _write_stats(captures, output_dir / "wiggle_amp_stats.csv")

    for capture in captures:
        print(_summarize(capture))
    print(f"wiggle_amp_compare: {(output_dir / 'wiggle_amp_compare.png').resolve()}")
    for amplitude in AMPLITUDES:
        print(
            f"wiggle_amp{amplitude}_gif: "
            f"{(output_dir / f'wiggle_amp{amplitude}_zoom.gif').resolve()}"
        )
    print(f"wiggle_amp_stats: {(output_dir / 'wiggle_amp_stats.csv').resolve()}")
    print(
        "采集参数："
        f"{FRAME_COUNT} 帧，帧间隔 {FRAME_INTERVAL_MS}ms，"
        f"裁剪框 {CROP_BOX}，放大 {CROP_SCALE}x，"
        f"静态对比时刻 {COMPARE_TIME_MS}ms"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
