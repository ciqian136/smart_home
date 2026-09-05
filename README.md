# STM32 Smart Home Project

这是一个基于 STM32F103 的智能家居综合控制项目，当前仓库为最终测试完成版本。项目集成环境传感器、灯带、风扇、USART HMI 屏幕、ASRPRO 语音模块、ESP32 OneNET 联网模块和 OpenART 人脸识别模块。

## 当前状态

- 当前版本已完成主要功能测试，不再作为继续开发版本维护。
- 当前仓库内容是最终源码基准，旧的新版本工程仅作为历史参考，不再合并。
- ASRPRO 和 OpenART 代码作为辅助模块代码保存在仓库根目录，分别需要在对应模块环境中单独烧录或部署。

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
- `docs/`：项目使用、硬件连接和已知问题说明。

## 构建

推荐使用 VS Code + STM32CubeCLT。命令行构建：

```powershell
cmake --build --preset Debug
```

如果 CMake 缓存路径冲突，重新配置：

```powershell
cmake --fresh --preset Debug
```

## 相关文档

- `docs/USAGE.md`：烧录、运行、调试和辅助模块使用说明。
- `docs/HARDWARE.md`：当前硬件连接、外设分配和注意事项。
- `docs/PROTOCOLS.md`：USART / UART 通信格式、帧结构和 OneNET 属性说明。
- `docs/KNOWN_ISSUES.md`：已知问题、限制和后续排查建议。
