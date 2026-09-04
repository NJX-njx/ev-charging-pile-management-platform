# 电动汽车充电桩应用管理平台通信协议

本文档是 Qt 用户端、Qt 管理端、Socket 服务端与 Web 数据大屏之间的唯一通信基准。双方开发任何消息、字段、枚举或错误码前，必须先同步修改本文档。

本协议覆盖需求矩阵冻结的五个模块：`client`、`admin`、`server`、`web` 和 `docs`。项目说明书中的机器学习智能分析属于扩展设想，不在当前协议范围内。

## 1. 通信边界

| 调用方 | 服务 | 协议 | 默认地址 | 用途 |
|---|---|---|---|---|
| Qt 用户端 | Socket 服务端 | TCP 长连接 + UTF-8 JSON Lines | `127.0.0.1:8888` | 登录、资料、钱包、找站、充电与订单 |
| Qt 管理端 | Socket 服务端 | TCP 长连接 + UTF-8 JSON Lines | `127.0.0.1:8888` | 登录、营收、设备、站点与用户管理 |
| Web 数据大屏 | 服务端只读 HTTP 接口 | HTTP/1.1 + UTF-8 JSON | `http://127.0.0.1:8080` | 营收、订单、充电量、电桩状态与站点数据 |

- `8888` 和 `8080` 都必须支持启动参数覆盖。
- Qt 用户端与 Qt 管理端不得直接访问数据库，也不得互相直接通信。
- Web 页面只读取聚合数据，不提供写操作。
- 腾讯地图地址解析和路线规划由 Qt 用户端通过 `QWebEngineView` 调用腾讯地图完成，不经过业务服务端，因此没有对应业务消息。

## 2. TCP 传输约定

### 2.1 连接与分包

- 使用 TCP 长连接。客户端连接成功后先登录，断线重连后必须重新登录。
- 编码为 UTF-8，不发送 BOM。
- 每条消息是单行 JSON 对象，以换行符 `\n` 结束。接收方必须按字节流累积，找到 `\n` 后再解析，不能假设一次 `readyRead` 或 `recv` 正好得到一条消息。
- 单条消息包含结尾换行在内不得超过 2 MiB。超限返回 `code=4001`，随后服务端可关闭连接。
- 客户端同一连接上的 `seq` 必须从 1 开始单调递增。服务端对每个完整请求恰好返回一个响应，不主动推送业务消息。
- 客户端可以同时发送多个请求；必须使用 `seq` 匹配响应，不能依赖响应顺序。
- 客户端不得在结果未知时自动重试充值、预约、停止或结算等写操作。重连后应先查询资料或当前订单状态。

### 2.2 请求与响应信封

请求：

```json
{"seq":1,"type":"ping","payload":{}}
```

成功响应：

```json
{"seq":1,"type":"ping","code":0,"msg":"ok","data":{}}
```

失败响应：

```json
{"seq":1,"type":"charge_start","code":3002,"msg":"order status must be reserved","data":null}
```

| 字段 | 方向 | 类型 | 规则 |
|---|---|---|---|
| `seq` | 双向 | integer | 正整数；响应原样带回。无法读取请求编号时使用 `-1` |
| `type` | 双向 | string | 消息类型；响应与请求相同。无法读取类型时使用 `error` |
| `payload` | 请求 | object | 必须存在；无参数时为 `{}` |
| `code` | 响应 | integer | `0` 成功，非 `0` 失败 |
| `msg` | 响应 | string | 成功固定为 `ok`；失败为可展示的简短提示 |
| `data` | 响应 | object | 成功时按消息定义返回；失败时固定为 `null` |

无法解析 JSON、外层不是对象或缺少必要信封字段时，服务端也必须响应，不能让客户端无限等待：

```json
{"seq":-1,"type":"error","code":3001,"msg":"invalid message","data":null}
```

### 2.3 会话与权限

