#include "st7735.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "font5x7.h"

static const char *TAG = "ST7735";

// Wiring - adapt if you wired differently
#define PIN_NUM_MOSI 23
#define PIN_NUM_MISO -1
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5
#define PIN_NUM_DC   2
#define PIN_NUM_RST  4
// BLK tied to 3.3V in your setup

// Helpers: low-level SPI send
static esp_err_t st7735_send_cmd(st7735_handle_t *dev, const uint8_t cmd)
{
    gpio_set_level(PIN_NUM_DC, 0); // command
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    return spi_device_polling_transmit(dev->spi, &t);
}

static esp_err_t st7735_send_data(st7735_handle_t *dev, const uint8_t *data, int len)
{
    if (len == 0) return ESP_OK;
    gpio_set_level(PIN_NUM_DC, 1); // data
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(dev->spi, &t);
}

static void st7735_write_cmd_data(st7735_handle_t *dev, uint8_t cmd, const uint8_t *data, int len)
{
    // ignore return values for brevity; could check and log errors
    st7735_send_cmd(dev, cmd);
    if (data && len) st7735_send_data(dev, data, len);
}

static void st7735_set_window(st7735_handle_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t caset[4] = { (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF), (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF) };
    uint8_t raset[4] = { (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF), (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF) };
    st7735_write_cmd_data(dev, 0x2A, caset, 4); // CASET
    st7735_write_cmd_data(dev, 0x2B, raset, 4); // RASET
    // RAMWR will be sent before actual pixel data
}

// Initialize SPI bus/device, reset, and run ST7735 init sequence
esp_err_t st7735_init(st7735_handle_t *dev)
{
    esp_err_t ret;

    // init DC and RST as outputs
    gpio_reset_pin(PIN_NUM_DC);
    gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_NUM_RST);
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);

    // Initialize SPI bus (ignore "already initialized" error)
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ST7735_WIDTH * ST7735_HEIGHT * 2 + 16
    };
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %d", ret);
        return ret;
    }

    // Add device (chip select handled by driver)
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000, // start with 10 MHz (stable)
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 3,
    };
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &dev->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %d", ret);
        return ret;
    }

    // Hardware reset
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // --------------- ST7735 init sequence ---------------
    // This sequence is a common/tested one for 1.44" ST7735S (128x128)
    st7735_write_cmd_data(dev, 0x01, NULL, 0); vTaskDelay(pdMS_TO_TICKS(150)); // SWRESET
    st7735_write_cmd_data(dev, 0x11, NULL, 0); vTaskDelay(pdMS_TO_TICKS(150)); // SLPOUT

    uint8_t fr1[] = {0x01, 0x2C, 0x2D};
    st7735_write_cmd_data(dev, 0xB1, fr1, 3);
    st7735_write_cmd_data(dev, 0xB2, fr1, 3);
    uint8_t fr3[] = {0x01,0x2C,0x2D, 0x01,0x2C,0x2D};
    st7735_write_cmd_data(dev, 0xB3, fr3, 6);

    uint8_t inv1[] = {0x03};
    st7735_write_cmd_data(dev, 0xB4, inv1, 1);

    uint8_t pwr1[] = {0xA2,0x02,0x84};
    st7735_write_cmd_data(dev, 0xC0, pwr1, 3);
    uint8_t pwr2[] = {0xC5};
    st7735_write_cmd_data(dev, 0xC1, pwr2, 1);
    uint8_t pwr3[] = {0x0A,0x00};
    st7735_write_cmd_data(dev, 0xC2, pwr3, 2);
    uint8_t pwr4[] = {0x8A,0x2A};
    st7735_write_cmd_data(dev, 0xC3, pwr4, 2);
    uint8_t pwr5[] = {0x8A,0xEE};
    st7735_write_cmd_data(dev, 0xC4, pwr5, 2);
    uint8_t vcom[] = {0x0E};
    st7735_write_cmd_data(dev, 0xC5, vcom, 1);

    // Memory access control (rotation + RGB/BGR)
    uint8_t madctl = 0xC8; // try 0xC8; if colors swapped or rotated, change to 0x00/0x08/0xC0 etc
    st7735_write_cmd_data(dev, 0x36, &madctl, 1);

    // Color mode 16-bit
    uint8_t colmod = 0x05;
    st7735_write_cmd_data(dev, 0x3A, &colmod, 1);

    // Gamma correction (recommended)
    uint8_t gm1[16] = {0x02,0x1C,0x07,0x12,0x37,0x32,0x29,0x2D,0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10};
    st7735_write_cmd_data(dev, 0xE0, gm1, 16);
    uint8_t gm2[16] = {0x03,0x1D,0x07,0x06,0x2E,0x2C,0x29,0x2D,0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10};
    st7735_write_cmd_data(dev, 0xE1, gm2, 16);

    // Normal display mode
    st7735_write_cmd_data(dev, 0x13, NULL, 0);

    // Display ON
    st7735_write_cmd_data(dev, 0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "ST7735 init complete");
    return ESP_OK;
}

