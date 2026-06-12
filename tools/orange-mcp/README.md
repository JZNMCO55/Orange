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

## 现状（全 42 tool 落地：P0 11 + P1 17 + P2 14）

M0 spike + M1 读闭环 + M2 写闭环 + M4（P1 17 tool）+ M5（P2 14 tool）已全部落地：

**P0（首版读写协同闭环，11 tool）**

| tool | 类别 | 说明 |
|---|---|---|
| `ping` | 会话 | 连通性 / 版本握手 → `{editorVersion, protocolVersion, sceneName}` |
| `list_component_types` | 会话 | 枚举全部组件类型 + 字段元数据（能力自发现） |
| `get_scene_info` | 读 | 当前场景实体树 → 每实体 `{guid, name, parentGuid, components[], position}` |
| `get_entity` | 读 | 单实体全字段（schema 逐字段，结构化 `{component:{field:value}}`） |
| `capture_viewport` | 读 | viewport 截图（含后处理）→ PNG，让 AI「看见」场景 |
| `create_entity` | 写 | 建空实体（可 Undo），返回 guid |
| `set_field` | 写 | 改任意组件字段（走命令栈，可 Ctrl+Z + coalesce） |
| `add_component` | 写 | 挂组件（⚠️ 现状不可 Undo） |
| `delete_entity` | 写 | 删实体子树（**已可 Undo**，2026-06-12 命令化） |
| `select_entity` | 写 | 设编辑器选中（UI 状态） |
| `save_scene` | 写 | 保存场景（文件 IO，下一帧写盘） |

**P1（E1 协同效率 + E2 资产 + E3 Play，17 tool）**

| tool | 类别 | 说明 |
|---|---|---|
| `get_editor_state` | 读 | 状态快照：playState / scenePath / dirty / selectedGuid / gizmo 模式 |
| `find_entities` | 读 | 按 name 子串 / 组件 / underGuid 子树过滤实体 |
| `get_camera` / `set_camera` | 相机 | 读 / 写轨道相机（pivot/azimuth/elevation/radius/fov，越界 clamp） |
| `frame_entity` | 相机 | 相机对准实体（空 guid = Frame All），不改选中 |
| `duplicate_entity` | 写 | 复制子树（可 Undo），返回克隆根 guid |
| `reparent_entity` | 写 | 改父（keep-world，可 Undo，环检测） |
| `remove_component` | 写 | 卸组件（多数可 Undo，复用 Inspector 状态快照） |
| `begin_undo_group` / `end_undo_group` | 写 | 批量操作合并为一条 Undo（30s/断连自动闭合护栏） |
| `open_scene` | 写 | 打开 .scene.json（dirty 保护，force 丢弃） |
| `play` / `pause` / `resume` / `stop` | Play | Play 模式控制（快照往返；脚本 tick 待 B1 接线） |
| `list_assets` | 读 | 枚举 assets/（按 AssetKind / pathPrefix 过滤） |
| `import_asset` | 写 | 导入外部资产（.png/.obj/.gltf/.glb/.fbx，FBX scale 参数） |

**P2（E5 prefab + E6 动画 + E7 脚本 + E8 杂项，14 tool）**

| tool | 类别 | 说明 |
|---|---|---|
| `create_prefab` | prefab | 子树落盘 .prefab.json（文件 IO） |
| `instantiate_prefab` | prefab | 实例化 .prefab.json（可 Undo），返回实例根 guid |
| `get_prefab_status` | prefab | 读实例 override 状态（templatePath + overriddenPaths） |
| `revert_override` | prefab | 字段 / 全部回退到模板值（⚠️ 不可 Undo） |
| `apply_instance` | prefab | 实例推回模板（重写 .prefab，⚠️ 不可 Undo） |
| `get_animation_clip` | 动画 | 读 ClipAnimator / .anim → clip JSON |
| `set_animation_clip` | 动画 | 整 clip 写回（可 Undo，与 timeline 同命令） |
| `preview_animation` | 动画 | 编辑期预览 play/pause/seek（与 Play 互斥） |
| `set_script_field` | 脚本 | 改 ScriptComponent fieldOverride（⚠️ 不可 Undo） |
| `set_entity_order` | 杂项 | 根级重排 Move Up/Down（可 Undo） |
| `new_scene` | 杂项 | 新建空场景（dirty 保护） |
| `create_material` | 杂项 | 建 .material（指定模板，文件 IO） |
| `set_material_param` | 杂项 | 改 .material uniform 值（文件 IO） |
| `get_editor_log` | 可观测 | 拉编辑器最近日志（level / timestamp / message） |

完整 tool 路线 + 契约见 [`docs/mcp-requirements.md`](../../docs/mcp-requirements.md) §4/§6。

> **写操作纪律**：`set_field` / `create_entity` / `delete_entity` / `duplicate_entity` /
> `reparent_entity` / `remove_component`（多数）走编辑器命令栈，AI 的操作用户可 Ctrl+Z 撤销。
> `add_component` 受编辑器现状限制不可 Undo（会清 undo 历史），tool 显式返回 `undoable:false`。
> Play 模式下写操作默认拒绝（传 `allowInPlay` 逃生门）。批量操作用 `begin_undo_group` /
> `end_undo_group` 合并成一条 Undo（有 30s 超时 + 连接断开自动闭合护栏）。

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
