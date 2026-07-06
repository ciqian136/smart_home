#include "asr.h"
extern "C"{ void * __dso_handle = 0 ;}
#include "setup.h"

uint32_t snid;
void ASR_CODE();

//{speak:小蝶-清新女声,vol:10,speed:10,platform:haohaodada}
//{playid:10001,voice:欢迎使用语音助手，用天问五幺唤醒我。}
//{playid:10002,voice:我退下了，用天问五幺唤醒我}

// ============================================================
//  软件串口（GPIO 模拟 UART，9600bps）
//  TX: GPIO3 → 接 STM32 USART3 RX (PB11)
//  每 bit = 104µs，时序精准
// ============================================================
#define UART_TX_PIN 3
#define BIT_DELAY_US 104    // 9600 bps

static void uart_send_byte(uint8_t data)
{
    digitalWrite(UART_TX_PIN, 0);            // 起始位
    delayMicroseconds(BIT_DELAY_US);
    for (uint8_t i = 0; i < 8; i++) {        // 8 位数据 LSB first
        digitalWrite(UART_TX_PIN, (data >> i) & 0x01);
        delayMicroseconds(BIT_DELAY_US);
    }
    digitalWrite(UART_TX_PIN, 1);            // 停止位
    delayMicroseconds(BIT_DELAY_US);
}

static void uart_println(const char *str)
{
    while (*str) uart_send_byte((uint8_t)*str++);
    uart_send_byte('\r');
    uart_send_byte('\n');
}


void ASR_CODE(){
  set_state_enter_wakeup(10000);

  switch (snid) {
    // ========== 灯带控制 ==========
    case 1:   uart_println("LIGHT:ON");       break;
    case 2:   uart_println("LIGHT:OFF");      break;
    case 3:   uart_println("LIGHT:BRIGHT:UP");    break;
    case 4:   uart_println("LIGHT:BRIGHT:DOWN");  break;
    case 5:   uart_println("LIGHT:BRIGHT:50");    break;
    case 6:   uart_println("LIGHT:COLOR:WARM");   break;
    case 7:   uart_println("LIGHT:COLOR:WHITE");  break;
    case 8:   uart_println("LIGHT:COLOR:RED");    break;
    case 9:   uart_println("LIGHT:COLOR:GREEN");  break;
    case 10:  uart_println("LIGHT:COLOR:BLUE");   break;
    case 11:  uart_println("LIGHT:MODE:READ");    break;
    case 12:  uart_println("LIGHT:MODE:SLEEP");   break;
    case 13:  uart_println("LIGHT:MODE:NIGHT");   break;

    // ========== 风扇控制 ==========
    case 14:  uart_println("FAN:ON");         break;
    case 15:  uart_println("FAN:OFF");        break;
    case 16:  uart_println("FAN:SPEED:UP");   break;
    case 17:  uart_println("FAN:SPEED:DOWN"); break;
    case 18:  uart_println("FAN:SPEED:1");    break;
    case 19:  uart_println("FAN:SPEED:2");    break;
    case 20:  uart_println("FAN:SPEED:3");    break;
    case 21:  uart_println("FAN:SPEED:4");    break;

    // ========== 测试 LED ==========
    case 22:  uart_println("LED:ON");         break;
    case 23:  uart_println("LED:OFF");        break;

    // ========== 环境查询 ==========
    case 24:  uart_println("QUERY:TEMP");     break;
    case 25:  uart_println("QUERY:HUMI");     break;
    case 26:  uart_println("QUERY:PM25");     break;
    case 27:  uart_println("QUERY:LIGHT");    break;
    case 28:  uart_println("QUERY:ALL");      break;

    default: break;
  }
}

void hardware_init(){
  // 初始化软件串口 TX 引脚（GPIO3 → STM32 PB11）
  setPinFun(UART_TX_PIN, FIRST_FUNCTION);
  pinMode(UART_TX_PIN, output);
  digitalWrite(UART_TX_PIN, 1);  // 空闲态高电平

  vol_set(3);  // 音量减半（范围 1~7）
  vTaskDelete(NULL);
}

