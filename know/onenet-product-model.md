# OneNET 产品模型字段清单

本文档记录 STM32 固件当前会向 OneNET 上报、会从 OneNET 接收的全部属性字段。OneNET 侧新增或修改物模型时，按本清单核对，避免字段遗漏或命名不一致。

## 主题

| 方向 | Topic |
|------|-------|
| STM32/ESP32 上报 | `$sys/{PRODUCT_ID}/{DEVICE_NAME}/thing/property/post` |
| OneNET 下发 | `$sys/{PRODUCT_ID}/{DEVICE_NAME}/thing/property/set` |
| STM32/ESP32 回复下发 | `$sys/{PRODUCT_ID}/{DEVICE_NAME}/thing/property/set_reply` |
| OneNET 上报回复 | `$sys/{PRODUCT_ID}/{DEVICE_NAME}/thing/property/post/reply` |

所有属性上报当前使用固定消息 ID `"123"`。这个 `id` 是 OneNET 属性上报 payload 的消息 ID，不是物模型属性 identifier。

## STM32 上报字段访问表

以下为当前 STM32 固件会主动上报到 OneNET 的完整属性全集。`构建类型` 对应 `build_onenet_cmd()` 的类型参数：`i=int`，`f=float`，`b=bool`。

| OneNET identifier | OneNET 类型 | 构建类型 | STM32 数值访问 | 上报条件/说明 |
|-------------------|-------------|----------|----------------|---------------|
| `test_int` | int | `i` | `test_int++` | `esp32_run_send()` 内部静态计数器，每轮对应 case 到达时递增 |
| `MQ2` | float | `f` | `device_state_get_smoke()` | 仅 `device_state_smoke_valid()` 为真时上报，单位 ppm |
| `PM25` | float | `f` | `device_state_get_pm25()` | 仅 `device_state_pm25_valid()` 为真时上报，单位 ug/m3 |
| `humi` | float | `f` | `device_state_get_humidity()` | 仅 `device_state_humidity_valid()` 为真时上报，单位 % |
| `temp` | float | `f` | `device_state_get_temperature()` | 仅 `device_state_temperature_valid()` 为真时上报，单位 degC |
| `light` | float | `f` | `device_state_get_light()` | 仅 `device_state_light_valid()` 为真时上报，单位 lux |
| `fan` | int | `i` | `device_state_get_fan_speed()` | 总是上报，范围 0~1000 PWM |
| `RGB1_RAD` | int | `i` | `device_state_get_strip_rgb(1, &r, &g, &b)` 里的 `r` | 室内灯红色分量，0~255；历史字段名是 `RAD`，不是 `RED` |
| `RGB1_GREEN` | int | `i` | `device_state_get_strip_rgb(1, &r, &g, &b)` 里的 `g` | 室内灯绿色分量，0~255 |
| `RGB1_BLUE` | int | `i` | `device_state_get_strip_rgb(1, &r, &g, &b)` 里的 `b` | 室内灯蓝色分量，0~255 |
| `RGB3_RAD` | int | `i` | `device_state_get_strip_rgb(3, &r, &g, &b)` 里的 `r` | 室外灯红色分量，0~255；历史字段名是 `RAD`，不是 `RED` |
| `RGB3_GREEN` | int | `i` | `device_state_get_strip_rgb(3, &r, &g, &b)` 里的 `g` | 室外灯绿色分量，0~255 |
| `RGB3_BLUE` | int | `i` | `device_state_get_strip_rgb(3, &r, &g, &b)` 里的 `b` | 室外灯蓝色分量，0~255 |
| `LED` | bool | `b` | `device_state_get_board_led()` | 总是上报，板载 LED 状态 |
| `auto_enabled` | bool | `b` | `device_state_get_auto_enabled() && !device_state_manual_override_active()` | 总是上报；自动开启但处于手动覆盖期时上报 false |
| `fan_mode` | int | `i` | `device_state_get_fan_mode()` | 总是上报，`0=停止`，`1=手动`，`2=自动` |
| `PM25_alarm` | bool | `b` | `device_state_pm25_alarm()` | 总是上报，PM2.5 是否超过当前阈值 |
| `MQ2_alarm` | bool | `b` | `device_state_smoke_alarm()` | 总是上报，MQ2 DO 报警或 ppm 超过当前阈值 |
| `RGB1_PRESET_STATE` | int | `i` | `esp32_get_light_preset_state(1)` | 室内灯当前预设状态，`0~8` 或 `99=CUSTOM` |
| `RGB3_PRESET_STATE` | int | `i` | `esp32_get_light_preset_state(3)` | 室外灯当前预设状态，`0~8` 或 `99=CUSTOM` |

