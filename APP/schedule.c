#include "schedule.h"
#include "PM25.h"
#include "dht11.h"
#include "esp32.h"
#include "BH1750.h"
#include "lcd.h"

#include "my_uart.h"
#include "face.h"
#include "smoke.h"
#include "voice.h"

typedef struct {
    void (*task_func)(void);
    uint32_t rate_ms;
    uint32_t last_run;
} task_t;

uint8_t task_num;

void test_proc(void)
{
}

/* 调度任务表 */
static task_t schedule_task_t[] = {
    //{test_proc, 1000, 0},       
    {esp32_init_nonblock, 20, 0},  /* 非阻塞初始化状态机，20ms 驱动一次 */
    {face_proc, 20, 0},            /* OpenART face result parser */
    {esp32_run_send, 100, 0},     /* 100ms 调用但每 10 次发 1 次（1s/条），10 cases = 10s */
    {smoke_proc, 300, 0},       
    {PM25_proc, 300, 0},        
    {bh1750_proc,300,0},        
	{voice_run_send,10,0},
    {DHT11_proc,300,0},
    {lcd_recv,10,0},
    {lcd_send,1000,0},
    {esp32_check_online, 500, 0}, /* 每 500ms 检测在线状态（内部含 30s ping/10s WiFi）*/
};

void schedule_init(void)
{
    task_num = sizeof(schedule_task_t) / sizeof(task_t);
    	/*DWT初始化用于微秒延时*/
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	/*基本原件初始化*/
  my_uart_init();
	face_init();
	my_adc_init();
	uart_printf(&huart1,"[stm32]start");
	/*各模块初始化*/
	 /* 灯带统一初始化：注册硬件参数 → 初始关闭 */
	ws2812_strip_init(1, 48,  ws2812_set_all);     /* TIM4_CH1 PD12 */
	ws2812_strip_init(2, 192, ws2812_2_set_all);   /* TIM4_CH2 PD13 */
	ws2812_strip_init(3, 192, ws2812_3_set_all);   /* TIM4_CH3 PD14 */
	ws2812_strip_set_all(1, 0, 0, 0);
	ws2812_strip_set_all(2, 0, 0, 0);
	ws2812_strip_set_all(3, 0, 0, 0);
	 smoke_init();
   DHT11_init();
   PM25_init();
   fan_init();
	 bh1750_init();
   //esp32_init();
}

void schedule_run(void)
{
    if (esp32_rx_pending) {
        esp32_rx_pending = 0;
        esp32_run_recv();
    }

    /* AT 指令超时检测：无条件每轮执行，防止 ESP32 死机导致 busy 永久卡死 */
    esp32_check_cmd_timeout();

    /* 发送 set_reply（每轮都尝试，不依赖 100ms 定时任务，确保 6s 内响应）*/
    esp32_flush_reply();

    uint8_t i = 0;
    for (i = 0; i < task_num; i++)
    {
        uint32_t now_time = HAL_GetTick();  /* 获取当前系统时间（ms）*/
        /* 判断是否到达执行时间（当前时间 >= 上次执行时间 + 周期）*/
        if (now_time >= schedule_task_t[i].rate_ms + schedule_task_t[i].last_run)
        {
           schedule_task_t[i].last_run = now_time;  /* 更新上次执行时间 */
           schedule_task_t[i].task_func();           /* 执行任务函数 */
        }
    }
}