- 新连接初始为未登录状态，只允许 `ping`、`user_login` 和 `admin_login`。
- `user_login` 成功后，会话角色为 `user`；`admin_login` 成功后，会话角色为 `admin`。
- 一个连接只能绑定一个角色和一个账号。切换账号或角色时应断开并新建连接。
- 除登录响应外，客户端身份以服务端会话为准。客户端不得在业务请求中提交 `userId` 或 `adminId` 冒充其他账号。
- 未登录调用受保护消息返回 `1003`，角色不匹配返回 `1004`。

## 3. 通用数据规则

### 3.1 基础格式

| 数据 | 约定 |
|---|---|
| 字段命名 | camelCase |
| ID | 正整数 |
| 手机号 | 字符串，正则 `^1\d{10}$` |
| 金额 | JSON number，单位元，业务精度 2 位小数 |
| 电量 | JSON number，单位 kWh，业务精度 3 位小数 |
| 功率 | JSON number，单位 kW |
| 经纬度 | `lng` 为 `[-180,180]`，`lat` 为 `[-90,90]` |
| 时间 | ISO 8601，东八区，例如 `2026-09-04T10:20:30+08:00` |
| 日期 | `yyyy-MM-dd` |
| 空集合 | 返回 `[]`，不返回 `null` |
| 可空字段 | 字段保留，值为 `null` |

金额在数据库中建议使用“分”的整数保存，输出协议时再换算为元，避免浮点累计误差。

### 3.2 枚举

协议与数据库只保存英文枚举；中文仅由界面映射。

| 枚举 | 取值 | 中文含义 |
|---|---|---|
| 电桩状态 `pile.status` | `idle` / `in_use` / `fault` | 空闲 / 在用 / 故障 |
| 电桩类型 `pile.type` | `fast` / `slow` | 快充 / 慢充 |
| 用户状态 `user.status` | `normal` / `frozen` | 正常 / 冻结 |
| 订单状态 `order.status` | `reserved` / `charging` / `pending_payment` / `completed` / `cancelled` | 已预约 / 充电中 / 待结算 / 已完成 / 已取消 |

### 3.3 标准对象

用户摘要 `UserSummary`：

```json
{"userId":1,"phone":"13800001234","nickname":"用户1234","balance":86.50,"regTime":"2026-09-01T10:20:30+08:00","status":"normal"}
```

用户资料 `UserProfile` 在用户摘要基础上增加头像。没有头像时 `avatar` 为 `null`：

```json
{"userId":1,"phone":"13800001234","nickname":"用户1234","balance":86.50,"regTime":"2026-09-01T10:20:30+08:00","status":"normal","avatar":{"mime":"image/png","base64":"iVBORw0KGgo..."}}
```

站点摘要 `StationSummary`：

```json
{"stationId":1,"name":"星海广场站","address":"沙河口区星海广场","lng":121.594,"lat":38.881,"pricePerKwh":1.20,"pileTotal":10,"pileIdle":6,"onlineRate":0.90}
```

- `pileIdle` 是 `status=idle` 的电桩数。
- `onlineRate=(idle + in_use) / pileTotal`；没有电桩时固定为 `0`。

电桩 `Pile`：

```json
{"pileId":101,"code":"P-0101","stationId":1,"stationName":"星海广场站","type":"fast","powerKw":60.0,"status":"idle","chargeCount":152,"chargeMinutes":6040}
```

订单 `Order`：

```json
{"orderId":10001,"stationId":1,"stationName":"星海广场站","pileId":101,"pileCode":"P-0101","status":"pending_payment","reservedAt":"2026-09-04T10:00:00+08:00","startTime":"2026-09-04T10:02:00+08:00","endTime":"2026-09-04T10:32:00+08:00","settledAt":null,"energyKwh":20.000,"unitPrice":1.20,"amount":24.00}
```

- `unitPrice` 是预约时保存的站点电价快照，之后站点价格变化不影响该订单。
- 尚未发生的时间、能耗和金额字段返回 `null`。

## 4. 消息总表

