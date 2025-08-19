#include "st7735.h"

st7735_handle_t tft;

void display_init(void) {
    st7735_init(&tft);
    st7735_fill_color(&tft, 0x07E0);
    st7735_draw_string_scaled(&tft, "esp32-wifi-tool", 10, 10, 0xFFFF, 0x0000, 1.5);
}
