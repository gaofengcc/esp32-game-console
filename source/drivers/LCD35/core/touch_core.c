/**
 * @file touch_core.c
 * @brief 触摸屏核心逻辑
 *
 * 平台无关的XPT2046触摸屏功能实现,包括校准
 *
 * @copyright Copyright (c) 2024
 */

#include "../include/touch_config.h"
#include "../hal/touch_hal.h"
#include "../adapter/storage_adapter.h"
#include <string.h>
#include <math.h>

/*===========================================================================*/
/* 触摸设备结构体                                                            */
/*===========================================================================*/

typedef struct {
    uint8_t (*init)(void);          /* 初始化函数指针 */
    uint8_t (*scan)(uint8_t);       /* 扫描函数指针 */
    void (*adjust)(void);           /* 校准函数指针 */
    uint16_t x0;                    /* 原始X坐标 */
    uint16_t y0;                    /* 原始Y坐标 */
    uint16_t x;                     /* 当前X坐标 */
    uint16_t y;                     /* 当前Y坐标 */
    uint8_t sta;                    /* 状态 */
    float xfac;                     /* X校准因子 */
    float yfac;                     /* Y校准因子 */
    int16_t xoff;                   /* X偏移 */
    int16_t yoff;                   /* Y偏移 */
    uint8_t touchtype;              /* 触摸类型 */
} _m_tp_dev;

/*===========================================================================*/
/* 静态变量                                                                  */
/*===========================================================================*/

static _m_tp_dev tp_dev = {
    .init = NULL,
    .scan = NULL,
    .adjust = NULL,
    .x0 = 0,
    .y0 = 0,
    .x = 0,
    .y = 0,
    .sta = 0,
    .xfac = 0,
    .yfac = 0,
    .xoff = 0,
    .yoff = 0,
    .touchtype = 0
};

static storage_handle_t s_storage = NULL;
static bool touch_initialized = false;

/* XPT2046读取命令 */
static uint8_t CMD_RDX = XPT2046_CMD_XPOS;
static uint8_t CMD_RDY = XPT2046_CMD_YPOS;

/*===========================================================================*/
/* 前向声明                                                                  */
/*===========================================================================*/

static uint16_t touch_read_xoy(uint8_t xy);
static uint8_t touch_read_xy(uint16_t *x, uint16_t *y);
static uint8_t touch_read_xy2(uint16_t *x, uint16_t *y);
static int touch_core_load_calibration_internal(void);

/*===========================================================================*/
/* 内部函数实现                                                              */
/*===========================================================================*/

/**
 * @brief 带滤波的单坐标读取
 */
static uint16_t touch_read_xoy(uint8_t xy)
{
    uint16_t i, j;
    uint16_t buf[TOUCH_READ_TIMES];
    uint16_t sum = 0;
    uint16_t temp;

    /* 连续读取多次 */
    for (i = 0; i < TOUCH_READ_TIMES; i++) {
        buf[i] = lcd35_touch_hal_read_ad(xy);
    }

    /* 冒泡排序 */
    for (i = 0; i < TOUCH_READ_TIMES - 1; i++) {
        for (j = i + 1; j < TOUCH_READ_TIMES; j++) {
            if (buf[i] > buf[j]) {
                temp = buf[i];
                buf[i] = buf[j];
                buf[j] = temp;
            }
        }
    }

    /* 去掉最大最小值后取平均 */
    sum = 0;
    for (i = TOUCH_LOST_VAL; i < TOUCH_READ_TIMES - TOUCH_LOST_VAL; i++) {
        sum += buf[i];
    }

    temp = sum / (TOUCH_READ_TIMES - 2 * TOUCH_LOST_VAL);
    return temp;
}

/**
 * @brief 读取坐标(基础版)
 */
static uint8_t touch_read_xy(uint16_t *x, uint16_t *y)
{
    uint16_t xtemp, ytemp;

    xtemp = touch_read_xoy(CMD_RDX);
    ytemp = touch_read_xoy(CMD_RDY);

    /* 检查有效性 */
    if (xtemp < TOUCH_PRESS_MIN || ytemp < TOUCH_PRESS_MIN) {
        return 0;
    }

    *x = xtemp;
    *y = ytemp;
    return 1;
}

/**
 * @brief 读取坐标(带校验)
 */
