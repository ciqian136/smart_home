#include "asr.h"
extern "C"{ void * __dso_handle = 0 ;}
#include "setup.h"
#include "HardwareSerial.h"
#include "myLib/asr_event.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

uint32_t snid;
void ASR_CODE();

//{speak:小蝶-清新女声,vol:10,speed:10,platform:haohaodada}
//{playid:10001,voice:欢迎使用语音助手，用天问五幺唤醒我。}
//{playid:10002,voice:我退下了，用天问五幺唤醒我}

// ============================================================
//  Serial1 (GPIO2=TX, GPIO3=RX) 9600bps，FreeRTOS 任务驱动
//  协议: PLAY:XXXXX\r\n 单条播报 / PLAYS:X,Y,Z\r\n 多条顺序播报
//  外部播报只负责播放队列，不主动进入唤醒接收窗口，避免无人唤醒时播报退出提示
// ============================================================
#define RX_BUF_SIZE  256
#define PLAY_QUEUE_DEPTH 64
#define PLAY_CMD_ID_MIN 1U
#define PLAY_CMD_ID_MAX 130U
#define PLAY_VOICE_ID_MIN 10000U
#define PLAY_VOICE_ID_MAX 65535U
#define PLAY_START_RETRY_LOOPS 500U
#define PLAY_DONE_WAIT_LOOPS 2500U
#define ASR_WAKE_TIMEOUT_MS 30000U
#define ASR_QUERY_WAKE_TIMEOUT_MS 30000U
#define ASR_SERIAL1_RX_LOG_ENABLED 0
#define ASR_BOARD_LED_PIN 4
#define ASR_BOARD_LED_ON_LEVEL 1

static char rx_buf[RX_BUF_SIZE];
static QueueHandle_t play_queue = NULL;
static volatile uint8_t plays_batch_receiving = 0U;
static volatile uint8_t play_task_busy = 0U;
static bool board_led_on = false;

static void board_led_set(bool on)
{
    board_led_on = on;
    digitalWrite(ASR_BOARD_LED_PIN,
                 on ? ASR_BOARD_LED_ON_LEVEL : !ASR_BOARD_LED_ON_LEVEL);
}

static void board_led_toggle(void)
{
    board_led_set(!board_led_on);
}

static bool play_id_valid(uint32_t id)
{
    return ((id >= PLAY_CMD_ID_MIN && id <= PLAY_CMD_ID_MAX) ||
            (id >= PLAY_VOICE_ID_MIN && id <= PLAY_VOICE_ID_MAX));
}

static void queue_play_id(uint32_t id)
{
    if (play_queue == NULL || !play_id_valid(id)) return;

    (void)xQueueSend(play_queue, &id, 10);
}

static uint32_t start_prompt_play(uint32_t id)
{
    if (id >= PLAY_VOICE_ID_MIN) {
        return prompt_play_by_voice_id((uint16_t)id, NULL, false);
    }

    return prompt_play_by_cmd_id((uint16_t)id, -1, NULL, false);
}

static void notify_play_busy(void)
{
    if (!play_task_busy) {
        play_task_busy = 1U;
        Serial1.println("VOICE:BUSY");
    }
}

static void notify_play_idle_if_needed(void)
{
    if (play_queue == NULL) return;

    if (play_task_busy && uxQueueMessagesWaiting(play_queue) == 0U && !plays_batch_receiving) {
        play_task_busy = 0U;
        Serial1.println("VOICE:IDLE");
    }
}

static bool asr_is_query_cmd(uint32_t id)
{
    return (id == 24U || id == 25U || id == 26U ||
            id == 27U || id == 28U || id == 39U);
}

static void queue_play_list(const char *p)
{
    plays_batch_receiving = 1U;

    while (p != NULL && *p != '\0') {
        char *end = NULL;
        uint32_t id = (uint32_t)strtoul(p, &end, 10);

        if (end == p) break;
        queue_play_id(id);

        p = end;
        while (*p == ',' || *p == ' ') p++;
    }

    plays_batch_receiving = 0U;
}