| 类型 | 允许角色 | 作用 |
|---|---|---|
| `ping` | 未登录、用户、管理员 | 心跳 |
| `user_login` | 未登录 | 手机号登录或自动注册 |
| `user_profile_get` | 用户 | 获取个人资料 |
| `user_profile_update` | 用户 | 修改昵称或头像 |
| `wallet_recharge` | 用户 | 模拟充值 |
| `nearby_station_list` | 用户 | 按距离查询附近站点 |
| `station_detail` | 用户、管理员 | 查询站点及站内电桩 |
| `active_order_get` | 用户 | 查询未完成订单 |
| `charge_reserve` | 用户 | 预约空闲电桩 |
| `charge_start` | 用户 | 开始充电 |
| `charge_stop` | 用户 | 停止充电并模拟计费 |
| `charge_settle` | 用户 | 扣款结算并释放电桩 |
| `charge_cancel` | 用户 | 取消尚未开始的预约 |
| `user_order_list` | 用户 | 查询自己的订单记录 |
| `admin_login` | 未登录 | 管理员登录 |
| `revenue_summary` | 管理员 | 今日、本月和总营收 |
| `revenue_trend` | 管理员 | 近 7 日或 30 日营收趋势 |
| `pile_status_overview` | 管理员 | 电桩状态数量 |
| `pile_list` | 管理员 | 查询电桩列表 |
| `pile_restart` | 管理员 | 模拟远程重启故障电桩 |
| `station_list` | 管理员 | 查询全部站点 |
| `station_add` | 管理员 | 新增站点和模拟电桩 |
| `user_list` | 管理员 | 查询用户 |
| `user_set_status` | 管理员 | 冻结或解冻用户 |

## 5. 通用消息

### 5.1 ping 心跳

客户端每 10 至 30 秒发送一次。心跳不延长业务操作超时，也不改变登录状态。

请求：

```json
{"seq":1,"type":"ping","payload":{}}
```

响应：

```json
{"seq":1,"type":"ping","code":0,"msg":"ok","data":{"serverTime":"2026-09-04T10:20:30+08:00"}}
```

## 6. Qt 用户端消息

### 6.1 user_login 手机号登录或自动注册

请求：

```json
{"seq":2,"type":"user_login","payload":{"phone":"13800001234"}}
```

响应：

```json
{"seq":2,"type":"user_login","code":0,"msg":"ok","data":{"isNew":false,"user":{"userId":1,"phone":"13800001234","nickname":"用户1234","balance":86.50,"regTime":"2026-09-01T10:20:30+08:00","status":"normal","avatar":null}}}
```

- 手机号不存在时，服务端自动创建用户，默认昵称为“用户”加手机号后 4 位，余额为 `0`，状态为 `normal`，并返回 `isNew=true`。
- 手机号格式非法返回 `2001`；账号已冻结返回 `1002`。

### 6.2 user_profile_get 获取个人资料

请求：

```json
{"seq":3,"type":"user_profile_get","payload":{}}
```

响应：

```json
{"seq":3,"type":"user_profile_get","code":0,"msg":"ok","data":{"user":{"userId":1,"phone":"13800001234","nickname":"用户1234","balance":86.50,"regTime":"2026-09-01T10:20:30+08:00","status":"normal","avatar":null}}}
```

### 6.3 user_profile_update 修改资料

请求中至少提供 `nickname` 或 `avatar` 之一。没有修改的字段应省略。

```json
{"seq":4,"type":"user_profile_update","payload":{"nickname":"小宋","avatar":{"mime":"image/jpeg","base64":"/9j/4AAQSk..."}}}
```

响应返回更新后的完整资料：

```json
{"seq":4,"type":"user_profile_update","code":0,"msg":"ok","data":{"user":{"userId":1,"phone":"13800001234","nickname":"小宋","balance":86.50,"regTime":"2026-09-01T10:20:30+08:00","status":"normal","avatar":{"mime":"image/jpeg","base64":"/9j/4AAQSk..."}}}}
```

- `nickname` 去除首尾空白后长度为 1 至 20 个字符。
- 头像仅允许 JPEG 或 PNG，Base64 解码后不得超过 512 KiB。非法内容返回 `2001`，整条消息超限返回 `4001`。

