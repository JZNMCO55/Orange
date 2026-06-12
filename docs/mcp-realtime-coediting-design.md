# 设计提案：OrangeEditor MCP 实时协同桥（AI ↔ 编辑器实时操作）

> 状态：**设计提案（未实现）**。用户 2026-06-12 拍板「做一个 OE 的 MCP 工具，用于和 AI 协同实时操作引擎开发游戏」+「首版做读写协同闭环」+「本 session 先落设计文档 + ADR」后的地基调研。
> 本文是**提案不是决策**——关键选型由 [ADR-020](../../Orange-Wiki/case-studies/orange-engine/decisions/ADR-020-editor-mcp-realtime-coediting-bridge.md) 裁定。
> 参考范本：Blender MCP（addon socket server + 独立 Python MCP server，用户已端到端验证过的协同模式）。

## 1. 目标与范围

让 AI（Claude）能**实时操作正在运行的 OrangeEditor GUI 实例**——查询当前场景、看 viewport 画面、创建/修改实体、加组件、改字段、保存——与用户在同一个编辑器里**协同**开发游戏。把编辑器前面那一长串「headless 绿但没 dogfood」的能力拉进 AI+人的实时回路，AI 能「看见 + 动手 + 截图自验」，而不是隔着代码盲改。

**首版范围（用户 2026-06-12 拍板 = 读写协同闭环）**：
- **读**：`get_scene_info`（实体树 + 每实体组件清单）、`get_entity`（单实体全字段）、`capture_viewport`（viewport 截图回读）
- **写**：`create_entity`、`set_field`（改任意组件字段，含 Transform）、`add_component`、`delete_entity`、`save_scene`、`select_entity`
- 全部走编辑器现有命令栈（AI 操作可被用户 Ctrl+Z）

**非目标（首版）**：Play/Pause/Stop 控制、跑 C# 脚本、改材质资产、导入资产、prefab 实例化、多编辑器实例。这些留后续分期（§13），首版先打通「看 + 改 + 验证」最小有用闭环。

**非目标（永久）**：把 MCP 当成对外服务暴露（仅 localhost + flag 启动）；让 AI 绕过命令栈直接改 World（破坏 undo 一致性）；MCP 协议在 C++ 侧实现（交给 Python 桥）。

## 2. 已有地基（必须复用，别重造）

调研确认编辑器已有的基础设施几乎覆盖 MCP 所需，MCP 是往这些机制上加一个**外部命令入口 + 截图出口**，不是从零做：

