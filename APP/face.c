#include "face.h"
#include "my_uart.h"
#include "voice.h"

#include <stdlib.h>
#include <string.h>

/* 调试时改为 1，正常使用保持 0 */
#define FACE_DEBUG 0
#define FACE_DEBUG_INTERVAL_MS 1000U

#define FACE_RX_LINE_SIZE 96U
#define FACE_MAX_LINES_PER_PROC 6U
#define FACE_SCORE_THRESHOLD 70U
#define FACE_HIT_CONFIRM_COUNT 2U
#define FACE_NONE_CONFIRM_COUNT 3U
#define FACE_TIMEOUT_MS 2000U

#if FACE_DEBUG
#define FACE_DEBUG_PRINTF(...) uart_printf(&huart1, __VA_ARGS__)
#else
#define FACE_DEBUG_PRINTF(...) ((void)0)
#endif

static uint8_t face_online = 0U;
static uint8_t face_zeng_detected = 0U;
static uint8_t face_score = 0U;
static uint8_t face_hit_count = 0U;
static uint8_t face_none_count = 0U;
static uint8_t face_zeng_detected_event = 0U;
static uint32_t face_last_frame_tick = 0U;

static uint8_t face_clamp_score(int score)
{
    if (score < 0) return 0U;
    if (score > 100) return 100U;
    return (uint8_t)score;
}

static void face_trim_tail(char *text)
{
    uint16_t len;

    if (text == NULL) return;

    len = (uint16_t)strlen(text);
    while (len > 0U) {
        char c = text[len - 1U];
        if (c != '\r' && c != '\n' && c != ' ' && c != '\t') break;
        text[len - 1U] = '\0';
        len--;
    }
}

static void face_clear_recognition(void)
{
    face_zeng_detected = 0U;
    face_score = 0U;
    face_hit_count = 0U;
}

static void face_notify_zeng_detected(void)
{
    voice_play_face_zeng();
}

static void face_mark_none(void)
{
    face_online = 1U;
    face_last_frame_tick = HAL_GetTick();
    face_hit_count = 0U;

    if (face_none_count < FACE_NONE_CONFIRM_COUNT) {
        face_none_count++;
    }

    if (face_none_count >= FACE_NONE_CONFIRM_COUNT) {
        face_clear_recognition();
    }
}

static void face_mark_zeng(uint8_t score)
{
    face_online = 1U;
    face_last_frame_tick = HAL_GetTick();
    face_none_count = 0U;

    if (score < FACE_SCORE_THRESHOLD) {
        face_mark_none();
        return;
    }

    face_score = score;

    if (face_hit_count < FACE_HIT_CONFIRM_COUNT) {
        face_hit_count++;
    }

    if (face_hit_count >= FACE_HIT_CONFIRM_COUNT && !face_zeng_detected) {
        face_zeng_detected = 1U;
        face_zeng_detected_event = 1U;
        face_notify_zeng_detected();
        FACE_DEBUG_PRINTF("[FACE] zeng score=%u\r\n", (unsigned int)score);
    }
}

static void face_mark_error(const char *reason)
{
    face_online = 1U;
    face_last_frame_tick = HAL_GetTick();
    face_none_count = FACE_NONE_CONFIRM_COUNT;
    face_clear_recognition();
    FACE_DEBUG_PRINTF("[FACE] error %s\r\n", reason ? reason : "unknown");
}

static void face_parse_line(char *line)
{
    char *msg;
    char *cmd;
    char *arg1;
    uint8_t score;

    face_trim_tail(line);
    msg = strstr(line, "FACE:");
    if (msg == NULL) return;

    msg += 5;
    cmd = strtok(msg, ",");
    if (cmd == NULL) return;

    if (strcmp(cmd, "ZENG") == 0) {
        arg1 = strtok(NULL, ",");
        score = face_clamp_score(arg1 ? atoi(arg1) : 100);
        face_mark_zeng(score);
        return;
    }

    if (strcmp(cmd, "NONE") == 0) {
        face_mark_none();
        return;
    }

    if (strcmp(cmd, "ERR") == 0) {
        arg1 = strtok(NULL, ",");
        face_mark_error(arg1);
        return;
    }
}

void face_init(void)
{
    face_online = 0U;
    face_zeng_detected = 0U;
    face_score = 0U;
    face_hit_count = 0U;
    face_none_count = 0U;
    face_zeng_detected_event = 0U;
    face_last_frame_tick = 0U;
}

void face_proc(void)
{
    char line[FACE_RX_LINE_SIZE];
    uint8_t lines = 0U;
    uint32_t now;

    while (lines < FACE_MAX_LINES_PER_PROC) {
        uint16_t len = my_uart_read_line(&huart5, line, sizeof(line));
        if (len == 0U) break;
        face_parse_line(line);
        lines++;
    }

    now = HAL_GetTick();
    if (face_online && now - face_last_frame_tick > FACE_TIMEOUT_MS) {
        face_online = 0U;
        face_clear_recognition();
        FACE_DEBUG_PRINTF("[FACE] offline\r\n");
    }

#if FACE_DEBUG
    static uint32_t debug_last_tick = 0U;
    if (now - debug_last_tick >= FACE_DEBUG_INTERVAL_MS) {
        debug_last_tick = now;
        FACE_DEBUG_PRINTF("[FACE] online=%u zeng=%u score=%u rx5=%u ov5=%lu\r\n",
                          (unsigned int)face_online,
                          (unsigned int)face_zeng_detected,
                          (unsigned int)face_score,
                          (unsigned int)my_uart_available(&huart5),
                          (unsigned long)my_uart_get_rx_overflow(&huart5));
    }
#endif
}

uint8_t face_is_zeng_detected(void)
{
    return face_zeng_detected;
}

uint8_t face_take_zeng_detected_event(void)
{
    uint8_t event = face_zeng_detected_event;
    face_zeng_detected_event = 0U;
    return event;
}

uint8_t face_get_online(void)
{
    return face_online;
}

uint8_t face_get_score(void)
{
    return face_score;
}
