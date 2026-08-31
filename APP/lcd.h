#ifndef __LCD_H__
#define __LCD_H__
#include "headfile.h"

void lcd_recv(void);
void lcd_send(void);
void lcd_send_strip_state(uint8_t strip_id);
void lcd_send_page_enter(uint8_t page_id);
#endif

