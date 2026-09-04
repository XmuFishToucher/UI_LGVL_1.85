set(src_dirs
            EXIO
            UART
            I2C_Driver
            IOT
            LCD_Driver
            LVGL
            Touch_Driver)

set(include_dirs
            EXIO
            UART
            I2C_Driver
            IOT
            LCD_Driver
            LVGL
            Touch_Driver)

set(requires
            driver
            lvgl
            esp_lvgl_port
            esp_lcd_touch_cst816s
            esp_lcd_st77916
            esp_timer
            esp_adc
            esp_wifi
            esp_netif
            nvs_flash 
            mqtt
            json)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

set(ONENET_DEVICE_NAME_OVERRIDE "" CACHE STRING
    "Override the default OneNET device name for receiver firmware builds")
if(ONENET_DEVICE_NAME_OVERRIDE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        ONENET_DEVICE_NAME=\"${ONENET_DEVICE_NAME_OVERRIDE}\")
endif()

component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)
