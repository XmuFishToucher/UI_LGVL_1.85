# UI_LGVL_1.85 项目实现总结

日期：2026-06-12

## 1. 项目实现目标

本项目当前目标是实现两块 ESP 设备之间的数据采集、云端转发、远端显示和放电控制。

整体角色：

```text
device_A：本地采集端，采集 47 点阵列数据并上报 OneNET
OneNET：物模型数据平台，接收 device_A 上报并触发规则引擎
本地 Python 转发服务：接收 OneNET HTTP 推送，再调用 OneNET API 下发给 device_B
device_B：远端显示和放电控制端，接收完整阵列数据并更新 UI，同时输出 UART 控制数据
STM/H723：接收 device_B UART 数据，执行后级放电或阵列处理逻辑
```

当前设计目标：

- `device_A` 上报完整 47 点阵列 `matrix_data`。
- 同时保留 `max_tx_idx` 和 `max_tx_value`，兼容旧的最大点链路。
- `device_B` 使用完整阵列数据显示远端矩阵。
- 放电控制目前以 UART 输出为准。当前代码优先完整阵列输出；无阵列时回退最大点输出。
- 本地 Python 服务负责过滤 OneNET 乱序、重复和旧帧，降低转发压力。

## 2. 当前关键文件

项目目录：

```text
D:\Documents\ESP_Projects\UI_LGVL_1.85
```

关键文件：

```text
main/main.c
```

- `device_B` 主流程。
- 初始化 LCD、LVGL、UART、WiFi、MQTT。
- 接收本地 UART 完整阵列帧。
- 更新本地显示层和远端显示层。
- 提供 STIM 开关状态、ZERO 校准、矩阵更新回调。

```text
components/BSP/IOT/onenet_mqtt.c
components/BSP/IOT/onenet_mqtt.h
```

- `device_A` 分支：负责 OneNET 物模型属性上报。
- `device_B` 分支：负责 OneNET 属性下发接收、解析和处理。
- 当前 `device_B` 可解析 `source_id`、`frame_id`、`max_tx_idx`、`max_tx_value`、`matrix_data[47]`。

```text
tools/onenet_forward_server.py
```

- 本地 HTTP 转发服务。
- 接收 OneNET 规则引擎 HTTP POST。
- 调用 OneNET `set-device-property` API，把数据下发给 `device_B`。
- 当前 active 转发默认间隔为 `200ms`。

```text
components/BSP/UART/uart.c
components/BSP/UART/uart.h
```

- UART2。
- TXD：GPIO43。
- RXD：GPIO44。
- 波特率：115200，8N1。

```text
components/BSP/LVGL/ui_matrix.c
components/BSP/LVGL/ui_matrix.h
components/BSP/LVGL/lvgl_ui.c
```

- 47 点手部矩阵 UI。
- 本地数据和远端数据分层显示。
- STIM 开关已移动到 ZERO 按钮上方。

## 3. OneNET 物模型字段

当前使用的物模型字段：

```text
source_id      string            数据来源，例如 device_A
frame_id       int32             帧序号，用于过滤旧帧和重复帧
max_tx_idx     int32  0-46       最大值通道
max_tx_value   int32  0-65535    最大通道值
matrix_data    array<int32>[47]  47 点完整阵列
```

典型上报 payload：

```json
{
  "id": "1",
  "version": "1.0",
  "params": {
    "source_id": { "value": "device_A" },
    "frame_id": { "value": 202 },
    "max_tx_idx": { "value": 15 },
    "max_tx_value": { "value": 83 },
    "matrix_data": {
      "value": [10, 10, 15, 12, 11, 11, 9, 12, 13, 14, 10, 15, 12, 14, 10, 83, 16, 12, 13, 12, 14, 11, 12, 13, 7, 12, 10, 12, 12, 13, 14, 15, 13, 11, 12, 13, 10, 10, 9, 11, 9, 9, 10, 7, 9, 10, 13]
    }
  }
}
```

