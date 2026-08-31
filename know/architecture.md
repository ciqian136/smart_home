# 项目架构

## 硬件平台
- **MCU**: STM32F103ZE (Cortex-M3, 72MHz, 512KB Flash)
- **主要开发环境**: VSCode + CMake + STM32CubeCLT/GCC
- **兼容环境**: Keil MDK (ARMCC V5)，仅作为兼容验证，不是每次修改的必需步骤
- **项目文件**: `smart_home.ioc` (CubeMX), `CMakeLists.txt`, `.vscode/tasks.json`, `MDK-ARM/smart_home.uvprojx`

## 软件分层

```
Core/           HAL 层 (CubeMX 生成) — main.c, tim.c, dma.c, gpio.c, usart.c...
APP/            应用层 (用户代码)
  headfile.h    统一头文件（包含所有 HAL + APP 模块）
  schedule.c    协作式任务调度器
  my_uart.c     UART 驱动抽象 + RX 回调
  ws2812.c      灯带统一管理层
  ws2812_2.c    灯带2 硬件驱动 (TIM4_CH2)
  ws2812_3.c    灯带3 硬件驱动 (TIM4_CH3)
  lcd.c         串口屏通信 (UART4)
  esp32.c       ESP32 WiFi/MQTT/OneNET (UART2)
  voice.c       ASRPRO 语音模块 (UART3)
  face.c        OpenART 人脸识别结果解析 (UART5)
  fan.c         风扇 PWM 控制
  json_parser.c 轻量级 JSON 解析器 (OneNET 协议)
  BH1750.c      光照传感器 (I2C1)
  dht11.c       温湿度传感器 (GPIO bit-bang)
  PM25.c        PM2.5 粉尘传感器 (ADC)
  smoke.c       MQ2 烟雾传感器 (ADC)
  device_state.c 统一设备状态与配置落盘入口
  automation.c  人脸、温度和光照自动化策略
  config_store.c 内部 Flash 双槽位配置保存
  health_monitor.c 任务心跳与可选 IWDG 喂狗
asrpro_code.cpp  ASRPRO 语音模块固件 (独立芯片)
```

## 传感器日志策略

- DHT11、BH1750、PM2.5、MQ2 均保留 UART1 日志开关，默认关闭，避免周期采样刷屏影响实时性。
- 当前调试配置在 `schedule_init()` 中关闭 `face_uart1_status_report_enabled`，打开 `smoke_uart1_log_enabled`，用于优先排查 MQ2。
- MQ2 每约 1 秒输出 ADC、ppm、DO 报警、软件报警和异常位图；异常码说明见 `know/sensor-diagnostics.md`。

## 自动控制策略

- 默认配置为自动控制：`auto_enabled=1`，风扇模式为 `DEVICE_FAN_AUTO`。
- 语音、LCD 或云端手动调整灯光/风扇后进入 10 分钟手动覆盖；覆盖期间自动化不抢控制权。
- 语音可通过 `AUTO:ON`、`FAN:AUTO`、`LIGHT:AUTO` 切回自动控制，通过 `AUTO:OFF`、`FAN:MANUAL`、`LIGHT:MANUAL` 切到手动控制。
- 光照低于 `light_on_lux` 时自动恢复可用灯带最近一次非零 RGB；光照高于 `light_off_lux` 时关闭由自动化打开的灯光。
- 风扇自动速度取温度、PM2.5 和 MQ2 三类需求的最高值：温度按 0/300/500/800 PWM 分档，PM2.5 超限取 800，MQ2 超限或 DO 报警取 1000。
- 自动开启、关闭或调节灯光/风扇时，`automation.c` 只上报事件，`voice.c` 统一排队播报，避免和人脸欢迎、环境查询播报冲突。

## 外设引脚总览

| 外设 | GPIO | 说明 |
|------|------|------|
| USART1 | PA9(TX)/PA10(RX) | 调试串口 115200 |
| USART2 | PA2(TX)/PA3(RX) | ESP32 115200 |
| UART3 | PB10(TX)/PB11(RX) | 语音模块 9600 |
| UART4 | PC10(TX)/PC11(RX) | 串口屏 9600 |
| UART5 | PC12(TX)/PD2(RX) | OpenART 人脸识别 115200 |
| TIM4_CH1 | PD12 | 灯带1：室内灯 (48 LED) + DMA1_CH7 |
| TIM4_CH2 | PD13 | 灯带2：原入户灯保留不用 (192 LED) + DMA1_CH4 |
| TIM4_CH3 | PD14 | 灯带3：室外灯 (192 LED) + DMA1_CH5 |
| TIM4_CH4 | PD15 | 风扇 PWM |
| I2C1 | PB6(SCL)/PB7(SDA) | BH1750 光照 |
| GPIO | PE5 | 板载 LED |
| GPIO | PA7 | DHT11 温湿度 |

## 任务调度器 (schedule.c)

每轮 `schedule_run()` 按间隔执行注册的任务：

| 间隔 | 任务 |
|------|------|
| 10ms | voice_run_send, lcd_recv |
| 20ms | esp32_init_nonblock, face_proc |
| 100ms | esp32_run_send (降频 10:1 → 1s/条) |
| 300ms | smoke_proc, PM25_proc, bh1750_proc, DHT11_proc |
| 500ms | esp32_check_online |
| 1000ms | lcd_send |

每轮主循环也执行 `esp32_run_recv()` (rx_pending 触发)、`esp32_check_cmd_timeout()`、`esp32_flush_reply()`。

## ESP32 断线重连

- ESP32 初始化仍使用非阻塞状态机：`ATE0 → AT+RST → CWMODE → CWJAP → MQTTUSERCFG → MQTTCONN → SUB`。
- 运行期每 10 秒发送 `AT+CWSTATE?`，每 30 秒发送 `AT+MQTTPING=0`。
- 收到 `WIFI DISCONNECT`、`MQTTDISCONNECTED`、`+CWSTATE` 非连接状态、连续 3 次运行期 AT 超时，或 120 秒无有效回应时，会清 UART 缓冲和 AT busy 状态并重新进入初始化状态机。
- 本地 LCD、语音、人脸和自动化不依赖 ESP32 在线状态；断线期间继续本地运行，重连后继续上报状态。

## OpenART 人脸识别集成

- 图像采集、模型训练和推理都在 OpenART 上完成，STM32 不做图像处理。
- 第一版只识别用户本人，OpenART 通过 UART5 向 STM32 发送 `FACE:*` 文本帧。
- `face.c` 只维护控制标志位：在线状态、是否稳定识别到主人、最近置信度。
- `automation.c` 消费人脸确认事件：无手动覆盖且自动化开启时，按温度档位调节风扇，按光照阈值恢复最近一次灯带状态。
- `voice.c` 消费独立欢迎事件：向 ASRPRO 发送 `PLAYS` 队列，播报“欢迎回家曾先生”、实时温湿度、风扇调节、实时光照和灯光调节结果。
- 欢迎播报冷却时间为 30 秒；传感器无有效数据时播报“环境数据正在更新”。
- 后续语音、灯光、风扇等动作应读取 `face` 模块状态，不要直接在 `face.c` 内驱动执行器。
