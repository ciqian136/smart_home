# WS2812 统一驱动架构

## 核心思想：函数指针替代开关语句

不再用 `if/else` 或 `switch` 路由到不同硬件驱动，而是每个灯带注册时带上自己的发送函数指针。

## 数据结构 (ws2812.h)

```c
#define MAX_STRIPS  4

typedef void (*ws2812_set_all_func_t)(uint8_t r, uint8_t g, uint8_t b);

typedef struct {
    uint8_t  id;
    uint16_t num_leds;
    uint8_t  cur_r, cur_g, cur_b;       // 当前颜色（单一数据源）
    ws2812_set_all_func_t set_all;       // 硬件发送函数指针
} ws2812_strip_t;
```

## 统一 API

| 函数 | 用途 |
|------|------|
| `ws2812_strip_init(id, num_leds, set_all_func)` | 注册一条灯带 |
| `ws2812_strip_set_all(id, r, g, b)` | 设置颜色（含防抖） |
| `ws2812_strip_get_r/g/b(id)` | 查询当前颜色 |
| `ws2812_strip_is_open(id)` | 查询是否开启 |
| `ws2812_strip_get_count()` | 已注册灯带数量 |

## 防抖机制

```c
if (r == s->cur_r && g == s->cur_g && b == s->cur_b) return;
```
防止查询→反馈→事件触发→重复发送的回环。即使 HMI 在 STM32 设置滑块值时触发滑动事件，相同 RGB 值会被静默跳过。

## 初始化示例 (main.c)

```c
ws2812_strip_init(1, 48,  ws2812_set_all);     // TIM4_CH1, 48 LED
ws2812_strip_init(2, 192, ws2812_2_set_all);   // TIM4_CH2, 192 LED
ws2812_strip_init(3, 192, ws2812_3_set_all);   // TIM4_CH3, 192 LED
ws2812_strip_set_all(1, 0, 0, 0);  // 全部初始关闭
ws2812_strip_set_all(2, 0, 0, 0);
ws2812_strip_set_all(3, 0, 0, 0);
```

## 添加新灯带步骤

1. 复制 `ws2812_2.c/.h` → 改名为 `ws2812_4.c/.h`
2. 修改宏定义 (NUM_LEDS4, RESET_US4 等)
3. 修改所有寄存器引用 (CCR2→CCR4, CC2DE→CC4DE, DMA_FLAG_TC4→...)
4. 在 CubeMX 配置新 DMA 通道
5. 在 `headfile.h` 加 `#include "ws2812_4.h"`
6. 在 `main.c` 加 `ws2812_strip_init(4, N, ws2812_4_set_all);`
7. **无需修改 ws2812.c 统一层、lcd.c、voice.c、esp32.c** — 全部自动路由

## 三个硬件驱动对比

| 参数 | 灯带1 | 灯带2 | 灯带3 |
|------|-------|-------|-------|
| 文件 | ws2812.c | ws2812_2.c | ws2812_3.c |
| 定时器通道 | TIM4_CH1 (PD12) | TIM4_CH2 (PD13) | TIM4_CH3 (PD14) |
| DMA | CH7 (更新) | CH4 (比较) | CH5 (比较) |
| 触发源 | UDE | CC2DE | CC3DE |
| LED 数 | 48 | 192 | 192 |
| 复位时间 | 300µs | 80µs | 80µs |
| ARR | 89 | 共用 | 共用 |
| CODE_0/1 | 29/58 | 58/29 (内联) | 58/29 (内联) |

## 颜色数据格式

WS2812 使用 **G-R-B** 字节序，MSB 先发：
```c
for each LED: G7,G6...G0, R7,R6...R0, B7,B6...B0
```
每个 bit 编码为一个 PWM 周期：
- `CODE_0 = 29` (~0.4µs 高电平)
- `CODE_1 = 58` (~0.8µs 高电平)
- `RESET_CODE = 0` (低电平)

复位脉冲：发送 N 个周期的低电平后 LED 锁存颜色。