| 已有件 | 位置 | 复用方式 |
|---|---|---|
| **「pending op 帧末执行」模式** | `EditorRenderLayer::ApplyPendingSceneOp` (`EditorRenderLayer.cpp:1126`)、`ApplyPendingImports` (`:1780`)、`ApplyPendingPlayOp` (`:1547`) | MCP 命令队列同款：后台线程入队 → `OnUpdate` 帧末 drain 执行。新增 `ApplyPendingMcpCommands()` 与它们并列 |
| **拖拽文件入队（无 mutex 模式参照）** | `EditorHost::pendingImports` + `glfwSetDropCallback`（`main.cpp:938`，主线程同步触发故无需 mutex） | MCP 是**真后台线程**入队，故需 mutex（与 pendingImports 唯一区别，见 §4） |
| **命令栈 + coalesce** | `ICommand` (`command/ICommand.h:10`)、`CommandStack::Push/BeginGroup/EndGroup` (`command/CommandStack.h:56`) | MCP 写操作全部 `Push` 进同一栈 → AI 操作与手动操作统一 undo |
| **创建实体命令** | `CreateEntityCommand`（`command/EntityCommands.h:30`，`CreatorFn = function<Entity(World&)>`） | MCP `create_entity` 包一个 creator lambda 进此命令 |
| **字段写命令（模板）** | `SetFieldValueCommand<T>`（`command/SetFieldValueCommand.h:22`，old/new + ApplyFn） | MCP `set_field` 经 schema get/set 构造此命令 |
| **schema-first 组件注册表** | `ComponentSchemaRegistry::All()`（`schema/ComponentSchemaRegistry.h:74`）+ `ComponentSchema{has,get,add,remove}`（`schema/ComponentSchema.h:36`） | MCP 复用它**枚举组件 + 读写任意字段 + 加组件**，零逐组件手写命令 |
| **字段类型系统 + get/set** | `PropertyDescriptor{get,set}`（`schema/PropertyDescriptor.h:154`）、`PropertyType`（`schema/PropertyType.h:25`） | MCP 把字段值 ↔ JSON 的双向编解码按 PropertyType 分派 |
| **稳定实体身份** | `FindEntityByGuid`（`scene/EntityGuid.h:64`）、`GuidComponent`（`:25`）、`EnsureEntityGuids` | MCP 用 EntityGuid 字符串当实体句柄（跨命令/跨会话稳定，A2 epic 成果） |
| **实体枚举** | `World::Registry()`（`scene/World.h:125`）+ `NameComponent`（`:22`）+ `HierarchyComponent`（`:33`） | `get_scene_info` 遍历 registry 产实体树 |
| **单实体序列化** | `SaveSubtreeToString` / `LoadFromString`（`scene/SceneSerialization.h:217`） | `get_entity` 可直接产 JSON blob（或走 schema 逐字段，二选一见 §9） |
| **viewport 离屏渲染** | `Pipeline::RenderToTexture(world, RHITexture*, w, h)`（`render/Pipeline.h:520`，渲到 BGRA8 TransferSrc 纹理） | `capture_viewport` 的渲染源 |
| **GPU→CPU 像素回读** | OrangeRender RHI：`RHICommandList::CopyTextureToBuffer`（`RHICommandList.h:243`）+ `RHIBuffer::Map`（`RHIBuffer.h:14`）+ `MemoryUsage::GpuToCpu`（`RHITypes.h:33`），**已是公共 API、有单测/golden image 验证** | `capture_viewport` 把 RenderToTexture 的纹理 copy 到 readback buffer → map → base64 |
| **缩略图离屏渲染范本** | `ThumbnailService`（`render/ThumbnailService.h:78`，已用 RenderToTexture 渲 96×96 + ImGui 显示） | 截图实现可借鉴其离屏渲染流程（但需补 CPU readback，ThumbnailService 只到 GPU 纹理） |
| **argv 入口分支** | `main.cpp:308`（已有 `import-scene`/`import-mesh` headless 分支 + `ParseImportScaleFlag`） | 加 `--mcp-port <n>` flag 解析 + 启动 socket 线程 |

**关键结论**：首版**零跨仓依赖**。截图回读这个唯一疑似跨仓点，调研确认 OrangeRender RHI 早已支持（FEATURE-2026-05-08-rhi-texture-readback），全部能在 OrangeEngine 单仓落地。

## 3. 架构总览（仿 Blender MCP 三段式）

```
┌─────────┐   MCP (stdio)   ┌──────────────────┐   TCP/JSON (localhost)   ┌────────────────────────┐
│ Claude  │ ──────────────> │ orange-mcp        │ ───────────────────────> │ OrangeEditor 命令端 (C++) │
│  (我)   │ <────────────── │ (Python server)   │ <─────────────────────── │ 后台线程 listener +      │
└─────────┘  tool 结果/截图  └──────────────────┘   JSON 结果 / base64 图    │ 主线程帧末队列执行        │
                                                                            └────────────────────────┘
```

三个部件：

1. **OrangeEditor 命令端（C++，OrangeEngine 子仓 `tools/OrangeEditor/mcp/`）**：后台线程监听 `127.0.0.1:<port>`，按**行分隔 JSON**（NDJSON）收命令、回结果。命令不在 socket 线程执行，丢进 mutex 保护队列，主线程 `OnUpdate` 帧末 drain（§4）。

2. **orange-mcp（Python MCP server，`tools/orange-mcp/`）**：把命令包成 MCP tools（`get_scene_info` / `create_entity` / `capture_viewport` …），连 TCP socket 转发，结构直接仿 blender-mcp。截图 tool 返回 image content。

3. **命令协议（NDJSON over TCP）**：请求 `{"id":<n>,"op":"<name>","args":{...}}`，响应 `{"id":<n>,"ok":true,"result":{...}}` 或 `{"id":<n>,"ok":false,"error":"..."}`。`id` 配对请求/响应（命令异步执行，响应在命令真正跑完的那帧回写）。

