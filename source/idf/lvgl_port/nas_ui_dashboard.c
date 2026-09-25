/**
 * @file nas_ui_dashboard.c
 * @brief LVGL 端口最小首页（保留兼容入口，去除 NAS 业务依赖）。
 *
 * P1 主应用可直接调用 nas_ui_create_dashboard() 创建空首页，
 * 后续按键测试页在 LVGL 任务上下文中增量构建。
 */

#include "lvgl_port.h"
#include "lvgl.h"

void nas_ui_create_dashboard(void)
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

/* 兼容旧版调用方；P1 默认不需要外部 NAS 数据更新。 */
void nas_ui_update_data(const void *data)
{
    (void)data;
}

