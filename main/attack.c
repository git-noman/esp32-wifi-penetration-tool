/**
 * @file attack.c
 * @author risinek (risinek@gmail.com)
 * @date 2021-04-02
 * @copyright Copyright (c) 2021
 * 
 * @brief Implements common attack wrapper.
 */

 #include "attack.h"

 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
 #include "esp_log.h"
 #include "esp_err.h"
 #include "esp_event.h"
 #include "esp_timer.h"
 
 #include "attack_pmkid.h"
 #include "attack_handshake.h"
 #include "attack_dos.h"
 #include "webserver.h"
 #include "wifi_controller.h"
 #include "driver/gpio.h"
 #include "display.h"

 #define BLUE_LED_PIN 2   // change this if your board uses a different GPIO

 
 static const char* TAG = "attack";
 static attack_status_t attack_status = { .state = READY, .type = -1, .content_size = 0, .content = NULL };
 static esp_timer_handle_t attack_timeout_handle;
 
 const attack_status_t *attack_get_status() {
     return &attack_status;
 }
 
 void attack_update_status(attack_state_t state) {
     attack_status.state = state;
     if(state == FINISHED) {
         ESP_LOGD(TAG, "Stopping attack timeout timer");
         ESP_ERROR_CHECK(esp_timer_stop(attack_timeout_handle));
         gpio_set_level(BLUE_LED_PIN, 0); // turn off LED
     } 
 }
 
 void attack_append_status_content(uint8_t *buffer, unsigned size){
     if(size == 0){
         ESP_LOGE(TAG, "Size can't be 0 if you want to reallocate");
         return;
     }
     // temporarily save new location in case of realloc failure to preserve current content
     char *reallocated_content = realloc(attack_status.content, attack_status.content_size + size);
     if(reallocated_content == NULL){
         ESP_LOGE(TAG, "Error reallocating status content! Status content may not be complete.");
         return;
     }
     // copy new data after current content
     memcpy(&reallocated_content[attack_status.content_size], buffer, size);
     attack_status.content = reallocated_content;
     attack_status.content_size += size;
 }
 
 char *attack_alloc_result_content(unsigned size) {
     attack_status.content_size = size;
     attack_status.content = (char *) malloc(size);
     return attack_status.content;
 }
 
 /**
  * @brief Callback function for attack timeout timer.
  * 
  * This function is called when attack times out. 
  * It updates attack status state to TIMEOUT.
  * It calls appropriate abort functions based on current attack type.
  * @param arg not used.
  */
 static void attack_timeout(void* arg){
     ESP_LOGD(TAG, "Attack timed out");
     
     attack_update_status(TIMEOUT);
 
     switch(attack_status.type) {
         case ATTACK_TYPE_PMKID:
             ESP_LOGI(TAG, "Aborting PMKID attack...");
             attack_pmkid_stop();
             break;
         case ATTACK_TYPE_HANDSHAKE:
             ESP_LOGI(TAG, "Abort HANDSHAKE attack...");
             attack_handshake_stop();
             break;
         case ATTACK_TYPE_PASSIVE:
             ESP_LOGI(TAG, "Abort PASSIVE attack...");
             break;
         case ATTACK_TYPE_DOS:
             ESP_LOGI(TAG, "Abort DOS attack...");
             attack_dos_stop();
             break;
         default:
             ESP_LOGE(TAG, "Unknown attack type. Not aborting anything");
     }
 }
 
 /**
  * @brief Callback for WEBSERVER_EVENT_ATTACK_REQUEST event.
  * 
  * This function handles WEBSERVER_EVENT_ATTACK_REQUEST event from event loop.
  * It parses attack_request_t structure and set initial values to attack_status.
  * It sets attack state to RUNNING.
  * It starts attack timeout timer.
  * It starts attack based on chosen type.
  * 
  * @param args not used
  * @param event_base expects WEBSERVER_EVENTS
  * @param event_id expects WEBSERVER_EVENT_ATTACK_REQUEST
  * @param event_data expects attack_request_t
  */
 static void attack_request_handler(void *args, esp_event_base_t event_base, int32_t event_id, void *event_data) {
     ESP_LOGI(TAG, "Starting attack...");
     gpio_set_level(BLUE_LED_PIN, 1); // turn on LED
     attack_request_t *attack_request = (attack_request_t *) event_data;
     attack_config_t attack_config = { .type = attack_request->type, .method = attack_request->method, .timeout = attack_request->timeout };
     attack_config.ap_record = wifictl_get_ap_record(attack_request->ap_record_id);
     
     attack_status.state = RUNNING;
     attack_status.type = attack_config.type;
 
     if(attack_config.ap_record == NULL){
         ESP_LOGE(TAG, "NPE: No attack_config.ap_record!");
         return;
     }
     // set timeout
     // if timeout is <= 0 and attack type is dos
     if(attack_config.timeout <= 0 && attack_config.type == ATTACK_TYPE_DOS) {
         // the maximum timeout value for esp_timer_start_once is UINT32_MAX microseconds
         // which is equivalent to approximately 4,294,967,295 microseconds
         // or about 4294 seconds (approximately 71.58 minutes).
         ESP_LOGD(TAG, "Attack will run indefinitely");
         ESP_ERROR_CHECK(esp_timer_start_once(attack_timeout_handle, UINT32_MAX));
     } else {
         ESP_LOGD(TAG, "Setting attack timeout to %d seconds", attack_config.timeout);
         ESP_ERROR_CHECK(esp_timer_start_once(attack_timeout_handle, attack_config.timeout * 1000000));
     }

     // turn display red
     st7735_fill_color(&tft, 0xF800);

     // start attack based on it's type
     switch(attack_config.type) {
         case ATTACK_TYPE_PMKID:
             attack_pmkid_start(&attack_config);
             break;
         case ATTACK_TYPE_HANDSHAKE:
             attack_handshake_start(&attack_config);
             break;
         case ATTACK_TYPE_PASSIVE:
             ESP_LOGW(TAG, "ATTACK_TYPE_PASSIVE not implemented yet!");
             break;
         case ATTACK_TYPE_DOS:
             st7735_draw_string(&tft, "dos attack", 10, 10, 0xFFFF, 0x0000);
             char buf[32];
             snprintf(buf, sizeof(buf), "timeout: %d", attack_config.timeout);
             st7735_draw_string(&tft, buf, 10, 20, 0xFFFF, 0x0000);

             st7735_draw_string(&tft, "target:", 10, 30, 0xFFFF, 0x0000);
             char buftwo[64];
             snprintf(buftwo, sizeof(buftwo), "%s", attack_config.ap_record->ssid);
             st7735_draw_string(&tft, buftwo, 10, 40, 0xFFFF, 0x0000);

             attack_dos_start(&attack_config);
             break;
         default:
             ESP_LOGE(TAG, "Unknown attack type!");
     }
 }
 
 /**
  * @brief Callback for WEBSERVER_EVENT_ATTACK_RESET event.
  * 
  * This callback resets attack status by freeing previously allocated status content and putting attack to READY state.
  * 
  * @param args not used
  * @param event_base expects WEBSERVER_EVENTS
  * @param event_id expects WEBSERVER_EVENT_ATTACK_RESET
  * @param event_data not used
  */
 static void attack_reset_handler(void *args, esp_event_base_t event_base, int32_t event_id, void *event_data) {
     ESP_LOGD(TAG, "Resetting attack status...");
     if(attack_status.content){
         free(attack_status.content);
         attack_status.content = NULL;
     }
     attack_status.content_size = 0;
     attack_status.type = -1;
     attack_status.state = READY;

     gpio_set_level(BLUE_LED_PIN, 0); // turn off LED
 }
 
 /**
  * @brief Initialises common attack resources.
  * 
  * Creates attack timeout timer.
  * Registers event loop event handlers.
  */

 void attack_init(){
     const esp_timer_create_args_t attack_timeout_args = {
         .callback = &attack_timeout
     };
     ESP_ERROR_CHECK(esp_timer_create(&attack_timeout_args, &attack_timeout_handle));
 
     ESP_ERROR_CHECK(esp_event_handler_register(WEBSERVER_EVENTS, WEBSERVER_EVENT_ATTACK_REQUEST, &attack_request_handler, NULL));
     ESP_ERROR_CHECK(esp_event_handler_register(WEBSERVER_EVENTS, WEBSERVER_EVENT_ATTACK_RESET, &attack_reset_handler, NULL));

     // Blue LED.
     gpio_reset_pin(BLUE_LED_PIN);
     gpio_set_direction(BLUE_LED_PIN, GPIO_MODE_OUTPUT);
     gpio_set_level(BLUE_LED_PIN, 0); // LED off by default
 }