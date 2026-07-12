# HMI 串口屏修改说明（page1 - RGB控制页）

## 需要做的事

在 USART HMI 工具中打开工程，进入 page1（RGB控制页），按以下步骤修改：

---

## 1. 删除控件

删除灯带2的第二组独立控件（包括它的 h 滑块、n 数值、b 按钮等全部控件）。

---

## 2. 新增控件

### 2.1 灯带选择按钮（4个）

| 控件名 | 类型 | 用途 |
|--------|------|------|
| `b_led1` | 按钮 | 选择灯带1 |
| `b_led2` | 按钮 | 选择灯带2 |
| `b_led3` | 按钮 | 选择灯带3 |
| `b_led4` | 按钮 | 选择灯带4 |

每个按钮的**弹起事件**脚本（以 b_led1 为例，2/3/4 只改数字）：

```
h_cur.val=1
printh 55 AA 07
printh 01
printh 0D 0A
```

### 2.2 隐藏滑块（存储当前灯带ID）

| 控件名 | 类型 | 属性 |
|--------|------|------|
| `h_cur` | 滑块 | val 范围 1~4，`visible`=false 或放到屏幕外 |

> **关键技巧**：h_cur 充当"变量"存储当前选中的灯带ID（1~4）。所有RGB控件的脚本通过 `prints h_cur.val,1` 读取它。

---

## 3. 修改现有控件脚本

### 3.1 RGB滑块 h0（红）

原来的滑动事件和弹起事件都需要各加一行 `prints h_cur.val,1`：

```
// 滑动事件 & 弹起事件（两者相同）
if(h0.val>0) { b13.txt="开" } else { b13.txt="关" }
n0.val=h0.val
n1.val=h1.val
n2.val=h2.val
printh 55 AA 04
prints h_cur.val,1      // ← 新增：发送当前灯带ID
prints h0.val,1
prints h1.val,1
prints h2.val,1
printh 0D 0A
```

h1（绿）和 h2（蓝）同理——只需复制相同的脚本（它们的 `prints h0.val,1` 等不变因为已经是读自己的值）。

### 3.2 开关按钮 b13

```
// 弹起事件
if(b13.txt=="开") {
  b13.txt="关"
  h0.val=0; h1.val=0; h2.val=0
  n0.val=0; n1.val=0; n2.val=0
  printh 55 AA 04
  prints h_cur.val,1   // ← 新增
  printh 00 00 00
  printh 0D 0A
} else {
  b13.txt="开"
  h0.val=128; h1.val=128; h2.val=128
  n0.val=128; n1.val=128; n2.val=128
  printh 55 AA 04
  prints h_cur.val,1   // ← 新增
  printh 80 80 80
  printh 0D 0A
}
```

### 3.3 预设按钮 b3（白光 255,255,255）

```
// 弹起事件
b13.txt="开"
h0.val=255; h1.val=255; h2.val=255
n0.val=255; n1.val=255; n2.val=255
printh 55 AA 04
prints h_cur.val,1     // ← 新增
prints h0.val,1
prints h1.val,1
prints h2.val,1
printh 0D 0A
```

b4~b6 同理，只改 RGB 数值，同样加 `prints h_cur.val,1`。

---

## 4. 协议变化说明

| 旧协议 | 新协议 |
|--------|--------|
| `55 AA 04 R G B 0D 0A` (8字节, 灯带1) | `55 AA 04 ID R G B 0D 0A` (9字节) |
| `55 AA 06 R G B 0D 0A` (8字节, 灯带2) | **废弃**，统一用04+ID=2 替代 |
| 无 | `55 AA 07 ID 0D 0A` (6字节, 查询状态) |

- STM32 兼容 8 字节旧格式（自动默认为灯带1）
- 新格式在 RGB 前多了1字节的 strip_id

---

## 5. 测试步骤

1. 将修改后的 HMI 工程下载到串口屏
2. STM32 上电，进入 page1
3. **按 b_led1** → 滑块应跳变为灯带1当前颜色，b13 显示"开"或"关"
4. **按 b_led2** → 滑块应跳变为灯带2当前颜色
5. **拖动 h0** → 当前选中的灯带颜色应实时变化
6. **按 b3（白光预设）** → 当前选中的灯带变白
7. **按 b13** → 切换当前灯带的开关状态
8. 调试串口（UART1 115200）应输出 `[LCD] ->HMI strip1: R=...` 格式的日志
