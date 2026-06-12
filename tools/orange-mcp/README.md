# orange-mcp —— OrangeEditor 实时协同 MCP server

让 AI（Claude 等 MCP 客户端）**实时操作正在运行的 OrangeEditor GUI 实例**——查询场景、
（M1+）看 viewport 截图、改实体/组件、保存——与用户在同一个编辑器里协同开发游戏。

架构（仿 Blender MCP，详见 [`ADR-020`](../../../Orange-Wiki/case-studies/orange-engine/decisions/ADR-020-editor-mcp-realtime-coediting-bridge.md)
与 [`mcp-realtime-coediting-design.md`](../../docs/mcp-realtime-coediting-design.md)）：

```
Claude ──MCP(stdio)──> orange-mcp(server.py) ──TCP/NDJSON(127.0.0.1:<port>)──> OrangeEditor 命令端(C++)
```

- **orange-mcp**（本目录，Python）：把编辑器命令包成 MCP tools，转发 TCP socket。
- **OrangeEditor 命令端**（`tools/OrangeEditor/mcp/`，C++）：后台 socket 线程收 NDJSON、
  主线程帧末执行命令（走命令栈 / schema / EntityGuid）。

## 现状（M0 · spike 通路）

已打通端到端通路 + 两个 tool：

| tool | 说明 |
|---|---|
| `ping` | 连通性 / 版本握手 → `{editorVersion, protocolVersion, sceneName}` |
| `get_scene_info` | 当前场景实体树 → 每实体 `{guid, name, parentGuid, components[], position}` |

M1 加 `get_entity` / `list_component_types` / `capture_viewport`（读闭环）；
M2 加 `create_entity` / `set_field` / `add_component` / `delete_entity` / `select_entity` /
`save_scene`（写闭环，全走命令栈，AI 操作可被用户 Ctrl+Z）。完整 tool 路线见
[`docs/mcp-requirements.md`](../../docs/mcp-requirements.md)。

## 安装

需要 Python ≥ 3.10。

```
cd tools/orange-mcp
python -m venv .venv
.venv\Scripts\activate          # Windows PowerShell: .venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

## 使用

### 1. 启动带 MCP 端口的编辑器

```
OrangeEditor.exe --mcp-port 8765
```

不传 `--mcp-port` = 编辑器零监听、行为与现状完全一致（安全默认）。仅绑 `127.0.0.1`，
不对外暴露、无鉴权（本机单用户开发工具，对标 Blender MCP）。

### 2. 把 orange-mcp 注册给 MCP 客户端

**Claude Code**（项目根 `.mcp.json` 或用户级配置）：

```json
{
  "mcpServers": {
    "orange-mcp": {
      "command": "python",
      "args": ["E:/ForStudy/Rendering/Orange-Ecosystem/OrangeEngine/tools/orange-mcp/server.py"],
      "env": { "ORANGE_MCP_PORT": "8765" }
    }
  }
}
```

> 用 venv 时把 `command` 指向 venv 的 python（`.../tools/orange-mcp/.venv/Scripts/python.exe`）。

端口优先级：`--port` 命令行 > `ORANGE_MCP_PORT` 环境变量 > 默认 `8765`。必须与编辑器
`--mcp-port` 一致。

### 3. 验证（M0 验收）

在 MCP 客户端里调 `ping` → 应返回编辑器版本 + 当前场景名；调 `get_scene_info` → 返回的
实体树应与编辑器 Hierarchy 面板逐项一致。编辑器断开 / 重连不崩、不卡帧。

## 故障排查

- **`无法连接 OrangeEditor 命令端`**：确认编辑器已用 `--mcp-port <同端口>` 启动；端口未被占用。
- **`ping` 超时**：编辑器主循环可能卡住（命令在帧末执行，编辑器无响应则命令不返回）。
- server.py 失败重连一次（编辑器可在两次调用间重启）；仍失败则报错给客户端。
