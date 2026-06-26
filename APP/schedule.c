#include "schedule.h"
#include "PM25.h"
#include "esp32.h"
#include "BH1750.h"
#include "my_uart.h"
#include "smoke.h"

/**
  * @brief  任务控制块结构体
  * @param task_func 任务函数指针
  * @param rate_ms   任务执行周期（毫秒）
  * @param last_run  上次执行时间戳（ms）
  */
typedef struct{
    void (*task_func)(void);  /* 任务函数指针 */
    uint32_t rate_ms;         /* 任务执行周期（毫秒）*/
    uint32_t last_run;        /* 上次执行时的时间戳 */
} task_t;

/* 任务总数 */
uint8_t task_num;

/**
  * @brief  测试占位函数 - 防止任务表为空时编译报错
  */
void test_proc(void)
{
}

/* 调度任务表：按顺序定义需要周期性执行的任务及其周期 */
static task_t schedule_task_t[] = {
    //{test_proc, 1000, 0},       /* 测试任务（已注释）*/
    {esp32_run_recv, 10, 0},    /* ESP32接收处理*/
    {esp32_run_send, 2000, 0},  /* ESP32数据发送*/
    {smoke_proc, 1000, 0},       /* 烟雾传感器处理，每1000ms执行一次 */
    {PM25_proc, 1000, 0},        /* PM2.5传感器处理，每1000ms执行一次 */
    {bh1750_proc,1000,0},        /* 光敏传感器处理，每1000ms执行一次 */
};

/**
  * @brief  调度器初始化 - 计算任务总数
  */
void schedule_init(void)
{
    task_num = sizeof(schedule_task_t) / sizeof(task_t);
}

/**
  * @brief  调度器运行 - 遍历任务表，按时执行到期任务
  *         基于 HAL_GetTick() 毫秒时间戳实现非阻塞调度
  */
void schedule_run(void)
{
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



