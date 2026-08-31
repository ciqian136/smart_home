# 传感器诊断与串口日志

## UART1 日志开关

传感器模块默认不向 UART1 输出周期日志，避免影响 LCD、ESP32 和语音任务的时序。每个模块都有独立运行时标志位：

| 模块 | 默认值 | 标志位 |
|------|--------|--------|
| DHT11 温湿度 | 关闭 | `dht11_uart1_log_enabled` |
| BH1750 光照 | 关闭 | `bh1750_uart1_log_enabled` |
| PM2.5 | 关闭 | `pm25_uart1_log_enabled` |
| MQ2 烟雾 | 关闭 | `smoke_uart1_log_enabled` |

当前调试配置在 `schedule_init()` 中显式关闭人脸识别日志，并打开 MQ2 日志：

```c
face_uart1_status_report_enabled = 0U;
smoke_uart1_log_enabled = 1U;
```

如需切换日志，只修改这些标志位，不要在传感器采样循环中直接新增无条件 `uart_printf(&huart1, ...)`。

## MQ2 日志格式

MQ2 每约 1 秒输出一次：

```text
[MQ2] raw=<raw_adc> avg=<avg_adc> ppm=<ppm> do_alarm=<0|1> alarm=<0|1> anomaly=0xNN hint=<reason>
```

- `raw`: 当前 ADC1_IN1 / PA1 原始采样。
- `avg`: 5 点滑动平均后的 ADC 值。
- `ppm`: 基于当前 MQ2 经验曲线换算的烟雾浓度，仅用于趋势参考，需实测校准 `MQ2_RO_CLEAN_AIR`。
- `do_alarm`: MQ2 模块 DO 引脚报警状态，低电平为 1。
- `alarm`: 软件最终报警，`do_alarm || avg > ALARM_THRESHOLD`。
- `anomaly`: 异常位图，见下表。

| 位 | 宏 | 初步判断 |
|----|----|----------|
| `0x01` | `SMOKE_ANOMALY_ADC_LOW` | ADC 长期接近 0，更像传感器未供电、AO 断线、接错 ADC 引脚或 GND 短路。 |
| `0x02` | `SMOKE_ANOMALY_ADC_HIGH` | ADC 长期接近满量程，更像 AO 悬空、上拉过强、传感器输出饱和或 ADC 引脚异常。 |
| `0x04` | `SMOKE_ANOMALY_ADC_STUCK` | ADC 多次几乎不变，可能是传感器未加热、供电/接线问题，也可能是 ADC 通道切换或采样链路卡住。 |
| `0x08` | `SMOKE_ANOMALY_DO_ADC_MISMATCH` | DO 报警和 ADC 阈值不一致，优先检查 MQ2 模块比较器电位器和软件阈值是否一致。 |

## 判断硬件还是代码

- 如果 `raw` 随吹气/烟雾明显变化，代码采样通路大概率正常，后续重点是 MQ2 预热、Ro 校准和阈值调节。
- 如果 `raw` 一直为 `0~5` 或 `4090~4095`，优先检查硬件：3.3V/5V、GND、AO 是否接 PA1、DO 是否接 PC2、模块电位器和传感器预热。
- 如果 `raw` 被 PM2.5 采样影响、两个 ADC 通道互相串扰，重点检查 `my_adc_read_channel()` 的通道配置、采样时间和丢弃首样逻辑。
- 如果只有 `do_alarm` 异常而 `raw/avg` 正常变化，通常不是固件主逻辑问题，而是 MQ2 模块板载比较器阈值没有调准。
