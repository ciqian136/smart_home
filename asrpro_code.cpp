#include "asr.h"
extern "C"{ void * __dso_handle = 0 ;}
#include "setup.h"
#include "HardwareSerial.h"
#include "myLib/asr_event.h"

uint32_t snid;
void ASR_CODE();

//{speak:小蝶-清新女声,vol:10,speed:10,platform:haohaodada}
//{playid:10001,voice:欢迎使用语音助手，用小雨小雨唤醒我。}
//{playid:10002,voice:我退下了，用小雨小雨唤醒我}
//{playid:10003,voice:欢迎回家，曾先生}

// ============================================================
//  Serial1 (GPIO2=TX, GPIO3=RX) 9600bps，FreeRTOS 任务驱动
//  协议: PLAY:XXXXX\r\n 单条播报 / PLAYS:X,Y,Z\r\n 多条顺序播报
// ============================================================
#define RX_BUF_SIZE  256
#define PLAY_QUEUE_DEPTH 64
#define PLAY_WAKE_TIMEOUT_MS 45000
#define PLAY_KEEPALIVE_LOOPS 250
#define PLAY_START_RETRY_LOOPS 50
#define PLAY_DONE_WAIT_LOOPS 2500
#define PLAY_VOICE_ID_MIN 10000U
#define ASR_SERIAL1_RX_LOG_ENABLED 0

static char rx_buf[RX_BUF_SIZE];
static QueueHandle_t play_queue = NULL;

static void keep_playback_awake(void)
{
    set_state_enter_wakeup(PLAY_WAKE_TIMEOUT_MS);
}

static void queue_play_id(uint32_t id)
{
    if (id > 0 && play_queue != NULL) {
        keep_playback_awake();
        xQueueSend(play_queue, &id, 10);
    }
}

static uint32_t start_prompt_play(uint32_t id)
{
    if (id >= PLAY_VOICE_ID_MIN) {
        return prompt_play_by_voice_id((uint16_t)id, NULL, false);
    }

    return prompt_play_by_cmd_id((uint16_t)id, -1, NULL, false);
}

static void refresh_wakeup_periodically(uint16_t *loops)
{
    if (loops == NULL) return;

    (*loops)++;
    if (*loops >= PLAY_KEEPALIVE_LOOPS) {
        *loops = 0;
        keep_playback_awake();
    }
}

static bool play_prompt_id(uint32_t id)
{
    uint16_t loops = 0;
    uint16_t keepalive_loops = 0;

    keep_playback_awake();
    while (start_prompt_play(id) != 0) {
        loops++;
        if (loops >= PLAY_START_RETRY_LOOPS) {
            resume_voice_in();
            return false;
        }
        refresh_wakeup_periodically(&keepalive_loops);
        delay(2);
    }

    loops = 0;
    keepalive_loops = 0;
    while (prompt_is_playing()) {
        loops++;
        if (loops >= PLAY_DONE_WAIT_LOOPS) {
            prompt_stop_play();
            resume_voice_in();
            break;
        }
        refresh_wakeup_periodically(&keepalive_loops);
        delay(2);
    }

    resume_voice_in();
    return true;
}

/* ── 串口接收任务 ────────────────────────────────── */

static void app_uart(void *arg)
{
    int idx = 0;
    while (1) {
        if (Serial1.available() > 0) {
            char c = Serial1.read();

            if (c == '\n') {
                /* 去掉 \r */
                if (idx > 0 && rx_buf[idx - 1] == '\r') idx--;
                rx_buf[idx] = '\0';
                idx = 0;

                if (ASR_SERIAL1_RX_LOG_ENABLED) {
                    /* 回传确认：PLAYS 很长，只确认类型，避免 STM32 侧 UART3 接收被长 echo 干扰 */
                    if (strncmp(rx_buf, "PLAYS:", 6) == 0) {
                        Serial1.println("[RX]PLAYS");
                    } else {
                        Serial1.print("[RX]");
                        Serial1.println(rx_buf);
                    }
                }

                /* 解析 PLAY:XXXXX */
                if (strncmp(rx_buf, "PLAY:", 5) == 0) {
                    uint32_t id = atoi(rx_buf + 5);
                    keep_playback_awake();
                    queue_play_id(id);
                }
                /* 解析 PLAYS:X,Y,Z */
                else if (strncmp(rx_buf, "PLAYS:", 6) == 0) {
                    const char *p = rx_buf + 6;
                    keep_playback_awake();
                    while (*p) {
                        uint32_t id = atoi(p);
                        queue_play_id(id);
                        while (*p && *p != ',') p++;
                        if (*p == ',') p++;
                    }
                }
            } else {
                if (idx < RX_BUF_SIZE - 1) rx_buf[idx++] = c;
            }
        }
        delay(2);
    }
    vTaskDelete(NULL);
}

