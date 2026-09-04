# 管理端 ↔ Socket 服务端 通信协议

东软电动汽车充电桩应用管理平台 — Qt 管理端与 Socket 服务端之间的 JSON 消息协议。
双方开发以此文档为准，任何改动必须同步更新本文档并通知对方。

## 1. 传输层约定

| 项 | 约定 |
|---|---|
| 连接方式 | TCP 长连接，管理端启动后连接一次，断线自动重连 |
| 监听端口 | `8888`（如被占用，双方另行约定并在启动参数中可配） |
| 编码 | UTF-8 |
| 消息边界 | 每条 JSON 消息以 `\n` 结尾（JSON 序列化时不含格式化空白） |
| 消息结构 | 所有消息均为 JSON 对象，外层字段固定 |

外层信封：

```json
// 请求（管理端 → 服务端）
{"seq": 1, "type": "admin_login", "payload": {...}}

// 响应（服务端 → 管理端）
{"seq": 1, "type": "admin_login", "code": 0, "msg": "ok", "data": {...}}
```

- `seq`：请求方自增编号，响应原样带回，用于将响应对应到请求。
- `code`：`0` 表示成功；非 0 表示失败，`msg` 为给人看的提示，`data` 失败时为 `null`。
- 兜底规则：**任何解析失败的请求，服务端也必须回复** `{"seq":-1,"code":3001,...}`，不得让客户端干等。

## 2. 枚举值约定（全英文，与数据库中存储值一致）

| 枚举 | 取值 | 含义 |
|---|---|---|
| 电桩状态 `status` | `idle` / `in_use` / `fault` | 空闲 / 在用 / 故障 |
| 电桩类型 `type` | `fast` / `slow` | 快充 / 慢充 |
| 用户状态 `status` | `normal` / `frozen` | 正常 / 冻结 |

展示层（界面上的中文文案）由管理端自行映射，协议与数据库中只出现英文枚举值。

## 3. 消息清单

### 3.1 admin_login — 管理员登录

```json
→ {"seq":1, "type":"admin_login", "payload":{"username":"admin", "password":"123456"}}
← {"seq":1, "type":"admin_login", "code":0, "msg":"ok", "data":{"adminId":1, "username":"admin"}}
```

失败：`code=1001`（用户名或密码错误）。

### 3.2 revenue_summary — 营收指标

```json
→ {"seq":2, "type":"revenue_summary", "payload":{}}
← {"seq":2, "type":"revenue_summary", "code":0, "msg":"ok",
   "data":{"today":356.5, "month":8230.0, "total":45210.8}}
```

金额单位：元。统计口径为已完成（已结算）订单。

### 3.3 revenue_trend — 营收趋势

```json
→ {"seq":3, "type":"revenue_trend", "payload":{"range":7}}
← {"seq":3, "type":"revenue_trend", "code":0, "msg":"ok",
   "data":{"points":[{"date":"2026-08-29", "amount":120.5}]}}
```

- `range` 取值：`7` 或 `30`。
- `points` 数组长度等于 `range`，按日期升序，无营收的日期 `amount` 为 `0`。

### 3.4 pile_status_overview — 电桩状态总览

```json
→ {"seq":4, "type":"pile_status_overview", "payload":{}}
← {"seq":4, "type":"pile_status_overview", "code":0, "msg":"ok",
   "data":{"idle":32, "inUse":11, "fault":3}}
```

占比百分比由管理端自行计算。

### 3.5 pile_list — 电桩列表

```json
→ {"seq":5, "type":"pile_list", "payload":{"stationId":0}}
← {"seq":5, "type":"pile_list", "code":0, "msg":"ok",
   "data":{"piles":[
     {"pileId":101, "code":"P-0101", "stationId":1, "stationName":"星海广场站",
      "type":"fast", "powerKw":60, "status":"idle",
      "chargeCount":152, "chargeMinutes":6040}]}}
```