### 6.4 wallet_recharge 模拟充值

请求：

```json
{"seq":5,"type":"wallet_recharge","payload":{"amount":100.00}}
```

响应：

```json
{"seq":5,"type":"wallet_recharge","code":0,"msg":"ok","data":{"amount":100.00,"balance":186.50}}
```

`amount` 必须大于 `0` 且不超过 `10000`，最多 2 位小数；否则返回 `2001`。

### 6.5 nearby_station_list 附近站点

Qt 用户端先通过腾讯地图把区域或手动地址转换为坐标，再把坐标发送给服务端。

请求：

```json
{"seq":6,"type":"nearby_station_list","payload":{"lng":121.594,"lat":38.881,"limit":50}}
```

响应：

```json
{"seq":6,"type":"nearby_station_list","code":0,"msg":"ok","data":{"stations":[{"stationId":1,"name":"星海广场站","address":"沙河口区星海广场","lng":121.594,"lat":38.881,"pricePerKwh":1.20,"pileTotal":10,"pileIdle":6,"onlineRate":0.90,"distanceKm":0.3}]}}
```

- `limit` 可省略，默认 `50`，范围 1 至 100。
- 服务端按 `distanceKm` 升序返回；距离相同时按 `stationId` 升序。

### 6.6 station_detail 站点详情

该消息由用户端和管理端复用。

请求：

```json
{"seq":7,"type":"station_detail","payload":{"stationId":1}}
```

响应：

```json
{"seq":7,"type":"station_detail","code":0,"msg":"ok","data":{"station":{"stationId":1,"name":"星海广场站","address":"沙河口区星海广场","lng":121.594,"lat":38.881,"pricePerKwh":1.20,"pileTotal":10,"pileIdle":6,"onlineRate":0.90},"piles":[{"pileId":101,"code":"P-0101","stationId":1,"stationName":"星海广场站","type":"fast","powerKw":60.0,"status":"idle","chargeCount":152,"chargeMinutes":6040}]}}
```

站点不存在返回 `2002`。`piles` 按 `code` 升序。

### 6.7 active_order_get 查询未完成订单

请求：

```json
{"seq":8,"type":"active_order_get","payload":{}}
```

有未完成订单时：

```json
{"seq":8,"type":"active_order_get","code":0,"msg":"ok","data":{"order":{"orderId":10001,"stationId":1,"stationName":"星海广场站","pileId":101,"pileCode":"P-0101","status":"charging","reservedAt":"2026-09-04T10:00:00+08:00","startTime":"2026-09-04T10:02:00+08:00","endTime":null,"settledAt":null,"energyKwh":null,"unitPrice":1.20,"amount":null}}}
```

没有未完成订单时：

```json
{"seq":8,"type":"active_order_get","code":0,"msg":"ok","data":{"order":null}}
```

`reserved`、`charging` 和 `pending_payment` 都属于未完成订单。每个用户同时最多有一个未完成订单。

### 6.8 charge_reserve 预约电桩

请求：

```json
{"seq":9,"type":"charge_reserve","payload":{"pileId":101}}
```

响应：

```json
{"seq":9,"type":"charge_reserve","code":0,"msg":"ok","data":{"order":{"orderId":10001,"stationId":1,"stationName":"星海广场站","pileId":101,"pileCode":"P-0101","status":"reserved","reservedAt":"2026-09-04T10:00:00+08:00","startTime":null,"endTime":null,"settledAt":null,"energyKwh":null,"unitPrice":1.20,"amount":null}}}
```

- 服务端必须在一个数据库事务中确认用户无未完成订单、电桩为 `idle`、创建订单并把电桩改为 `in_use`。
- 用户已有未完成订单返回 `3005`；电桩不是 `idle` 返回 `3003`；电桩不存在返回 `2002`。

### 6.9 charge_start 开始充电

请求：

```json
{"seq":10,"type":"charge_start","payload":{"orderId":10001}}
```

响应：

