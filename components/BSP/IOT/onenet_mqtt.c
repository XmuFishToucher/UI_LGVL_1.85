#include "onenet_mqtt.h"
#include "mqtt_client.h"
#include "onenet_token.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cJSON.h"
//#include "onenet_dm.h"

#define TAG "onenet_mqtt"

static esp_mqtt_client_handle_t hqtt_handle = NULL;

//static void onenet_property_ask(const char* id, int code, const char* msg);

//static void onenet_subscribe(void);

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        // onenet_subscribe();
        // cJSON *property = onenet_property_upload();
        // char *data = cJSON_PrintUnformatted(property);
        // onenet_post_property_data(data);
        // cJSON_Delete(property);
        // cJSON_free(data);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        // if(strstr(event->topic, "property/set") != NULL)
        // {
        //     cJSON *property = cJSON_ParseWithLength(event->data, event->data_len);
        //     onenet_property_handle(property);
        //     onenet_property_ask(cJSON_GetObjectItem(property, "id")->valuestring, 200, "success");
        //     cJSON_Delete(property);
        // }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

esp_err_t onenet_start(void)
{
    esp_mqtt_client_config_t mqtt_config;
    memset(&mqtt_config, 0, sizeof(esp_mqtt_client_config_t));
    mqtt_config.broker.address.uri = "mqtt://mqtts.heclouds.com";        
    mqtt_config.broker.address.port = 1883;

    mqtt_config.credentials.client_id = ONENET_DEVICE_NAME;
    mqtt_config.credentials.username = ONENET_PRODUCT_ID;

    static char token[256];
    dev_token_generate(token, SIG_METHOD_SHA256, 2074859482, ONENET_PRODUCT_ID, ONENET_DEVICE_NAME, ONENET_PRODUCT_ACCESS_KEY);
    mqtt_config.credentials.authentication.password = token;

    hqtt_handle = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_register_event(hqtt_handle, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    return esp_mqtt_client_start(hqtt_handle);

}

// static void onenet_property_ask(const char* id, int code, const char* msg)
// {
//     if(hqtt_handle)
//     {
//         char topic[128];
//         snprintf(topic, 128, "$sys/%s/%s/thing/property/set_reply", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
//         cJSON *response = cJSON_CreateObject();
//         cJSON_AddStringToObject(response, "id", id);
//         cJSON_AddNumberToObject(response, "code", code);
//         cJSON_AddStringToObject(response, "msg", msg);
//         char* response_str = cJSON_PrintUnformatted(response);
//         esp_mqtt_client_publish(hqtt_handle, topic, response_str, strlen(response_str), 1, 0);
//         cJSON_Delete(response);
//         cJSON_free(response_str);
//     }
// }

// static void onenet_subscribe(void)
// {
//     if(hqtt_handle)
//     {
//         char topic[128];
//         snprintf(topic, 128, "$sys/%s/%s/thing/property/post/reply", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
//         esp_mqtt_client_subscribe_single(hqtt_handle, topic, 1);

//         snprintf(topic, 128, "$sys/%s/%s/thing/property/set", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
//         esp_mqtt_client_subscribe_single(hqtt_handle, topic, 1);
//     }
// }

// esp_err_t onenet_post_property_data(const char* data)
// {
//     if(hqtt_handle)
//     {
//         char topic[128];
//         snprintf(topic, 128, "$sys/%s/%s/thing/property/post", ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
//         ESP_LOGI(TAG, "Upload topic:%s,payload:%s", topic, data);
//         esp_mqtt_client_publish(hqtt_handle, topic, data, strlen(data), 1, 0);
//         return ESP_OK;
//     }
//     return ESP_FAIL;
// }