不会主动上报 `RGB2_*`、`RGB2_PRESET_STATE` 或任何灯带4字段。ID2 是原入户灯保留通道，ID4 不存在可用硬件输出。

## OneNET 上报配置速查表

| Identifier | 数据类型 | 建议范围/枚举 | 补偿/换算 | 备注 |
|------------|----------|---------------|-----------|------|
| `test_int` | int | 0~2147483647 | 无 | 通信计数心跳 |
| `MQ2` | float | 0~10000 ppm | `ppm = 100 * (Rs/Ro)^-2.5`，`Rs = 10k * (3.3 - Vout) / Vout`，`Vout = avg_adc * 3.3 / 4096`；`Ro` 默认 10k，需清洁空气实测校准 | 20s 预热，5 点滑动平均；固件不主动钳位上报值 |
| `PM25` | float | 0~1000 ug/m3 | `ugm3 = max(0, (avg_adc - 200) * 3300 / 4096 / 5.0)` | `200` 是当前清洁空气 ADC 补偿基线，5 点滑动平均 |
| `humi` | float | 0~100 % | 无线性补偿，DHT11 原始湿度值 + 校验和 | 启动等待 2s，约 2s 一次有效采样 |
| `temp` | float | -40~80 degC | 无线性补偿，DHT11 原始温度值 + 校验和 | DHT11 实际常用范围约 0~50 degC，物模型可按固件阈值范围放宽 |
| `light` | float | 0~65535 lux | `lux = raw / 1.2` 后做 5 点滑动平均 | 自动阈值下发限制为 1~10000 lux |
| `fan` | int | 0~1000 | 无 | 风扇 PWM 比较值，不是百分比 |
| `RGB1_RAD` | int | 0~255 | 无 | 室内灯 R；字段名历史拼写为 `RAD` |
| `RGB1_GREEN` | int | 0~255 | 无 | 室内灯 G |
| `RGB1_BLUE` | int | 0~255 | 无 | 室内灯 B |
| `RGB3_RAD` | int | 0~255 | 无 | 室外灯 R；字段名历史拼写为 `RAD` |
| `RGB3_GREEN` | int | 0~255 | 无 | 室外灯 G |
| `RGB3_BLUE` | int | 0~255 | 无 | 室外灯 B |
| `LED` | bool | true/false | 无 | 板载 LED 状态 |
| `auto_enabled` | bool | true/false | 手动覆盖补偿：`device_state_get_auto_enabled() && !device_state_manual_override_active()` | 用户手动控制后的 10 分钟覆盖期内上报 false |
| `fan_mode` | int | 0/1/2 | 无 | `0=停止`，`1=手动`，`2=自动` |
| `PM25_alarm` | bool | true/false | 阈值判断：`PM25 >= pm25_limit` | 默认阈值 75 ug/m3；自动风扇释放有 `limit - 10` 回差 |
| `MQ2_alarm` | bool | true/false | `DO低电平` 或 `avg_adc > 1000` 或 `MQ2 >= smoke_limit_ppm` | 默认阈值 300 ppm；自动风扇释放有 `limit - 50` 回差 |
| `RGB1_PRESET_STATE` | int | 0~8,99 | 当前 RGB 与固定预设表精确匹配；不匹配则 99 | 室内灯当前预设状态 |
| `RGB3_PRESET_STATE` | int | 0~8,99 | 当前 RGB 与固定预设表精确匹配；不匹配则 99 | 室外灯当前预设状态 |

## OneNET 下发配置速查表

