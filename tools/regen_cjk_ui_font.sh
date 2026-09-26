#!/usr/bin/env bash
# 重新生成 LVGL 20/28 中文字库子集.
# 依赖: npx lv_font_conv, SourceHanSansSC-Normal.otf
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FONT="${ROOT}/managed_components/lvgl__lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf"
CONV="${HOME}/.npm/_npx/b62fd1a864044392/node_modules/.bin/lv_font_conv"
OUT="${ROOT}/source/idf/lvgl_port/lv_font_cjk_ui.c"
SYMS_FILE="${1:-${ROOT}/tools/cjk_ui_symbols.txt}"

if [[ ! -x "${CONV}" ]]; then
    echo "找不到 lv_font_conv: ${CONV}" >&2
    exit 1
fi
if [[ ! -f "${FONT}" ]]; then
    echo "找不到字体: ${FONT}" >&2
    exit 1
fi
if [[ ! -f "${SYMS_FILE}" ]]; then
    echo "缺少符号表: ${SYMS_FILE}" >&2
    exit 1
fi

SYMS="$(cat "${SYMS_FILE}")"
TMP20="$(mktemp /tmp/cjk20.XXXXXX.c)"
TMP28="$(mktemp /tmp/cjk28.XXXXXX.c)"
trap 'rm -f "${TMP20}" "${TMP28}"' EXIT

"${CONV}" --font "${FONT}" -r 0x20-0x7F --symbols "${SYMS}" --size 20 --bpp 1 \
    --format lvgl --no-compress --no-kerning --lv-include lvgl.h \
    --lv-font-name lv_font_cjk_20 -o "${TMP20}"
"${CONV}" --font "${FONT}" -r 0x20-0x7F --symbols "${SYMS}" --size 28 --bpp 1 \
    --format lvgl --no-compress --no-kerning --lv-include lvgl.h \
    --lv-font-name lv_font_cjk_28 -o "${TMP28}"

python3 - "${TMP20}" "${TMP28}" "${OUT}" <<'PY'
from pathlib import Path
import re
import sys

def convert(src: Path, prefix: str, font_name: str, size_label: str) -> str:
    text = src.read_text(encoding="utf-8")
    m = re.search(
        r"#if LV_FONT_CJK_\d+\n(.*)\n#endif /\*#if LV_FONT_CJK_\d+\*/",
        text,
        re.S,
    )
    if not m:
        raise SystemExit(f"parse fail {src}")
    body = m.group(1)
    body = re.sub(
        r"#if LVGL_VERSION_MAJOR == 8\n/\*Store all the custom data of the font\*/\n"
        r"static  lv_font_fmt_txt_glyph_cache_t cache;\n#endif\n\n"
        r"#if LVGL_VERSION_MAJOR >= 8\nstatic const lv_font_fmt_txt_dsc_t font_dsc = \{\n"
        r"#else\nstatic lv_font_fmt_txt_dsc_t font_dsc = \{\n#endif\n",
        "static const lv_font_fmt_txt_dsc_t font_dsc = {\n",
        body,
    )
    body = re.sub(
        r"#if LVGL_VERSION_MAJOR == 8\n    \.cache = &cache\n#endif\n",
        "",
        body,
    )
    body = re.sub(
        rf"#if LVGL_VERSION_MAJOR >= 8\nconst lv_font_t {font_name} = \{{\n"
        rf"#else\nlv_font_t {font_name} = \{{\n#endif\n",
        f"const lv_font_t {font_name} = {{\n",
        body,
    )
    body = re.sub(
        r"#if !\(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0\)\n(.*?)\n#endif\n",
        r"\1\n",
        body,
        flags=re.S,
    )
    body = re.sub(
        r"#if LV_VERSION_CHECK\(7, 4, 0\) \|\| LVGL_VERSION_MAJOR >= 8\n(.*?)\n#endif\n",
        r"\1\n",
        body,
        flags=re.S,
    )
    body = re.sub(
        r"#if LV_VERSION_CHECK\(8, 2, 0\) \|\| LVGL_VERSION_MAJOR >= 9\n(.*?)\n#endif\n",
        r"\1\n",
        body,
        flags=re.S,
    )
    fields = [
        "glyph_bitmap",
        "glyph_dsc",
        "cmaps",
        "cache",
        "kern_dsc",
        "kern_scale",
        "cmap_num",
        "bpp",
        "kern_classes",
        "bitmap_format",
        "get_glyph_dsc",
        "get_glyph_bitmap",
        "line_height",
        "base_line",
        "subpx",
        "underline_position",
        "underline_thickness",
        "dsc",
        "fallback",
        "static_bitmap",
    ]
    for field in fields:
        body = re.sub(rf"\.{field}\b", f".__F_{field}__", body)
    for old, new in {
        "glyph_bitmap": f"{prefix}_glyph_bitmap",
        "glyph_dsc": f"{prefix}_glyph_dsc",
        "unicode_list_0": f"{prefix}_unicode_list_0",
        "unicode_list_1": f"{prefix}_unicode_list_1",
        "cmaps": f"{prefix}_cmaps",
        "cache": f"{prefix}_cache",
        "font_dsc": f"{prefix}_font_dsc",
    }.items():
        body = re.sub(rf"\b{old}\b", new, body)
    for field in fields:
        body = body.replace(f".__F_{field}__", f".{field}")
    body = re.sub(r"\nstatic lv_font_fmt_txt_glyph_cache_t .*?_cache;\n", "\n", body)
    body = re.sub(r"\n    \.cache = &.*?_cache\n", "\n", body)
    return f"/* {size_label} */\n{body.strip()}\n"

out = Path(sys.argv[3])
text = (
    "/* 本地 Source Han Sans SC 子集字体, LVGL v9 fmt_txt, 未经缩放. */\n"
    '#include "lvgl.h"\n\n'
    + convert(Path(sys.argv[1]), "cjk20", "lv_font_cjk_20", "20px 正文")
    + "\n"
    + convert(Path(sys.argv[2]), "cjk28", "lv_font_cjk_28", "28px 标题")
)
out.write_text(text, encoding="utf-8")
print(f"wrote {out} ({out.stat().st_size} bytes)")
PY
