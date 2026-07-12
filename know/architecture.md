# 项目架构

## 硬件平台
- **MCU**: STM32F103ZE (Cortex-M3, 72MHz, 512KB Flash)
- **开发环境**: Keil MDK (ARMCC V5), CubeMX, CMake
- **项目文件**: `smart_home.ioc` (CubeMX), `MDK-ARM/smart_home.uvprojx`

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
  fan.c         风扇 PWM 控制
  json_parser.c 轻量级 JSON 解析器 (OneNET 协议)
  BH1750.c      光照传感器 (I2C1)
  dht11.c       温湿度传感器 (GPIO bit-bang)
  PM25.c        PM2.5 粉尘传感器 (ADC)
  smoke.c       MQ2 烟雾传感器 (ADC)
asrpro_code.cpp  ASRPRO 语音模块固件 (独立芯片)
```

## 外设引脚总览

| 外设 | GPIO | 说明 |
|------|------|------|
| USART1 | PA9(TX)/PA10(RX) | 调试串口 115200 |
| USART2 | PA2(TX)/PA3(RX) | ESP32 115200 |
| UART3 | PB10(TX)/PB11(RX) | 语音模块 9600 |
| UART4 | PC10(TX)/PC11(RX) | 串口屏 9600 |
| TIM4_CH1 | PD12 | 灯带1 (48 LED) + DMA1_CH7 |
| TIM4_CH2 | PD13 | 灯带2 (192 LED) + DMA1_CH4 |
| TIM4_CH3 | PD14 | 灯带3 (192 LED) + DMA1_CH5 |
| TIM4_CH4 | PD15 | 风扇 PWM |
| I2C1 | PB6(SCL)/PB7(SDA) | BH1750 光照 |
| GPIO | PE5 | 板载 LED |
| GPIO | PA7 | DHT11 温湿度 |

## 任务调度器 (schedule.c)

每轮 `schedule_run()` 按间隔执行注册的任务：

| 间隔 | 任务 |
|------|------|
| 10ms | voice_run_send, lcd_recv |
| 20ms | esp32_init_nonblock |
| 100ms | esp32_run_send (降频 10:1 → 1s/条) |
| 300ms | smoke_proc, PM25_proc, bh1750_proc, DHT11_proc |
| 500ms | esp32_check_online |
| 1000ms | lcd_send |

每轮主循环也执行 `esp32_run_recv()` (rx_pending 触发)、`esp32_check_cmd_timeout()`、`esp32_flush_reply()`。
