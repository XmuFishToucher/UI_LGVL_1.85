#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_

typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTED
} WIFI_STATE;

typedef void(*p_wifi_state_cb)(WIFI_STATE state);

void wifi_manager_init(p_wifi_state_cb f);

void wifi_manager_connect(const char* ssid, const char* password);

#endif
