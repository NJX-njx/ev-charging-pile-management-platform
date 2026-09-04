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

## 硬性约定

1. **通信协议的唯一基准是 [docs/protocol.md](docs/protocol.md)**。任何消息的新增、字段或枚举改动，必须先改该文件再改代码，并在提交说明中提及。
2. 协议与数据库中的枚举值只用英文：电桩状态 `idle`/`in_use`/`fault`，电桩类型 `fast`/`slow`，用户状态 `normal`/`frozen`。中文文案只出现在 UI 展示层，由界面代码自行映射。
3. JSON 字段命名统一 camelCase；消息以 `\n` 作为分包边界；编码 UTF-8。
4. Qt 用户端与 Qt 管理端**不得直接访问数据库**，也不得互相直接通信，一切数据经 Socket 服务端中转。
5. 服务端默认监听端口 `8888`，须支持通过启动参数覆盖。
6. **界面视觉的唯一基准是 [docs/visual-design.md](docs/visual-design.md)**。颜色、字号只允许取自该文件色板；样式集中在 QSS（`*/resources/style.qss`，经 qrc 加载），禁止在代码中散落样式字面量。

## 技术栈与版本

- C++，Qt 5.15 / Qt 6.2 双兼容（Qt Creator 工程，qmake `.pro`）。说明书要求的「Qt Creator 6.2+」指 IDE 版本；课程虚拟机（Ubuntu 22.04）apt 源中 QtCharts 仅 Qt 5.15 提供，Qt 6.2.4 不含 QtCharts，因此 `.pro` 中用 `qtHaveModule(charts)` 条件链接，代码不得依赖仅单一版本存在的 API。
- 依赖限定：Qt 自带模块（QtNetwork、QtSql、QtCharts、QtWebEngineWidgets）与 SQLite。**引入任何第三方库前先与团队确认**。
- 目标运行环境：Ubuntu 22.04 虚拟机（课程验证环境为 UTM arm64/aarch64），代码不得依赖 macOS/Windows 专有特性。

## 构建与验证

各模块为独立 Qt Creator 工程：

```bash
# 以 server 为例（admin/client 同理，替换目录与 .pro 文件名）
cd server && qmake server.pro && make -j$(nproc)
```

- 提交前至少保证本模块 `qmake && make` 通过。
- 涉及协议字段的改动，用最小客户端（如 `nc 127.0.0.1 8888` 发送单行 JSON）手工验证服务端响应后再提交。
- 验证手段缺失时，在提交说明或 PR 中明确写出「未验证」的部分。

## 提交规范

- 提交信息格式：`<模块>: <中文简述>`，如 `admin: 实现管理员登录界面与Socket连接`。
- 不提交构建产物与本地配置：`*.o`、`Makefile`、`build-*/`、`*.pro.user`、核心转储等。如仓库缺少 `.gitignore`，先补一个 Qt/C++ 的再提交代码。
- 两人协作，各自在功能分支开发后合入 `main`；改动 `docs/protocol.md` 的提交必须通知对方。

## 跨平台协作

成员开发环境不同（macOS、Windows、Ubuntu 虚拟机），遵守以下约定：

- 换行符由 `.gitattributes` 统一（源码强制 LF 入库）。编辑器不要开启「保存时转换整个文件换行符」，避免整文件无意义 diff。
- Windows 成员用 Qt Creator 打开对应模块的 `.pro` 构建即可（MinGW/MSVC 套件均可），与文档中的 `qmake && make` 命令等价。`.pro.user`、`.vs/`、`debug/`、`release/` 等本地文件已由 `.gitignore` 覆盖，不得手动强制添加。
- 新增文件/目录的名称不得与已有文件仅大小写不同（Windows 与 macOS 文件系统默认不区分大小写，会造成检出冲突）。
- 协议、文档、工程配置类改动直接在 `main` 提交并及时推送；模块代码在功能分支开发，构建验证通过并经负责人确认后再合入 `main`。
- 在某一平台验证过的构建，不代表其他平台通过；提交涉及平台相关代码（路径、换行、网络）时，在提交信息中注明已验证的平台。

## 当前状态

仓库处于初始化阶段：`docs/` 含通信协议、项目说明书与需求矩阵；管理端骨架（登录窗口、主界面框架、Socket 通信封装）正在 `feat/admin` 分支开发，尚未合入。创建新模块目录时遵循上表结构，不要另起顶层目录。