void setup()
{

  // ========== 唤醒词 ==========
  //{ID:0,keyword:"唤醒词",ASR:"天问五幺",ASRTO:"我在"}

  // ========== 灯带控制 ==========
  //{ID:1,keyword:"命令词",ASR:"打开灯光",ASRTO:"好的，马上打开灯光"}
  //{ID:2,keyword:"命令词",ASR:"关闭灯光",ASRTO:"好的，马上关闭灯光"}
  //{ID:3,keyword:"命令词",ASR:"调亮一点",ASRTO:"好的，已调亮"}
  //{ID:4,keyword:"命令词",ASR:"调暗一点",ASRTO:"好的，已调暗"}
  //{ID:5,keyword:"命令词",ASR:"亮度百分之五十",ASRTO:"好的，已设置亮度"}
  //{ID:6,keyword:"命令词",ASR:"暖光模式",ASRTO:"好的，已切换暖光"}
  //{ID:7,keyword:"命令词",ASR:"白光模式",ASRTO:"好的，已切换白光"}
  //{ID:8,keyword:"命令词",ASR:"红色灯光",ASRTO:"好的，已切换红色"}
  //{ID:9,keyword:"命令词",ASR:"绿色灯光",ASRTO:"好的，已切换绿色"}
  //{ID:10,keyword:"命令词",ASR:"蓝色灯光",ASRTO:"好的，已切换蓝色"}
  //{ID:11,keyword:"命令词",ASR:"阅读模式",ASRTO:"好的，已切换阅读模式"}
  //{ID:12,keyword:"命令词",ASR:"睡眠模式",ASRTO:"好的，晚安"}
  //{ID:13,keyword:"命令词",ASR:"夜灯模式",ASRTO:"好的，已切换夜灯模式"}

  // ========== 风扇控制 ==========
  //{ID:14,keyword:"命令词",ASR:"打开风扇",ASRTO:"好的，已打开风扇"}
  //{ID:15,keyword:"命令词",ASR:"关闭风扇",ASRTO:"好的，已关闭风扇"}
  //{ID:16,keyword:"命令词",ASR:"风速加大",ASRTO:"好的，风速已加大"}
  //{ID:17,keyword:"命令词",ASR:"风速减小",ASRTO:"好的，风速已减小"}
  //{ID:18,keyword:"命令词",ASR:"风扇一档",ASRTO:"好的，一档"}
  //{ID:19,keyword:"命令词",ASR:"风扇二档",ASRTO:"好的，二档"}
  //{ID:20,keyword:"命令词",ASR:"风扇三档",ASRTO:"好的，三档"}
  //{ID:21,keyword:"命令词",ASR:"风扇四档",ASRTO:"好的，四档"}

  // ========== 测试 LED ==========
  //{ID:22,keyword:"命令词",ASR:"打开指示灯",ASRTO:"好的，已打开"}
  //{ID:23,keyword:"命令词",ASR:"关闭指示灯",ASRTO:"好的，已关闭"}

  // ========== 环境查询 ==========
  //{ID:24,keyword:"命令词",ASR:"当前温度",ASRTO:"正在查询温度"}
  //{ID:25,keyword:"命令词",ASR:"当前湿度",ASRTO:"正在查询湿度"}
  //{ID:26,keyword:"命令词",ASR:"空气质量",ASRTO:"正在查询空气质量"}
  //{ID:27,keyword:"命令词",ASR:"光照强度",ASRTO:"正在查询光照"}
  //{ID:28,keyword:"命令词",ASR:"全部环境信息",ASRTO:"正在查询环境信息"}

  // 板载 LED（可选保留）
  setPinFun(4, FIRST_FUNCTION);
  pinMode(4, output);
}
