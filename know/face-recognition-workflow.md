# OpenART 人脸识别训练与联调流程

## 当前目标
- 第一版只识别用户本人，模型类别名统一为 `zeng`。
- OpenART 负责拍照、训练后的模型推理和串口发送结果；STM32 只解析 `FACE:*` 状态帧。
- STM32 不直接在 `face.c` 中控制继电器或舵机，只提供 `face_is_zeng_detected()` 等状态接口。

## 1. 采集图片
- SD 卡根目录保留厂家要求的 `cmm_load.py`、`cmm_cfg.csv`，并创建 `duijiangji` 文件夹。
- 在 OpenMV IDE 中运行 `C:\Users\USER\Downloads\人脸识别模块\人脸识别模块\模型拍照代码\save image.py`。
- 图片会保存到 OpenART 的 `/sd/duijiangji`，对应电脑读卡器路径通常是 `D:\duijiangji`。
- 采集脚本会扫描已有图片编号并从最大编号继续保存，不会每次从 1 覆盖。
- 文件名保持英文、数字、下划线，例如 `zeng_00001.jpg`。
- 当前 OpenART 固件中，`cmm_load.py` 能成功打开 `/sd/cmm_cfg.csv`，但 `os.listdir("/sd")` 可能报错；采图脚本应通过打开 `/sd/cmm_cfg.csv` 检查 SD，而不是枚举 `/sd` 根目录。
- 采图时可暂时将 SD 根目录的 `main.py` 改名为 `main_face.py`，防止上电自动运行人脸推理脚本；不得改名或删除厂家要求的 `cmm_load.py`、`cmm_cfg.csv`。
- OpenART 某些固件中对已存在目录执行 `os.mkdir("/sd/duijiangji")` 可能返回 `-1`；采图脚本应通过在目录内创建临时文件验证可写性，不能把该返回值直接当作 SD 未挂载。
- OpenART V4.3 对 `os.listdir("/sd/duijiangji")` 可能返回 `ENODEV`；采图脚本不再扫描目录，而是按 `zeng_00001.jpg`、`zeng_00002.jpg` 顺序检查文件是否存在，避免覆盖旧图片。

## 2. 采集数量建议
- 35 张只能验证流程，不适合稳定识别。
- 第一轮建议至少 150 张，能跑通并观察问题。
- 可用版本建议 300~500 张，其中验证集至少 30~50 张。
- 图片要覆盖正脸、左侧脸、右侧脸、低头、抬头、近距离、中距离、远距离、强光、弱光、背光、不同背景。
- 删除明显模糊、半张脸、重复度很高、脸太小的图片。

## 3. 标注 VOC 数据集
- 将 `D:\duijiangji\*.jpg` 复制到 `C:\Users\USER\Desktop\object_file\yolo3_smartcar\zeng\JPEGImages`。
- 使用厂家 LabelImg/Auto Label 工具导入图片并标注人脸框。
- 类别名必须写 `zeng`，不要混用 `owner`、中文名或其他拼写。
- 导出 VOC 后确认目录结构：`zeng\JPEGImages\*.jpg` 和 `zeng\Annotations\*.xml`。
- 图片和 XML 必须同名，例如 `zeng_00123.jpg` 对应 `zeng_00123.xml`。

## 4. 训练配置
- 配置文件：`C:\Users\USER\Desktop\object_file\yolo3_smartcar\config.cfg`。
- `voc_folder=C:\Users\USER\Desktop\object_file\yolo3_smartcar\zeng`。
- `num_classes=1`，`class_names=zeng`。
- `batch_size` 默认可用 32；如果训练显存/内存报错，改为 16 或 8。
- `total_epochs` 默认 150；300 张以上可先用 150，不稳定再补数据重训。

