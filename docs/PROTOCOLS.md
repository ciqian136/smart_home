# 串口通信格式说明

本文按当前源码实现整理各串口用途和数据格式，方便后续单独排查屏幕、语音、联网和视觉模块。所有串口均为 8 位数据位、1 位停止位、无校验、无硬件流控。

## 串口总览

| 接口 | 模块 | STM32 引脚 | 波特率 | 数据方向 | 主要格式 |
| --- | --- | --- | --- | --- | --- |
| USART1 | 下载 / 调试 | PA9 TX、PA10 RX | 115200 | STM32 -> PC | 调试文本，默认由各模块 `*_DEBUG` 宏关闭 |
| USART2 | ESP32 | PA2 TX、PA3 RX | 115200 | 双向 | AT 指令、OneNET MQTT 数据 |
| USART3 | ASRPRO | PB10 TX、PB11 RX | 9600 | 双向 | 文本命令、`PLAY` / `PLAYS` 播报控制 |
| UART4 | USART HMI | PC10 TX、PC11 RX | 9600 | 双向 | HMI 控件命令、二进制控制帧 |
| UART5 | OpenART | PC12 TX、PD2 RX | 115200 | OpenART -> STM32 | 人脸识别文本结果 |

## USART1 调试口

USART1 只作为下载和调试接口，不承载正常业务协议。

- 发送方向：STM32 到串口助手 / VS Code 调试终端。
- 格式：普通文本日志，通常以 `\r\n` 结尾。
- 开关：各模块日志由 `SMOKE_DEBUG`、`PM25_DEBUG`、`LCD_DEBUG`、`VOICE_DEBUG`、`ESP32_DEBUG`、`FACE_DEBUG`、`UART_DEBUG` 等宏控制，默认值为 `0`。
- 要求：正常运行时不要在业务路径无条件向 USART1 打印，避免占用下载和调试串口。

## USART2 ESP32 / OneNET

STM32 通过 USART2 控制 ESP32 AT 固件，ESP32 负责 WiFi、MQTT 连接和 OneNET 属性收发。

### STM32 -> ESP32

基础格式：

```text
AT...\r\n
```

OneNET 属性上报使用 `AT+MQTTPUB`：

```text
AT+MQTTPUB=0,"$sys/<product_id>/<device_name>/thing/property/post","{\"id\":\"123\"\,\"version\":\"1.0\"\,\"params\":{\"key\":{\"value\":value}}}",0,0\r\n
```

当前分时上传属性：

| 属性名 | 类型 | 当前含义 | 建议范围 / 精度 |
| --- | --- | --- | --- |
| `test_int` | int | 在线测试值 | `0~100` |
| `MQ2` | int | 烟雾 ADC 原始值 | `0~4095`，步长 `1` |
| `PM25` | int | PM2.5 ADC 原始值 | `0~4095`，步长 `1` |
| `humi` | float | 湿度 | `0.0~100.0`，精度 `0.1` |
| `temp` | float | 温度 | 按 DHT11 实测值，精度 `0.1` |
| `light` | float | 光照 lux | `>=0.0`，精度 `0.1` |
| `fan` | int | 风扇速度 | `0~1000`，步长 `1` |
| `RGB1_RAD` | int | 灯带 1 红色通道 | `0~255`，步长 `1` |
| `RGB1_GREEN` | int | 灯带 1 绿色通道 | `0~255`，步长 `1` |
| `RGB1_BLUE` | int | 灯带 1 蓝色通道 | `0~255`，步长 `1` |
| `RGB2_RAD` | int | 灯带 2 红色通道 | `0~255`，步长 `1` |
| `RGB2_GREEN` | int | 灯带 2 绿色通道 | `0~255`，步长 `1` |
| `RGB2_BLUE` | int | 灯带 2 蓝色通道 | `0~255`，步长 `1` |
| `LED` | bool | 板载 LED 状态 | `true` / `false` |

注意：当前代码中红色属性名是 `RAD`，不是 `RED`。OneNET 产品模型必须与代码一致，否则下发或上传会匹配失败。

### ESP32 -> STM32

ESP32 AT 固件回传 `OK`、`ERROR`、`>`、MQTT 订阅消息等文本。STM32 主要解析 `+MQTTSUBRECV`：

```text
+MQTTSUBRECV:...
```

当前处理的 OneNET 下发主题：

```text
$sys/<product_id>/<device_name>/thing/property/set
```

下发 JSON 的 `params` 中可包含：

```json
{
  "id": "message_id",
  "params": {
    "LED": {"value": true},
    "fan": {"value": 500},
    "RGB1_RAD": {"value": 255},
    "RGB1_GREEN": {"value": 200},
    "RGB1_BLUE": {"value": 100},
    "RGB2_RAD": {"value": 255},
    "RGB2_GREEN": {"value": 200},
    "RGB2_BLUE": {"value": 100}
  }
}
```

STM32 执行后会向 `thing/property/set_reply` 回复：

```json
{"id":"message_id","code":200,"msg":"success"}
```

## USART3 ASRPRO 语音模块

ASRPRO 与 STM32 使用文本行协议，行尾为 `\r\n`。ASRPRO 识别语音后发命令给 STM32，STM32 需要播报时回发 `PLAY` 或 `PLAYS`。

### ASRPRO -> STM32

灯带 1：