- `stationId` 为 `0` 表示全部电桩；大于 0 时按站点筛选。站点详情页的站内电桩列表复用本消息。

### 3.6 pile_restart — 远程重启（模拟）

```json
→ {"seq":6, "type":"pile_restart", "payload":{"pileId":101}}
← {"seq":6, "type":"pile_restart", "code":0, "msg":"ok",
   "data":{"pileId":101, "status":"idle"}}
```

- 仅允许对 `fault` 状态的电桩执行；否则返回 `code=3002`。
- 成功后电桩状态置为 `idle`，管理端收到响应后刷新列表。

### 3.7 station_list — 站点列表

```json
→ {"seq":7, "type":"station_list", "payload":{}}
← {"seq":7, "type":"station_list", "code":0, "msg":"ok",
   "data":{"stations":[
     {"stationId":1, "name":"星海广场站", "address":"沙河口区星海广场",
      "lng":121.594, "lat":38.881, "pileTotal":10, "onlineRate":0.9}]}}
```

`onlineRate` 取值 0~1，由服务端按站内非故障电桩比例计算。

### 3.8 station_add — 新增站点

```json
→ {"seq":8, "type":"station_add",
   "payload":{"name":"高新园区站", "address":"黄浦路100号",
              "lng":121.52, "lat":38.86, "pileCount":8}}
← {"seq":8, "type":"station_add", "code":0, "msg":"ok", "data":{"stationId":9}}
```

- 服务端按 `pileCount` 批量生成模拟电桩（类型、功率按服务端固定规则生成）。
- 参数缺失或非法（如名称为空、经纬度非数字、`pileCount` 非正整数）返回 `code=2001`。

### 3.9 user_list — 用户列表

```json
→ {"seq":9, "type":"user_list", "payload":{"phoneKeyword":"138"}}
← {"seq":9, "type":"user_list", "code":0, "msg":"ok",
   "data":{"users":[
     {"userId":1, "phone":"13800001234", "nickname":"用户1234",
      "balance":86.5, "regTime":"2026-09-01 10:20:30", "status":"normal"}]}}
```

- `phoneKeyword` 为空字符串表示全部用户；非空时按手机号模糊匹配。

### 3.10 user_set_status — 冻结/解冻用户

```json
→ {"seq":10, "type":"user_set_status", "payload":{"userId":1, "status":"frozen"}}
← {"seq":10, "type":"user_set_status", "code":0, "msg":"ok",
   "data":{"userId":1, "status":"frozen"}}
```

- `status` 取值：`frozen`（冻结）/ `normal`（解冻）。

### 3.11 ping — 心跳

```json
→ {"seq":11, "type":"ping", "payload":{}}
← {"seq":11, "type":"ping", "code":0, "msg":"ok", "data":{}}
```

管理端每 10~30 秒发送一次，用于连接状态显示与调试。

## 4. 错误码表

| code | 含义 | 典型场景 |
|---|---|---|
| 0 | 成功 | — |
| 1001 | 认证失败 | 管理员账号或密码错误 |
| 1002 | 无权限 / 账号被冻结 | 冻结用户尝试操作 |
| 2001 | 参数缺失或格式非法 | 新增站点缺字段、手机号不是 11 位 |
| 2002 | 目标不存在 | 操作的 pileId / userId / stationId 查不到 |
| 3001 | 消息类型未知或消息无法解析 | type 拼错、JSON 语法错误 |
| 3002 | 状态冲突 | 对非故障电桩执行重启等 |
| 5000 | 服务端内部错误 | 数据库异常等，`msg` 中带简述 |

## 5. 其他约定

- 字段命名统一使用 camelCase（如 `pileId`、`stationName`）。
- 所有金额字段为浮点数，单位元；时间字段格式为 `yyyy-MM-dd HH:mm:ss`，日期为 `yyyy-MM-dd`。
- 本协议只覆盖管理端 ↔ 服务端。用户端 ↔ 服务端的消息另行约定，但共享同一套枚举值与错误码。