**为什么 C++ 侧只做裸 TCP+JSON 而非自实现 MCP**：MCP 协议（initialize/tools/list/tools/call、JSON-RPC、capability 协商）在 C++ 里实现成本高、迭代慢；Python 侧有成熟 MCP SDK，改 tool schema 不用重编引擎。C++ 侧只需一个稳定的「收 JSON 命令 → 帧末执行 → 回 JSON」的窄接口。这与「MCP 协议细节会演进、命令集会频繁加」的现实匹配。

## 4. 线程模型与命令队列（最关键的命门）

ImGui / EnTT World / Vulkan **全是单线程非线程安全**。socket 线程**绝不能**直接碰它们——必须 marshal 回主线程帧边界执行。这是和 Blender addon 用 modal timer poll 队列同构的约束，**不是设计选择**。

```cpp
// EditorHost.h —— 与 pendingImports 同位置新增
struct McpBridge {
    struct Request  { std::uint64_t id; std::string op; std::string argsJson; };
    struct Response { std::uint64_t id; bool ok; std::string resultOrErrorJson; };

    std::mutex              inMutex;
    std::vector<Request>    pendingRequests;   // socket 线程 push, 主线程 swap-drain
    std::mutex              outMutex;
    std::vector<Response>   pendingResponses;  // 主线程 push, socket 线程取走回写
    std::atomic<bool>       running{false};
};
```

帧末消费（`EditorRenderLayer::OnUpdate` 内，与 `ApplyPendingImports` 并列）：

```cpp
void EditorRenderLayer::ApplyPendingMcpCommands(const FrameContext& frame) {
    std::vector<McpBridge::Request> batch;
    { std::lock_guard lk(mHost.mcp.inMutex); batch.swap(mHost.mcp.pendingRequests); }
    for (auto& req : batch) {
        McpBridge::Response resp = ExecuteMcpCommand(req, frame);  // 在主线程、帧边界
        { std::lock_guard lk(mHost.mcp.outMutex); mHost.mcp.pendingResponses.push_back(std::move(resp)); }
    }
}
```

**与 `pendingImports` 的唯一区别**：拖拽 callback 在 `glfwPollEvents` 主线程同步触发，故 `pendingImports` 无需 mutex；MCP 是**真后台 socket 线程**，故 in/out 两个队列都要 mutex。其余「帧末 drain、原子执行、不阻断 ImGui frame」的范式完全一致。

**截图命令的特殊性**：`capture_viewport` 内部 `RenderToTexture` + `CopyTextureToBuffer` + `WaitIdle`，是帧内同步阻塞操作——必须在主线程帧末执行（已满足），不能在 socket 线程。WaitIdle 会让该帧略卡，可接受（截图非高频）。

## 5. 实体句柄：EntityGuid（不是 EnTT id）

MCP 跨命令、跨场景切换、跨会话引用实体，**必须用稳定句柄**。EnTT `entity` id 会因 World 重建（Open/New/Play 快照还原）失效，不能当句柄。

- **句柄 = `GuidComponent.guid` 的字符串形式**（`FindEntityByGuid` 反查，`EntityGuid.h:64`）。
- 命令端在 MCP listener 启动时或每次 `get_scene_info` 时对当前 World 跑一次 `EnsureEntityGuids`（幂等，给无 guid 的实体补 guid），保证所有实体可被 MCP 寻址。
- `create_entity` 返回新实体的 guid；后续 `set_field` / `add_component` 用该 guid 定位。
- guid 失效（实体已删/场景已换）→ 命令返回 `ok:false, error:"entity not found"`，不崩。

这直接复用 A2 epic（ADR-018 EntityGuid 主键迁移）的成果，是 MCP 能稳定工作的前提。

## 6. 命令集（首版读写闭环）

