idf_component_register(SRCS "attack_dos.c" "attack_method.c" "main.c" "attack_handshake.c" "attack_pmkid.c" "attack.c" "st7735.c" "font5x7.c"
                    INCLUDE_DIRS .
                    REQUIRES driver esp_wifi wifi_controller esp_timer frame_analyzer webserver wsl_bypasser pcap_serializer hccapx_serializer esp_lcd)