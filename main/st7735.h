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

// Deinit if you want (optional)
void st7735_deinit(st7735_handle_t *dev);