static uint8_t touch_read_xy2(uint16_t *x, uint16_t *y)
{
    uint16_t x1, y1;
    uint16_t x2, y2;
    uint8_t flag;

    flag = touch_read_xy(&x1, &y1);
    if (flag == 0) {
        return 0;
    }

    flag = touch_read_xy(&x2, &y2);
    if (flag == 0) {
        return 0;
    }

    /* 检查两次读取的偏差 */
    if (((x2 <= x1 && x1 < x2 + TOUCH_ERR_RANGE) ||
         (x1 <= x2 && x2 < x1 + TOUCH_ERR_RANGE)) &&
        ((y2 <= y1 && y1 < y2 + TOUCH_ERR_RANGE) ||
         (y1 <= y2 && y2 < y1 + TOUCH_ERR_RANGE))) {
        *x = (x1 + x2) / 2;
        *y = (y1 + y2) / 2;
        return 1;
    }

    return 0;
}

/*===========================================================================*/
/* API函数实现                                                               */
/*===========================================================================*/

/**
 * @brief 初始化触摸核心
 */
int touch_core_init(void)
{
    if (touch_initialized) {
        return 0;
    }

    /* 初始化存储 */
    if (s_storage == NULL) {
        s_storage = storage_init(TOUCH_STORAGE_NAMESPACE);
    }

    /* 第一次读取(初始化) */
    lcd35_touch_hal_read_ad(CMD_RDX);
    lcd35_touch_hal_read_ad(CMD_RDY);

    /* 尝试加载校准数据 */
    if (!touch_core_load_calibration_internal()) {
        /* 未校准,使用默认值 */
        tp_dev.xfac = 0;
        tp_dev.yfac = 0;
        tp_dev.xoff = 0;
        tp_dev.yoff = 0;
    }

    touch_initialized = true;
    return 0;
}

/**
 * @brief 扫描触摸
 */
uint8_t touch_core_scan(uint8_t mode)
{
    uint8_t res = 0;

    if (!lcd35_touch_hal_is_pressed()) {
        /* 无触摸或触摸释放 */
        if (tp_dev.sta & 0x80) {
            /* 之前有触摸,现在释放 */
            tp_dev.sta &= ~(1 << 7);
        }
        return res;
    }

    /* 有触摸 */
    if (touch_read_xy2(&tp_dev.x, &tp_dev.y)) {
        uint16_t raw_x = tp_dev.x;
        uint16_t raw_y = tp_dev.y;
        float screen_x_f;
        float screen_y_f;
        int32_t screen_x;
        int32_t screen_y;

        tp_dev.x0 = raw_x;
        tp_dev.y0 = raw_y;

        /* 物理坐标转屏幕坐标 */
        if (tp_dev.touchtype) {
            /* X,Y方向与屏幕相反 */
            screen_x_f = (float)tp_dev.yfac * raw_y + tp_dev.yoff;
            screen_y_f = (float)tp_dev.xfac * raw_x + tp_dev.xoff;
        } else {
            screen_x_f = (float)tp_dev.xfac * raw_x + tp_dev.xoff;
            screen_y_f = (float)tp_dev.yfac * raw_y + tp_dev.yoff;
        }

        screen_x = (int32_t)(screen_x_f >= 0.0f ? screen_x_f + 0.5f : screen_x_f - 0.5f);
        screen_y = (int32_t)(screen_y_f >= 0.0f ? screen_y_f + 0.5f : screen_y_f - 0.5f);

        /* 限制范围 */
        if (screen_x < 0) {
            screen_x = 0;
        } else if (screen_x >= TOUCH_SCREEN_WIDTH) {
            screen_x = TOUCH_SCREEN_WIDTH - 1;
        }
        if (screen_y < 0) {
            screen_y = 0;
        } else if (screen_y >= TOUCH_SCREEN_HEIGHT) {
            screen_y = TOUCH_SCREEN_HEIGHT - 1;
        }

        tp_dev.x = (uint16_t)screen_x;
        tp_dev.y = (uint16_t)screen_y;
        tp_dev.sta = 0x80;  /* 标记有触摸 */
        res = 1;
    }

    return res;
}

/**
 * @brief 检查是否被按下
 */
bool touch_core_is_pressed(void)
{
    return (tp_dev.sta & 0x80) != 0;
}

/**
 * @brief 获取触摸坐标
 */
void touch_core_get_coords(uint16_t *x, uint16_t *y)
{
    if (x != NULL) {
        *x = tp_dev.x;
    }
    if (y != NULL) {
        *y = tp_dev.y;
    }
}

/**
 * @brief 保存校准数据
 */
