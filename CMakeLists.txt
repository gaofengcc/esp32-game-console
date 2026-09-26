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

# esp32-lab-bridge v0.2.0 的 lvgl_screenshot 在条件 Kconfig 分支中声明
# PRIV_REQUIRES；ESP-IDF 5.3.5 的组件需求扫描阶段不会展开该分支。
# 在目标生成后补齐头文件和链接依赖，不修改只读 submodule。
if(CONFIG_LVGL_SCREENSHOT_ENABLE)
    idf_component_get_property(_lvgl_screenshot_lib lvgl_screenshot COMPONENT_LIB)
    if(_lvgl_screenshot_lib)
        target_include_directories(
            ${_lvgl_screenshot_lib}
            PRIVATE
                "${CMAKE_CURRENT_LIST_DIR}/managed_components/lvgl__lvgl"
                "${CMAKE_CURRENT_LIST_DIR}/managed_components/lvgl__lvgl/src"
                "${CMAKE_CURRENT_LIST_DIR}/source/idf/lvgl_port/include"
        )
        target_link_libraries(
            ${_lvgl_screenshot_lib}
            PRIVATE
                idf::lvgl__lvgl
                idf::lvgl_port
        )
    endif()
endif()