| Identifier | 数据类型 | 范围/枚举 | 补偿/限制 | 备注 |
|------------|----------|-----------|-----------|------|
| `LED` | bool | true/false | 无 | 控制板载 LED |
| `fan` | int | 0~1000 | 小于 0 按 0；大于 1000 在设备层限制为 1000 | 云端下发风扇速度会进入手动模式 |
| `auto_enabled` | bool | true/false | true 清除手动覆盖并让风扇进入自动；false 进入手动模式 | 自动控制总开关 |
| `fan_mode` | int | 0/1/2 | 非法值忽略 | `0=停止`，`1=手动`，`2=自动` |
| `RGB1_RAD` / `RGB1_GREEN` / `RGB1_BLUE` | int | 0~255 | 小于 0 按 0；大于 255 按 255 | 室内灯精确 RGB |
| `RGB3_RAD` / `RGB3_GREEN` / `RGB3_BLUE` | int | 0~255 | 小于 0 按 0；大于 255 按 255 | 室外灯精确 RGB |
| `RGB1_PRESET` | int | 0~8 | 非法预设忽略 | 室内灯预设 |
| `RGB3_PRESET` | int | 0~8 | 非法预设忽略 | 室外灯预设 |
| `RGBALL_PRESET` | int | 0~8 | 非法预设忽略 | 同时设置室内灯和室外灯 |
| `light_target` | int | 1/3/255 | 非法目标忽略；缺省按 255=全部 | 通用预设目标，`1=室内`，`3=室外`，`255=全部` |
| `light_preset` | int | 0~8 | 优先于 `light_preset_name` | 通用数字预设 |
| `light_preset_name` | string | OFF/ON/WARM/WHITE/NIGHT/READ/RED/GREEN/BLUE | 大小写不敏感；兼容 DEFAULT/NORMAL/DIM/CLOSE/CLOSED/SLEEP | 通用字符串预设 |
| `temp_low_c10` | int | -400~800 | 必须满足 `low < mid < high` | 单位 0.1 degC，默认 260 |
| `temp_mid_c10` | int | -400~800 | 必须满足 `low < mid < high` | 单位 0.1 degC，默认 280 |
| `temp_high_c10` | int | -400~800 | 必须满足 `low < mid < high` | 单位 0.1 degC，默认 300 |
| `light_on_lux` | int | 1~9999 | 必须小于 `light_off_lux` | 默认 120 lux |
| `light_off_lux` | int | 2~10000 | 必须大于 `light_on_lux` | 默认 200 lux |
| `pm25_limit` | int | 1~1000 | 非法值忽略 | 默认 75 ug/m3 |
| `smoke_limit_ppm` | int | 1~10000 | 非法值忽略 | 默认 300 ppm |

## 必须保留的上报属性

| 字段 | 类型 | 范围/枚举 | 说明 |
|------|------|-----------|------|
| `test_int` | int | >=0 | 通信计数/调试心跳 |
| `MQ2` | float | ppm | MQ2 烟雾浓度，有效时上报 |
| `PM25` | float | ug/m3 | PM2.5，有效时上报 |
| `humi` | float | % | 湿度，有效时上报 |
| `temp` | float | degC | 温度，有效时上报 |
| `light` | float | lux | 光照，有效时上报 |
| `fan` | int | 0~1000 | 风扇 PWM，支持上报和下发 |
| `LED` | bool | true/false | 板载 LED，支持上报和下发 |
| `auto_enabled` | bool | true/false | 自动控制总开关；手动覆盖期间上报 false |
| `fan_mode` | int | 0/1/2 | 0=停止，1=手动，2=自动 |
| `PM25_alarm` | bool | true/false | PM2.5 是否超过阈值 |
| `MQ2_alarm` | bool | true/false | MQ2 是否报警或超过阈值 |

## 灯带 RGB 属性

| 字段 | 类型 | 范围 | 方向 | 说明 |
|------|------|------|------|------|
| `RGB1_RAD` | int | 0~255 | 上报/下发 | 室内灯红色分量；历史字段名保留为 `RAD`，不要在 OneNET 写成 `RED` |
| `RGB1_GREEN` | int | 0~255 | 上报/下发 | 室内灯绿色分量 |
| `RGB1_BLUE` | int | 0~255 | 上报/下发 | 室内灯蓝色分量 |
| `RGB3_RAD` | int | 0~255 | 上报/下发 | 室外灯红色分量；历史字段名保留为 `RAD` |
| `RGB3_GREEN` | int | 0~255 | 上报/下发 | 室外灯绿色分量 |
| `RGB3_BLUE` | int | 0~255 | 上报/下发 | 室外灯蓝色分量 |