## 5. 训练命令
- 使用 conda 环境 `yolo3_car`，不要用系统 Python。
- 推荐在 Anaconda Prompt 中执行：
```bat
conda activate yolo3_car
cd /d C:\Users\USER\Desktop\object_file\yolo3_smartcar
python voc_convertor.py
python kmeans.py
python train.py
python detect.py -model yolo3_iou_smartcar_final.tflite -image test.jpg
```
- 如果在普通 PowerShell 中 conda 不在 PATH，可用：
```powershell
& C:\Users\USER\anaconda3\Scripts\conda.exe run -n yolo3_car python .\voc_convertor.py
& C:\Users\USER\anaconda3\Scripts\conda.exe run -n yolo3_car python .\kmeans.py
& C:\Users\USER\anaconda3\Scripts\conda.exe run -n yolo3_car python .\train.py
```

## 6. 部署到 OpenART
- 训练完成后复制 `yolo3_iou_smartcar_final_with_post_processing.tflite` 到 SD 卡根目录。
- SD 卡根目录应至少包含：`main.py`、`cmm_load.py`、`cmm_cfg.csv`、`yolo3_iou_smartcar_final_with_post_processing.tflite`、`duijiangji/`。
- OpenART 推理脚本路径：`C:\Users\USER\Downloads\人脸识别模块\人脸识别模块\人脸识别代码\视觉模块.txt`，部署到 SD 根目录时命名为 `main.py`。
- OpenART 串口输出协议为 `FACE:ZENG,<score>`、`FACE:NONE`、`FACE:ERR,MODEL`。

## 7. STM32 联调
- 接线：OpenART TX 接 STM32 `PD2/UART5_RX`，OpenART RX 接 STM32 `PC12/UART5_TX`，两板 GND 共地。
- 串口参数：115200, 8N1。
- STM32 调试口 USART1 仍为 PA9/PA10, 115200。
- 上电后应看到 `[FACE] init UART5 115200`。
- 连续收到 3 次 `FACE:ZENG` 且分数 >= 70 后，`face_is_zeng_detected()` 返回 1。
- 连续 3 次 `FACE:NONE` 或超过 2000ms 未收到有效帧后，识别状态清零。
- 人脸模块只产生状态和事件，不直接控制执行器；确认识别后由 `automation.c` 按当前温度调节风扇、按当前光照恢复灯带状态。
- 确认识别后由 `voice.c` 发送动态欢迎播报：欢迎回家曾先生、当前温度/湿度、风扇已调节、当前光照、灯光已调节。
- 欢迎播报使用 30 秒冷却，避免人脸短时间离开/重入时重复打断其他语音交互。
- ASRPRO 外部播报路径对 `PLAY/PLAYS` 增加 45 秒唤醒保活，避免欢迎语播到一半进入休眠，剩余语音等下次唤醒才继续播。
- 如果温度、湿度或光照还没有有效数据，播报“环境数据正在更新”，不播报默认值或过期值。

## 8. 调参方法
- OpenART 画面太亮/发白：先改采集脚本和推理脚本中的 `CAMERA_EXPOSURE_US`，默认 3500；仍过曝就依次试 2500、1500、1000，同时保持 `CAMERA_BRIGHTNESS=-2`、关闭自动曝光和自动增益。
- 如果连接 OpenART 但还没手动运行脚本就已经发白，先运行 `模型拍照代码\exposure test.py` 找曝光值；它会循环 800~6000us 并在画面/串口打印亮度。选脸部五官清楚且 `L max` 不长期贴近 255 的曝光值。
- 训练图片和实际推理必须使用接近的曝光配置，否则训练时正常、部署时仍可能识别差。
- 误识别别人：提高 OpenART `DETECT_THRESHOLD` 到 0.75~0.85，或提高 STM32 `FACE_SCORE_THRESHOLD`。
- 识别不稳定：补拍失败角度和当前光照环境，优先增加数据，不优先降阈值。
- 反应太慢：把 STM32 确认次数从 3 降到 2，但误触发风险会增加。
- 需要更安全地区分陌生人时，不应只靠单类别 `zeng`；应增加“陌生人/其他人”策略或额外确认动作。
