#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "wifi_manager"

#define MAX_CONNECT_RETRY 10

static int sta_connect_cnt = 0;

static bool is_sta_connected = false;

static p_wifi_state_cb wifi_callback = NULL;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if(event_base == WIFI_EVENT)
    {
        switch(event_id)
        {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                if(is_sta_connected)
                {
                    is_sta_connected = false;
                    if(wifi_callback)
                    {
                        wifi_callback(WIFI_STATE_DISCONNECTED);
                    }
                }
                if(sta_connect_cnt < MAX_CONNECT_RETRY)
                {
                    esp_wifi_connect();
                    sta_connect_cnt++;
                }
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "connected to ap");
                break;
            default:
                break;
        }
    }
    else if(event_base == IP_EVENT)
    {
        if(event_id == IP_EVENT_STA_GOT_IP)
        {
            ESP_LOGI(TAG, "got ip");
            is_sta_connected = true;
            if(wifi_callback)
            {
                wifi_callback(WIFI_STATE_CONNECTED);
            }
        }
    }
}

void wifi_manager_init(p_wifi_state_cb f)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));

    wifi_callback = f;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_start() );
}

void wifi_manager_connect(const char* ssid, const char* password)
{
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    snprintf((char*)wifi_config.sta.ssid, 32, "%s", ssid);
    snprintf((char*)wifi_config.sta.password, 64, "%s", password);

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if(mode != WIFI_MODE_STA)
    {
        esp_wifi_stop();
        esp_wifi_set_mode(WIFI_MODE_STA);

    }
    sta_connect_cnt = 0;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_start();
}