cmake_minimum_required(VERSION 3.16)
project(snake_sim C)
set(CMAKE_C_STANDARD 11)
find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_subdirectory(third_party/lvgl)
set(SIM_COMMON_SRCS
    snake_sim.c
    sim_port.c
    ../source/idf/game_ui/game_ui.c
    ../source/idf/game_ui/game_ui_common.c
    ../source/idf/game_ui/game_select_ui.c
    ../source/idf/game_ui/snake_ui.c
    ../source/idf/game_ui/maze_ui.c
    ../source/idf/game_ui/klotski_ui.c
    ../source/game/snake_logic.c
    ../source/game/maze_logic.c
    ../source/game/maze_gen.c
    ../source/game/klotski_logic.c
    ../source/idf/lvgl_port/lvgl_bmp_encoder.c
    ../source/idf/lvgl_port/lv_font_cjk.c
    ../source/idf/lvgl_port/lv_font_cjk_ui.c)
add_executable(snake_sim ${SIM_COMMON_SRCS})
target_include_directories(snake_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/game
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/idf/game_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/idf/game_ui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/idf/ad_keys/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/idf/lvgl_port/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl
    ${SDL2_INCLUDE_DIRS})
target_link_libraries(snake_sim PRIVATE lvgl ${SDL2_LIBRARIES} ZLIB::ZLIB m)
target_compile_options(snake_sim PRIVATE ${SDL2_CFLAGS_OTHER})

add_executable(snake_test ${SIM_COMMON_SRCS})
target_compile_definitions(snake_test PRIVATE SNAKE_TEST_ONLY=1)
target_include_directories(snake_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/game
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/idf/game_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/idf/game_ui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/idf/ad_keys/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/idf/lvgl_port/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl
    ${SDL2_INCLUDE_DIRS})
target_link_libraries(snake_test PRIVATE lvgl ${SDL2_LIBRARIES} ZLIB::ZLIB m)
target_compile_options(snake_test PRIVATE ${SDL2_CFLAGS_OTHER})
