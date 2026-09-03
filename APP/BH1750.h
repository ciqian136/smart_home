#ifndef __BH1750_H__
#define __BH1750_H__

#include "headfile.h"

#define BH1750_ADDR (0x23 << 1)

// 指令列表
#define BH1750_POWER_ON       0x01   // 上电
#define BH1750_RESET          0x07   // 复位
#define BH1750_CONT_H_MODE    0x10   // 连续高分辨率模式，1 lx，测量时间 120 ms
#define BH1750_CONT_H_MODE2   0x11   // 连续高分辨率模式2，0.5 lx，测量时间 120 ms
#define BH1750_CONT_L_MODE    0x13   // 连续低分辨率模式，4 lx，测量时间 16 ms
#define BH1750_ONE_H_MODE     0x20   // 单次高分辨率模式，测量后自动断电
#define BH1750_ONE_H_MODE2    0x21   // 单次高分辨率模式2
#define BH1750_ONE_L_MODE     0x23   // 单次低分辨率模式
/** @brief 光照传感器初始化 */
void bh1750_init(void);
/** @brief 光照传感器数据处理（读取当前值）*/
void bh1750_proc(void);
/** @brief 光照传感器反初始化 */
void bh1750_deinit(void);
/** @brief 获取最近一次光照值（lux）*/
float bh1750_get_lux(void);

#endif
