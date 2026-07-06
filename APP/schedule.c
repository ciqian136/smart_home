#include "schedule.h"
#include "PM25.h"
#include "dht11.h"
#include "esp32.h"
#include "BH1750.h"
#include "lcd.h"

#include "my_uart.h"
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
    //{esp32_run_send, 1000, 0},  
    {smoke_proc, 300, 0},       
    {PM25_proc, 300, 0},        
    {bh1750_proc,300,0},        
		{voice_run_send,10,0},
    {DHT11_proc,300,0},
    {lcd_recv,10,0},
    {lcd_send,1000,0},
};

void schedule_init(void)
{
    task_num = sizeof(schedule_task_t) / sizeof(task_t);
}

void schedule_run(void)
{
    if (esp32_rx_pending) {
        esp32_rx_pending = 0;
        esp32_run_recv();
    }

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