| op | args | 走命令栈 | 实现路径 |
|---|---|---|---|
| `get_scene_info` | — | 否（只读） | 遍历 `World::Registry()`，产实体树（guid/name/parent/组件名列表）。EnsureEntityGuids 先行 |
| `get_entity` | `guid` | 否 | `FindEntityByGuid` → 遍历 `ComponentSchemaRegistry::All()` 中 `has(world,e)` 为真的 schema，逐字段 `get` 编 JSON（§9） |
| `capture_viewport` | `width?`,`height?` | 否 | `RenderToTexture` → `CopyTextureToBuffer` → `Map` → base64（§7） |
| `select_entity` | `guid` | 否（UI 状态） | 设 `mHost.selection.selectedEntity`（让用户看到 AI 在操作哪个） |
| `create_entity` | `name?`,`parentGuid?` | 是 | `CreateEntityCommand`，creator 建空实体 + Name/Transform/Hierarchy（+ EnsureGuid）。返回新 guid |
| `set_field` | `guid`,`component`,`field`,`value` | 是 | schema 查 PropertyDescriptor → `SetFieldValueCommand<T>`（old=当前 get，new=解码 value）（§8/§9） |
| `add_component` | `guid`,`component` | 是 | schema 查 `add != nullptr` → 包进命令调 `schema.add(host, e)` |
| `delete_entity` | `guid` | 是* | 设 `mHost.selection.pendingDelete`（注：现状删除走 `CommandStack::Clear` 非 undo，见开放问题 Q5） |
| `save_scene` | `path?` | 否 | 设 `mHost.scene.pendingSceneOp = Save`（path 非空先设 currentScenePath）。复用 ApplyPendingSceneOp |

**命令执行即同帧响应**：每条命令在 `ExecuteMcpCommand` 内同步跑完并产 Response，故 MCP 调用是「请求 → 该帧执行 → 响应」一个来回，AI 拿到结果即已落地。

## 7. 截图回读（capture_viewport）

让 AI「看见」是协同的核心。流程（全主线程帧末）：

```cpp
// 伪代码，落在 tools/OrangeEditor/mcp/McpCapture.cpp
1. 拿一张 scratch RHITexture（BGRA8, RenderTarget|Sampled|TransferSrc），尺寸 = args.width×height（默认取 viewport 当前尺寸）
2. mpScenePipeline->RenderToTexture(*world, scratchRt, w, h)      // Pipeline.h:520，渲 shadow+sky+PBR（不含后处理）
3. 建 readback RHIBuffer（size=w*h*4, usage=Transfer, MemoryUsage::GpuToCpu）  // RHITypes.h:33
4. cmd: TransitionTexture(scratchRt → TransferSrc); CopyTextureToBuffer(scratchRt, rb, {w,h,1});  // RHICommandList.h:243
5. device.WaitIdle()
6. const uint8_t* px = rb->Map(); base64-encode(px, w*h*4); rb->Unmap();   // RHIBuffer.h:14
7. Response.result = { "format":"BGRA8", "width":w, "height":h, "base64":"..." }
```

Python 侧 `capture_viewport` tool 把 base64 解码、转 PNG（Pillow）作为 MCP image content 返回，我即可直接看到画面。

**实现归属**：建议在引擎层 `Pipeline` 上加一个 `CaptureToCpu(world, w, h, std::vector<uint8_t>& outBgra)` 一站式 API（封装 step 1-6），既给 MCP 用、也给未来「截图存盘」等复用；编辑器 MCP 命令端只调它 + base64。这是 OrangeEngine 引擎代码改动（render 模块），单仓内。

**已知限制**：RenderToTexture 不含后处理（bloom/tonemap）——AI 看到的是 PBR 直出，与 viewport 最终画面有色调差异。首版可接受（看几何/布局/材质足够）；后续要像素级一致需让 Pipeline 截图走带后处理的路径。

## 8. 写操作走命令栈（与手动操作统一 undo）

所有写命令 `Push` 进 `mHost.cmdStack`，关键收益：
- **AI 操作可被用户 Ctrl+Z**——AI 改错了，用户一键回退，不用对话来回。
- **dirty 标志自动联动**（`CommandStack::SetOnChanged` 已接 `scene.dirty = true`），AI 改完场景标题自动显示未保存。
- **coalesce 复用**——AI 连续微调同一字段（如逐步逼近某个 position）走 `SetFieldValueCommand::Merge` 合并成一条。

`set_field` 的命令构造（复用 schema get/set，与 Inspector 拖字段完全同路径）：

