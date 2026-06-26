#ifndef __SCHEDULE_H__
#define __SCHEDULE_H__

#include "headfile.h"

/** @brief 任务调度器初始化（计算任务总数）*/
void schedule_init(void);
/** @brief 任务调度器运行（遍历执行到期任务）*/
void schedule_run(void);

#endif


