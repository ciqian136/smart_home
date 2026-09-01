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

- 当前用户可操作灯带只保留 `1=室内灯` 和 `3=室外灯`；`2=原入户灯` 仅物理注册并在启动时强制关闭，不在 LCD/语音/云端暴露；`TIM4_CH4/PD15` 已用于风扇 PWM，不再暴露灯带4。

### 解析机制
- 累积缓冲区 `lcd_buf[128]` + `lcd_buf_len`
- `lcd_recv()` 每10ms将 UART4 RX 追加到累积缓冲
- `parse_lcd_buf()` 滑动窗口搜索 `55 AA` 帧头，验证 `0D 0A` 帧尾
- 处理完后 `memmove` 移除已处理数据

### 屏幕脚本关键函数
- `printh XX` — 发送单字节十六进制
- `prints h0.val,1` — 发送控件值 (1字节)
- 屏幕脚本**没有变量**，用隐藏控件 `h_cur` 充当变量存储灯带ID，仅允许写入 1 或 3
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
- 上报分 case 轮询（约1条/秒），含 3 路灯带 RGB、风扇、自动模式、空气质量告警等状态

### 云端下发
- `property/set` → `parse_onenet_params()` 批量解析
- 遍历已启用灯带，逐条检查 `RGB<n>_RAD/GREEN/BLUE` 字段；`RGB2_*` 下发会被识别为保留通道并忽略
- 灯带预设支持两类下发方式：
  - 独立字段：`RGB1_PRESET`、`RGB3_PRESET`、`RGBALL_PRESET`
  - 通用字段：`light_target` + `light_preset` 或 `light_preset_name`
- 预设 ID：`0=OFF`, `1=ON`, `2=WARM`, `3=WHITE`, `4=NIGHT`, `5=READ`, `6=RED`, `7=GREEN`, `8=BLUE`, `99=CUSTOM(仅上报)`
- 设备回传 `RGB1_PRESET_STATE`、`RGB3_PRESET_STATE`；当前 RGB 不匹配固定预设时返回 `99`。
- 只更新下发的通道，其余保持当前值
- 通过 `queue_set_reply()` 排队回复

---

## 3. 语音模块 ASRPRO (UART3, 9600)

### 协议格式
```
CATEGORY:TARGET:ACTION:VALUE\r\n
```
- 新格式: `LIGHT:1:ON`, `LIGHT:3:COLOR:WARM`, `LIGHT:ALL:OFF`
- 兼容旧格式: `LIGHT:ON` (默认灯带1) → tgt字段被识别为action自动回退
- 风扇: `FAN:ON/OFF/SPEED:UP/DOWN/1~4`
- 自动/手动模式: `AUTO:ON`, `AUTO:OFF`, `FAN:AUTO`, `FAN:MANUAL`, `LIGHT:AUTO`, `LIGHT:MANUAL`
- 查询: `QUERY:TEMP/HUMI/PM25/LIGHT/SMOKE/ALL`

### STM32端解析
```c
cat = strtok(buf, ":\r\n");   // LIGHT / FAN / QUERY...
tgt = strtok(NULL, ":\r\n");  // 1 / 3 / ALL (或兼容旧格式的action)
act = strtok(NULL, ":\r\n");  // ON / OFF / COLOR / MODE
val = strtok(NULL, ":\r\n");  // WARM / WHITE / RED...
```
- `strip_id=255` → 全部灯带，遍历 FOR_EACH_STRIP 宏
- 查询回应通过 `PLAYS:id,...` 命令触发语音播报
- 语音/LCD/云端手动控制灯光或风扇后会进入 10 分钟手动覆盖；自动模式命令会清除覆盖并恢复自动策略。

### STM32 → ASRPRO 播报
- 单条播报: `PLAY:<id>\r\n`
- 多条顺序播报: `PLAYS:<id>,<id>,...\r\n`
- 人脸识别确认后，STM32 会发送动态 `PLAYS` 序列：`10003` 欢迎回家、`118/119/121` 拼接温湿度和光照、`126/127` 播报风扇和灯光已自动调节。
- 传感器数据无效时使用 `128` 播报“环境数据正在更新”，避免播报过期或未初始化数据。
- 自动化动作播报使用 `129~133`：自动开灯、自动关灯、自动开风扇、自动关风扇、自动调节风扇。
- ASRPRO 收到外部 `PLAY/PLAYS` 后会把唤醒保持时间延长到 45 秒，播放任务每约 500ms 刷新一次保活，避免人脸欢迎这类长播报中途退出唤醒态，导致剩余队列等到下次唤醒才继续播。
- 外部播报队列按 ID 类型选择 API：`10000` 及以上的 `playid` 使用 `prompt_play_by_voice_id()`，普通命令词提示音 ID 使用 `prompt_play_by_cmd_id()`；启动失败和播放完成等待都有超时，避免单个无效 ID 卡死播放任务。

### ASRPRO 固件 (asrpro_code.cpp)
- 语音 ID → `Serial1.println("LIGHT:1:ON")` 映射
- 位于 `ASR_CODE()` 的 switch 语句中；欢迎语 `playid=10003`、播放片段命令词 ID `100~135` 位于 `setup()` 顶部注释配置区
- 外部播报队列在 `app_uart()` 中入队，在 `app_play()` 中串行播放；长播报保活逻辑位于 `keep_playback_awake()`。

### 自动控制触发
- 光照 `<= light_on_lux`：自动恢复最近一次非零灯光状态。
- 光照 `>= light_off_lux`：关闭由自动化打开的灯光。
- 温度：`<=26℃` 关风扇，`26~28℃` 约 30%，`28~30℃` 约 50%，`>30℃` 约 80%，带约 1℃ 回差。
- PM2.5：达到 `pm25_limit` 后自动开风扇到约 80%，低于 `pm25_limit - 10` 后解除空气质量触发。
- MQ2：达到 `smoke_limit_ppm` 或 DO 报警后自动开风扇到 100%，低于 `smoke_limit_ppm - 50` 后解除烟雾触发。

---

## 4. OpenART 人脸识别 (UART5, 115200)

### OpenART → STM32
```
FACE:ZENG,85\r\n
FACE:NONE\r\n
FACE:ERR,MODEL\r\n
```
- `FACE:ZENG,<score>`: 检测到 `zeng` 类别，`score` 为 0~100 的置信度整数。
- `FACE:NONE`: 当前未检测到目标；STM32 连续收到 3 次后才清除主人识别状态。
- `FACE:ERR,<code>`: OpenART 模型或推理异常；STM32 立即清除主人识别状态。
- STM32 仍兼容旧的 `FACE:OWNER,<score>` 帧，但 OpenART 新版本只发送 `FACE:ZENG,<score>`。

### STM32 端状态规则
- `score >= 70` 且连续 3 次 ZENG，`face_is_zeng_detected()` 才返回 1。
- 超过 2000ms 没收到有效帧，`face_get_online()` 和 `face_is_zeng_detected()` 都返回 0。
- 后续语音、灯光、风扇等扩展只读取 `face` 模块状态，不在 UART 回调里直接执行动作。
- 连续确认识别到曾先生后，`automation.c` 负责按温度/光照调节风扇和灯光，`voice.c` 负责欢迎回家及环境播报。
