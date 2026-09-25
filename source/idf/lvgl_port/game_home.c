/**
 * @file game_home.c
 * @brief 游戏机最小首页入口。
 */

#include "lvgl_port.h"
#include "lvgl.h"

void game_home_create(void)
{
    lv_obj_t *screen = lv_screen_active();
    if (screen == NULL) {
        return;
    }

    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "ESP32-S3 游戏机");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8F1F2), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, FONT_CJK, LV_PART_MAIN);
    lv_obj_center(title);
}

void game_home_update_data(const void *data)
{
    (void)data;
}