// Fill entire screen with RGB565 color
esp_err_t st7735_fill_color(st7735_handle_t *dev, uint16_t color)
{
    // Set window to whole screen
    st7735_set_window(dev, 0, 0, ST7735_WIDTH - 1, ST7735_HEIGHT - 1);

    // Send RAMWR
    st7735_send_cmd(dev, 0x2C);

    // Prepare one line buffer (128 pixels * 2 bytes)
    static uint8_t line[ST7735_WIDTH * 2];
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;
    for (int i = 0; i < ST7735_WIDTH; i++) {
        line[i*2 + 0] = hi;
        line[i*2 + 1] = lo;
    }

    // Send each line
    for (int y = 0; y < ST7735_HEIGHT; y++) {
        // Note: spi_device_polling_transmit is synchronous so using stack/static buffer is safe
        st7735_send_data(dev, line, sizeof(line));
    }
    return ESP_OK;
}

// Draw a single pixel
void st7735_draw_pixel(st7735_handle_t *dev, uint16_t x, uint16_t y, uint16_t color) {
    st7735_set_window(dev, x, y, x, y);
    st7735_send_cmd(dev, 0x2C);
    uint8_t buf[2] = { color >> 8, color & 0xFF };
    st7735_send_data(dev, buf, 2);
}

// Draw one character at (x,y) with foreground and background colors
void st7735_draw_char(st7735_handle_t *dev, char c, int16_t x, int16_t y, uint16_t color, uint16_t bg) {
    if (c < 32 || c > 127) return; // only printable ASCII

    const uint8_t *bitmap = font5x7[c - 32];
    for (int8_t col = 0; col < 5; col++) {      // 5 columns
        uint8_t bits = bitmap[col];
        for (int8_t row = 0; row < 8; row++) {  // 8 rows (max)
            if (bits & 0x1) {
                st7735_draw_pixel(dev, x + col, y + row, color);
            } else {
                st7735_draw_pixel(dev, x + col, y + row, bg);
            }
            bits >>= 1;
        }
    }
    // Optional: 1-pixel spacing between characters
    for (int8_t row = 0; row < 8; row++) {
        st7735_draw_pixel(dev, x + 5, y + row, bg);
    }
}

void st7735_draw_string(st7735_handle_t *dev, const char *str, int16_t x, int16_t y, uint16_t color, uint16_t bg) {
    while (*str) {
        st7735_draw_char(dev, *str, x, y, color, bg);
        x += 6;  // 5 pixels character + 1 pixel spacing
        str++;
    }
}

int16_t st7735_center_x(const char *str) {
    int len = strlen(str);
    int total_width = len * 6; // 5 px per char + 1 px spacing
    return (ST7735_WIDTH - total_width) / 2;
}

int16_t st7735_center_y() {
    return (ST7735_HEIGHT - 8) / 2; // font height is 8 px
}


void st7735_deinit(st7735_handle_t *dev)
{
    if (dev->spi) {
        spi_bus_remove_device(dev->spi);
        dev->spi = NULL;
    }
    // Optionally: don't free SPI bus since other users may use it
}