/* ── 语音播报任务 ────────────────────────────────── */

static void app_play(void *arg)
{
    uint32_t id;
    while (1) {
        if (xQueueReceive(play_queue, &id, 0)) {
            play_prompt_id(id);
        }
        delay(10);
    }
    vTaskDelete(NULL);
}


void ASR_CODE(){
  // 唤醒超时 30 秒（ALL 查询播报含烟雾约 10~25 秒，留足余量）
  // 非 ALL 查询时恢复短超时
  if (snid == 28)
      set_state_enter_wakeup(30000);
  else
      set_state_enter_wakeup(10000);

  switch (snid) {
    // ========== 室外灯控制（LIGHT:3，192灯珠）==========
    case 1:   Serial1.println("LIGHT:3:ON");       break;
    case 2:   Serial1.println("LIGHT:3:OFF");      break;
    case 6:   Serial1.println("LIGHT:3:COLOR:WARM");   break;
    case 7:   Serial1.println("LIGHT:3:COLOR:WHITE");  break;
    case 8:   Serial1.println("LIGHT:3:COLOR:RED");    break;
    case 9:   Serial1.println("LIGHT:3:COLOR:GREEN");  break;
    case 10:  Serial1.println("LIGHT:3:COLOR:BLUE");   break;

    // ========== 风扇控制 ==========
    case 14:  Serial1.println("FAN:ON");         break;
    case 15:  Serial1.println("FAN:OFF");        break;
    case 16:  Serial1.println("FAN:SPEED:UP");   break;
    case 17:  Serial1.println("FAN:SPEED:DOWN"); break;
    case 18:  Serial1.println("FAN:SPEED:1");    break;
    case 19:  Serial1.println("FAN:SPEED:2");    break;
    case 20:  Serial1.println("FAN:SPEED:3");    break;
    case 21:  Serial1.println("FAN:SPEED:4");    break;

    // ========== 测试 LED ==========
    case 22:  Serial1.println("LED:ON");         break;
    case 23:  Serial1.println("LED:OFF");        break;

    // ========== 入户灯控制（LIGHT:2 保留不用，不下发控制）==========
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:  break;

    // ========== 室内灯控制（LIGHT:1，48灯珠，含阅读/睡眠/夜灯模式）==========
    case 39:  Serial1.println("LIGHT:1:ON");          break;
    case 40:  Serial1.println("LIGHT:1:OFF");         break;
    case 41:  Serial1.println("LIGHT:1:COLOR:WARM");  break;
    case 42:  Serial1.println("LIGHT:1:COLOR:WHITE"); break;
    case 43:  Serial1.println("LIGHT:1:COLOR:RED");   break;
    case 44:  Serial1.println("LIGHT:1:COLOR:GREEN"); break;
    case 45:  Serial1.println("LIGHT:1:COLOR:BLUE");  break;
    case 46:  Serial1.println("LIGHT:1:MODE:READ");   break;
    case 47:  Serial1.println("LIGHT:1:MODE:SLEEP");  break;
    case 48:  Serial1.println("LIGHT:1:MODE:NIGHT");  break;

    // ========== 环境查询（发送指令后立即返回，播报由 app_play 任务异步完成）==========
    case 24:  Serial1.println("QUERY:TEMP");   break;
    case 25:  Serial1.println("QUERY:HUMI");   break;
    case 26:  Serial1.println("QUERY:PM25");   break;
    case 27:  Serial1.println("QUERY:LIGHT");  break;
    case 28:  Serial1.println("QUERY:ALL");    break;
    case 49:  Serial1.println("QUERY:SMOKE");  break;

    // ========== 自动控制 ==========
    case 50:  Serial1.println("AUTO:ON");       break;
    case 51:  Serial1.println("AUTO:OFF");      break;
    case 52:  Serial1.println("FAN:AUTO");      break;
    case 53:  Serial1.println("LIGHT:AUTO");    break;
    case 54:  Serial1.println("FAN:MANUAL");    break;
    case 55:  Serial1.println("LIGHT:MANUAL");  break;

    default: break;
  }
}

void hardware_init(){
  play_queue = xQueueCreate(PLAY_QUEUE_DEPTH, sizeof(uint32_t));

  // Serial1: GPIO2=TX, GPIO3=RX
  setPinFun(2, FORTH_FUNCTION);
  setPinFun(3, FORTH_FUNCTION);
  Serial1.begin(9600);

  // 板载 LED
  setPinFun(4, FIRST_FUNCTION);
  pinMode(4, output);

  vol_set(3);

  xTaskCreate(app_uart, "app_uart", 512, NULL, 3, NULL);
  xTaskCreate(app_play, "app_play", 512, NULL, 4, NULL);

  vTaskDelete(NULL);
}

