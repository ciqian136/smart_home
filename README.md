# STM32 Smart Home Project

这是一个基于 STM32F103 的智能家居综合控制项目，当前仓库为最终测试完成版本。项目集成环境传感器、灯带、风扇、USART HMI 屏幕、ASRPRO 语音模块、ESP32 OneNET 联网模块和 OpenART 人脸识别模块。

## 当前状态

- 当前版本已完成主要功能测试，不再作为继续开发版本维护。
- 当前仓库内容是最终源码基准，旧的新版本工程仅作为历史参考，不再合并。
- ASRPRO 和 OpenART 代码作为辅助模块代码保存在仓库根目录，分别需要在对应模块环境中单独烧录或部署。
- 本项目文档不包含也不引用共创 PCB 文件。

## 注意

- ASRPRO 辅助代码为仓库根目录的 `asrpro_code.cpp`，不参与 STM32 CMake 编译。
- OpenART 辅助代码为仓库根目录的 `openart.py`，需要放入视觉模块 SD 卡对应位置。
- 烟雾传感器 AO 当前按实际接线使用 PA1 / ADC1_IN1，PA2 是 ESP32 的 USART2_TX。

## 功能总览

- 环境采集：DHT11 温湿度、BH1750 光照、MQ-2 烟雾 ADC、PM2.5 传感器 ADC。
- 本地控制：两路 WS2812 灯带、风扇 PWM、板载 LED。
- HMI 显示与控制：USART HMI 屏显示传感器数据，并支持 RGB 和风扇控制。
- 语音控制：ASRPRO 识别语音命令，通过 UART3 控制灯光、风扇、LED 和环境查询播报。
- 云端通信：ESP32 AT 指令连接 OneNET，分时上传属性并处理云端下发控制。
- 人脸识别：OpenART 识别 `zeng` 后通过 UART5 通知 STM32，再触发语音播报。

## 目录说明

- `APP/`：用户应用层代码，包括传感器、UART 队列、语音、LCD、ESP32、人脸识别等模块。
- `Core/`：STM32CubeMX 生成的主程序、外设初始化和中断文件。
- `Drivers/`：STM32 HAL 与 CMSIS 驱动库。
- `MDK-ARM/`：Keil 工程文件。
- `cmake/`、`CMakeLists.txt`、`CMakePresets.json`：VS Code / STM32CubeCLT CMake 构建配置。
- `asrpro_code.cpp`：ASRPRO 语音模块辅助代码。
- `openart.py`：OpenART 人脸识别模块 SD 卡脚本。
- `smart_home.ioc`：STM32CubeMX 工程配置。
- `docs/`：硬件连接、通信协议和已知问题说明。

## 开发环境

推荐环境：

- Visual Studio Code
- STM32CubeCLT / CMake / Ninja
- STM32CubeMX，用于查看或重新生成 `.ioc` 外设配置
- Keil MDK，可选，用于打开 `MDK-ARM/smart_home.uvprojx`
- 天问 Block，用于 ASRPRO 语音模块工程
- USART HMI，用于编辑 `test.HMI` 屏幕工程
- OpenART / 人脸识别模块配套工具，用于部署 `openart.py`

## 构建

推荐使用 VS Code + STM32CubeCLT。命令行构建：

```powershell
cmake --build --preset Debug
```

如果 CMake 缓存路径冲突，重新配置并构建：

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

USART1 是下载和调试串口，业务输出应保持由调试宏控制。

## 串口分配

- USART1：下载 / 调试日志。
- USART2：ESP32 AT 指令通信。
- USART3：ASRPRO 语音模块通信。
- UART4：USART HMI 屏幕通信，当前波特率为 9600。
- UART5：OpenART 人脸识别模块通信。

UART 接收和发送均通过 `APP/my_uart.c` 的环形队列封装，发送使用中断方式。常用发送接口仍为 `uart_printf()`。

## 辅助模块使用要点

- ASRPRO：识别后通过 `LIGHT:*`、`LIGHT2:*`、`FAN:*`、`LED:*`、`QUERY:*` 等文本命令控制 STM32；STM32 通过 `PLAY:` / `PLAYS:` 触发语音播报。
- USART HMI：屏幕通过 UART4 发送 `55 AA ... 0D 0A` 二进制控制帧；STM32 通过控件命令加 `FF FF FF` 回写页面数据。
- OpenART：当前只识别 `zeng`，通过 UART5 输出 `FACE:ZENG,score`、`FACE:NONE` 或 `FACE:ERR,...`。
- OneNET：ESP32 通过 AT 指令分时上传属性，`MQ2` 和 `PM25` 当前为 ADC 原始值，建议配置为整数 `0~4095`、步长 `1`。

## 单模块测试方法

项目不引入额外测试框架。需要单独测试模块时，在 `APP/schedule.c` 中手动注释其他任务和初始化，只保留目标模块及必要依赖。测试完成后恢复全功能任务表。

## 相关文档

- `docs/硬件.md`：当前硬件连接、外设分配和注意事项。
- `docs/通信.md`：USART / UART 通信格式、示例帧和 OneNET 属性说明。
- `docs/问题.md`：已知问题、限制和后续排查建议。
