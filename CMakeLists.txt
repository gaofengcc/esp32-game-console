cmake_minimum_required(VERSION 3.16)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND EXTRA_COMPONENT_DIRS
    "source/drivers/LCD35"
    "source/idf"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp32_game_console)
