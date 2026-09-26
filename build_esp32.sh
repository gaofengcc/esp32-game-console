#!/usr/bin/env bash
# ESP32-S3 游戏机 P1 构建与发布脚本。
# 生成双 OTA 所需的分散镜像和 flash_args；不生成整包 merged bin。

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build}"
SDKCONFIG="${SDKCONFIG:-${PROJECT_DIR}/sdkconfig}"
IDF_TARGET="${IDF_TARGET:-esp32s3}"
APP_NAME="${APP_NAME:-esp32_game_console}"
APP_BIN="${APP_BIN:-${APP_NAME}.bin}"
OUTPUT_DIR="${OUTPUT_DIR:-${PROJECT_DIR}/output}"
LOCAL_TOOLCHAIN_FILE="${LOCAL_TOOLCHAIN_FILE:-${PROJECT_DIR}/tools/toolchain-esp32s3.cmake}"

case "${BUILD_DIR}" in
    /*) ;;
    *) BUILD_DIR="${PROJECT_DIR}/${BUILD_DIR}" ;;
esac
case "${SDKCONFIG}" in
    /*) ;;
    *) SDKCONFIG="${PROJECT_DIR}/${SDKCONFIG}" ;;
esac
case "${OUTPUT_DIR}" in
    /*) ;;
    *) OUTPUT_DIR="${PROJECT_DIR}/${OUTPUT_DIR}" ;;
esac

echo -e "${GREEN}构建 ESP32-S3 游戏机固件${NC}"

# 默认优先使用与 P1 参考工程迁移验证一致的 ESP-IDF v5.3.5。
if [[ -z "${IDF_PATH:-}" ]]; then
    if [[ -d "/home/gaofeng/esp/esp-idf-v5.3.5" ]]; then
        export IDF_PATH="/home/gaofeng/esp/esp-idf-v5.3.5"
    elif [[ -d "${HOME}/esp/esp-idf-v5.3.5" ]]; then
        export IDF_PATH="${HOME}/esp/esp-idf-v5.3.5"
    elif [[ -d "${HOME}/esp/esp-idf" ]]; then
        export IDF_PATH="${HOME}/esp/esp-idf"
    else
        echo -e "${RED}未找到 ESP-IDF，请设置 IDF_PATH。${NC}" >&2
        exit 1
    fi
fi

if [[ ! -f "${IDF_PATH}/export.sh" ]]; then
    echo -e "${RED}IDF_PATH 无效：${IDF_PATH}${NC}" >&2
    exit 1
fi
# shellcheck disable=SC1091
source "${IDF_PATH}/export.sh" >/dev/null

if ! command -v idf.py >/dev/null 2>&1; then
    echo -e "${RED}找不到 idf.py，请检查 ESP-IDF 环境。${NC}" >&2
    exit 1
fi
# 本机 v5.3.5 已预置二进制子模块；跳过 IDF 的联网子模块补全，避免构建阶段
# 因目录非空而尝试访问 GitHub。若本地 IDF 缺文件，编译器仍会给出明确错误。
export IDF_SKIP_CHECK_SUBMODULES="${IDF_SKIP_CHECK_SUBMODULES:-1}"
# 本地文件系统在高并行下偶发生成依赖目录竞态，默认单并行保证可复现。
export NINJAFLAGS="${NINJAFLAGS:--j1}"
if [[ ! -f "${PROJECT_DIR}/partitions_game.csv" ]]; then
    echo -e "${RED}缺少 partitions_game.csv。${NC}" >&2
    exit 1
fi
if [[ ! -f "${LOCAL_TOOLCHAIN_FILE}" ]]; then
    echo -e "${RED}缺少本地工具链入口：${LOCAL_TOOLCHAIN_FILE}${NC}" >&2
    exit 1
fi
# 精简的 LVGL 组件包仍会在 CMake 中注册 examples/demos 的 include 路径；
# 即使配置关闭示例，也需要目录存在才能通过 ESP-IDF v5.3.5 的路径校验。
if [[ -d "${PROJECT_DIR}/managed_components/lvgl__lvgl" ]]; then
    mkdir -p "${PROJECT_DIR}/managed_components/lvgl__lvgl/examples"
    mkdir -p "${PROJECT_DIR}/managed_components/lvgl__lvgl/demos"
fi

cd "${PROJECT_DIR}"
echo -e "${YELLOW}使用 ${IDF_PATH}，$(idf.py --version)${NC}"

if [[ "${CLEAN:-0}" == "1" ]]; then
    echo -e "${YELLOW}清理构建目录：${BUILD_DIR}${NC}"
    # 上一次构建为精简 LVGL 包补的空目录不属于托管组件内容，先移除空目录，
    # 避免 ESP-IDF 的 remove_managed_components 将其误报为手工修改。
    if [[ -d "${PROJECT_DIR}/managed_components/lvgl__lvgl" ]]; then
        rmdir "${PROJECT_DIR}/managed_components/lvgl__lvgl/examples" 2>/dev/null || true
        rmdir "${PROJECT_DIR}/managed_components/lvgl__lvgl/demos" 2>/dev/null || true
    fi
    idf.py -B "${BUILD_DIR}" -DSDKCONFIG="${SDKCONFIG}" \
        -DCMAKE_TOOLCHAIN_FILE="${LOCAL_TOOLCHAIN_FILE}" fullclean || true
fi

if [[ ! -f "${SDKCONFIG}" ]] || ! grep -q "^CONFIG_IDF_TARGET=\"${IDF_TARGET}\"$" "${SDKCONFIG}"; then
    echo -e "${YELLOW}配置目标芯片：${IDF_TARGET}${NC}"
    # 首次配置时可能只留下了上一次失败的半成品目录，idf.py 会拒绝对它 fullclean。
    if [[ -d "${BUILD_DIR}" && ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
        rm -rf "${BUILD_DIR}"
    fi
    mkdir -p "${BUILD_DIR}"
    idf.py -B "${BUILD_DIR}" -DSDKCONFIG="${SDKCONFIG}" \
        -DCMAKE_TOOLCHAIN_FILE="${LOCAL_TOOLCHAIN_FILE}" set-target "${IDF_TARGET}"
fi

echo -e "${YELLOW}开始编译...${NC}"
idf.py -B "${BUILD_DIR}" -DSDKCONFIG="${SDKCONFIG}" \
    -DCMAKE_TOOLCHAIN_FILE="${LOCAL_TOOLCHAIN_FILE}" build

BOOTLOADER_BIN="${BUILD_DIR}/bootloader/bootloader.bin"
PARTITION_BIN="${BUILD_DIR}/partition_table/partition-table.bin"
OTA_DATA_BIN="${BUILD_DIR}/ota_data_initial.bin"
FLASH_ARGS="${BUILD_DIR}/flash_args"
APP_PATH="${BUILD_DIR}/${APP_BIN}"
for required in "${BOOTLOADER_BIN}" "${PARTITION_BIN}" "${OTA_DATA_BIN}" "${APP_PATH}" "${FLASH_ARGS}"; do
    if [[ ! -f "${required}" ]]; then
        echo -e "${RED}构建产物缺失：${required}${NC}" >&2
        exit 1
    fi
done

mkdir -p "${OUTPUT_DIR}"
cp "${BOOTLOADER_BIN}" "${OUTPUT_DIR}/bootloader.bin"
cp "${PARTITION_BIN}" "${OUTPUT_DIR}/partition-table.bin"
cp "${OTA_DATA_BIN}" "${OUTPUT_DIR}/ota_data_initial.bin"
cp "${APP_PATH}" "${OUTPUT_DIR}/${APP_BIN}"
cp "${FLASH_ARGS}" "${OUTPUT_DIR}/flash_args"
if [[ -f "${BUILD_DIR}/flasher_args.json" ]]; then
    cp "${BUILD_DIR}/flasher_args.json" "${OUTPUT_DIR}/flasher_args.json"
fi

export OUTPUT_DIR APP_BIN
python - <<'PY'
import hashlib
import json
import os
from pathlib import Path

output_dir = Path(os.environ["OUTPUT_DIR"])
app_name = os.environ["APP_BIN"]
names = [
    "bootloader.bin",
    "partition-table.bin",
    "ota_data_initial.bin",
    app_name,
    "flash_args",
]
artifacts = []
for name in names:
    path = output_dir / name
    if not path.is_file():
        continue
    data = path.read_bytes()
    artifacts.append({
        "name": name,
        "path": str(path.resolve()),
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    })
(output_dir / "manifest.json").write_text(
    json.dumps({
        "format": "esp32-game-console-artifacts-v1",
        "artifacts": artifacts,
        "ota_app": next((item for item in artifacts if item["name"] == app_name), None),
    }, ensure_ascii=False, indent=2) + "\n",
    encoding="utf-8",
)
for item in artifacts:
    print(
        f"{item['name']}: {item['path']} "
        f"({item['size']} bytes, sha256={item['sha256']})"
    )
PY

echo -e "${GREEN}构建完成，发布目录：${OUTPUT_DIR}${NC}"
echo -e "${GREEN}请使用 output/flash_args 或 idf.py flash 分地址烧录；不生成 merged 整包。${NC}"
