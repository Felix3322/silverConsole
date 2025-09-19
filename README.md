# silverConsole
Remotely controlled Trojan, requires a server

---

这是一个三端构成的远控系统（仅用于学习与实验）：

* **服务端 (server.cpp)**：运行在 Linux (Ubuntu)，负责管理 Agent 的注册、心跳、命令分发与结果收集。
* **被控端 (agent.cpp)**：运行在 Windows，负责与服务端通信，执行命令并返回结果。
* **控制端 (controller\_tui.py)**：运行在任意环境 (推荐 Python 3.9+)，提供交互式 TUI 界面，查看 Agent 状态并下发命令。

---

## 功能特性

* **Agent 管理**

  * 注册分配唯一 ID（`agent-1`、`agent-2` …）
  * 上线 / 下线检测与时间记录
  * 权限信息（普通 / 管理员）、是否可提权、主机名、操作系统等信息同步

* **心跳机制**

  * 默认 0.5 秒轮询一次
  * 掉线超过 10 秒判定为离线

* **命令执行**

  * 控制端输入命令 → 下发至指定 Agent → 实时逐行回传结果
  * 特殊命令：

    * `/priv` → 提权 (UAC)
    * `/bsod` → 蓝屏 (测试用)
    * `/persist` → 添加自启动

* **控制端 (TUI)**

  * Rich 表格实时展示 Agent 状态
  * 类似 “简化版 SSH” 的终端模式
  * 输出逐行显示，不丢失数据

---

## 项目结构

```
.
├── server.cpp          # Linux 服务端
├── agent.cpp           # Windows 被控端
├── controller_tui.py   # Python 控制端
└── README.md           # 项目说明
```

---

## 编译与运行

### 服务端 (Ubuntu)

依赖：g++17、[cpp-httplib](https://github.com/yhirose/cpp-httplib)、[nlohmann/json](https://github.com/nlohmann/json)

```bash
g++ -std=c++17 -O2 server.cpp -pthread -o server
./server
```

默认监听 `0.0.0.0:1143`。

---

### 被控端 (Windows)

依赖：Visual Studio / MinGW，静态链接 WinHTTP、Shell32。

```powershell
cl /EHsc agent.cpp /link winhttp.lib shell32.lib
```

首次运行会请求服务端注册，并保存 `agent_id.txt`。
运行时会每隔 0.5 秒发送心跳。

---

### 控制端 (Python)

依赖：`requests`、`rich`

```bash
pip install requests rich
python controller_tui.py
```

在表格中选中 Agent ID，进入终端模式即可交互。

---

## 协议说明

* 所有通信均为 HTTP POST，使用 `application/x-www-form-urlencoded`。
* Agent → Server:

  * `/register`：首次注册
  * `/heartbeat`：定期心跳
  * `/result`：命令输出结果回传
* Server → Agent:

  * `/pull`：下发命令
* Controller → Server:

  * `/agents`：获取所有 Agent 状态
  * `/push`：发送命令
  * `/results`：拉取命令执行结果

编码约定：

* Agent 端所有字段先 `url_encode` → Server 收到后 `url_decode` 存 JSON → Controller 直接展示。

---

## 已知问题与改进方向

* **并发安全**：当前 `agents.json` 使用互斥锁保护，但仍存在“读-改-写”覆盖风险，推荐改用 SQLite。
* **权限提升**：`__UAC__` 重启后会断开连接，可通过参数带回原 ID 改进。
* **跨平台**：Agent 仅支持 Windows，可扩展 Linux 版本。
* **安全性**：通信未加密，未认证，**禁止用于生产环境**。

---

## 声明

本项目仅供 **学习研究** 使用，禁止用于非法用途。
开发者不对任何滥用行为负责。
