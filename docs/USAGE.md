# 使用说明

## 开发环境

推荐环境：

- Visual Studio Code
- STM32CubeCLT / CMake / Ninja
- STM32CubeMX，用于查看或重新生成 `.ioc` 外设配置
- Keil MDK，可选，用于打开 `MDK-ARM/smart_home.uvprojx`
- 天问 Block，用于 ASRPRO 语音模块工程
- USART HMI，用于编辑 `test.HMI` 屏幕工程
- OpenART / 人脸识别模块配套工具，用于部署 `openart.py`

## 编译 STM32 工程

在仓库根目录执行：

```powershell
cmake --build --preset Debug
```

如果出现 CMakeCache 路径不一致，例如工程目录移动后仍引用旧路径，执行：

```powershell
cmake --fresh --preset Debug
cmake --build --preset Debug
```

生成文件位于 `build/Debug/`，常用输出包括 `smart_home.elf`、`smart_home.hex`、`smart_home.bin`。

## 烧录与运行

1. 编译 STM32 工程并烧录 `build/Debug/smart_home.hex` 或 `smart_home.bin`。
2. 单独将 `asrpro_code.cpp` 按天问 Block / ASRPRO 流程烧录到语音模块。
3. 将 `openart.py` 放入 OpenART 视觉模块 SD 卡对应位置，并确认模型路径存在。
4. 使用 USART HMI 工具打开归档中的 `lcd/test.HMI`，下载到屏幕。
5. 上电后等待 ESP32 非阻塞初始化完成，OneNET 属性会按分时方式上传。

## 调试开关

各模块保留独立调试宏，正常使用默认关闭。需要单独测试时，将对应文件中的宏从 `0` 改为 `1`，测试完成后恢复为 `0`。

常见调试宏：

- `APP/smoke.c`：`SMOKE_DEBUG`
- `APP/PM25.c`：`PM25_DEBUG`
- `APP/BH1750.c`：`BH1750_DEBUG`
- `APP/dht11.c`：`DHT11_DEBUG`
- `APP/lcd.c`：`LCD_DEBUG`
- `APP/voice.c`：`VOICE_DEBUG`
- `APP/esp32.c`：`ESP32_DEBUG`
- `APP/face.c`：`FACE_DEBUG`
- `APP/fan.c`：`FAN_DEBUG`
- `APP/my_uart.c`：`UART_DEBUG`

UART1 是下载和调试串口，业务输出应保持由调试宏控制。

## 串口分配

- USART1：下载 / 调试日志。
- USART2：ESP32 AT 指令通信。
- USART3：ASRPRO 语音模块通信。
- UART4：USART HMI 屏幕通信，当前波特率为 9600。
- UART5：OpenART 人脸识别模块通信。

UART 接收和发送均通过 `APP/my_uart.c` 的环形队列封装，发送使用中断方式，减少阻塞和丢包风险。常用发送接口仍为 `uart_printf()`。

各串口的完整帧格式、文本协议和 OneNET 属性格式见 `docs/PROTOCOLS.md`。

## ASRPRO 使用

ASRPRO 识别命令后通过 Serial1 输出文本协议到 STM32：

- `LIGHT:ON` / `LIGHT:OFF`
- `LIGHT2:ON` / `LIGHT2:OFF`
- `FAN:ON` / `FAN:OFF` / `FAN:SPEED:x`
- `LED:ON` / `LED:OFF` / `LED:TOGGLE`
- `QUERY:TEMP` / `QUERY:HUMI` / `QUERY:DUST` / `QUERY:SMOKE` / `QUERY:LIGHT` / `QUERY:ALL`

STM32 回传语音播报协议：

- `PLAY:id`：播放单条语音片段。
- `PLAYS:id,id,...`：按顺序播放多条语音片段。
- `VOICE:BUSY` / `VOICE:IDLE`：ASRPRO 回传播放忙闲状态，STM32 用于避免人脸、报警和命令播报冲突。

当前 ASRPRO 等待窗口策略：

- 唤醒词后等待 30 秒。
- 查询类命令等待 30 秒，避免查询播报被退出提示打断。
- 灯光、风扇、LED 等控制命令执行后不主动打开等待窗口。
- STM32 外部人脸/报警 `PLAY/PLAYS` 不主动打开 ASRPRO 唤醒窗口。

## USART HMI 使用

使用 `lcd/test.HMI` 作为当前屏幕工程。HMI 与 STM32 之间使用 UART4：

- STM32 向 HMI 写文本控件：`t0.txt="25.5"` 后跟 `FF FF FF`。
- HMI 向 STM32 发 RGB 控制帧：`55 AA 04 ID R G B 0D 0A`。
- HMI 向 STM32 发风扇控制帧：`55 AA 05 H L 0D 0A`，代码兼容大小端速度值。
- HMI 查询 RGB / 风扇状态后，STM32 会回写控件值，避免页面状态不同步。

## OpenART 使用

`openart.py` 当前只识别 `zeng`，识别成功后通过 UART5 输出：

- `FACE:ZENG,score`
- `FACE:NONE`
- `FACE:ERR,MODEL`

STM32 侧连续确认后触发 `voice_play_face_zeng()`，由语音模块排队播报。没有固定 10 秒冷却，离开画面后再次进入可以再次触发。

## OneNET 使用

ESP32 通过 AT 指令连接 OneNET，分时上传属性。当前上传属性包括：

- `test_int`
- `MQ2`
- `PM25`
- `humi`
- `temp`
- `light`
- `fan`
- `RGB1_RAD`、`RGB1_GREEN`、`RGB1_BLUE`
- `RGB2_RAD`、`RGB2_GREEN`、`RGB2_BLUE`
- `LED`

其中 `MQ2` 和 `PM25` 当前上传 ADC 原始值，建议 OneNET 产品模型配置为整数，范围 `0~4095`，步长 `1`。

## 单模块测试方法

项目不引入额外测试框架。需要单独测试模块时，在 `APP/schedule.c` 中手动注释其他任务和初始化，只保留目标模块及必要依赖。测试完成后恢复全功能任务表。
