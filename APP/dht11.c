#include "dht11.h"
#include <stdint.h>
uint8_t cnt=0;
#define ONE_PORT GPIOA
#define ONE_GPIO GPIO_PIN_7
#define ONE_HIGH HAL_GPIO_WritePin(ONE_PORT,ONE_GPIO,GPIO_PIN_SET)
#define ONE_LOW HAL_GPIO_WritePin(ONE_PORT,ONE_GPIO,GPIO_PIN_RESET)
void delay_us(uint32_t us)
{
   for(uint32_t i=0;i<us;i++)
	{
	   for(volatile uint32_t j=0;j<7;j++);
	}
}	
void delay_ms(uint32_t ms)
{
  for(volatile uint32_t i=0;i<ms;i++)
	{
	 for(volatile uint32_t j=0;j<7200;j++);
	}
}
void dht11_init(void)
{
    
  GPIO_InitTypeDef GPIO_InitStruct;
  GPIO_InitStruct.Pin = ONE_GPIO;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ONE_PORT, &GPIO_InitStruct);
}
void dht11_Mode(uint8_t cmd)
{
  GPIO_InitTypeDef GPIO_InitStruct;	
  if(cmd)
	{
		GPIO_InitStruct.Pin=ONE_GPIO;
		GPIO_InitStruct.Mode=GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
		GPIO_InitStruct.Speed=GPIO_SPEED_FREQ_LOW;
	}
	else
	{
        GPIO_InitStruct.Pin = ONE_GPIO;
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
	}
     HAL_GPIO_Init(ONE_PORT, &GPIO_InitStruct);
}

uint8_t dht11_Start(void)
{
    cnt = 0;
    dht11_Mode(1);
    ONE_LOW;
    delay_ms(18);
    ONE_HIGH;
    delay_us(40);
    dht11_Mode(0);

    // 等待 DHT11 拉低总线（应答）
    cnt = 0;
    while(!HAL_GPIO_ReadPin(ONE_PORT, ONE_GPIO))
    {
        delay_us(10);
        cnt++;
        if(cnt > 15) return 0;    // 150us 超时，放宽一些
    }

    // 等待 DHT11 释放总线（拉高）
    cnt = 0;
    while(HAL_GPIO_ReadPin(ONE_PORT, ONE_GPIO))
    {
        delay_us(10);
        cnt++;
        if(cnt > 15) return 0;
    }
    return 1;
}

uint8_t dht11_Read(uint8_t *temp)
{
    uint8_t i, j, dat;
    for(j = 0; j < 5; j++)
    {
        dat = 0;                 // ★ 每个字节必须清零
        for(i = 0; i < 8; i++)
        {
            cnt = 0;
            // 等待低电平结束
            while(!HAL_GPIO_ReadPin(ONE_PORT, ONE_GPIO))
            {
                delay_us(10);
                cnt++;
                if(cnt > 10) return 0;    // 适当放宽
            }

            // 延时 30us 后采样
            delay_us(30);

            if(HAL_GPIO_ReadPin(ONE_PORT, ONE_GPIO))
            {
                // 如果是高电平，等待其结束（“1” 的高电平约 70us）
                cnt = 0;
                while(HAL_GPIO_ReadPin(ONE_PORT, ONE_GPIO))
                {
                    delay_us(10);
                    cnt++;
                    if(cnt > 10) return 0;
                }
                dat = (dat << 1) | 0x01;
            }
            else
            {
                dat = (dat << 1) & 0xFE;
            }
        }
        temp[j] = dat;
    }
    // 简单校验
    if(temp[4] != (temp[0] + temp[1] + temp[2] + temp[3]))
        return 0;
    return 1;
}

void dht11_proc(void)
{
    uint8_t val[5] = {0};

    if(dht11_Start() == 0) {
        uart_printf(&huart1, "DHT11 start failed\r\n");
        return;
    }

    if(dht11_Read(val) == 0) {
        uart_printf(&huart1, "DHT11 read error\r\n");
        return;
    }

    uart_printf(&huart1, "humity = %u.%u  temp = %u.%u",
                val[0], val[1], val[2], val[3]);
}