```json
{"seq":10,"type":"charge_start","code":0,"msg":"ok","data":{"order":{"orderId":10001,"stationId":1,"stationName":"星海广场站","pileId":101,"pileCode":"P-0101","status":"charging","reservedAt":"2026-09-04T10:00:00+08:00","startTime":"2026-09-04T10:02:00+08:00","endTime":null,"settledAt":null,"energyKwh":null,"unitPrice":1.20,"amount":null}}}
```

订单必须属于当前用户且状态为 `reserved`；否则分别返回 `2002` 或 `3002`。

### 6.10 charge_stop 停止并计费

请求：

```json
{"seq":11,"type":"charge_stop","payload":{"orderId":10001}}
```

响应：

```json
{"seq":11,"type":"charge_stop","code":0,"msg":"ok","data":{"order":{"orderId":10001,"stationId":1,"stationName":"星海广场站","pileId":101,"pileCode":"P-0101","status":"pending_payment","reservedAt":"2026-09-04T10:00:00+08:00","startTime":"2026-09-04T10:02:00+08:00","endTime":"2026-09-04T10:32:00+08:00","settledAt":null,"energyKwh":20.000,"unitPrice":1.20,"amount":24.00}}}
```

- 订单必须属于当前用户且状态为 `charging`，否则返回 `2002` 或 `3002`。
- 模拟电量由服务端计算，不能由客户端提交。`amount` 按 `energyKwh × unitPrice` 四舍五入到分。
- 停止后订单进入 `pending_payment`，电桩在结算前仍保持 `in_use`。

### 6.11 charge_settle 结算

请求：

```json
{"seq":12,"type":"charge_settle","payload":{"orderId":10001}}
```

响应：

```json
{"seq":12,"type":"charge_settle","code":0,"msg":"ok","data":{"order":{"orderId":10001,"stationId":1,"stationName":"星海广场站","pileId":101,"pileCode":"P-0101","status":"completed","reservedAt":"2026-09-04T10:00:00+08:00","startTime":"2026-09-04T10:02:00+08:00","endTime":"2026-09-04T10:32:00+08:00","settledAt":"2026-09-04T10:33:00+08:00","energyKwh":20.000,"unitPrice":1.20,"amount":24.00},"balance":62.50}}
```

- 服务端必须在一个事务中校验余额、扣款、完成订单、累计电桩充电次数和时长，并把电桩释放为 `idle`。
- 余额不足返回 `3004`，订单保持 `pending_payment`，电桩保持 `in_use`；用户充值后可再次结算。

### 6.12 charge_cancel 取消预约

请求：

```json
{"seq":13,"type":"charge_cancel","payload":{"orderId":10001}}
```

响应：

```json
{"seq":13,"type":"charge_cancel","code":0,"msg":"ok","data":{"orderId":10001,"status":"cancelled","pileId":101,"pileStatus":"idle"}}
```

只允许取消当前用户的 `reserved` 订单。服务端必须在一个事务中取消订单并释放电桩；充电已经开始时返回 `3002`。

### 6.13 user_order_list 订单记录

请求：

```json
{"seq":14,"type":"user_order_list","payload":{"page":1,"pageSize":20}}
```

响应：

```json
{"seq":14,"type":"user_order_list","code":0,"msg":"ok","data":{"page":1,"pageSize":20,"total":1,"orders":[{"orderId":10001,"stationId":1,"stationName":"星海广场站","pileId":101,"pileCode":"P-0101","status":"completed","reservedAt":"2026-09-04T10:00:00+08:00","startTime":"2026-09-04T10:02:00+08:00","endTime":"2026-09-04T10:32:00+08:00","settledAt":"2026-09-04T10:33:00+08:00","energyKwh":20.000,"unitPrice":1.20,"amount":24.00}]}}
```

- `page` 默认 `1`，`pageSize` 默认 `20`，范围 1 至 100。
- 只返回当前用户的订单，按 `reservedAt` 降序、`orderId` 降序。

## 7. Qt 管理端消息

### 7.1 admin_login 管理员登录

请求：

```json
{"seq":2,"type":"admin_login","payload":{"username":"admin","password":"123456"}}
```