```cpp
const ComponentSchema* sc = registry.Find(componentTypeName);
const PropertyDescriptor* pd = sc->FindProperty(fieldName);
T oldVal; pd->get(sc->get(world, e), &oldVal);
T newVal = DecodeJson<T>(args.value);          // 按 PropertyType 解码
cmdStack.Push(make_unique<SetFieldValueCommand<T>>(
    e, std::string(componentTypeName) + "." + fieldName, oldVal, newVal,
    [&host, eGuid, sc, pd](const T& v){ /* 重解 entity + component, pd->set */ }));
```

注意 ApplyFn 闭包内**按 guid 重解 entity + 重调 `sc->get`**（不捕获裸 component 指针）——对齐 SchemaInspector 应对 Undo/Redo 后地址变化的做法（避免悬空）。

## 9. schema 驱动通用读写（零逐组件手写）

`get_entity` 和 `set_field` 都复用 `ComponentSchemaRegistry`，**新增一个组件类型时 MCP 自动支持，无需改 MCP 代码**——契合编辑器 schema-first / 禁 hardcode 纪律（ADR-001）。

字段值 ↔ JSON 的编解码按 `PropertyType`（`PropertyType.h:25`）分派：
- `Float/Int/UInt/Bool` → JSON number/bool
- `Vec2/Vec3/Vec4/Quat` → JSON number array
- `String` → JSON string
- `Enum` → JSON string（按 `enumNames`）或 int
- `AssetRef` → JSON string（资产路径，走 `assetRefGet/Set` + EditorAssetContext）
- `EntityRef` → JSON string（目标实体 guid）
- `PolygonVertices/EdgeChainVertices` → JSON number array（首版可只读，写后续）

**`get_entity` 两条路线**（开放问题 Q3）：
- **A · schema 逐字段**：遍历 schema 产结构化 `{component: {field: value}}`，AI 友好、可精确改单字段。**推荐**。
- **B · SaveSubtreeToString**：直接产引擎序列化 JSON blob，零新代码但格式是序列化 schema（嵌套深、含内部 id），AI 改起来不直观。
- 倾向 A（与 set_field 对称、AI 直接可读可改），B 留作 `export_entity` 调试用途。

## 10. 安全

- `--mcp-port <n>` flag **默认不启动**（不传 flag = 零行为变化、零监听）。
- 仅绑 `127.0.0.1`（localhost），不对外。
- 无鉴权（本机单用户开发工具，对标 Blender MCP）；若未来要跨机再加 token。
- 命令端对未知 op / 非法 args / 失效 guid 一律 graceful 返回 `ok:false`，不崩编辑器。

## 11. 模块归属与跨仓 / 纪律

- **命令端 C++**：`tools/OrangeEditor/mcp/`（不污染引擎 runtime；编辑器专属，类比 `import/`）。
- **截图引擎 API**：`Pipeline::CaptureToCpu` 落 `src/render/`（引擎 render 模块，单仓）。
- **Python server**：`tools/orange-mcp/`（随引擎走，跟着 editor 版本演进）。
- **跨仓**：首版**零跨仓**（截图 readback OrangeRender 已支持）。socket 用平台 TCP（Windows winsock），不引第三方网络库 / 不引 JSON 新库（复用 `Core::Serialization`）。
- **header isolation**：MCP 命令端在 editor 侧，不受引擎公共头约束；若 winsock/线程头需隔离，集中在 `mcp/` 的 .cpp。
- **schema-first 纪律**：MCP 不得给 EditorState/mega-class 加 per-component 分支；读写一律走 ComponentSchemaRegistry（§9）。

## 12. 待裁定开放问题（→ ADR-020）

> **2026-06-12 用户拍板**：下表 + 需求规格 §8.2 开放问题全部按「倾向/建议」采纳，裁定固化于 [ADR-020](../../Orange-Wiki/case-studies/orange-engine/decisions/ADR-020-editor-mcp-realtime-coediting-bridge.md)（accepted）Decision 末表。

