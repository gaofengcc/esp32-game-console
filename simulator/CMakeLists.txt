cmake_minimum_required(VERSION 3.16)
project(snake_sim C)
set(CMAKE_C_STANDARD 11)
find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_subdirectory(third_party/lvgl)
add_executable(snake_sim snake_sim.c ../source/game/snake_logic.c ../source/idf/lvgl_port/lv_font_cjk.c)
target_include_directories(snake_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/game
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl
    ${SDL2_INCLUDE_DIRS})
target_link_libraries(snake_sim PRIVATE lvgl ${SDL2_LIBRARIES} ZLIB::ZLIB m)
target_compile_options(snake_sim PRIVATE ${SDL2_CFLAGS_OTHER})

add_executable(snake_test snake_sim.c ../source/game/snake_logic.c ../source/idf/lvgl_port/lv_font_cjk.c)
target_compile_definitions(snake_test PRIVATE SNAKE_TEST_ONLY=1)
target_include_directories(snake_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/game
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl
    ${SDL2_INCLUDE_DIRS})
target_link_libraries(snake_test PRIVATE lvgl ${SDL2_LIBRARIES} ZLIB::ZLIB m)
target_compile_options(snake_test PRIVATE ${SDL2_CFLAGS_OTHER})