## 4. device_A 当前发送时序

`device_A` 采集本地 47 点数据后，会计算最大通道和最大值，并按状态机上报 OneNET。

当前时序：

```text
无信号：
  最大值 < 20
  不发送

首次有信号：
  最大值 >= 20
  立即发送一帧

active 状态：
  最大值 > 15
  按节流间隔发送

信号变弱：
  最大值 <= 15
  不发送低值过渡帧
  连续 3 次确认后发送一次清零帧

清零后：
  回到静默，等待下一次触发
```

清零帧内容：

```text
max_tx_idx = 0
max_tx_value = 0
matrix_data = 47 个 0
```

## 5. OneNET 到本地转发链路

OneNET 规则引擎监听 `device_A` 属性上报，命中后通过 HTTP 推送到本地服务。

当前链路：

```text
device_A
  -> OneNET MQTT property/post
  -> OneNET 规则引擎
  -> cpolar 公网 URL
  -> 本地 Python 服务 /onenet/forward
  -> OneNET set-device-property API
  -> device_B MQTT property/set
```

cpolar 的作用：

```text
把公网 HTTPS 地址映射到本地电脑 http://127.0.0.1:3000
```

Python 脚本的作用：

```text
接收 OneNET HTTP POST
解包 msg
提取 source_id/frame_id/max/matrix_data
过滤错误来源、旧帧、重复帧、乱序帧
调用 OneNET API 下发给 device_B
```

当前转发服务关键参数：

```text
FORWARD_ACTIVE_MIN_MS=200
FORWARD_CLEAR_MIN_MS=1000
FORWARD_ACCEPT_PAST_MS=5000
FORWARD_LOG_SKIPS=0
FORWARD_LOG_REQUESTS=0
```

启动模板：

```cmd
cd /d D:\Documents\ESP_Projects\UI_LGVL_1.85

set "FORWARD_SHARED_TOKEN=deviceA2B2026"
set "FORWARD_REQUIRE_POST_TOKEN=0"
set "FORWARD_ACTIVE_MIN_MS=200"
set "FORWARD_CLEAR_MIN_MS=1000"
set "FORWARD_ACCEPT_PAST_MS=5000"
set "FORWARD_LOG_SKIPS=0"
set "FORWARD_LOG_REQUESTS=0"

set "ONENET_SET_PROPERTY_URL=https://iot-api.heclouds.com/thingmodel/set-device-property"
set "ONENET_AUTH_HEADER_NAME=authorization"
set "ONENET_AUTH_HEADER_VALUE=<新的 authorization 完整内容>"

python tools\onenet_forward_server.py
```

注意：Windows CMD 中 authorization 必须使用 `set "NAME=value"` 格式，否则 `&` 会被当作命令分隔符。

## 6. device_B 收到下发数据后的动作

`device_B` 订阅：

```text
$sys/{product_id}/{device_B}/thing/property/set
```

收到属性设置后执行：

```text
1. 检查 source_id 是否为 device_A
2. 解析 frame_id
3. 解析 max_tx_idx/max_tx_value
4. 解析 matrix_data[47]
5. 使用 frame_id 过滤旧帧和重复帧
6. 判断是否清零
7. 更新 UI
8. 根据 STIM 状态决定是否输出 UART
9. 回复 property/set_reply
```

清零条件：

```text
max_tx_value == 0
或 matrix_data 全 0
```

当前 UI 逻辑：

```text
有 matrix_data：
  使用完整 47 点阵列更新远端显示层

无 matrix_data：
  使用 max_tx_idx/max_tx_value 单点更新远端显示层
```

当前 UART/放电逻辑：

```text
STIM 关闭：
  不输出 active 数据
  关闭时发送清零帧

STIM 开启且有 matrix_data：
  输出完整 99 字节阵列帧

STIM 开启但无 matrix_data：
  输出旧 6 字节最大点帧

清零或超时：
  输出完整 99 字节全 0 阵列帧
```

