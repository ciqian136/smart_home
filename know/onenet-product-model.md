# OneNET 产品模型字段清单

本文档记录 STM32 固件当前会向 OneNET 上报、会从 OneNET 接收的全部属性字段。OneNET 侧新增或修改物模型时，按本清单核对，避免字段遗漏或命名不一致。

## 主题

| 方向 | Topic |
|------|-------|
| STM32/ESP32 上报 | `$sys/{PRODUCT_ID}/{DEVICE_NAME}/thing/property/post` |
| OneNET 下发 | `$sys/{PRODUCT_ID}/{DEVICE_NAME}/thing/property/set` |
| STM32/ESP32 回复下发 | `$sys/{PRODUCT_ID}/{DEVICE_NAME}/thing/property/set_reply` |
| OneNET 上报回复 | `$sys/{PRODUCT_ID}/{DEVICE_NAME}/thing/property/post/reply` |

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
