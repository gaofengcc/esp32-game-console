/**
 * @file lcd_config.h
 * @brief LCD配置参数定义
 *
 * ILI9488 LCD控制器配置,可根据硬件修改
 *
 * @copyright Copyright (c) 2024
 */

#ifndef LCD_CONFIG_H
#define LCD_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/* LCD显示配置                                                               */
/*===========================================================================*/

/* LCD分辨率（物理面板 320x480，横屏逻辑坐标为 480x320） */
#define LCD_WIDTH               320     /* 面板短边 */
#define LCD_HEIGHT              480     /* 面板长边 */

/* 显示方向: 0-0度, 1-90度, 2-180度, 3-270度 */
#define LCD_USE_HORIZONTAL      1

/*===========================================================================*/
/* ESP32-S3引脚配置                                                          */
/*===========================================================================*/

/* LCD SPI引脚 */
#define LCD_PIN_LED             4       /* 背光控制引脚 */
#define LCD_PIN_DC              10      /* 数据/命令选择引脚 */
#define LCD_PIN_RST             9       /* 复位引脚 */
#define LCD_PIN_CS              8       /* 片选引脚 */
#define LCD_SPI_SCK             12      /* SPI时钟引脚 */
#define LCD_SPI_MOSI            11      /* SPI数据输出引脚 */
#define LCD_SPI_MISO            13      /* SPI数据输入引脚(可选) */

/* SPI配置 */
#define LCD_SPI_HOST            3       /* SPI3_HOST (VSPI) */
#define LCD_SPI_FREQ            40000000 /* SPI时钟频率 40MHz */
#define LCD_SPI_MODE            0       /* SPI模式0 (CPOL=0, CPHA=0) */

/*===========================================================================*/
/* ILI9488控制器命令                                                         */
/*===========================================================================*/

/* 基本命令 */
#define LCD_CMD_NOP             0x00    /* 空命令 */
#define LCD_CMD_SWRESET         0x01    /* 软件复位 */
#define LCD_CMD_RDDID           0x04    /* 读取ID */
#define LCD_CMD_RDDST           0x09    /* 读取状态 */
#define LCD_CMD_RDMODE          0x0A    /* 读取显示模式 */
#define LCD_CMD_RDMADCTL        0x0B    /* 读取MADCTL */
#define LCD_CMD_RDPIXFMT        0x0C    /* 读取像素格式 */
#define LCD_CMD_RDIMGFMT        0x0D    /* 读取图像格式 */
#define LCD_CMD_RDSIGMODE       0x0E    /* 读取信号模式 */
#define LCD_CMD_RDSELFDIAG      0x0F    /* 读取自诊断 */

/* 显示控制命令 */
#define LCD_CMD_SLPIN           0x10    /* 进入睡眠模式 */
#define LCD_CMD_SLPOUT          0x11    /* 退出睡眠模式 */
#define LCD_CMD_PTLON           0x12    /* 部分显示模式开启 */
#define LCD_CMD_NORON           0x13    /* 正常显示模式 */

#define LCD_CMD_INVOFF          0x20    /* 显示反转关闭 */
#define LCD_CMD_INVON           0x21    /* 显示反转开启 */
#define LCD_CMD_GAMSET          0x26    /* 伽马曲线设置 */
#define LCD_CMD_DISPOFF         0x28    /* 显示关闭 */
#define LCD_CMD_DISPON          0x29    /* 显示开启 */

/* 内存访问命令 */
#define LCD_CMD_CASET           0x2A    /* 列地址设置 */
#define LCD_CMD_PASET           0x2B    /* 页地址设置 */
#define LCD_CMD_RAMWR           0x2C    /* 内存写入 */
#define LCD_CMD_RAMRD           0x2E    /* 内存读取 */

#define LCD_CMD_PTLAR           0x30    /* 部分区域设置 */
#define LCD_CMD_VSCRDEF         0x33    /* 垂直滚动定义 */
#define LCD_CMD_TEOFF           0x34    /* 撕裂效应线关闭 */
#define LCD_CMD_TEON            0x35    /* 撕裂效应线开启 */
#define LCD_CMD_MADCTL          0x36    /* 内存访问控制 */
#define LCD_CMD_VSCRSADD        0x37    /* 垂直滚动起始地址 */
#define LCD_CMD_IDMOFF          0x38    /* 空闲模式关闭 */
#define LCD_CMD_IDMON           0x39    /* 空闲模式开启 */
#define LCD_CMD_COLMOD          0x3A    /* 像素格式设置 */

/* ILI9488扩展命令 */
#define LCD_CMD_IFMODE          0xB0    /* 接口模式控制 */
#define LCD_CMD_FRMCTR1         0xB1    /* 帧率控制(正常模式) */
#define LCD_CMD_FRMCTR2         0xB2    /* 帧率控制(空闲模式) */
#define LCD_CMD_FRMCTR3         0xB3    /* 帧率控制(部分模式) */
#define LCD_CMD_INVCTR          0xB4    /* 显示反转控制 */
#define LCD_CMD_PRCTR           0xB5    /* 空白 porch 控制 */
#define LCD_CMD_DISSET5         0xB6    /* 显示功能控制 */