## 7. UART 协议

### 7.1 旧最大点协议

6 字节：

```text
byte0 = max_tx_idx
byte1 = max_tx_value low byte
byte2 = max_tx_value high byte
byte3 = byte0 + byte1 + byte2
byte4 = 0x0D
byte5 = 0x0A
```

示例：

```text
2E DA 00 08 0D 0A
```

含义：

```text
idx = 46
value = 218
checksum = 0x2E + 0xDA + 0x00 = 0x108 -> 0x08
```

### 7.2 完整阵列协议

99 字节：

```text
AA 55
+ 47 个 uint16 little-endian
+ 1 字节 checksum
+ 0D 0A
```

长度：

```text
2 + 47 * 2 + 1 + 2 = 99
```

checksum：

```text
47 个点的所有低/高字节累加，取 uint8_t 低 8 位
```

此协议与旧 H723 项目中的 `send_full_array()` 兼容。

## 8. 技术栈

```text
ESP-IDF
FreeRTOS
LVGL
OneNET 物模型 MQTT
OneNET 规则引擎 HTTP 推送
Python 3 HTTP 转发服务
cpolar 内网穿透
UART 115200 8N1
ST77916 LCD
CST816 Touch
STM32H723 旧放电/采集项目
```

## 9. 重要决策

- 使用 OneNET 物模型属性而不是自定义 topic，是为了兼容 OneNET 规则引擎和属性下发 API。
- `device_A` 同时上报最大点和完整阵列，兼容旧链路并支持新显示。
- `device_B` 显示优先使用完整阵列。
- `frame_id` 是过滤乱序和重复帧的主依据。
- Python 转发 active 默认节流调整为 `200ms`，用于平衡实时性和 OneNET API 压力。
- cpolar + 本地 Python 是开发调试方案，不是最终产品化部署方案。

## 10. 已知问题

### 10.1 OneNET 链路延迟和抖动

完整链路较长：

```text
device_A -> OneNET -> cpolar -> Python -> OneNET API -> device_B
```

可能出现：

```text
乱序推送
重复推送
HTTPS 握手超时
SSL unexpected EOF
几百毫秒到数秒延迟
```

### 10.2 authorization token 过期

过期错误：

```text
code=10403
authentication failed
request has expired
```

需要重新从 OneNET API 调试页复制 `authorization`。

### 10.3 device_B 不在线

错误：

```text
code=10411
属性设置失败:设备不在线
```

需要确认 `device_B` 串口出现：

```text
MQTT_EVENT_CONNECTED
MQTT_EVENT_SUBSCRIBED
```

### 10.4 硬件上电和串口连接问题

已观察到：

```text
只接电源正常
TX/RX/GND 连接或显示器电源带 STM 时可能导致初始化失败
```

建议：

```text
STM 单独稳压
STM NRST 加 RC 延时或复位芯片
UART TX/RX 串 1k~4.7k
如果只需单向控制，先不接 STM TX -> ESP RX
高压/放电地和数字地单点连接
示波器检查 STM 3.3V 上电波形
```

## 11. 后续优化方向

### 11.1 转发服务 latest-only 队列

当前 Python 服务仍是收到请求后调用 OneNET API。后续可改为：

```text
HTTP 收包只保存最新 frame_id
后台单线程按固定周期下发最新帧
旧帧直接丢弃
清零帧优先下发
```

这样可以降低 HTTPS 并发和请求堆积。

### 11.2 拆分最大点和完整阵列

如果放电需要更低延迟，可以拆成两类下发：

```text
高频小包：max_tx_idx/max_tx_value
低频大包：matrix_data[47]
```

### 11.3 改用公网 MQTT broker

当前链路不适合极低延迟。如果要进一步降低延迟，建议改为：

```text
device_A -> MQTT broker -> device_B
```

可选：

```text
EMQX
Mosquitto
云厂商 IoT/MQTT 服务
```

这样可以绕开 OneNET 规则引擎和 set-device-property API。
