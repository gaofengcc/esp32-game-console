/**
 * @file board_lcd_pins.h
 * @brief 本工程硬件引脚 (ESP32-S3 N16R8 + 3.5 寸 ILI9488)
 *
 * 按开发板左侧排针物理顺序分组, 方便排线:
 *   触摸: GPIO4 - 7, 15 (5 根相邻)
 *   LCD:  GPIO8 (背光) + GPIO9 - 14 (DC/CS/SPI/RST, 连续 7 根同区)
 *
 * 左侧排针顺序参考: 4,5,6,7,15,16,17,18,8,3,46,9,10,11,12,13,14
 * 背光用 GPIO8 (紧挨 LCD 的 GPIO9, 勿用 GPIO3/46 以免影响启动/调试)
 * 不使用 GPIO34-37 (Flash/PSRAM), GPIO45/46 (启动配置).
 */

#ifndef BOARD_LCD_PINS_H
#define BOARD_LCD_PINS_H

/*===========================================================================*/
/* 触摸 XPT2046 软件 SPI: GPIO4-7 连续, CLK 用紧邻的 GPIO15                 */
/*===========================================================================*/

#define BOARD_TOUCH_PIN_CS      4       /* 片选 */
#define BOARD_TOUCH_PIN_IRQ     5       /* 中断 */
#define BOARD_TOUCH_PIN_DO      6       /* MISO */
#define BOARD_TOUCH_PIN_DIN     7       /* MOSI */
#define BOARD_TOUCH_PIN_CLK     15      /* 时钟 (紧挨 GPIO7) */

/*===========================================================================*/
/* LCD ILI9488: GPIO8 背光 + GPIO9-14 连续 (DC/CS + FSPI + RST)              */
/*===========================================================================*/

#define BOARD_LCD_PIN_LED       8       /* 背光, 与 GPIO9 相邻, 同接一排线 */
#define BOARD_LCD_PIN_DC        9       /* 数据/命令 */
#define BOARD_LCD_PIN_CS        10      /* 片选 */
#define BOARD_LCD_SPI_MOSI      11      /* MOSI / FSPID */
#define BOARD_LCD_SPI_SCK       12      /* SCK / FSPICLK */
#define BOARD_LCD_SPI_MISO      13      /* MISO / FSPIQ */
#define BOARD_LCD_PIN_RST       14      /* 复位 */
#define BOARD_LCD_SPI_FREQ_HZ   40000000U

#endif /* BOARD_LCD_PINS_H */