响应：

```json
{"seq":2,"type":"admin_login","code":0,"msg":"ok","data":{"adminId":1,"username":"admin"}}
```

用户名或密码错误返回 `1001`。密码不得写入普通运行日志或出现在后续响应中。

### 7.2 revenue_summary 营收指标

请求：

```json
{"seq":3,"type":"revenue_summary","payload":{}}
```

响应：

```json
{"seq":3,"type":"revenue_summary","code":0,"msg":"ok","data":{"today":356.50,"month":8230.00,"total":45210.80}}
```

仅统计 `status=completed` 的订单，以 `settledAt` 归属日期。

### 7.3 revenue_trend 营收趋势

请求：

```json
{"seq":4,"type":"revenue_trend","payload":{"range":7}}
```

响应：

```json
{"seq":4,"type":"revenue_trend","code":0,"msg":"ok","data":{"range":7,"points":[{"date":"2026-08-29","amount":120.50},{"date":"2026-08-30","amount":0.00},{"date":"2026-08-31","amount":98.00},{"date":"2026-09-01","amount":145.20},{"date":"2026-09-02","amount":131.80},{"date":"2026-09-03","amount":202.40},{"date":"2026-09-04","amount":356.50}]}}
```

- `range` 只能是 `7` 或 `30`。
- `points` 覆盖含当天在内的连续自然日，长度严格等于 `range`，按日期升序；无营收日期的 `amount` 为 `0`。

### 7.4 pile_status_overview 电桩状态总览

请求：

```json
{"seq":5,"type":"pile_status_overview","payload":{}}
```

响应：

```json
{"seq":5,"type":"pile_status_overview","code":0,"msg":"ok","data":{"total":46,"idle":32,"inUse":11,"fault":3}}
```

管理端根据 `total` 计算各状态占比；`total=0` 时占比显示为 `0`。

### 7.5 pile_list 电桩列表

请求：

```json
{"seq":6,"type":"pile_list","payload":{"stationId":0,"status":null}}
```

响应：

```json
{"seq":6,"type":"pile_list","code":0,"msg":"ok","data":{"piles":[{"pileId":101,"code":"P-0101","stationId":1,"stationName":"星海广场站","type":"fast","powerKw":60.0,"status":"idle","chargeCount":152,"chargeMinutes":6040}]}}
```

- `stationId` 可省略或为 `0`，表示全部站点；正整数表示按站点筛选。
- `status` 可省略或为 `null`，表示全部状态；否则必须是电桩状态枚举。
- 结果按 `stationId`、`code` 升序。

### 7.6 pile_restart 模拟远程重启

请求：

```json
{"seq":7,"type":"pile_restart","payload":{"pileId":101}}
```

响应：

```json
{"seq":7,"type":"pile_restart","code":0,"msg":"ok","data":{"pileId":101,"status":"idle"}}
```

- 只允许重启 `fault` 且没有关联未完成订单的电桩；否则返回 `3002`。
- 成功后状态改为 `idle`，管理端重新请求列表和状态总览。

### 7.7 station_list 站点列表

请求：

```json
{"seq":8,"type":"station_list","payload":{}}
```

响应：

```json
{"seq":8,"type":"station_list","code":0,"msg":"ok","data":{"stations":[{"stationId":1,"name":"星海广场站","address":"沙河口区星海广场","lng":121.594,"lat":38.881,"pricePerKwh":1.20,"pileTotal":10,"pileIdle":6,"onlineRate":0.90}]}}
```

结果按 `stationId` 升序。查看站内电桩时使用共享消息 `station_detail`。

### 7.8 station_add 新增站点

请求：

```json
{"seq":9,"type":"station_add","payload":{"name":"高新园区站","address":"黄浦路100号","lng":121.520,"lat":38.860,"pricePerKwh":1.30,"pileCount":8}}
```

响应：

```json
{"seq":9,"type":"station_add","code":0,"msg":"ok","data":{"station":{"stationId":9,"name":"高新园区站","address":"黄浦路100号","lng":121.520,"lat":38.860,"pricePerKwh":1.30,"pileTotal":8,"pileIdle":8,"onlineRate":1.00},"createdPileCount":8}}
```

