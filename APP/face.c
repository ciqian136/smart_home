#include "face.h"
#include "my_uart.h"

#define FACE_SCORE_THRESHOLD        70U
#define FACE_ZENG_CONFIRM_COUNT     3U
#define FACE_NONE_CONFIRM_COUNT     3U
#define FACE_TIMEOUT_MS             2000U
#define FACE_MAX_FRAMES_PER_SERVICE 6U

volatile uint8_t face_uart1_status_report_enabled =
    FACE_UART1_STATUS_REPORT_DEFAULT;

static uint8_t face_zeng_detected = 0;
static uint8_t face_online = 0;
static uint8_t face_score = 0;
static uint8_t face_zeng_count = 0;
static uint8_t face_none_count = 0;
static uint32_t face_last_frame_tick = 0;
static uint8_t face_owner_detected_event = 0;
static uint8_t face_welcome_event = 0;
static uint8_t face_report_has_sent = 0;
static uint8_t face_report_pending = 0;
static uint8_t face_report_last_state = 0;
static uint8_t face_report_pending_state = 0;
static uint32_t face_report_last_tick = 0;

static void face_report_status(void)
{
    uint32_t now;
    uint8_t recognized;

    if (!face_uart1_status_report_enabled) {
        face_report_has_sent = 0;
        face_report_pending = 0;
        return;
    }

    now = HAL_GetTick();
    recognized = face_zeng_detected ? 1U : 0U;

    if (!face_report_has_sent || recognized != face_report_last_state) {
        face_report_pending = 1;
        face_report_pending_state = recognized;
    } else if (now - face_report_last_tick >=
               FACE_UART1_STATUS_REPORT_INTERVAL_MS) {
        face_report_pending = 1;
        face_report_pending_state = recognized;
    }

    /*
     * 状态变化也遵守最小发送间隔，避免 OpenART 帧率较高时刷满串口1。
     * 首次打开标志位时立即发送一次当前状态。
     */
    if (face_report_pending &&
        (!face_report_has_sent ||
         now - face_report_last_tick >= FACE_UART1_STATUS_REPORT_INTERVAL_MS)) {
        uart_printf(&huart1, "[FACE_STATUS] recognized=%u\r\n",
                    face_report_pending_state);
        face_report_last_state = face_report_pending_state;
        face_report_last_tick = now;
        face_report_has_sent = 1;
        face_report_pending = 0;
    }
}

static uint8_t face_clamp_score(int score)
{
    if (score < 0) return 0;
    if (score > 100) return 100;
    return (uint8_t)score;
}

static void face_clear_zeng_if_needed(void)
{
    if (face_none_count >= FACE_NONE_CONFIRM_COUNT) {
        face_zeng_detected = 0;
        face_score = 0;
    }
}

static void face_mark_none(void)
{
    face_online = 1;
    face_last_frame_tick = HAL_GetTick();
    face_zeng_count = 0;
    if (face_none_count < FACE_NONE_CONFIRM_COUNT) {
        face_none_count++;
    }
    face_clear_zeng_if_needed();
}

static void face_mark_zeng(uint8_t score)
{
    face_online = 1;
    face_last_frame_tick = HAL_GetTick();
    face_score = score;
    face_none_count = 0;

    if (score < FACE_SCORE_THRESHOLD) {
        face_zeng_count = 0;
        return;
    }

    if (face_zeng_count < FACE_ZENG_CONFIRM_COUNT) {
        face_zeng_count++;
    }

    if (face_zeng_count >= FACE_ZENG_CONFIRM_COUNT) {
        if (!face_zeng_detected) {
            face_owner_detected_event = 1;
            face_welcome_event = 1;
        }
        face_zeng_detected = 1;
    }
}

static void face_trim_tail(char *buf)
{
    uint16_t len = (uint16_t)strlen(buf);
    while (len > 0) {
        char c = buf[len - 1];
        if (c != '\r' && c != '\n' && c != ' ' && c != '\t') {
            break;
        }
        buf[len - 1] = '\0';
        len--;
    }
}

static void face_parse_frame(char *frame);

static void face_parse_frames(char *frame)
{
    char *line = frame;

    while (line != NULL && *line != '\0') {
        char *next = strpbrk(line, "\r\n");

        if (next != NULL) {
            *next = '\0';
            next++;
            while (*next == '\r' || *next == '\n') {
                next++;
            }
        }

        if (strstr(line, "FACE:") != NULL) {
            face_parse_frame(line);
        }

        line = next;
    }
}

static void face_parse_frame(char *frame)
{
    char *msg;
    char *comma;
    uint8_t score;

    face_trim_tail(frame);
    msg = strstr(frame, "FACE:");
    if (msg == NULL) {
        return;
    }

    if (strncmp(msg, "FACE:ZENG", 9) == 0 ||
        strncmp(msg, "FACE:OWNER", 10) == 0) {
        comma = strchr(msg, ',');
        score = comma ? face_clamp_score(atoi(comma + 1)) : 0;
        face_mark_zeng(score);
        return;
    }

    if (strncmp(msg, "FACE:NONE", 9) == 0) {
        face_mark_none();
        return;
    }

    if (strncmp(msg, "FACE:ERR", 8) == 0) {
        face_online = 1;
        face_last_frame_tick = HAL_GetTick();
        face_zeng_detected = 0;
        face_zeng_count = 0;
        face_none_count = FACE_NONE_CONFIRM_COUNT;
        face_score = 0;
        return;
    }
}

void face_init(void)
{
    face_zeng_detected = 0;
    face_online = 0;
    face_score = 0;
    face_zeng_count = 0;
    face_none_count = 0;
    face_last_frame_tick = 0;
    face_owner_detected_event = 0;
    face_welcome_event = 0;
    face_report_has_sent = 0;
    face_report_pending = 0;
    face_report_last_state = 0;
    face_report_pending_state = 0;
    face_report_last_tick = 0;
}

void face_proc(void)
{
    char local_buf[64];
    uint32_t now = HAL_GetTick();
    uint8_t frames = 0U;

    while (frames < FACE_MAX_FRAMES_PER_SERVICE) {
        uint16_t len = my_uart5_take_frame((uint8_t *)local_buf,
                                           (uint16_t)(sizeof(local_buf) - 1U));
        if (len == 0U) break;

        local_buf[len] = '\0';
        face_parse_frames(local_buf);
        frames++;
    }

    if (face_online && (now - face_last_frame_tick > FACE_TIMEOUT_MS)) {
        face_online = 0;
        face_zeng_detected = 0;
        face_zeng_count = 0;
        face_none_count = 0;
        face_score = 0;
    }

    face_report_status();
}

uint8_t face_is_zeng_detected(void)
{
    return face_zeng_detected;
}

uint8_t face_is_owner_detected(void)
{
    return face_is_zeng_detected();
}

uint8_t face_take_owner_detected_event(void)
{
    uint8_t event = face_owner_detected_event;
    face_owner_detected_event = 0;
    return event;
}

uint8_t face_take_welcome_event(void)
{
    uint8_t event = face_welcome_event;
    face_welcome_event = 0;
    return event;
}

uint8_t face_get_score(void)
{
    return face_score;
}

uint8_t face_get_online(void)
{
    return face_online;
}
