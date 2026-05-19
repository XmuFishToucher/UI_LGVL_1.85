# OneNET 数据流转记录

本文档记录当前 `device_A -> OneNET -> 本地转发服务 -> OneNET -> device_B` 的数据流转方案。

## 目标

`device_A` 上报当前最强通道信息：

```json
{
  "id": "1",
  "version": "1.0",
  "params": {
    "source_id": { "value": "device_A" },
    "max_tx_idx": { "value": 46 },
    "max_tx_value": { "value": 218 }
  }
}
```

OneNET 根据规则筛选来自 `device_A` 的消息，通过 cpolar 推送到本地 HTTP 转发服务；本地转发服务再调用 OneNET 的“设置设备属性”API，把 `max_tx_idx`、`max_tx_value` 和 `source_id` 设置给 `device_B`。`device_B` 收到 `thing/property/set` 后点亮对应通道。

## 设备端代码改动

### master / device_A

提交：

```text
d279aba refactor-device-a: use property post only
```

改动内容：

- 删除自定义 topic 直发逻辑。
- `device_A` 现在只向 OneNET 物模型属性上报 topic 发布数据：

```text
$sys/{product_id}/{device_name}/thing/property/post
```

- 上报属性：

```text
source_id = device_A
max_tx_idx = 最大值通道号
max_tx_value = 最大值强度
```

### deviceB / device_B

提交：

```text
920a63d refactor-device-b: receive property set from platform
```

改动内容：

- 删除自定义 topic 订阅和解析逻辑。
- `device_B` 现在只接收 OneNET 平台下发的属性设置消息：

```text
$sys/{product_id}/{device_name}/thing/property/set
```

- 增加来源过滤：

```c
#define ONENET_EXPECTED_SOURCE_ID "device_A"
```

- `device_B` 只有在以下条件满足时才更新通道：

```text
source_id == device_A
0 <= max_tx_idx < 47
max_tx_value >= 0
```

- 将 LCD/LVGL 初始化移动到 MQTT 启动前，避免属性设置消息到达时 UI 还没有准备好。

## OneNET 规则引擎

SQL 调试消息体应使用类似结构：

```json
{
  "deviceId": 123,
  "productId": "8x5w9DD3Av",
  "deviceName": "device_A",
  "messageType": "notify",
  "notifyType": "property",
  "data": {
    "id": "1",
    "version": "1.0",
    "params": {
      "source_id": {
        "value": "device_A",
        "time": 1524448722123
      },
      "max_tx_idx": {
        "value": 7,
        "time": 1524448722123
      },
      "max_tx_value": {
        "value": 22,
        "time": 1524448722123
      }
    }
  }
}
```

已验证可用的 SQL：

```sql
select * from notify where data.params.source_id.value = "device_A"
```

注意：OneNET SQL 调试页面里，字符串值需要使用双引号。

## HTTP 推送实例

规则动作使用 HTTP 推送实例。

示例配置：

```text
实例名称: deviceA_to_deviceB_http
URL: https://<cpolar-domain>/onenet/forward
token: deviceA2B2026
消息加密方式: 明文模式
```

免费 cpolar 域名重启后可能变化。只要 cpolar 域名变化，就需要同步更新 OneNET HTTP 推送实例里的 URL。

## 本地转发服务

脚本：

```text
tools/onenet_forward_server.py
```

在 CMD 中启动：

```cmd
cd /d D:\Documents\ESP_Projects\UI_LGVL_1.85
set FORWARD_SHARED_TOKEN=deviceA2B2026
set ONENET_SET_PROPERTY_URL=https://iot-api.heclouds.com/thingmodel/set-device-property
set ONENET_AUTH_HEADER_NAME=authorization
set ONENET_AUTH_HEADER_VALUE=<OneNET authorization token>
python tools\onenet_forward_server.py
```

另开一个终端启动 cpolar：

```cmd
cpolar http 3000
```

然后将 OneNET HTTP 推送 URL 设置为：

```text
https://<cpolar-domain>/onenet/forward
```

脚本也支持 dry-run 模式。如果没有设置 `ONENET_SET_PROPERTY_URL`，脚本不会真正调用 OneNET API，而是打印准备下发给 `device_B` 的请求内容：

```text
DRY-RUN set device_B: {"product_id":"8x5w9DD3Av","device_name":"device_B","params":{"source_id":"device_A","max_tx_idx":46,"max_tx_value":218}}
```

## OneNET 设置设备属性 API

从 OneNET API 调试页面确认到的接口：

```text
POST https://iot-api.heclouds.com/thingmodel/set-device-property
```

请求头：

```text
authorization: version=2022-05-01&res=userid%2F521020&et=...&method=sha1&sign=...
```

请求体：

```json
{
  "product_id": "8x5w9DD3Av",
  "device_name": "device_B",
  "params": {
    "source_id": "device_A",
    "max_tx_idx": 46,
    "max_tx_value": 218
  }
}
```

已知成功的 API 调试响应：

```json
{
  "code": 0,
  "data": {
    "code": 200,
    "id": "3",
    "msg": "success"
  },
  "msg": "succ"
}
```

`authorization` token 里有过期时间字段 `et`。过期后需要重新生成或从 OneNET API 调试页面复制新的 token。

## 已验证节点

- cpolar URL 校验成功。
- OneNET 规则 SQL 调试有输出。
- OneNET HTTP 推送可以到达本地 Python 服务。
- Python 服务可以解析 OneNET 外层 `msg` 包裹的消息体。
- dry-run 转发可以生成预期的 `device_B` 属性设置请求体。
- OneNET API 调试中，设置 `device_B` 属性返回成功。

## 每次从零启动流程

下面这部分是每次重新开始测试时按顺序执行的完整流程。

