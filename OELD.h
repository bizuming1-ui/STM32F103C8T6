/* SSD1306 PB6 SCL/PB7 SDA. Page-major framebuffer.
 * Delay-loop timing requires measurement. Do not assume >400 kHz is safe. */
#ifndef APP_OLED_H
#define APP_OLED_H
#include "stm32f10x.h"
#include <stdint.h>
#define OLED_W 128
#define OLED_H 64
#define OLED_I2C_ADDR 0x78
#define OLED_SCL_PORT GPIOB
#define OLED_SDA_PORT GPIOB
#define OLED_SCL_PIN ((uint16_t)0x0040)
#define OLED_SDA_PIN ((uint16_t)0x0080)
#define OLED_GPIO_RCC_MASK RCC_APB2ENR_IOPBEN
#define OLED_I2C_DELAY_TICKS 12u
#define OLED_DRIVER_SH1106 0
#define OLED_FLIP_180 0
#define OLED_BLACK 0
#define OLED_WHITE 1
extern uint8_t OLED_GRAM[OLED_H/8][OLED_W];
uint8_t OLED_Init(void);
void OLED_WriteCommand(uint8_t cmd);
void OLED_Update(void);
uint8_t OLED_LastTransferOK(void);
void OLED_DisplayOn(void);
void OLED_DisplayOff(void);
void OLED_SetContrast(uint8_t level);
void OLED_DelayMs(uint32_t ms);
void OLED_Clear(void);
void OLED_Fill(uint8_t color);
void OLED_SetClipCircle(int32_t cx, int32_t cy, int32_t r);
void OLED_DrawPixel(int32_t x, int32_t y, uint8_t color);
uint8_t OLED_GetPixel(int32_t x, int32_t y);
void OLED_DrawHLine(int32_t x, int32_t y, int32_t w, uint8_t color);
void OLED_DrawVLine(int32_t x, int32_t y, int32_t h, uint8_t color);
void OLED_DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color);
void OLED_DrawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color);
void OLED_FillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color);
uint32_t OLED_Isqrt(uint32_t n);
void OLED_DrawCircle(int32_t cx, int32_t cy, int32_t r, uint8_t color);
void OLED_FillCircle(int32_t cx, int32_t cy, int32_t r, uint8_t color);
void OLED_FillDiscQ8(int32_t xq, int32_t yq, int32_t rq, uint8_t color);
void OLED_DrawBrushSegQ8(int32_t x0q, int32_t y0q, int32_t r0q, int32_t x1q, int32_t y1q, int32_t r1q, uint8_t color);
void OLED_DrawThickLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t r, uint8_t color);
void OLED_ShowChar(int32_t x, int32_t y, char ch, uint8_t color);
void OLED_ShowString(int32_t x, int32_t y, const char *str, uint8_t color);
void OLED_DrawTinyDigit(int32_t x, int32_t y, uint8_t d, uint8_t color);
void OLED_DrawTinyNum2(int32_t cx, int32_t cy, uint8_t v, uint8_t color);
void OLED_DrawTinyNum(int32_t cx, int32_t cy, uint8_t v, uint8_t color);
#endif