- `name` 和 `address` 去除首尾空白后不能为空；`pricePerKwh` 必须大于 `0`，最多 2 位小数；`pileCount` 为 1 至 100 的整数。
- 服务端在一个事务中创建站点和模拟电桩。电桩编号必须全局唯一；类型和功率按服务端固定、可重复测试的规则生成。
- 参数非法返回 `2001`；事务中任何一步失败必须整体回滚。

### 7.9 user_list 用户查询

请求：

```json
{"seq":10,"type":"user_list","payload":{"phoneKeyword":"138"}}
```

响应：

```json
{"seq":10,"type":"user_list","code":0,"msg":"ok","data":{"users":[{"userId":1,"phone":"13800001234","nickname":"用户1234","balance":86.50,"regTime":"2026-09-01T10:20:30+08:00","status":"normal"}]}}
```

`phoneKeyword` 可省略或为空字符串，表示全部用户；非空时只允许数字并按手机号包含匹配。结果按 `userId` 升序，不返回头像 Base64。

### 7.10 user_set_status 冻结或解冻用户

请求：

```json
{"seq":11,"type":"user_set_status","payload":{"userId":1,"status":"frozen"}}
```

响应：

```json
{"seq":11,"type":"user_set_status","code":0,"msg":"ok","data":{"userId":1,"status":"frozen"}}
```

- `status` 只能为 `frozen` 或 `normal`。
- 为避免订单和电桩被锁死，存在 `reserved`、`charging` 或 `pending_payment` 订单的用户不能被冻结，返回 `3002`。
- 已冻结用户登录返回 `1002`；已建立的旧连接下一次业务请求也返回 `1002` 并由服务端关闭连接。

## 8. Web 数据大屏 HTTP 接口

### 8.1 通用约定

- 基础地址默认为 `http://127.0.0.1:8080/api/v1`。
- 只支持 `GET`。响应头为 `Content-Type: application/json; charset=utf-8`，禁止缓存：`Cache-Control: no-store`。
- 推荐由同一 HTTP 监听器提供 `web/` 静态文件，使页面与 API 同源；若分开部署，只对配置的 Web 来源返回 CORS 头。
- 本地实训环境默认只绑定回环地址。需要局域网展示时再通过启动参数显式绑定，并限制允许访问的来源。
- Web 响应不含 TCP 的 `seq` 和 `type`，其余成功/失败语义一致。

成功响应：

```json
{"code":0,"msg":"ok","data":{}}
```

失败响应：

```json
{"code":2001,"msg":"range must be 7 or 30","data":null}
```

| HTTP 状态 | 使用场景 |
|---|---|
| `200` | 成功 |
| `400` | 查询参数非法 |
| `404` | 路径不存在 |
| `500` | 服务端内部错误 |

### 8.2 GET /dashboard/overview 大屏数据

请求示例：

```text
GET /api/v1/dashboard/overview?range=7 HTTP/1.1
Host: 127.0.0.1:8080
Accept: application/json
```

- `range` 可省略，默认 `7`，只能为 `7` 或 `30`。
- 页面首次加载和用户点击“刷新”时都调用该接口；不另设刷新接口。

响应：

```json
{"code":0,"msg":"ok","data":{"generatedAt":"2026-09-04T10:20:30+08:00","revenue":{"today":356.50,"month":8230.00,"total":45210.80},"orders":{"today":12,"total":1468},"energy":{"todayKwh":328.500,"totalKwh":87342.000},"revenueTrend":{"range":7,"points":[{"date":"2026-08-29","amount":120.50},{"date":"2026-08-30","amount":0.00},{"date":"2026-08-31","amount":98.00},{"date":"2026-09-01","amount":145.20},{"date":"2026-09-02","amount":131.80},{"date":"2026-09-03","amount":202.40},{"date":"2026-09-04","amount":356.50}]},"pileStatus":{"total":46,"idle":32,"inUse":11,"fault":3},"stations":[{"stationId":1,"name":"星海广场站","address":"沙河口区星海广场","pricePerKwh":1.20,"pileTotal":10,"pileIdle":6,"onlineRate":0.90}]}}
```

