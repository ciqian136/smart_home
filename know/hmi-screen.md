# HMI 串口屏设计

## 屏幕信息
- **型号**: USART HMI 320×240 串口屏
- **工具**: USART HMI 官方软件，工程文件 `.HMI`
- **连接**: STM32 UART4 (PC10/PC11), 9600bps
- **脚本能力**: 只支持基本事件脚本，**不支持变量/数组**

## 页面结构

| 页面 | 名称 | 用途 |
|------|------|------|
| page0 | 数据显示 | 温湿度、PM2.5、烟雾、光照、灯带状态 |
| page1 | RGB控制 | 室内灯/室外灯 RGB 滑块 + 预设 + 开关 |
| page2 | 风扇控制 | 风扇转速滑块 |

## page1 (RGB控制) 控件清单

### 共用RGB控件
| 控件 | 类型 | 用途 |
|------|------|------|
| `h0` | 滑块 0-255 | 红色分量 |
| `h1` | 滑块 0-255 | 绿色分量 |
| `h2` | 滑块 0-255 | 蓝色分量 |
| `n0` | 数字 | 显示R值 |
| `n1` | 数字 | 显示G值 |
| `n2` | 数字 | 显示B值 |
| `b13` | 按钮/文本 | 开关切换 "开"/"关" |
| `b3` | 按钮 | 预设白光 255,255,255 |
| `b4` | 按钮 | 预设颜色2 |
| `b5` | 按钮 | 预设颜色3 |
| `b6` | 按钮 | 预设颜色4 |

### 灯带选择控件
| 控件 | 类型 | 用途 |
|------|------|------|
| `b_led1` | 按钮 | 选择室内灯，灯带ID=1，48 LED |
| `b_led3` | 按钮 | 选择室外灯，灯带ID=3，192 LED |
| `h_cur` | 滑块 (隐藏) | 存储当前灯带ID，仅允许 1 或 3 |

> `b_led2`/入户灯通道保留但不显示、不发送控制；灯带4不可用，因为 TIM4_CH4/PD15 已分配给风扇 PWM。

## 关键脚本模式

### 隐藏滑块技巧
**问题**: HMI 脚本没有变量，无法在各控件间共享"当前选中的灯带ID"。

**方案**: 用一个隐藏滑块 `h_cur` (visible=false) 存储灯带ID：
- 灯带选择按钮只设置 `h_cur.val=1` 或 `h_cur.val=3`
- RGB控件通过 `prints h_cur.val,1` 读取当前ID

### 室内灯选择按钮 b_led1 (弹起事件)
```
h_cur.val=1
printh 55 AA 07
printh 01
printh 0D 0A
```

### 室外灯选择按钮 b_led3 (弹起事件)
```
h_cur.val=3
printh 55 AA 07
printh 03
printh 0D 0A
```

### RGB滑块 (滑动+弹起事件)
```
if(h0.val>0) { b13.txt="开" } else { b13.txt="关" }
n0.val=h0.val
n1.val=h1.val
n2.val=h2.val
printh 55 AA 04
prints h_cur.val,1      // strip_id
prints h0.val,1         // R
prints h1.val,1         // G
prints h2.val,1         // B
printh 0D 0A
```

### 开关按钮 (弹起事件)
```
if(b13.txt=="开") {
  b13.txt="关"
  h0.val=0; h1.val=0; h2.val=0
  n0.val=0; n1.val=0; n2.val=0
  printh 55 AA 04
  prints h_cur.val,1
  printh 00 00 00
  printh 0D 0A
} else {
  b13.txt="开"
  h0.val=128; h1.val=128; h2.val=128
  n0.val=128; n1.val=128; n2.val=128
  printh 55 AA 04
  prints h_cur.val,1
  printh 80 80 80
  printh 0D 0A
}
```

### 预设按钮 (弹起事件, b3 白光示例)
```
b13.txt="开"
h0.val=255; h1.val=255; h2.val=255
n0.val=255; n1.val=255; n2.val=255
printh 55 AA 04
prints h_cur.val,1
prints h0.val,1
prints h1.val,1
prints h2.val,1
printh 0D 0A
```

## STM32 反馈机制

STM32 可通过 UART 直接设置 HMI 控件值：
```
h0.val=128\xFF\xFF\xFF     → 设置滑块位置
b13.txt="开"\xFF\xFF\xFF   → 设置按钮文字
```

**注意**: 不确定 HMI 接收外部设置值时是否会触发控件事件。防抖机制 (`ws2812_strip_set_all` 同值跳过) 已作为保护。

## page0 数据显示控件

| 控件 | 数据 |
|------|------|
| `t0` | 温度 °C |
| `t1` | 湿度 % |
| `t2` | PM2.5 µg/m³ |
| `t12` | 烟雾 ppm |
| `t13` | 光照 lux |
| `t14` | 灯带状态 "true"/"false" |
