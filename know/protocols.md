# 通信协议汇总

## 1. 串口屏 HMI (UART4, 9600)

### STM32 → 屏幕（文本指令）
```
t0.txt="25.5"\xFF\xFF\xFF
h0.val=128\xFF\xFF\xFF
b13.txt="开"\xFF\xFF\xFF
```
- `uart_printf(&huart4, ...)` 发送文本
- `send_end()` 追加 3字节 0xFF 结束符
- 可控制任何控件的任何属性

### 屏幕 → STM32（二进制帧）
格式: `55 AA <CMD> <DATA...> 0D 0A`

| 命令 | 帧格式 | 长度 | 说明 |
|------|--------|------|------|
| `04` | `55 AA 04 <strip_id> R G B 0D 0A` | 9B | 设置灯带 (新格式含ID) |
| `04` | `55 AA 04 R G B 0D 0A` | 8B | 设置灯带 (旧格式→默认灯带1) |
| `07` | `55 AA 07 <strip_id> 0D 0A` | 6B | 查询灯带状态 |
| `05` | `55 AA 05 H L 0D 0A` | 7B | 风扇转速 (大端) |

### 解析机制
- 累积缓冲区 `lcd_buf[128]` + `lcd_buf_len`
- `lcd_recv()` 每10ms将 UART4 RX 追加到累积缓冲
- `parse_lcd_buf()` 滑动窗口搜索 `55 AA` 帧头，验证 `0D 0A` 帧尾
- 处理完后 `memmove` 移除已处理数据

### 屏幕脚本关键函数
- `printh XX` — 发送单字节十六进制
- `prints h0.val,1` — 发送控件值 (1字节)
- 屏幕脚本**没有变量**，用隐藏控件 `h_cur` 充当变量存储灯带ID
- 控件可互相读取：`n0.val=h0.val`, `prints h_cur.val,1`

---

## 2. ESP32 / OneNET 云平台 (UART2, 115200)

### AT 指令通信
- ESP32 跑标准 AT 固件
- `at_cmd_busy` 锁防护：同一时刻只能有一条 AT 指令
- 超时保护 `AT_CMD_TIMEOUT_MS = 5000ms`，超时自动复位
- 非阻塞初始化状态机 (`esp32_init_nonblock`)：ATE0 → AT+RST → CWMODE → CWJAP → MQTT → SUB

### OneNET 协议
- 服务器: `mqtts.heclouds.com:1883`
- 上报主题: `$sys/{PID}/{DEV}/thing/property/post`
- 订阅: `post/reply` + `property/set`
- JSON 格式: `{"id":"123","version":"1.0","params":{"temp":{"value":25.5}}}`
- 上报分 9 个 case 轮询（1条/秒），含 4 条灯带的 RGB 合并上报

### 云端下发
- `property/set` → `parse_onenet_params()` 批量解析
- 遍历 `MAX_STRIPS` 逐条检查 `RGB<n>_RAD/GREEN/BLUE` 字段
- 只更新下发的通道，其余保持当前值
- 通过 `queue_set_reply()` 排队回复

---

## 3. 语音模块 ASRPRO (UART3, 9600)

### 协议格式
```
CATEGORY:TARGET:ACTION:VALUE\r\n
```
- 新格式: `LIGHT:1:ON`, `LIGHT:2:COLOR:WARM`, `LIGHT:ALL:OFF`
- 兼容旧格式: `LIGHT:ON` (默认灯带1) → tgt字段被识别为action自动回退
- 风扇: `FAN:ON/OFF/SPEED:UP/DOWN/1~4`
- 查询: `QUERY:TEMP/HUMI/PM25/LIGHT/ALL`

### STM32端解析
```c
cat = strtok(buf, ":\r\n");   // LIGHT / FAN / QUERY...
tgt = strtok(NULL, ":\r\n");  // 1~4 / ALL (或兼容旧格式的action)
act = strtok(NULL, ":\r\n");  // ON / OFF / COLOR / MODE
val = strtok(NULL, ":\r\n");  // WARM / WHITE / RED...
```
- `strip_id=255` → 全部灯带，遍历 FOR_EACH_STRIP 宏
- 查询回应通过 `PLAYS:id,...` 命令触发语音播报

### ASRPRO 固件 (asrpro_code.cpp)
- 语音 ID → `Serial1.println("LIGHT:1:ON")` 映射
- 位于 case 1~38 的 switch 语句中
