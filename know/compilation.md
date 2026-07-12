# 编译注意事项 (Keil MDK)

## 编译器
- **工具链**: Keil MDK ARMCC V5.06 update 7
- **路径**: `E:\keil\ARM\ARMCC\Bin`

## 常见问题

### 1. UTF-8 中文字符串编译错误
**现象**: `error: #8: missing closing quote` 或 `warning: #870-D: invalid multibyte character sequence`

**原因**: ARMCC V5 不完全支持 UTF-8 编码的源文件。中文 Windows 环境默认 GB2312 编码，UTF-8 字符串字面量中的中文字符被错误解析。

**解决方案**:
- 调试日志用英文替代（UART1 输出）
- 必须发往串口屏的中文字符用 UTF-8 hex 转义序列：
  ```c
  // "开" = \xE5\xBC\x80, "关" = \xE5\x85\xB3
  uart_printf(&huart4, "b13.txt=\"%s\"",
              on ? "\xE5\xBC\x80" : "\xE5\x85\xB3");
  ```
- 注释中的中文通常 OK（不在字符串字面量内）

**已验证的 UTF-8 hex 序列**:
| 字符 | UTF-8 字节 |
|------|-----------|
| 开 | E5 BC 80 |
| 关 | E5 85 B3 |

### 2. 未使用变量警告 (`#550-D`)
`variable was set but never used` — 删除未使用的 `static` 变量，或确认它们确实被需要。

### 3. CubeMX 重新生成后的 DMA 方向
CubeMX 默认分配 DMA 方向可能是 `PERIPH_TO_MEMORY`。WS2812 需要 `MEMORY_TO_PERIPH`（内存 → TIM CCR 寄存器）。每次 CubeMX 重新生成后需检查 `tim.c` 中的 `hdma_tim4_ch*.Init.Direction`。

### 4. TIM4 共用问题
TIM4 的 4 个通道共享同一个定时器。发送 WS2812 数据时：
- **不能**调用 `HAL_TIM_PWM_Stop/Start` — 会影响其他通道
- 使用 `CLEAR_BIT/SET_BIT(TIM4->CCER, TIM_CCER_CCxE)` 仅开关单个通道
- 使用 `TIM_DIER_UDE`（更新DMA）或 `TIM_DIER_CCxDE`（比较DMA）启停 DMA，不停止定时器

## 编译目标
- 输出文件: `MDK-ARM/smart_home/smart_home.hex` (flash 烧录)
- 输出文件: `MDK-ARM/smart_home/smart_home.axf` (调试)