void setup()
{

  // ========== 唤醒词 ==========
  //{ID:0,keyword:"唤醒词",ASR:"小雨小雨",ASRTO:"我在"}

  // ========== 室外灯控制（LIGHT:3，192灯珠） ==========
  //{ID:1,keyword:"命令词",ASR:"打开室外灯",ASRTO:"好的，马上打开室外灯"}
  //{ID:2,keyword:"命令词",ASR:"关闭室外灯",ASRTO:"好的，马上关闭室外灯"}
  //{ID:6,keyword:"命令词",ASR:"室外灯切换暖光模式",ASRTO:"好的，室外灯已切换暖光"}
  //{ID:7,keyword:"命令词",ASR:"室外灯切换白光模式",ASRTO:"好的，室外灯已切换白光"}
  //{ID:8,keyword:"命令词",ASR:"室外灯切换红光模式",ASRTO:"好的，室外灯已切换红光"}
  //{ID:9,keyword:"命令词",ASR:"室外灯切换绿光模式",ASRTO:"好的，室外灯已切换绿光"}
  //{ID:10,keyword:"命令词",ASR:"室外灯切换蓝光模式",ASRTO:"好的，室外灯已切换蓝光"}

  // ========== 入户灯控制（LIGHT:2 保留不用）==========
  //{ID:29,keyword:"命令词",ASR:"打开入户灯",ASRTO:"入户灯通道已保留，暂不使用"}
  //{ID:30,keyword:"命令词",ASR:"关闭入户灯",ASRTO:"入户灯通道已保留，暂不使用"}
  //{ID:31,keyword:"命令词",ASR:"入户灯切换暖光模式",ASRTO:"入户灯通道已保留，暂不使用"}
  //{ID:32,keyword:"命令词",ASR:"入户灯切换白光模式",ASRTO:"入户灯通道已保留，暂不使用"}
  //{ID:33,keyword:"命令词",ASR:"入户灯切换红光模式",ASRTO:"入户灯通道已保留，暂不使用"}
  //{ID:34,keyword:"命令词",ASR:"入户灯切换绿光模式",ASRTO:"入户灯通道已保留，暂不使用"}
  //{ID:35,keyword:"命令词",ASR:"入户灯切换蓝光模式",ASRTO:"入户灯通道已保留，暂不使用"}

  // ========== 室内灯控制（LIGHT:1，48灯珠，含阅读/睡眠/夜灯模式）==========
  //{ID:39,keyword:"命令词",ASR:"打开室内灯",ASRTO:"好的，马上打开室内灯"}
  //{ID:40,keyword:"命令词",ASR:"关闭室内灯",ASRTO:"好的，马上关闭室内灯"}
  //{ID:41,keyword:"命令词",ASR:"室内灯切换暖光模式",ASRTO:"好的，室内灯已切换暖光"}
  //{ID:42,keyword:"命令词",ASR:"室内灯切换白光模式",ASRTO:"好的，室内灯已切换白光"}
  //{ID:43,keyword:"命令词",ASR:"室内灯切换红光模式",ASRTO:"好的，室内灯已切换红光"}
  //{ID:44,keyword:"命令词",ASR:"室内灯切换绿光模式",ASRTO:"好的，室内灯已切换绿光"}
  //{ID:45,keyword:"命令词",ASR:"室内灯切换蓝光模式",ASRTO:"好的，室内灯已切换蓝光"}
  //{ID:46,keyword:"命令词",ASR:"室内灯切换阅读模式",ASRTO:"好的，室内灯已切换阅读模式"}
  //{ID:47,keyword:"命令词",ASR:"室内灯切换睡眠模式",ASRTO:"好的，室内灯已切换睡眠模式"}
  //{ID:48,keyword:"命令词",ASR:"室内灯切换夜灯模式",ASRTO:"好的，室内灯已切换夜灯模式"}

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
  //{ID:49,keyword:"命令词",ASR:"烟雾浓度",ASRTO:"正在查询烟雾浓度"}

  // ========== 自动控制 ==========
  //{ID:50,keyword:"命令词",ASR:"开启自动控制",ASRTO:"已切换为自动控制模式"}
  //{ID:51,keyword:"命令词",ASR:"关闭自动控制",ASRTO:"已切换为手动控制模式"}
  //{ID:52,keyword:"命令词",ASR:"风扇自动模式",ASRTO:"风扇已切换自动模式"}
  //{ID:53,keyword:"命令词",ASR:"灯光自动模式",ASRTO:"灯光已切换自动模式"}
  //{ID:54,keyword:"命令词",ASR:"风扇手动模式",ASRTO:"风扇已切换手动模式"}
  //{ID:55,keyword:"命令词",ASR:"灯光手动模式",ASRTO:"灯光已切换手动模式"}

  // ====== 语音片段库（ID 100~135，prompt_play_by_cmd_id 调用）======

  // --- 数字 0~10 (ID 100~110) ---
  //{ID:100,keyword:"命令词",ASR:"数字零",ASRTO:"零"}
  //{ID:101,keyword:"命令词",ASR:"数字一",ASRTO:"一"}
  //{ID:102,keyword:"命令词",ASR:"数字二",ASRTO:"二"}
  //{ID:103,keyword:"命令词",ASR:"数字三",ASRTO:"三"}
  //{ID:104,keyword:"命令词",ASR:"数字四",ASRTO:"四"}
  //{ID:105,keyword:"命令词",ASR:"数字五",ASRTO:"五"}
  //{ID:106,keyword:"命令词",ASR:"数字六",ASRTO:"六"}
  //{ID:107,keyword:"命令词",ASR:"数字七",ASRTO:"七"}
  //{ID:108,keyword:"命令词",ASR:"数字八",ASRTO:"八"}
  //{ID:109,keyword:"命令词",ASR:"数字九",ASRTO:"九"}
  //{ID:110,keyword:"命令词",ASR:"数字十",ASRTO:"十"}

  // --- 数量级 (ID 111~112) ---
  //{ID:111,keyword:"命令词",ASR:"单位百",ASRTO:"百"}
  //{ID:112,keyword:"命令词",ASR:"单位千",ASRTO:"千"}

  // --- 小数点 (ID 113) ---
  //{ID:113,keyword:"命令词",ASR:"单位点",ASRTO:"点"}

  // --- 单位 (ID 114~117) ---
  //{ID:114,keyword:"命令词",ASR:"单位度",ASRTO:"度"}
  //{ID:115,keyword:"命令词",ASR:"单位百分之",ASRTO:"百分之"}
  //{ID:116,keyword:"命令词",ASR:"单位微克",ASRTO:"微克每立方米"}
  //{ID:117,keyword:"命令词",ASR:"单位勒克斯",ASRTO:"勒克斯"}

  // --- 前缀 (ID 118~121) ---
  //{ID:118,keyword:"命令词",ASR:"前缀温度",ASRTO:"当前温度"}
  //{ID:119,keyword:"命令词",ASR:"前缀湿度",ASRTO:"当前湿度"}
  //{ID:120,keyword:"命令词",ASR:"前缀粉尘",ASRTO:"粉尘浓度"}
  //{ID:121,keyword:"命令词",ASR:"前缀光照",ASRTO:"当前光照"}

  // --- 负号 (ID 122) ---
  //{ID:122,keyword:"命令词",ASR:"零下温度",ASRTO:"零下"}

  // --- 温度告警 (ID 123~124) ---
  //{ID:123,keyword:"命令词",ASR:"警告高温",ASRTO:"温度偏高"}
  //{ID:124,keyword:"命令词",ASR:"温度正常",ASRTO:"温度正常"}

  // --- 烟雾 (ID 125) ---
  //{ID:125,keyword:"命令词",ASR:"前缀烟雾",ASRTO:"烟雾浓度"}

  // --- 人脸联动播报 (ID 126~128) ---
  //{ID:126,keyword:"命令词",ASR:"前缀风扇已调节",ASRTO:"已为您调节风扇到合适挡位"}
  //{ID:127,keyword:"命令词",ASR:"前缀灯光已调节",ASRTO:"已为您调节室内灯光到合适亮度"}
  //{ID:128,keyword:"命令词",ASR:"前缀环境更新",ASRTO:"环境数据正在更新"}

  // --- 自动化动作播报 (ID 129~133) ---
  //{ID:129,keyword:"命令词",ASR:"自动开启灯光",ASRTO:"已自动开启灯光"}
  //{ID:130,keyword:"命令词",ASR:"自动关闭灯光",ASRTO:"已自动关闭灯光"}
  //{ID:131,keyword:"命令词",ASR:"自动开启风扇",ASRTO:"已自动开启风扇"}
  //{ID:132,keyword:"命令词",ASR:"自动关闭风扇",ASRTO:"已自动关闭风扇"}
  //{ID:133,keyword:"命令词",ASR:"自动调节风扇",ASRTO:"风扇已自动调节"}

  // --- 空气质量告警 (ID 134~135) ---
  //{ID:134,keyword:"命令词",ASR:"警告粉尘超标",ASRTO:"警告，粉尘浓度超标，请及时通风"}
  //{ID:135,keyword:"命令词",ASR:"警告烟雾超标",ASRTO:"警告，烟雾浓度超标，请立即处理"}

  // 板载 LED
  setPinFun(4, FIRST_FUNCTION);
  pinMode(4, output);
}
