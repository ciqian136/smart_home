# 硬件设计与连接说明

本文只基于当前源码、`smart_home.ioc` 和已确认的模块连接整理，不包含也不引用共创 PCB 文件。

## 主控与供电

- 主控：STM32F103xE 系列。
- 逻辑电平：STM32 IO 为 3.3V。
- 外设模块如 ESP32、ASRPRO、USART HMI、OpenART、风扇和传感器需要按模块规格供电。
- 不同电源模块必须共地，否则 UART、PWM、ADC 和 I2C 信号会不稳定。

## 串口连接

| 接口 | 用途 | 引脚 | 波特率 | 说明 |
| --- | --- | --- | --- | --- |
| USART1 | 下载 / 调试 | PA9 TX、PA10 RX | 115200 | 调试日志默认由各模块 DEBUG 宏控制 |
| USART2 | ESP32 | PA2 TX、PA3 RX | 115200 | AT 指令、OneNET MQTT |
| USART3 | ASRPRO | PB10 TX、PB11 RX | 9600 | 语音命令和播报控制 |
| UART4 | USART HMI | PC10 TX、PC11 RX | 9600 | HMI 数据显示和触摸控制 |
| UART5 | OpenART | PC12 TX、PD2 RX | 115200 | 人脸识别结果输入 |

具体通信帧格式见 `docs/PROTOCOLS.md`。

## ADC 与传感器

| 模块 | STM32 资源 | 说明 |
| --- | --- | --- |
| MQ-2 烟雾 | PA1 / ADC1_IN1（AO），PC2 / GPIO_Input（DO 可选） | 当前上传和播报 ADC 原始值；代码中烟雾 ADC 读取 PA1，PC2 仅用于低电平数字报警辅助判断 |
| PM2.5 | PA4 / ADC1_IN4，PA6 控制采样 LED | 当前上传和播报 ADC 原始值 |
| DHT11 | PA7 单总线 | 读取温度和湿度，内部使用非阻塞起始等待状态 |
| BH1750 | PB6 SCL、PB7 SDA / I2C1 | 光照强度，测量等待用状态机处理 |

注意：烟雾模拟量 AO 当前按实际接线配置在 PA1 / ADC1_IN1；PA2 是 USART2_TX，用于 ESP32，不再作为烟雾输入。若烟雾 DO 没有实际接入，可主要使用 PA1 ADC 原始值判断烟雾状态。

## 灯光与执行器

| 模块 | STM32 资源 | 说明 |
| --- | --- | --- |
| WS2812 灯带 1 | TIM4_CH1 / PD12 | 48 颗灯珠，DMA/PWM 输出 |
| WS2812 灯带 2 | TIM4_CH2 / PD13 | 192 颗灯珠，DMA/PWM 输出 |
| 风扇 | TIM4_CH4 / PD15 | 当前使用 PWM 模式 2，API 范围 `0~1000` |
| 板载 LED | PE5 | 低电平点亮，用于单模块测试和状态指示 |

## 风扇模块说明

当前风扇接口丝印为 `G / V / S`：

- `G`：GND。
- `V`：5V 供电。
- `S`：信号输入。

如果只接 `G + V` 风扇就满速转，说明模块供电后默认开启，`S` 只能调速或使能，未必能完全关断。若要确认是否支持信号关断，可在确保模块允许的前提下测试 `S` 直接接 GND 是否停转。

如果 `S` 接 GND 也不停，软件无法仅靠 PWM 让风扇归零，需要在风扇供电回路增加 MOS 管或使能开关。

## HMI 屏幕

- 使用 `lcd/test.HMI` 工程。
- HMI 页面通过 `printh` / `prints` 发送二进制帧到 STM32。
- RGB 控制帧建议使用 `55 AA 04 ID R G B 0D 0A`。
- 风扇控制帧建议使用 `55 AA 05 H L 0D 0A`。
- 页面初始化或查询时通过 `55 AA 07 ID 0D 0A` / `55 AA 08 0D 0A` 获取 STM32 当前状态。

## ASRPRO 语音模块

- ASRPRO Serial1：GPIO2 TX、GPIO3 RX，9600bps。
- 识别命令后输出文本协议到 STM32 USART3。
- STM32 通过 `PLAY:` / `PLAYS:` 控制 ASRPRO 播放语音片段。
- ASRPRO 外部播放路径不主动打开唤醒窗口，避免无人唤醒时周期性播报退出提示。

## OpenART 人脸识别模块

- OpenART 通过 UART5 向 STM32 输出文本结果。
- 当前只识别 `zeng`，协议为 `FACE:ZENG,score`。
- STM32 侧需要连续确认后才触发语音，降低单帧误判影响。

## ESP32 联网模块

- ESP32 使用 USART2 AT 指令通信。
- 连接 OneNET MQTT 后分时上报属性。
- 云端下发可控制 LED、风扇和两路 RGB。
- 若更换 OneNET 产品或设备，需同步修改 `APP/esp32.c` 中的产品 ID、设备名、鉴权 token 和属性名。
