cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND EXTRA_COMPONENT_DIRS
    "source/drivers/LCD35"
    "source/idf"
    "third_party/esp32-lab-bridge/wireless/diag_service"
    "third_party/esp32-lab-bridge/wireless/ota_update"
    "third_party/esp32-lab-bridge/firmware/lvgl_screenshot"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32_game_console)