统计口径：

- 营收只统计已完成订单的 `amount`，按 `settledAt` 计入日期。
- 订单数只统计已完成订单；`today` 按 `settledAt` 当日计算。
- 充电量只统计已完成订单的 `energyKwh`。
- `revenueTrend.points` 必须补齐无营收日期，规则与 `revenue_trend` 相同。
- `stations` 按 `stationId` 升序。

### 8.3 GET /health 服务状态

请求示例：

```text
GET /api/v1/health HTTP/1.1
Host: 127.0.0.1:8080
Accept: application/json
```

响应：

```json
{"code":0,"msg":"ok","data":{"status":"up","serverTime":"2026-09-04T10:20:30+08:00"}}
```

健康检查只表示 HTTP 服务能响应，不代表所有业务数据都通过验收。

## 9. 订单与电桩状态机

```text
电桩 idle
  │ charge_reserve（同一事务：创建订单 + 电桩置 in_use）
  ▼
订单 reserved ── charge_cancel ──> 订单 cancelled + 电桩 idle
  │ charge_start
  ▼
订单 charging
  │ charge_stop（服务端计算电量和金额）
  ▼
订单 pending_payment
  │ charge_settle（同一事务：扣款 + 完成订单 + 累计电桩数据 + 释放电桩）
  ▼
订单 completed + 电桩 idle
```

- 任一步骤只能由订单所属用户执行。
- 不符合上图顺序的请求返回 `3002`，不得部分更新数据库。
- TCP 断开不会自动改变订单或电桩状态。用户重连登录后使用 `active_order_get` 恢复界面。
- `fault` 电桩不能预约；`in_use` 电桩必须关联一个未完成订单。

## 10. 错误码

| code | 含义 | 典型场景 |
|---|---|---|
| `0` | 成功 | 请求完成 |
| `1001` | 认证失败 | 管理员用户名或密码错误 |
| `1002` | 账号已冻结 | 冻结用户登录或继续操作 |
| `1003` | 未登录 | 未建立会话就调用受保护消息 |
| `1004` | 角色无权限 | 用户调用管理消息，或管理员调用用户消息 |
| `2001` | 参数缺失或格式非法 | 手机号、金额、枚举、分页或经纬度非法 |
| `2002` | 目标不存在或不属于当前账号 | 找不到站点、电桩或订单 |
| `3001` | 消息无法解析或类型未知 | JSON 错误、信封错误、未知 `type` |
| `3002` | 状态冲突 | 订单步骤错误、重启非故障桩、冻结有活动订单的用户 |
| `3003` | 电桩不可用 | 预约在用或故障电桩 |
| `3004` | 余额不足 | 结算金额超过钱包余额 |
| `3005` | 已有未完成订单 | 重复预约 |
| `4001` | 消息过大 | 超过 2 MiB 或头像超过限制 |
| `5000` | 服务端内部错误 | 数据库或未预期异常 |

服务端错误响应不得包含 SQL、堆栈、文件路径、密码等内部或敏感信息。详细错误只写入服务端日志。

## 11. 实现与联调检查

- 客户端发送前确保 JSON 是单行 UTF-8，并在末尾追加 `\n`。
- 服务端为每个 TCP 连接保存独立接收缓冲区、`seq` 关联信息和登录会话。
- 服务端使用参数化 SQL；密码至少保存为带盐哈希，不以明文落库。
- 预约、取消、结算、批量新增站点等跨表修改必须使用数据库事务。
- 时间计算、今日和本月统计统一使用 `Asia/Shanghai`。
- 管理端与 Web 大屏共享相同统计函数，避免相同指标口径不一致。
- 联调至少覆盖：粘包、半包、非法 JSON、未知消息、断线重登、重复预约、错误状态顺序、余额不足、冻结账号、空列表和数据库异常。