static void log_received_line(const char *line)
{
#if ASR_SERIAL1_RX_LOG_ENABLED
    static char log_buf[RX_BUF_SIZE + 5];

    if (line == NULL) return;
    if (strncmp(line, "PLAYS:", 6) == 0) {
        Serial1.println("[RX]PLAYS");
    } else {
        snprintf(log_buf, sizeof(log_buf), "[RX]%s", line);
        Serial1.println(log_buf);
    }
#else
    (void)line;
#endif
}

static void handle_received_line(const char *line)
{
    if (line == NULL || line[0] == '\0') return;

    log_received_line(line);

    if (strncmp(line, "PLAY:", 5) == 0) {
        queue_play_id((uint32_t)strtoul(line + 5, NULL, 10));
    }
    else if (strncmp(line, "PLAYS:", 6) == 0) {
        queue_play_list(line + 6);
    }
}

static bool play_prompt_id(uint32_t id)
{
    uint16_t loops = 0;

    if (!play_id_valid(id)) return false;

    /* 外部 PLAY/PLAYS 不主动 set_state_enter_wakeup，避免无人唤醒时触发“我退下了”。 */
    while (start_prompt_play(id) != 0U) {
        loops++;
        if (loops >= PLAY_START_RETRY_LOOPS) {
            return false;
        }
        delay(10);
    }

    loops = 0;
    while (prompt_is_playing()) {
        loops++;
        if (loops >= PLAY_DONE_WAIT_LOOPS) {
            prompt_stop_play();
            break;
        }
        delay(2);
    }

    return true;
}

/* ── 串口接收任务 ────────────────────────────────── */

static void app_uart(void *arg)
{
    int idx = 0;
    bool overflowed = false;

    while (1) {
        while (Serial1.available() > 0) {
            char c = Serial1.read();

            if (c == '\n') {
                /* 去掉 \r */
                if (idx > 0 && rx_buf[idx - 1] == '\r') idx--;
                rx_buf[idx] = '\0';
                idx = 0;

                if (!overflowed) {
                    handle_received_line(rx_buf);
                }
                overflowed = false;
            } else {
                if (idx < RX_BUF_SIZE - 1) {
                    rx_buf[idx++] = c;
                } else {
                    overflowed = true;
                }
            }
        }
        delay(1);
    }
    vTaskDelete(NULL);
}

/* ── 语音播报任务 ────────────────────────────────── */

static void app_play(void *arg)
{
    uint32_t id;
    while (1) {
        if (xQueueReceive(play_queue, &id, 0)) {
            notify_play_busy();
            (void)play_prompt_id(id);
        }
        notify_play_idle_if_needed();
        delay(10);
    }
    vTaskDelete(NULL);
}


