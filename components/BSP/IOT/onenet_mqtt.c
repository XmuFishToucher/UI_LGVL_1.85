#include "onenet_mqtt.h"
#include "mqtt_client.h"
#include "onenet_token.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "cJSON.h"
#include "uart.h"

#define TAG "onenet_mqtt"
#define MQTT_SIGNAL_TIMEOUT_MS 1000
#define MATRIX_POINTS 47

static esp_mqtt_client_handle_t hqtt_handle = NULL;
static volatile uint32_t last_signal_ms = 0;
static volatile uint8_t matrix_is_active = 0;
static int last_frame_id = -1;

static void uart_forward_max_data(uint8_t max_tx_idx, uint16_t max_tx_value)
{
    uint8_t tx_buf[6];
    tx_buf[0] = max_tx_idx;
    tx_buf[1] = (uint8_t)(max_tx_value & 0xFF);
    tx_buf[2] = (uint8_t)((max_tx_value >> 8) & 0xFF);
    tx_buf[3] = tx_buf[0] + tx_buf[1] + tx_buf[2];
    tx_buf[4] = 0x0D;
    tx_buf[5] = 0x0A;

    int written = uart_send_data(tx_buf, sizeof(tx_buf));
    if (written != (int)sizeof(tx_buf)) {
        ESP_LOGW(TAG, "UART forward incomplete: written=%d expected=%d", written, (int)sizeof(tx_buf));
    }
}

static void uart_forward_matrix_data(const uint16_t *matrix_data)
{
    uint8_t tx_buf[2 + MATRIX_POINTS * 2 + 1 + 2];
    uint8_t sum = 0;

    tx_buf[0] = 0xAA;
    tx_buf[1] = 0x55;

    for (int i = 0; i < MATRIX_POINTS; i++) {
        uint16_t value = matrix_data ? matrix_data[i] : 0;
        tx_buf[2 + i * 2] = value & 0xFF;
        tx_buf[2 + i * 2 + 1] = (value >> 8) & 0xFF;
        sum += tx_buf[2 + i * 2];
        sum += tx_buf[2 + i * 2 + 1];
    }

    tx_buf[2 + MATRIX_POINTS * 2] = sum;
    tx_buf[3 + MATRIX_POINTS * 2] = 0x0D;
    tx_buf[4 + MATRIX_POINTS * 2] = 0x0A;

    int written = uart_send_data(tx_buf, sizeof(tx_buf));
    if (written != (int)sizeof(tx_buf)) {
        ESP_LOGW(TAG, "UART matrix forward incomplete: written=%d expected=%d", written, (int)sizeof(tx_buf));
    }
}

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

    char topic[128];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", "1");
    cJSON_AddStringToObject(root, "version", "1.0");
    cJSON *params = cJSON_CreateObject();

    // source_id
    cJSON *src = cJSON_CreateObject();
    cJSON_AddStringToObject(src, "value", ONENET_DEVICE_NAME);
    cJSON_AddItemToObject(params, "source_id", src);

    // max_tx_idx
    cJSON *ch = cJSON_CreateObject();
    cJSON_AddNumberToObject(ch, "value", channel);
    cJSON_AddItemToObject(params, "max_tx_idx", ch);

    // max_tx_value
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

static int parse_property_number(cJSON *params, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(params, key);
    if (!item) return -1;
    if (cJSON_IsNumber(item)) return item->valueint;

    cJSON *value = cJSON_GetObjectItem(item, "value");
    if (value && cJSON_IsNumber(value)) return value->valueint;

    return -1;
}

static cJSON *get_property_value(cJSON *params, const char *key)
{
    cJSON *item = cJSON_GetObjectItem(params, key);
    if (!item) return NULL;

    cJSON *value = cJSON_GetObjectItem(item, "value");
    return value ? value : item;
}

static bool parse_matrix_data(cJSON *params, uint16_t *out)
{
    cJSON *array = get_property_value(params, "matrix_data");
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) != MATRIX_POINTS) {
        return false;
    }

    for (int i = 0; i < MATRIX_POINTS; i++) {
        cJSON *item = cJSON_GetArrayItem(array, i);
        if (!cJSON_IsNumber(item)) {
            return false;
        }

        int value = item->valueint;
        if (value < 0) value = 0;
        if (value > 65535) value = 65535;
        out[i] = (uint16_t)value;
    }

    return true;
}

static void onenet_property_handle(cJSON *property)
{
    cJSON *params = cJSON_GetObjectItem(property, "params");
    if (!params) return;

    // 解析 source_id
    cJSON *src = cJSON_GetObjectItem(params, "source_id");
    cJSON *src_val = src && cJSON_IsString(src) ? src : NULL;
    if (!src_val && src) {
        src_val = cJSON_GetObjectItem(src, "value");
    }
    if (!src_val || !src_val->valuestring) {
        ESP_LOGW(TAG, "Ignore property set without source_id");
        return;
    }

    ESP_LOGI(TAG, "Data from: %s", src_val->valuestring);
    if (strcmp(src_val->valuestring, ONENET_EXPECTED_SOURCE_ID) != 0) {
        ESP_LOGW(TAG, "Ignore property set from unexpected source: %s", src_val->valuestring);
        return;
    }

    int channel = parse_property_number(params, "max_tx_idx");
    int value = parse_property_number(params, "max_tx_value");
    int frame_id = parse_property_number(params, "frame_id");
    uint16_t matrix_data[MATRIX_POINTS];
    bool has_matrix_data = parse_matrix_data(params, matrix_data);

    if (frame_id >= 0) {
        if (last_frame_id >= 0 && frame_id <= last_frame_id) {
            ESP_LOGW(TAG, "Ignore stale frame_id: current=%d last=%d", frame_id, last_frame_id);
            return;
        }
        last_frame_id = frame_id;
    }

    if (value == 0) {
        ESP_LOGI(TAG, "Clear max data");
        matrix_is_active = 0;
        last_signal_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        matrix_clear_from_mqtt();
        uart_forward_matrix_data(NULL);
    } else if (channel >= 0 && channel < 47 && value > 0) {
        ESP_LOGI(TAG, "Apply max data: channel=%d value=%d", channel, value);
        matrix_is_active = 1;
        last_signal_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (has_matrix_data) {
            matrix_update_all_from_mqtt(matrix_data);
            uart_forward_matrix_data(matrix_data);
        } else {
            matrix_update_from_mqtt((uint8_t)channel, (uint16_t)value);
            uart_forward_max_data((uint8_t)channel, (uint16_t)value);
        }
    } else {
        ESP_LOGW(TAG, "Ignore invalid max data: channel=%d value=%d", channel, value);
    }
}

static void mqtt_signal_timeout_task(void *arg)
{
    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (matrix_is_active && last_signal_ms != 0 && now - last_signal_ms > MQTT_SIGNAL_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Signal timeout, clear matrix");
            matrix_is_active = 0;
            matrix_clear_from_mqtt();
            uart_forward_matrix_data(NULL);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
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
    BaseType_t task_ret = xTaskCreate(mqtt_signal_timeout_task, "mqtt_signal_timeout", 6144, NULL, 4, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create mqtt_signal_timeout task: %d", task_ret);
    }
    return esp_mqtt_client_start(hqtt_handle);
}