int touch_core_save_calibration(void)
{
    if (s_storage == NULL) {
        return -1;
    }

    uint8_t data[TOUCH_CAL_DATA_SIZE];

    /* 将float转换为uint32_t存储 */
    union {
        float f;
        uint32_t u;
    } converter;

    converter.f = tp_dev.xfac;
    data[0] = (converter.u >> 24) & 0xFF;
    data[1] = (converter.u >> 16) & 0xFF;
    data[2] = (converter.u >> 8) & 0xFF;
    data[3] = converter.u & 0xFF;

    converter.f = tp_dev.yfac;
    data[4] = (converter.u >> 24) & 0xFF;
    data[5] = (converter.u >> 16) & 0xFF;
    data[6] = (converter.u >> 8) & 0xFF;
    data[7] = converter.u & 0xFF;

    data[8] = (tp_dev.xoff >> 8) & 0xFF;
    data[9] = tp_dev.xoff & 0xFF;
    data[10] = (tp_dev.yoff >> 8) & 0xFF;
    data[11] = tp_dev.yoff & 0xFF;

    data[12] = tp_dev.touchtype;

    if (storage_write(s_storage, TOUCH_STORAGE_KEY, data, TOUCH_CAL_DATA_SIZE) != 0) {
        return -1;
    }

    if (storage_commit(s_storage) != 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief 加载校准数据(内部实现)
 */
static int touch_core_load_calibration_internal(void)
{
    if (s_storage == NULL) {
        return 0;
    }

    if (!storage_exists(s_storage, TOUCH_STORAGE_KEY)) {
        return 0;
    }

    uint8_t data[TOUCH_CAL_DATA_SIZE];
    int len = storage_read(s_storage, TOUCH_STORAGE_KEY, data, TOUCH_CAL_DATA_SIZE);
    if (len < 0) {
        return 0;
    }

    union {
        float f;
        uint32_t u;
    } converter;

    converter.u = ((uint32_t)data[0] << 24) |
                  ((uint32_t)data[1] << 16) |
                  ((uint32_t)data[2] << 8) |
                  (uint32_t)data[3];
    tp_dev.xfac = converter.f;

    converter.u = ((uint32_t)data[4] << 24) |
                  ((uint32_t)data[5] << 16) |
                  ((uint32_t)data[6] << 8) |
                  (uint32_t)data[7];
    tp_dev.yfac = converter.f;

    tp_dev.xoff = (int16_t)((data[8] << 8) | data[9]);
    tp_dev.yoff = (int16_t)((data[10] << 8) | data[11]);
    tp_dev.touchtype = data[12];

    /* 更新读取命令 */
    if (tp_dev.touchtype) {
        CMD_RDX = XPT2046_CMD_YPOS;
        CMD_RDY = XPT2046_CMD_XPOS;
    } else {
        CMD_RDX = XPT2046_CMD_XPOS;
        CMD_RDY = XPT2046_CMD_YPOS;
    }

    /* 检查校准因子是否有效 */
    if (tp_dev.xfac == 0 || tp_dev.yfac == 0) {
        return 0;
    }

    return 1;
}

/**
 * @brief 加载校准数据(对外API)
 */
int touch_core_load_calibration(void)
{
    return touch_core_load_calibration_internal();
}

/**
 * @brief 获取校准因子
 */
void touch_core_get_calibration(float *xfac, float *yfac, int16_t *xoff, int16_t *yoff)
{
    if (xfac != NULL) {
        *xfac = tp_dev.xfac;
    }
    if (yfac != NULL) {
        *yfac = tp_dev.yfac;
    }
    if (xoff != NULL) {
        *xoff = tp_dev.xoff;
    }
    if (yoff != NULL) {
        *yoff = tp_dev.yoff;
    }
}

/**
 * @brief 设置校准因子
 */
void touch_core_set_calibration(float xfac, float yfac, int16_t xoff, int16_t yoff, uint8_t touchtype)
{
    tp_dev.xfac = xfac;
    tp_dev.yfac = yfac;
    tp_dev.xoff = xoff;
    tp_dev.yoff = yoff;
    tp_dev.touchtype = touchtype;

    /* 更新读取命令 */
    if (touchtype) {
        CMD_RDX = XPT2046_CMD_YPOS;
        CMD_RDY = XPT2046_CMD_XPOS;
    } else {
        CMD_RDX = XPT2046_CMD_XPOS;
        CMD_RDY = XPT2046_CMD_YPOS;
    }
}