void ASR_CODE(){
  // 只有唤醒词和查询命令保留等待窗口；控制命令执行后不主动触发退出提示
  if (snid == 0)
      set_state_enter_wakeup(ASR_WAKE_TIMEOUT_MS);
  else if (asr_is_query_cmd(snid))
      set_state_enter_wakeup(ASR_QUERY_WAKE_TIMEOUT_MS);

  switch (snid) {
    // ========== 灯带控制 ==========
    case 1:   Serial1.println("LIGHT:ON");       break;
    case 2:   Serial1.println("LIGHT:OFF");      break;
    case 6:   Serial1.println("LIGHT:COLOR:WARM");   break;
    case 7:   Serial1.println("LIGHT:COLOR:WHITE");  break;
    case 8:   Serial1.println("LIGHT:COLOR:RED");    break;
    case 9:   Serial1.println("LIGHT:COLOR:GREEN");  break;
    case 10:  Serial1.println("LIGHT:COLOR:BLUE");   break;
    case 11:  Serial1.println("LIGHT:MODE:READ");    break;
    case 12:  Serial1.println("LIGHT:MODE:SLEEP");   break;
    case 13:  Serial1.println("LIGHT:MODE:NIGHT");   break;

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
    case 22:  board_led_set(true);  Serial1.println("LED:ON");     break;
    case 23:  board_led_set(false); Serial1.println("LED:OFF");    break;
    case 40:  board_led_toggle();   Serial1.println("LED:TOGGLE"); break;

    // ========== 灯带2控制（192灯珠，PD13）==========
    case 29:  Serial1.println("LIGHT2:ON");          break;
    case 30:  Serial1.println("LIGHT2:OFF");         break;
    case 31:  Serial1.println("LIGHT2:COLOR:WARM");  break;
    case 32:  Serial1.println("LIGHT2:COLOR:WHITE"); break;
    case 33:  Serial1.println("LIGHT2:COLOR:RED");   break;
    case 34:  Serial1.println("LIGHT2:COLOR:GREEN"); break;
    case 35:  Serial1.println("LIGHT2:COLOR:BLUE");  break;
    case 36:  Serial1.println("LIGHT2:MODE:READ");   break;
    case 37:  Serial1.println("LIGHT2:MODE:SLEEP");  break;
    case 38:  Serial1.println("LIGHT2:MODE:NIGHT");  break;

    // ========== 环境查询（发送指令后立即返回，播报由 app_play 任务异步完成）==========
    case 24:  Serial1.println("QUERY:TEMP");   break;
    case 25:  Serial1.println("QUERY:HUMI");   break;
    case 26:  Serial1.println("QUERY:DUST");   break;
    case 27:  Serial1.println("QUERY:LIGHT");  break;
    case 28:  Serial1.println("QUERY:ALL");    break;
    case 39:  Serial1.println("QUERY:SMOKE");  break;

    default: break;
  }
}

void hardware_init(){
  plays_batch_receiving = 0U;
  play_task_busy = 0U;
  play_queue = xQueueCreate(PLAY_QUEUE_DEPTH, sizeof(uint32_t));

  // Serial1: GPIO2=TX, GPIO3=RX
  setPinFun(2, FORTH_FUNCTION);
  setPinFun(3, FORTH_FUNCTION);
  Serial1.begin(9600);

  // 板载 LED
  setPinFun(ASR_BOARD_LED_PIN, FIRST_FUNCTION);
  pinMode(ASR_BOARD_LED_PIN, output);
  board_led_set(false);

  vol_set(3);

  xTaskCreate(app_uart, "app_uart", 512, NULL, 3, NULL);
  xTaskCreate(app_play, "app_play", 512, NULL, 4, NULL);

  vTaskDelete(NULL);
}