### 1. 启动本地转发服务

打开一个 CMD 窗口，进入项目目录：

```cmd
cd /d D:\Documents\ESP_Projects\UI_LGVL_1.85
```

设置 OneNET HTTP 推送校验 token：

```cmd
set FORWARD_SHARED_TOKEN=deviceA2B2026
```

如果只是验证 OneNET 能不能推到本地服务，可以不设置 OneNET 属性设置 API，此时脚本会进入 dry-run 模式：

```cmd
python tools\onenet_forward_server.py
```

看到下面日志表示本地服务启动成功：

```text
listening on http://127.0.0.1:3000/onenet/forward
ONENET_SET_PROPERTY_URL is not set; running in dry-run mode
```

如果要真正下发到 `device_B`，需要设置 OneNET API 地址和鉴权 token 后再启动：

```cmd
set ONENET_SET_PROPERTY_URL=https://iot-api.heclouds.com/thingmodel/set-device-property
set ONENET_AUTH_HEADER_NAME=authorization
set ONENET_AUTH_HEADER_VALUE=<从OneNET API调试页复制的authorization>
python tools\onenet_forward_server.py
```

注意：`ONENET_AUTH_HEADER_VALUE` 里的 token 有过期时间，过期后需要重新从 OneNET API 调试页复制。

### 2. 启动 cpolar 内网穿透

再打开一个新的 CMD 窗口，执行：

```cmd
cpolar http 3000
```

cpolar 会输出类似：

```text
Forwarding          https://d29d5ae.r20.cpolar.top -> http://localhost:3000
```

复制 `https://...` 这一行的 HTTPS 地址。

免费 cpolar 地址每次重启可能变化。只要地址变化，就必须回 OneNET 修改 HTTP 推送实例 URL。

### 3. 更新 OneNET HTTP 推送实例

进入 OneNET 的 HTTP 推送实例，例如：

```text
deviceA_to_deviceB_http
```

填写：

```text
URL: https://<cpolar-domain>/onenet/forward
token: deviceA2B2026
消息加密方式: 明文模式
```

示例：

```text
https://d29d5ae.r20.cpolar.top/onenet/forward
```

保存或重新校验。

校验成功时，本地 Python 窗口应出现：

```text
url verification passed
```

cpolar 窗口应出现：

```text
GET /onenet/forward 200 OK
```

### 4. 检查 OneNET 规则引擎

规则 SQL 使用：

```sql
select * from notify where data.params.source_id.value = "device_A"
```

动作选择 HTTP 推送实例：

```text
deviceA_to_deviceB_http
```

确认规则处于启用状态。

### 5. 启动 device_A 上报

烧录并运行 master 分支的 `device_A` 固件。

`device_A` 串口应看到类似日志：

```text
Publish: {"id":"1","version":"1.0","params":{"source_id":{"value":"device_A"},"max_tx_idx":{"value":46},"max_tx_value":{"value":218}}}
```

### 6. 验证 OneNET 推送到本地

如果规则命中，cpolar 窗口应出现：

```text
POST /onenet/forward 200 OK
```

Python 窗口应看到 OneNET 推送内容：

```text
push payload: ...
```

dry-run 模式下应看到：

```text
DRY-RUN set device_B: {"product_id":"8x5w9DD3Av","device_name":"device_B","params":{"source_id":"device_A","max_tx_idx":46,"max_tx_value":218}}
```

这表示链路已经跑通到本地转发服务：

```text
device_A -> OneNET -> 规则引擎 -> cpolar -> 本地 Python 服务
```

### 7. 验证真正下发到 device_B

确认本地 Python 服务不是 dry-run 模式，也就是启动前已经设置：

```cmd
set ONENET_SET_PROPERTY_URL=https://iot-api.heclouds.com/thingmodel/set-device-property
set ONENET_AUTH_HEADER_NAME=authorization
set ONENET_AUTH_HEADER_VALUE=<有效authorization>
```

`device_B` 需要烧录并运行 deviceB 分支固件。

如果下发成功，`device_B` 串口应看到类似：

```text
Data from: device_A
Apply max data: channel=46 value=218
```

屏幕上对应通道应被点亮。

### 8. 常见异常排查

如果 OneNET URL 校验失败：

- 确认 Python 服务正在运行。
- 确认 cpolar 正在运行。
- 确认 OneNET URL 使用的是最新 cpolar HTTPS 地址。
- 确认 URL 末尾包含 `/onenet/forward`。
- 确认 token 是 `deviceA2B2026`。

如果只有 `GET /onenet/forward 200 OK`，没有 POST：

- URL 校验成功，但规则没有触发。
- 检查规则是否启用。
- 检查 SQL 是否为 `select * from notify where data.params.source_id.value = "device_A"`。
- 检查 `device_A` 是否真的在上报属性。

如果 Python 收到 POST 但没有下发：

- 看是否仍处于 dry-run 模式。
- 检查 `ONENET_SET_PROPERTY_URL` 是否设置。
- 检查 `ONENET_AUTH_HEADER_VALUE` 是否过期。
- 先在 OneNET API 调试页手动调用“设置设备属性”，确认 API 本身可用。

## 后续待解决问题

- 短时间内可能收到很多重复 POST。真正更新 `device_B` 前需要增加去重或节流。
- `device_A` 当前上报频率偏高，后续需要调整发布间隔和阈值。
- 本地转发服务依赖 cpolar 和本地电脑，适合开发调试，不适合作为最终部署方案。
- 免费 cpolar 域名重启后会变化，需要手动更新 OneNET HTTP 推送 URL。
- OneNET `authorization` token 会过期，需要刷新。
- 最终上线前，需要用真实转发再次确认 OneNET API 请求体和请求头格式，而不只依赖 API 调试结果。
