#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#define LCD_WIDTH   240
#define LCD_HEIGHT  240

#define LCD_COLOR_BLACK       0x0000
#define LCD_COLOR_WHITE       0xFFFF
#define LCD_COLOR_RED         0xF800
#define LCD_COLOR_GREEN       0x07E0
#define LCD_COLOR_BLUE        0x001F
#define LCD_COLOR_CYAN        0x07FF
#define LCD_COLOR_MAGENTA     0xF81F
#define LCD_COLOR_YELLOW      0xFFE0
#define LCD_COLOR_ORANGE      0xFD20

typedef void *lcd_handle_t;

lcd_handle_t lcd_init(void);
void lcd_clear(lcd_handle_t lcd, uint16_t color);
void lcd_set_window(lcd_handle_t lcd, int x, int y, int width, int height);
void lcd_draw_pixel(lcd_handle_t lcd, int x, int y, uint16_t color);
void lcd_draw_hline(lcd_handle_t lcd, int x, int y, int width, uint16_t color);
void lcd_draw_vline(lcd_handle_t lcd, int x, int y, int height, uint16_t color);
void lcd_draw_line(lcd_handle_t lcd, int x0, int y0, int x1, int y1, uint16_t color);
void lcd_draw_rect(lcd_handle_t lcd, int x, int y, int width, int height, uint16_t color);
void lcd_fill_rect(lcd_handle_t lcd, int x, int y, int width, int height, uint16_t color);
void lcd_draw_string(lcd_handle_t lcd, int x, int y, const char *str, uint16_t color, int scale);
void lcd_draw_circle(lcd_handle_t lcd, int cx, int cy, int radius, uint16_t color);
void lcd_fill_circle(lcd_handle_t lcd, int cx, int cy, int radius, uint16_t color);
void lcd_swap_buffers(lcd_handle_t lcd);
int lcd_get_width(void);
int lcd_get_height(void);
void lcd_deinit(lcd_handle_t lcd);

#endif // LCD_DRIVER_H