void setup()
{

  // ========== 唤醒词 ==========
  //{ID:0,keyword:"唤醒词",ASR:"天问五幺",ASRTO:"我在"}

  // ========== 灯带控制 ==========
  //{ID:1,keyword:"命令词",ASR:"打开灯光",ASRTO:"好的，马上打开灯光"}
  //{ID:2,keyword:"命令词",ASR:"关闭灯光",ASRTO:"好的，马上关闭灯光"}

  //{ID:6,keyword:"命令词",ASR:"暖光模式",ASRTO:"好的，已切换暖光"}
  //{ID:7,keyword:"命令词",ASR:"白光模式",ASRTO:"好的，已切换白光"}
  //{ID:8,keyword:"命令词",ASR:"红色灯光",ASRTO:"好的，已切换红色"}
  //{ID:9,keyword:"命令词",ASR:"绿色灯光",ASRTO:"好的，已切换绿色"}
  //{ID:10,keyword:"命令词",ASR:"蓝色灯光",ASRTO:"好的，已切换蓝色"}
  //{ID:11,keyword:"命令词",ASR:"阅读模式",ASRTO:"好的，已切换阅读模式"}
  //{ID:12,keyword:"命令词",ASR:"睡眠模式",ASRTO:"好的，晚安"}
  //{ID:13,keyword:"命令词",ASR:"夜灯模式",ASRTO:"好的，已切换夜灯模式"}

  // ========== 灯带2控制（192灯珠）==========
  //{ID:29,keyword:"命令词",ASR:"打开灯带二",ASRTO:"好的，马上打开灯带二"}
  //{ID:30,keyword:"命令词",ASR:"关闭灯带二",ASRTO:"好的，马上关闭灯带二"}
  //{ID:31,keyword:"命令词",ASR:"灯带二暖光",ASRTO:"好的，已切换灯带二暖光"}
  //{ID:32,keyword:"命令词",ASR:"灯带二白光",ASRTO:"好的，已切换灯带二白光"}
  //{ID:33,keyword:"命令词",ASR:"灯带二红色",ASRTO:"好的，已切换灯带二红色"}
  //{ID:34,keyword:"命令词",ASR:"灯带二绿色",ASRTO:"好的，已切换灯带二绿色"}
  //{ID:35,keyword:"命令词",ASR:"灯带二蓝色",ASRTO:"好的，已切换灯带二蓝色"}
  //{ID:36,keyword:"命令词",ASR:"灯带二阅读模式",ASRTO:"好的，已切换灯带二阅读模式"}
  //{ID:37,keyword:"命令词",ASR:"灯带二睡眠模式",ASRTO:"好的，晚安灯带二"}
  //{ID:38,keyword:"命令词",ASR:"灯带二夜灯模式",ASRTO:"好的，已切换灯带二夜灯模式"}

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
  //{ID:40,keyword:"命令词",ASR:"切换指示灯",ASRTO:"好的，已切换"}

  // ========== 环境查询 ==========
  //{ID:24,keyword:"命令词",ASR:"当前温度",ASRTO:"正在查询温度"}
  //{ID:25,keyword:"命令词",ASR:"当前湿度",ASRTO:"正在查询湿度"}
  //{ID:26,keyword:"命令词",ASR:"粉尘浓度",ASRTO:"正在查询粉尘数值"}
  //{ID:27,keyword:"命令词",ASR:"光照强度",ASRTO:"正在查询光照"}
  //{ID:28,keyword:"命令词",ASR:"全部环境信息",ASRTO:"正在查询环境信息"}
  //{ID:39,keyword:"命令词",ASR:"烟雾浓度",ASRTO:"正在查询烟雾数值"}

  // ====== 语音片段库（ID 100~122，prompt_play_by_cmd_id 调用）======

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

  // --- 单位 (ID 114~115/117) ---
  //{ID:114,keyword:"命令词",ASR:"单位度",ASRTO:"度"}
  //{ID:115,keyword:"命令词",ASR:"单位百分之",ASRTO:"百分之"}
  //{ID:117,keyword:"命令词",ASR:"单位勒克斯",ASRTO:"勒克斯"}

  // --- 前缀 (ID 118~121) ---
  //{ID:118,keyword:"命令词",ASR:"前缀温度",ASRTO:"当前温度"}
  //{ID:119,keyword:"命令词",ASR:"前缀湿度",ASRTO:"当前湿度"}
  //{ID:120,keyword:"命令词",ASR:"前缀粉尘",ASRTO:"当前粉尘数值"}
  //{ID:121,keyword:"命令词",ASR:"前缀光照",ASRTO:"当前光照"}

  // --- 负号 (ID 122) ---
  //{ID:122,keyword:"命令词",ASR:"零下温度",ASRTO:"零下"}

  // --- 温度告警 (ID 123~124) ---
  //{ID:123,keyword:"命令词",ASR:"警告高温",ASRTO:"温度偏高"}
  //{ID:124,keyword:"命令词",ASR:"温度正常",ASRTO:"温度正常"}

  // --- 联动播报预留 (ID 125~130) ---
  //{ID:125,keyword:"命令词",ASR:"前缀烟雾",ASRTO:"当前烟雾数值"}
  //{ID:127,keyword:"命令词",ASR:"警告烟雾",ASRTO:"警告，烟雾数值过高"}
  //{ID:128,keyword:"命令词",ASR:"警告粉尘",ASRTO:"警告，粉尘数值过高"}
  //{ID:129,keyword:"命令词",ASR:"识别人脸",ASRTO:"识别到人脸"}
  //{ID:130,keyword:"命令词",ASR:"陌生人员",ASRTO:"未识别人员"}

  // 板载 LED
  setPinFun(ASR_BOARD_LED_PIN, FIRST_FUNCTION);
  pinMode(ASR_BOARD_LED_PIN, output);
  board_led_set(false);
}
