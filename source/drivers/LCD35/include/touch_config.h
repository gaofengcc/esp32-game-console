/**
 * @file touch_config.h
 * @brief 触摸屏配置参数定义
 *
 * XPT2046触摸屏控制器配置,可根据硬件修改
 *
 * @copyright Copyright (c) 2024
 */

#ifndef TOUCH_CONFIG_H
#define TOUCH_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/* ESP32-S3引脚配置                                                          */
/*===========================================================================*/

/* 触摸屏SPI引脚(软件SPI) */
#define TOUCH_PIN_IRQ           5       /* 触摸中断引脚(低电平有效) */
#define TOUCH_PIN_CS            14      /* 片选引脚 */
#define TOUCH_PIN_CLK           15      /* 时钟引脚 */
#define TOUCH_PIN_DIN           7       /* 数据输入引脚(MOSI) */
#define TOUCH_PIN_DO            6       /* 数据输出引脚(MISO) */

/*===========================================================================*/
/* 触摸配置参数                                                              */
/*===========================================================================*/

/* 触摸原始面板尺寸；LVGL 端口按横屏 480x320 做仿射校准 */
#define TOUCH_SCREEN_WIDTH      320
#define TOUCH_SCREEN_HEIGHT     480

/* 触摸读取参数 */
#define TOUCH_READ_TIMES        5       /* 连续读取次数 */
#define TOUCH_LOST_VAL          1       /* 丢弃的最大最小值个数 */
#define TOUCH_ERR_RANGE         50      /* 误差范围 */

/* 触摸阈值 */
#define TOUCH_PRESS_MIN         100     /* 最小触摸压力阈值 */

/* 校准点位置(距离边缘的距离) */
#define TOUCH_CAL_OFFSET        20      /* 校准点距离边缘的偏移 */

/* 校准数据存储键名 */
#define TOUCH_STORAGE_KEY       "touch_cal"
#define TOUCH_STORAGE_NAMESPACE "lcd35"

/*===========================================================================*/
/* XPT2046命令定义                                                           */
/*===========================================================================*/

/* 控制字节位定义 */
#define XPT2046_START           0x80    /* 起始位 */
#define XPT2046_CHANNEL_MASK    0x70    /* 通道选择掩码 */
#define XPT2046_MODE_12BIT      0x00    /* 12位模式 */
#define XPT2046_MODE_8BIT       0x08    /* 8位模式 */
#define XPT2046_SER             0x04    /* 单端模式 */
#define XPT2046_DFR             0x00    /* 差分模式 */
#define XPT2046_PWR_MODE0       0x00    /* 省电模式0 */
#define XPT2046_PWR_MODE1       0x01    /* 省电模式1 */
#define XPT2046_PWR_MODE2       0x02    /* 省电模式2 */
#define XPT2046_PWR_MODE3       0x03    /* 完全省电模式 */

/* 完整命令 */
#define XPT2046_CMD_XPOS        0xD0    /* 读取X位置(差分,12位,省电0) */
#define XPT2046_CMD_YPOS        0x90    /* 读取Y位置(差分,12位,省电0) */
#define XPT2046_CMD_Z1POS       0xB0    /* 读取Z1位置(差分,12位,省电0) */
#define XPT2046_CMD_Z2POS       0xC0    /* 读取Z2位置(差分,12位,省电0) */
#define XPT2046_CMD_TEMP0       0x00    /* 读取温度0 */
#define XPT2046_CMD_TEMP1       0x00    /* 读取温度1 */
#define XPT2046_CMD_VBAT        0xA0    /* 读取电池电压 */
#define XPT2046_CMD_AUX         0xE0    /* 读取辅助输入 */

/*===========================================================================*/
/* 校准数据存储偏移                                                          */
/*===========================================================================*/

#define TOUCH_CAL_ADDR_XFAC     0       /* X校准因子偏移 */
#define TOUCH_CAL_ADDR_YFAC     4       /* Y校准因子偏移 */
#define TOUCH_CAL_ADDR_XOFF     8       /* X偏移偏移 */
#define TOUCH_CAL_ADDR_YOFF     10      /* Y偏移偏移 */
#define TOUCH_CAL_ADDR_TYPE     12      /* 触摸类型偏移 */

#define TOUCH_CAL_DATA_SIZE     16      /* 校准数据总大小 */

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_CONFIG_H */