#define LCD_CMD_PWCTR1          0xC0    /* 电源控制1 */
#define LCD_CMD_PWCTR2          0xC1    /* 电源控制2 */
#define LCD_CMD_PWCTR3          0xC2    /* 电源控制3(正常模式) */
#define LCD_CMD_PWCTR4          0xC3    /* 电源控制4(空闲模式) */
#define LCD_CMD_PWCTR5          0xC4    /* 电源控制5(部分模式) */
#define LCD_CMD_VMCTR1          0xC5    /* VCOM控制1 */
#define LCD_CMD_VMCTR2          0xC6    /* VCOM控制2 */

#define LCD_CMD_VMOFCTR         0xC7    /* VCOM偏移控制 */
#define LCD_CMD_WRID2           0xD1    /* 写ID2值 */
#define LCD_CMD_WRID3           0xD2    /* 写ID3值 */
#define LCD_CMD_NVFCTR1         0xD9    /* NV内存控制器1 */
#define LCD_CMD_NVFCTR2         0xDE    /* NV内存控制器2 */
#define LCD_CMD_NVFCTR3         0xDF    /* NV内存控制器3 */

#define LCD_CMD_GMCTRP1         0xE0    /* 伽马正极性校正 */
#define LCD_CMD_GMCTRN1         0xE1    /* 伽马负极性校正 */
#define LCD_CMD_GPVAn           0xE2    /* 伽马正极性使能 */
#define LCD_CMD_GNVAn           0xE3    /* 伽马负极性使能 */

#define LCD_CMD_DGMLUTR         0xE4    /* 数字伽马查找表(红色) */
#define LCD_CMD_DGMLUTB         0xE5    /* 数字伽马查找表(蓝色) */
#define LCD_CMD_GATECTRL        0xE6    /* 门控制 */
#define LCD_CMD_SPIREAD         0xE7    /* SPI读取命令 */
#define LCD_CMD_PWSET           0xE8    /* 电源设置 */
#define LCD_CMD_UNKOWN1         0xE9    /* 未知命令1 */
#define LCD_CMD_UNKOWN2         0xEF    /* 未知命令2 */

#define LCD_CMD_ADJCTRL         0xF2    /* 调整控制 */
#define LCD_CMD_GAMMASET        0xF7    /* 伽马设置 */

/*===========================================================================*/
/* MADCTL数据位定义                                                          */
/*===========================================================================*/

#define MADCTL_MY               0x80    /* 行地址顺序 */
#define MADCTL_MX               0x40    /* 列地址顺序 */
#define MADCTL_MV               0x20    /* 行/列交换 */
#define MADCTL_ML               0x10    /* 垂直刷新顺序 */
#define MADCTL_BGR              0x08    /* BGR顺序 */
#define MADCTL_MH               0x04    /* 水平刷新顺序 */

/*===========================================================================*/
/* 颜色定义                                                                  */
/*===========================================================================*/

#define LCD_COLOR_BLACK         0x0000  /* 黑色 */
#define LCD_COLOR_NAVY          0x000F  /* 深蓝色 */
#define LCD_COLOR_DARKGREEN     0x03E0  /* 深绿色 */
#define LCD_COLOR_DARKCYAN      0x03EF  /* 深青色 */
#define LCD_COLOR_MAROON        0x7800  /* 褐红色 */
#define LCD_COLOR_PURPLE        0x780F  /* 紫色 */
#define LCD_COLOR_OLIVE         0x7BE0  /* 橄榄色 */
#define LCD_COLOR_LIGHTGREY     0xC618  /* 浅灰色 */
#define LCD_COLOR_DARKGREY      0x7BEF  /* 深灰色 */
#define LCD_COLOR_BLUE          0x001F  /* 蓝色 */
#define LCD_COLOR_GREEN         0x07E0  /* 绿色 */
#define LCD_COLOR_CYAN          0x07FF  /* 青色 */
#define LCD_COLOR_RED           0xF800  /* 红色 */
#define LCD_COLOR_MAGENTA       0xF81F  /* 洋红色 */
#define LCD_COLOR_YELLOW        0xFFE0  /* 黄色 */
#define LCD_COLOR_WHITE         0xFFFF  /* 白色 */
#define LCD_COLOR_ORANGE        0xFD20  /* 橙色 */
#define LCD_COLOR_GREENYELLOW   0xAFE5  /* 黄绿色 */
#define LCD_COLOR_PINK          0xF81F  /* 粉色 */

#ifdef __cplusplus
}
#endif

#endif /* LCD_CONFIG_H */