```text
LIGHT:ON\r\n
LIGHT:OFF\r\n
LIGHT:COLOR:WARM\r\n
LIGHT:COLOR:WHITE\r\n
LIGHT:COLOR:RED\r\n
LIGHT:COLOR:GREEN\r\n
LIGHT:COLOR:BLUE\r\n
LIGHT:MODE:READ\r\n
LIGHT:MODE:SLEEP\r\n
LIGHT:MODE:NIGHT\r\n
```

灯带 2：

```text
LIGHT2:ON\r\n
LIGHT2:OFF\r\n
LIGHT2:COLOR:WARM\r\n
LIGHT2:COLOR:WHITE\r\n
LIGHT2:COLOR:RED\r\n
LIGHT2:COLOR:GREEN\r\n
LIGHT2:COLOR:BLUE\r\n
LIGHT2:MODE:READ\r\n
LIGHT2:MODE:SLEEP\r\n
LIGHT2:MODE:NIGHT\r\n
```

风扇：

```text
FAN:ON\r\n
FAN:OFF\r\n
FAN:SPEED:UP\r\n
FAN:SPEED:DOWN\r\n
FAN:SPEED:1\r\n
FAN:SPEED:2\r\n
FAN:SPEED:3\r\n
FAN:SPEED:4\r\n
```

板载 LED：

```text
LED:ON\r\n
LED:OFF\r\n
LED:TOGGLE\r\n
```

环境查询：

```text
QUERY:TEMP\r\n
QUERY:HUMI\r\n
QUERY:DUST\r\n
QUERY:SMOKE\r\n
QUERY:LIGHT\r\n
QUERY:ALL\r\n
```

播放状态：

```text
VOICE:BUSY\r\n
VOICE:IDLE\r\n
```

`VOICE:BUSY` / `VOICE:IDLE` 由 ASRPRO 在播放开始和播放结束时回传，STM32 用于避免命令播报、报警播报和人脸播报互相打断。

### STM32 -> ASRPRO

播放单条语音：

```text
PLAY:<voice_id>\r\n
```

顺序播放多条语音：

```text
PLAYS:<voice_id>,<voice_id>,...\r\n
```

当前常用语音片段编号在 `APP/voice.c` 中维护，例如数字 `100~112`、小数点 `113`、单位 `114~117`、查询前缀 `118~125`、报警 `127~128`、人脸识别 `129`。

## UART4 USART HMI 屏幕

HMI 屏幕工程为 `lcd/test.HMI`，当前 STM32 UART4 波特率为 `9600`。屏幕向 STM32 发二进制帧，STM32 向屏幕写 USART HMI 控件命令。

### STM32 -> HMI

控件命令格式：

```text
<component>.txt="text" FF FF FF
<component>.val=value FF FF FF
sys0=0 FF FF FF
sys1=0 FF FF FF
```

当前传感器显示：

| 控件 | 含义 | 格式 |
| --- | --- | --- |
| `t0` | 温度 | 文本，1 位小数 |
| `t1` | 湿度 | 文本，1 位小数 |
| `t2` | PM2.5 ADC | 文本，整数 |
| `t12` | 烟雾 ADC | 文本，整数 |
| `t13` | 光照 lux | 文本，1 位小数 |
| `t14` | 灯带 1 开关状态 | `true` / `false` |

RGB 查询回写控件：`h_cur`、`h0`、`h1`、`h2`、`n0`、`n1`、`n2`、`b13`，最后写 `sys0=0`。

风扇查询回写控件：`h0`、`n0`、`b7`，最后写 `sys1=0`。

### HMI -> STM32

所有控制帧以 `55 AA` 开头，以 `0D 0A` 结束。

RGB 控制，旧格式，默认灯带 1：

```text
55 AA 04 R G B 0D 0A
```

RGB 控制，新格式，带灯带 ID：

```text
55 AA 04 ID R G B 0D 0A
```

- `ID=0` 或 `ID=1`：灯带 1。
- `ID=2` 或 `ID=3`：灯带 2。
- `R/G/B`：`0~255`。

灯带 2 旧兼容格式：

```text
55 AA 06 R G B 0D 0A
```

风扇控制：

```text
55 AA 05 H L 0D 0A
```

- 速度范围：`0~1000`。
- `H L` 是 2 字节速度值。代码优先按大端解析，也兼容 USART HMI `prints h0.val,2` 产生的小端顺序；超过 `1000` 会钳位到 `1000`。

灯带状态查询：

```text
55 AA 07 ID 0D 0A
```

风扇状态查询：

```text
55 AA 08 0D 0A
```

USART HMI 自带状态返回如 `1A FF FF FF` 不属于业务帧，STM32 会丢弃等待下一个 `55 AA` 帧头。

## UART5 OpenART 人脸识别

OpenART 只向 STM32 发送文本识别结果，STM32 当前不向 OpenART 发送控制命令。文本建议以换行结束，当前代码按行读取。

识别到 `zeng`：

```text
FACE:ZENG,<score>\r\n
```

- `score`：OpenART 输出的置信度整数，当前 STM32 阈值为 `70`。
- STM32 连续确认后触发 `voice_play_face_zeng()`，不使用固定 10 秒冷却。

未识别到目标：

```text
FACE:NONE\r\n
```

错误状态：

```text
FACE:ERR,<reason>\r\n
```

当前常见错误为：

```text
FACE:ERR,MODEL\r\n
```
