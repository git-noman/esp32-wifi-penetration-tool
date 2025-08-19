/**
 * @file main.c
 * @author risinek (risinek@gmail.com)
 * @date 2021-04-03
 * @copyright Copyright (c) 2021
 * 
 * @brief Main file used to setup ESP32 into initial state
 * 
 * Starts management AP and webserver  
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_event.h"

#include "attack.h"
#include "wifi_controller.h"
#include "webserver.h"
#include "st7735.h"

static const char* TAG = "main";

void app_main(void)
{
    ESP_LOGD(TAG, "app_main started");
    st7735_handle_t tft;
    memset(&tft, 0, sizeof(tft));

    esp_err_t r = st7735_init(&tft);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "ST7735 init failed: %d", r);
        return;
    }

    st7735_fill_color(&tft, 0xF800);

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifictl_mgmt_ap_start();
    attack_init();
    webserver_run();
}
