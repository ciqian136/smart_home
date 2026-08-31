# WS2812 统一驱动架构

## 核心思想：函数指针替代开关语句

不再用 `if/else` 或 `switch` 路由到不同硬件驱动，而是每个灯带注册时带上自己的发送函数指针。

## 数据结构 (ws2812.h)

```c
#define MAX_STRIPS  3

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

## 当前硬件映射

| 灯带ID | 用户名称 | 状态 | LED 数 | 硬件通道 |
|--------|----------|------|--------|----------|
| 1 | 室内灯 | 启用 | 48 | TIM4_CH1 / PD12 |
| 2 | 原入户灯 | 保留不用 | 192 | TIM4_CH2 / PD13 |
| 3 | 室外灯 | 启用 | 192 | TIM4_CH3 / PD14 |

ID2 只保留物理注册和启动强制关闭，不在 LCD、语音或云端控制中暴露。ID4 不可用，TIM4_CH4 / PD15 已经分配给风扇 PWM。

## 初始化示例 (schedule.c)

```c
ws2812_strip_init(DEVICE_STRIP_INDOOR_ID, 48, ws2812_set_all);
ws2812_strip_init(DEVICE_STRIP_ENTRY_RESERVED_ID, 192, ws2812_2_set_all);
ws2812_strip_init(DEVICE_STRIP_OUTDOOR_ID, 192, ws2812_3_set_all);
device_state_init();  // 读取配置，并对三条物理灯带各强制发送一次 OFF
```

`device_state_init()` 使用 `ws2812_strip_set_all_force()`，因此即使驱动缓存默认也是 0/0/0，启动时仍会真实刷新一次全灭状态。

## 扩展约束

当前项目不再规划灯带4。若未来增加新硬件，必须先重新分配风扇 PWM 或新增可用定时器/DMA 输出，再扩展 `MAX_STRIPS` 和上层控制协议。

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
