# AGENTS.md

本文件面向在本仓库中工作的 AI 编码助手，描述仓库结构、硬性约定与常用命令。人类读者请看 [README.md](README.md)。

## 仓库概览

东软电动汽车充电桩应用管理平台（小学期实训，第 13 组）。五个模块，边界已冻结，**不得新增模块**：

| 目录 | 模块 | 负责人 |
|---|---|---|
| `client/` | Qt 用户端 | 宋昊润 |
| `admin/` | Qt 管理端 | 倪家兴 |
| `server/` | Socket 服务端（多线程，独占数据库访问） | 宋昊润 |
| `web/` | Web 数据大屏（只读服务端 JSON） | 宋昊润 |
| `docs/` | 协议与设计文档 | 共同 |
| `tools/` | 开发辅助数据（如 `seed_stations.json` 站点导入数据），**非交付模块** | 共同 |

## 硬性约定

1. **通信协议的唯一基准是 [docs/protocol.md](docs/protocol.md)**。任何消息的新增、字段或枚举改动，必须先改该文件再改代码，并在提交说明中提及。
2. 协议与数据库中的枚举值只用英文：电桩状态 `idle`/`in_use`/`fault`，电桩类型 `fast`/`slow`，用户状态 `normal`/`frozen`。中文文案只出现在 UI 展示层，由界面代码自行映射。
3. JSON 字段命名统一 camelCase；消息以 `\n` 作为分包边界；编码 UTF-8。
4. Qt 用户端与 Qt 管理端**不得直接访问数据库**，也不得互相直接通信，一切数据经 Socket 服务端中转。
5. 服务端默认监听端口 `8888`，须支持通过启动参数覆盖。
6. **界面视觉的唯一基准是 [docs/visual-design.md](docs/visual-design.md)**。颜色、字号只允许取自该文件色板；样式集中在 QSS（`*/resources/style.qss`，经 qrc 加载），禁止在代码中散落样式字面量。

## 技术栈与版本

- C++，仅支持 Qt 6.2 及以上的 Qt 6（Qt Creator 工程，qmake `.pro`）。管理端须安装 Qt Charts；构建时选择 Qt 6 套件。
- 依赖限定：Qt 自带模块（QtNetwork、QtSql、QtCharts、QtWebEngineWidgets）与 SQLite。**引入任何第三方库前先与团队确认**。
- 目标运行环境：Ubuntu 22.04 虚拟机（开发环境为 VMware 虚拟机），代码不得依赖 macOS/Windows 专有特性。

## 构建与验证

各模块为独立 Qt Creator 工程：

```bash
# 以 server 为例（admin/client 同理，替换目录与 .pro 文件名）
cd server && qmake6 server.pro && make -j$(nproc)

# 推荐影子构建（产物不进源码目录，Qt Creator 默认即此方式）：
mkdir build-server && cd build-server && qmake6 ../server/server.pro && make -j$(nproc)
```

就地构建时各模块 `.pro` 已把中间产物收进 `.build/`、可执行文件收进 `bin/`（均已 gitignore），但仍以影子构建为准。

- 提交前至少保证本模块 `qmake6 && make` 通过。
- 涉及协议字段的改动，用最小客户端（如 `nc 127.0.0.1 8888` 发送单行 JSON）手工验证服务端响应后再提交。
- 验证手段缺失时，在提交说明或 PR 中明确写出「未验证」的部分。

## 提交规范

- 提交信息格式：`<模块>: <中文简述>`，如 `admin: 实现管理员登录界面与Socket连接`。
- 不提交构建产物与本地配置：`*.o`、`Makefile`、`build-*/`、`*.pro.user`、核心转储等。如仓库缺少 `.gitignore`，先补一个 Qt/C++ 的再提交代码。
- 两人协作，**所有改动（代码、文档、配置）直接在 `main` 分支提交**；推送前先 `git pull --rebase` 避免推送冲突。改动 `docs/protocol.md` 的提交必须通知对方。

## 跨平台协作

成员开发环境不同（macOS、Windows、Ubuntu 虚拟机），遵守以下约定：

- 换行符由 `.gitattributes` 统一（源码强制 LF 入库）。编辑器不要开启「保存时转换整个文件换行符」，避免整文件无意义 diff。
- Windows 成员用 Qt Creator 打开对应模块的 `.pro` 构建即可（MinGW/MSVC 套件均可），与文档中的 `qmake6 && make` 命令等价。`.pro.user`、`.vs/`、`debug/`、`release/` 等本地文件已由 `.gitignore` 覆盖，不得手动强制添加。
- 新增文件/目录的名称不得与已有文件仅大小写不同（Windows 与 macOS 文件系统默认不区分大小写，会造成检出冲突）。
- 协议、文档、工程配置、模块代码等所有改动均直接在 `main` 提交并及时推送；提交前保证本模块构建通过，推送前 `git pull --rebase`。
- 在某一平台验证过的构建，不代表其他平台通过；提交涉及平台相关代码（路径、换行、网络）时，在提交信息中注明已验证的平台。

## 当前状态

`docs/` 含通信协议、项目说明书、需求矩阵（共 51 项，按模块归组：NO.1~3 项目基础、4~18 充电用户端、19~41 运营管理端、42~48 服务端、49~50 Web 数据展示、51 项目收尾）与视觉规范。`server/`（多线程 Socket 服务端 + HTTP 只读接口）、`client/`（Qt 用户端：登录/找站/导航/充电多订单/我的）、`admin/`（Qt 管理端五个导航页：销售业绩/站点与电桩/用户管理/订单管理/系统管理；站点与电桩为左右联动合并页，顶部并入全站电桩状态总览条、右栏可切「全部站点」查看全部电桩，各列表经 `ui/filtertable.*` 提供 Excel 式筛选排序）三个模块均已完成开发并合入 `main`，当前协议版本为 **v2.5**（v2.4：`charge_stop` 即释放电桩、待结算不再占桩；`pile_list` 附 `occupancy` 区分预约/充电占用；`user_update` 支持头像与余额、新增 `user_detail` 与 `admin_order_cancel`/`admin_order_stop`；`pile_restart`/`pile_disable` 放开到 `in_use` 并强制终结占用订单。v2.5：`station_add` 改为调用方显式提交 `piles` 数组，`pileCount` 移除，服务端不再生成电桩；`tools/seed_stations.json` 为逐桩明细。另：客户端导航页有 profile 析构顺序崩溃修复、鼠标转触摸脚本与 WebEngine 噪音日志过滤）。三端已完成联调；仓库不保留测试专用代码。注意：服务端已移除全部数据库迁移/兼容代码，旧结构数据库需手工删除测试库文件后重启。剩余：Web 大屏前端页面（NO.49/50）与项目收尾（NO.51）。创建新模块目录时遵循上表结构，不要另起顶层目录。