不要在云端界面展示 `RGB2_*` 作为可控灯带。STM32 会识别 `RGB2_*` / `RGB2_PRESET`，但只记录为原入户灯保留通道并忽略，不执行控制。

## 新增灯带预设属性

推荐优先使用 per-strip 字段，OneNET 面板最直观：

| 字段 | 类型 | 范围/枚举 | 方向 | 说明 |
|------|------|-----------|------|------|
| `RGB1_PRESET` | int | 0~8 | 下发 | 设置室内灯预设 |
| `RGB3_PRESET` | int | 0~8 | 下发 | 设置室外灯预设 |
| `RGBALL_PRESET` | int | 0~8 | 下发 | 同时设置室内灯和室外灯 |
| `RGB1_PRESET_STATE` | int | 0~8,99 | 上报 | 室内灯当前匹配的预设；99=自定义 RGB |
| `RGB3_PRESET_STATE` | int | 0~8,99 | 上报 | 室外灯当前匹配的预设；99=自定义 RGB |

预设枚举：

| 值 | 名称 | RGB | 用途 |
|----|------|-----|------|
| 0 | `OFF` | 0,0,0 | 关闭 |
| 1 | `ON` | 125,125,125 | 默认开启亮度 |
| 2 | `WARM` | 255,200,100 | 暖光 |
| 3 | `WHITE` | 255,255,255 | 白光 |
| 4 | `NIGHT` | 12,10,5 | 夜灯 |
| 5 | `READ` | 178,140,70 | 阅读 |
| 6 | `RED` | 255,0,0 | 红光 |
| 7 | `GREEN` | 0,255,0 | 绿光 |
| 8 | `BLUE` | 0,0,255 | 蓝光 |
| 99 | `CUSTOM` | 当前 RGB 不匹配固定预设 | 仅设备上报，不要作为下发值 |

## 可选通用预设属性

如果 OneNET 面板想用一个“目标灯带 + 预设”的组合控件，可增加以下字段：

| 字段 | 类型 | 范围/枚举 | 方向 | 说明 |
|------|------|-----------|------|------|
| `light_target` | int | 1/3/255 | 下发 | 1=室内灯，3=室外灯，255=全部；缺省时 `light_preset` 作用于全部启用灯带 |
| `light_preset` | int | 0~8 | 下发 | 数字预设，优先于 `light_preset_name` |
| `light_preset_name` | string | OFF/ON/WARM/WHITE/NIGHT/READ/RED/GREEN/BLUE | 下发 | 字符串预设；也兼容 DEFAULT/NORMAL/DIM/CLOSE/CLOSED/SLEEP |

同一条 `property/set` 中如果同时下发预设和精确 RGB，STM32 会先应用预设，再应用 `RGB*_RAD/GREEN/BLUE`，所以精确 RGB 最终生效。

## 自动化阈值下发属性

| 字段 | 类型 | 范围 | 方向 | 说明 |
|------|------|------|------|------|
| `temp_low_c10` | int | -400~800 | 下发 | 温度低阈值，单位 0.1 degC，默认 260 |
| `temp_mid_c10` | int | -400~800 | 下发 | 温度中阈值，单位 0.1 degC，默认 280 |
| `temp_high_c10` | int | -400~800 | 下发 | 温度高阈值，单位 0.1 degC，默认 300 |
| `light_on_lux` | int | 1~10000 | 下发 | 低于/等于该光照时允许自动开灯，默认 120 |
| `light_off_lux` | int | 1~10000 | 下发 | 高于/等于该光照时自动关闭由自动化打开的灯，默认 200 |
| `pm25_limit` | int | 1~1000 | 下发 | PM2.5 自动风扇触发阈值，默认 75 |
| `smoke_limit_ppm` | int | 1~10000 | 下发 | MQ2 自动风扇触发阈值，默认 300 |

## 面板建议

- 设备选择只展示“室内灯”和“室外灯”，不要展示“入户灯”“池塘灯”或“灯带4”。
- 室内灯控件绑定 `RGB1_*` 与 `RGB1_PRESET` / `RGB1_PRESET_STATE`。
- 室外灯控件绑定 `RGB3_*` 与 `RGB3_PRESET` / `RGB3_PRESET_STATE`。
- 总控预设按钮绑定 `RGBALL_PRESET`。
- 自动控制按钮绑定 `auto_enabled`，风扇模式绑定 `fan_mode`。
