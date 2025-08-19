#pragma once
#include <stdint.h>
#include "driver/spi_master.h"
#include "esp_err.h"

#define ST7735_WIDTH  128
#define ST7735_HEIGHT 128

typedef struct {
    spi_device_handle_t spi;
} st7735_handle_t;

// Initialize the display (also initializes SPI bus & device)
esp_err_t st7735_init(st7735_handle_t *dev);

// Fill whole screen with RGB565 color
esp_err_t st7735_fill_color(st7735_handle_t *dev, uint16_t color);

// Draw a single pixel
void st7735_draw_pixel(st7735_handle_t *dev, uint16_t x, uint16_t y, uint16_t color);

// Draw one character
void st7735_draw_char(st7735_handle_t *dev, char c, int16_t x, int16_t y, uint16_t color, uint16_t bg);

// Draw string
void st7735_draw_string(st7735_handle_t *dev, const char *str, int16_t x, int16_t y, uint16_t color, uint16_t bg);

// Center x
int16_t st7735_center_x(const char *str);

// Center y
int16_t st7735_center_y();

// Deinit if you want (optional)
void st7735_deinit(st7735_handle_t *dev);