| # | 问题 | 倾向（= 最终裁定） |
|---|---|---|
| Q1 | 命令端架构：裸 TCP+JSON + Python MCP 桥 vs C++ 直接 MCP vs stdio | **TCP+JSON + Python 桥**（仿 Blender MCP，C++ 侧窄接口） |
| Q2 | 线程模型：主线程帧末队列（约束非选择） | 主线程帧末队列 + in/out mutex |
| Q3 | `get_entity` 格式：schema 逐字段 vs SaveSubtreeToString blob | schema 逐字段（A） |
| Q4 | 实体句柄：EntityGuid vs EnTT id vs name | EntityGuid |
| Q5 | `delete_entity` 现状走 `CommandStack::Clear`（不可 undo）——MCP 删除是否先不暴露，等删除命令化后再加？ | 首版**暴露但明确告知不可 undo**，或 de-scope 到 v2 |
| Q6 | Python server 归属：`tools/orange-mcp/`（本仓）vs 独立仓 | 本仓 tools/ |
| Q7 | 截图是否含后处理（像素级一致）vs PBR 直出（RenderToTexture 现状） | 首版 PBR 直出，一致性后续 |
| Q8 | 是否首版就走命令栈（vs 直接改 World 更简单） | 走命令栈（undo 一致性，§8） |

## 13. 实施顺序（分期）

- **M0 · spike 通路** ✅（2026-06-12 落地）：editor 加 `--mcp-port` + 后台 socket 线程（`tools/OrangeEditor/mcp/McpServer.cpp` winsock）+ in/out 队列（`mcp/McpBridge.h`，挂 EditorHost）+ `ApplyPendingMcpCommands`（EditorRenderLayer 帧末，与 ApplyPendingImports 同位）+ `ping`/`get_scene_info`（`mcp/McpCommandHandler.cpp`，schema 驱动枚举组件）；Python 侧最小 MCP server（`tools/orange-mcp/server.py`，FastMCP）。端到端已用 `tools/orange-mcp/smoke_test.py`（裸 TCP NDJSON）真验通过：ping 握手 / get_scene_info 返回 20 实体树（guid/name/parentGuid/components/position 全对）/ 未知 op graceful 报错 / 断连重连不崩 / 优雅关闭线程 join 干净。技术最高风险点（winsock 线程 + 帧末 marshal + Python 桥）已化解。剩 Python MCP 层 + 真实 Claude 客户端 dogfood（checklist item 57）。
- **M1 · 读闭环**：`get_entity`（schema 逐字段）+ `capture_viewport`（含 `Pipeline::CaptureToCpu` 引擎 API）。AI 能完整「看」场景结构 + 画面。
- **M2 · 写闭环**：`create_entity` / `set_field` / `add_component` / `select_entity` / `save_scene`（全走命令栈）。AI 能「改 + 截图自验」。**首版（读写协同闭环）到此完整**。
- **M3+（后续分期，非首版）**：Play/Pause/Stop 控制 → 跑 C# 脚本 → 改材质 → 导入资产 → prefab 实例化 → delete 命令化。

每个 M 是独立可 dogfood 的里程碑。M0–M2 都在 OrangeEngine 单仓（命令端 + 截图 API 是 editor/render 代码；Python server 是 tools 脚本）。

## 14. dogfood

MCP 几乎全是「跨进程 + GUI + 网络」行为，headless 测不到，**强 dogfood-gated**：
- M0：editor 带 flag 启动，Python 端连上跑 `get_scene_info`，看返回实体树是否匹配当前场景。
- M1：`capture_viewport` 回来的图，AI（我）能否正确「看到」当前 viewport（与用户屏幕对照）。
- M2：AI 创建实体 / 改 Transform，用户在编辑器里**实时看到** AI 的操作 + Ctrl+Z 能撤销 AI 的改动。
- 登记到 `docs/dogfood-checklist.md`（M0–M2 落地时各加一批）。

## 15. 与现有 epic 的关系

- **依赖 A2（EntityGuid，ADR-018）**：MCP 实体句柄的稳定性前提，已闭环 ✅。
- **协同 B1（PIE/脚本，ADR-017）**：M3 的「跑 C# 脚本」与 PIE 闭环同源——MCP 可成为驱动/调试 PIE 的外部入口（AI 摆场景 → Play → 看脚本行为 → 调），是 PIE 之上的协同放大器。
- **放大所有 dogfood-pending 功能**：编辑器大量「headless 绿但没 dogfood」的能力（动画 GUI、prefab override UI、gizmo），MCP 让 AI 能驱动它们暴露真问题——这是 MCP 对项目整体成熟度的最大杠杆。
