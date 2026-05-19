#include "onenet_mqtt.h"
#include "mqtt_client.h"
#include "onenet_token.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cJSON.h"

#define TAG "onenet_mqtt"

static esp_mqtt_client_handle_t hqtt_handle = NULL;

// ==================== 传感器数据处理 ====================

sensor_peak_t find_max_channel(const uint16_t *data)
{
    sensor_peak_t peak = {0, 0};
    for (int i = 0; i < 47; i++) {
        if (data[i] > peak.intensity) {
            peak.intensity = data[i];
            peak.channel = i;
        }
    }
    return peak;
}

static esp_err_t onenet_post_max_data(uint8_t channel, uint16_t value)
{
    if (!hqtt_handle) return ESP_FAIL;

    // ===== 物模型上报 (发给 OneNET 平台) =====
    char topic[128];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", "1");
    cJSON_AddStringToObject(root, "version", "1.0");
    cJSON *params = cJSON_CreateObject();

    cJSON *src = cJSON_CreateObject();
    cJSON_AddStringToObject(src, "value", ONENET_DEVICE_NAME);
    cJSON_AddItemToObject(params, "source_id", src);

    cJSON *ch = cJSON_CreateObject();
    cJSON_AddNumberToObject(ch, "value", channel);
    cJSON_AddItemToObject(params, "max_tx_idx", ch);

    cJSON *val = cJSON_CreateObject();
    cJSON_AddNumberToObject(val, "value", value);
    cJSON_AddItemToObject(params, "max_tx_value", val);

    cJSON_AddItemToObject(root, "params", params);

    char *data = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "Publish: %s", data);
    esp_mqtt_client_publish(hqtt_handle, topic, data, strlen(data), 1, 0);
    cJSON_Delete(root);
    cJSON_free(data);

    return ESP_OK;
}

// 阈值过滤 + 100ms 限流 + MQTT 发布
void sensor_publish_max_channel(const uint16_t *data)
{
    static uint32_t last_publish_ms = 0;

    sensor_peak_t peak = find_max_channel(data);

    if (peak.intensity < 20) return;

    // 100ms 限流
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - last_publish_ms < 100) {
        return;
    }
    last_publish_ms = now;

    onenet_post_max_data(peak.channel, peak.intensity);
}

// ==================== 属性处理 (接收对端数据) ====================

static void onenet_property_handle(cJSON *property)
{
    cJSON *params = cJSON_GetObjectItem(property, "params");
    if (!params) return;

    // 解析 source_id
    cJSON *src = cJSON_GetObjectItem(params, "source_id");
    if (src) {
        cJSON *src_val = cJSON_GetObjectItem(src, "value");
        if (src_val && src_val->valuestring) {
            ESP_LOGI(TAG, "Data from: %s", src_val->valuestring);
        }
    }

    // 解析 max_tx_idx
    cJSON *ch = cJSON_GetObjectItem(params, "max_tx_idx");
    if (ch) {
        cJSON *ch_val = cJSON_GetObjectItem(ch, "value");
        if (ch_val) {
            int channel = (int)cJSON_GetNumberValue(ch_val);
            ESP_LOGI(TAG, "Received max_tx_idx: %d", channel);
        }
    }

    // 解析 max_tx_value
    cJSON *val = cJSON_GetObjectItem(params, "max_tx_value");
    if (val) {
        cJSON *val_val = cJSON_GetObjectItem(val, "value");
        if (val_val) {
            int value = (int)cJSON_GetNumberValue(val_val);
            ESP_LOGI(TAG, "Received max_tx_value: %d", value);
        }
    }
}

static void onenet_property_ask(const char *id, int code, const char *msg)
{
    if (!hqtt_handle) return;

    char topic[128];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set_reply",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "id", id);
    cJSON_AddNumberToObject(response, "code", code);
    cJSON_AddStringToObject(response, "msg", msg);
    char *response_str = cJSON_PrintUnformatted(response);
    esp_mqtt_client_publish(hqtt_handle, topic, response_str, strlen(response_str), 1, 0);
    cJSON_Delete(response);
    cJSON_free(response_str);
}

static void onenet_subscribe(void)
{
    if (!hqtt_handle) return;

    char topic[128];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(hqtt_handle, topic, 1);

    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post/reply",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(hqtt_handle, topic, 1);
}

// ==================== MQTT 事件处理 ====================

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        onenet_subscribe();
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
        if (strstr(event->topic, "property/set") != NULL) {
            cJSON *property = cJSON_ParseWithLength(event->data, event->data_len);
            if (property) {
                onenet_property_handle(property);
                cJSON *id_item = cJSON_GetObjectItem(property, "id");
                if (id_item && id_item->valuestring) {
                    onenet_property_ask(id_item->valuestring, 200, "success");
                }
                cJSON_Delete(property);
            }
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

// ==================== 启动 OneNET MQTT ====================

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
    ESP_LOGI(TAG, "Token: %s", token);
    mqtt_config.credentials.authentication.password = token;

    hqtt_handle = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_register_event(hqtt_handle, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    return esp_mqtt_client_start(hqtt_handle);
}
