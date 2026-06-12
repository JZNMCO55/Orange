# 需求规格：OrangeEditor MCP 实时协同工具（orange-mcp）

> 状态：需求规格（accepted）。Fable 5 起草、Opus 审查复核于 2026-06-12；用户 2026-06-12 拍板 §8.2 开放问题全部按「建议」采纳。配套架构设计见 mcp-realtime-coediting-design.md + ADR-020（accepted）。

本文回答「要什么、为什么、覆盖什么场景、每个能力的契约、优先级、怎么算达标」。**怎么实现**（socket 协议、线程模型、mutex、伪代码、file:line）在 [`mcp-realtime-coediting-design.md`](mcp-realtime-coediting-design.md)，本文不重复。架构关键选型（通信架构 / 线程模型 / 命令栈 / EntityGuid 句柄 / schema 驱动 / Python server 归属）由 [ADR-020](../../Orange-Wiki/case-studies/orange-engine/decisions/ADR-020-editor-mcp-realtime-coediting-bridge.md) 裁定，本文以其推荐方案（A 系列）为前提。

---

## 1. 目标与动机

### 1.1 痛点

1. **AI 隔着代码盲改，无法「看见 + 动手」**。当前 AI 参与编辑器/引擎开发的方式是：读代码 → 改代码 → 跑 headless 测试 → 把「视觉/手感待验证」登记进 `docs/dogfood-checklist.md` 等用户真机 dogfood。checklist 上已积压一长串「headless 绿但没 dogfood」的功能（动画 timeline 三件套 item 37-48、prefab override UI、gizmo world→local、glb 内嵌贴图视觉……）。AI 既不能驱动这些功能、也不能看到结果，dogfood 成为单点瓶颈。
2. **AI 与人不在同一个回路里**。用户在编辑器里摆场景、发现问题，要靠文字描述转述给 AI；AI 的修复建议要靠用户手动操作回放验证。一来一回的转述损耗大（参 memory：「交互功能勿凭读代码判定能用」——读代码答 ✅ 被实测打脸的教训）。
3. **已验证的范本存在**。用户已端到端验证过 Blender MCP（addon socket server + Python MCP server），证明「AI 实时操作 DCC + 截图回读自验」的协同模式在本工作流里真实有效（Blender headless 建模管线 + MCP 交互建模已是日常工序）。OrangeEditor 缺同款入口。

### 1.2 目标

让 AI（Claude）能**实时操作正在运行的 OrangeEditor GUI 实例**——查询场景、看 viewport 画面、创建/修改实体、加组件、改字段、保存——与用户在**同一个编辑器**里协同开发游戏（首款：2.5D Ori-like 平台跳跃）。

### 1.3 成功长什么样

- **看得见**：AI 调一个 tool 拿到 viewport 截图，能据此判断「这个实体为什么不可见」「这盏灯氛围对不对」，不再凭代码推测视觉结果。
- **动得了手**：AI 创建/摆放/调参的每一步，用户在编辑器里**实时看到**；AI 改错了，用户 **Ctrl+Z 一键撤销 AI 的操作**（与手动操作同一命令栈）。
- **自验闭环**：AI 做完一组改动 → 截图 → 自己确认效果 → 不对就继续调，全程不需要用户当「人肉执行器」。
- **dogfood 放大器**：编辑器后续每个新功能（动画、prefab、PIE），AI 都能用 MCP 驱动其数据侧 + 截图验证其视觉侧，把 dogfood 从「全靠用户」变成「AI 先扫一遍、用户验关键手感」。

---

## 2. 范围边界

### 2.1 是什么

- 一个 **MCP server（`tools/orange-mcp/`，Python）** + **OrangeEditor 进程内命令端（`tools/OrangeEditor/mcp/`，C++）**，让 MCP 客户端（Claude Code 等）以 tool 调用形式操作正在运行的编辑器。
- **首版（用户 2026-06-12 拍板）= 读写协同闭环**：场景查询 + 截图回读 + 实体 CRUD + 组件字段读写 + 选中 + 保存（本文 P0 集）。
- 后续分期覆盖编辑器的完整能力面：Play 调试、资产操作、prefab、动画创作、C# 脚本协同（P1/P2，§6）。

### 2.2 不是什么（首版不做，分期再做）

- Play/Pause/Stop 控制、资产导入/材质编辑、prefab 实例化/override、动画 clip 创作、C# 脚本字段——P1/P2（§6）。
- 多编辑器实例 / 多 MCP 客户端并发。

### 2.3 永久非目标

- **不对外暴露服务**：仅 `127.0.0.1` + `--mcp-port` flag 显式启动，默认零监听。这是开发机单用户工具，不是远程服务。
- **不绕过命令栈直接改 World**：所有写操作走 `CommandStack`，AI 操作必须可被用户 undo（ADR-020 决策 3 / invariant ②）。
- **不在 C++ 侧实现 MCP 协议**：C++ 只维护「收 JSON → 帧末执行 → 回 JSON」窄接口，MCP 协议归 Python（ADR-020 决策 1）。
- **不模拟 GUI 输入**：MCP 操作的是编辑器的**数据与命令层**（命令栈 / schema / pending op），不是合成鼠标点击 ImGui 控件。ImGui 交互手感（拖 gizmo、拖 timeline key）仍属用户 dogfood 范畴——MCP 验证的是这些交互背后的数据结果与视觉结果。

---

## 3. 协同用例（需求的灵魂）

以下用例全部贴合首游（2.5D Ori-like，灰盒优先、流体史莱姆主角）的真实开发节奏。每条注明用到的 MCP 能力（tool 名见 §4）。

### UC-1 · AI 搭灰盒关卡（P0）

- **人**：口头描述布局——「这一屏：左边一个 3 格高的平台，中间一道沟，右边斜坡上去，出口在右上」。
- **AI**：`begin_undo_group` → 批量 `create_entity` + `add_component`（Renderable 预置 cube+PBR）+ `set_field`（Transform.position/scale 摆成平台/沟/坡）→ `end_undo_group` → `frame_entity`/`set_camera` 调好构图 → `capture_viewport` 把成品图给人看。
- **人**：看图微调——「沟太窄了，加宽一格」；或直接在编辑器里手动拖，AI `get_entity` 读回人改后的值继续。整组 AI 操作在命令栈里是**一条** undo 记录，人不满意一键回退。
- **能力**：create_entity / add_component / set_field / undo group / capture_viewport / frame_entity。**产出**：可玩灰盒布局，dirty 标记自动点亮，人按 Ctrl+S 或 AI `save_scene`。

### UC-2 · AI 调试「这个实体怎么看不见」（P0）

- **人**：「我拖进来的鳄梨模型在 viewport 里看不到」。
- **AI**：`find_entities`（按名）→ `get_entity` 逐项排查——Renderable.visible？mesh 路径空？materialInstance 空？Transform.scale 是 0？position 在相机背后？→ `frame_entity` + `capture_viewport` 确认视觉 → 定位根因（如导入 scale 0.01 太小）→ `set_field` 修复 → 截图复验。
- **能力**：find_entities / get_entity / set_field / frame_entity / capture_viewport。**产出**：几分钟内闭环的可视化 debug，替代「人描述症状 → AI 盲猜代码」。

### UC-3 · 人摆好场景，AI 调灯光与后处理氛围（P0）

- **人**：把关卡摆好，说「给我一个黄昏的氛围，主角周围要有暖光」。
- **AI**：`get_scene_info` 找到 DirectionalLight / PointLight / PostProcess 实体 → `set_field` 调 DirectionalLight.color/intensity、主角 PointLight 的 color/haloEnabled/haloIntensity（Ori-like 发光主角的既有通道）、PostProcess 的 gradeTemperature/gradeExposure/bloom 相关字段 → 每轮 `capture_viewport` 自评 → 收敛后给人 2-3 个备选截图。
- **能力**：get_scene_info / set_field（PostProcess 40+ 字段全走 schema，零专用代码）/ capture_viewport。**产出**：氛围 lookdev 迭代从「人逐字段拖滑条」变成「人审美拍板、AI 跑参数」。注意：首版截图为 PBR 直出（无 bloom/tonemap，ADR-020 Q7），调 bloom 类字段的视觉确认需依赖用户屏幕或等 P1 后处理一致截图。

### UC-4 · Blender ↔ OrangeEditor 双 MCP 资产管线（P1）

- **AI**：在 Blender MCP 里建模/调材质（已验证的既有工作流）→ 导出 .glb/.fbx → 切到 orange-mcp `import_asset`（含 `--scale` 同款 importScale 参数）→ `create_entity` + `set_field` 挂上导入的 mesh → `capture_viewport` 对照 Blender 渲染图验证导入保真（贴图、多材质 sub-mesh、轴向）。
- **人**：只在两端看图拍板。
- **能力**：import_asset / list_assets / create_entity / set_field / capture_viewport。**产出**：「DCC 建模 → 引擎内验证」全程 AI 自驱，导入器的视觉残留项（dogfood-checklist 上的 glb 贴图等）顺带被 AI 扫掉。

### UC-5 · AI 协同 PIE 调玩法（P1，依赖 B1 EnterPlay 接 ScriptSystem）

- **人**：写好/挂好 C# 移动脚本（ScriptComponent），说「跳跃手感太飘」。
- **AI**：`play` 进 Play 模式 → 间隔 `capture_viewport` + `get_entity`（读主角 Transform 轨迹）观察脚本行为 → `stop`（World 快照还原）→ `set_field` 调 ScriptComponent fieldOverrides 的 jumpImpulse / gravityScale（B1.3 已有引擎层）→ 再 `play` 复验 → 收敛后报告参数对比。
- **能力**：play/pause/resume/stop / get_entity / set_field / set_script_field / capture_viewport。**产出**：玩法调参的「改-跑-看」循环 AI 可自驱；这是 MCP 与 B1 PIE epic 的协同放大点（设计文档 §15）。

### UC-6 · AI 审计并修复场景（P1）

- **人**：「这个场景是早期版本攒的，帮我清一遍」。
- **AI**：`get_scene_info` 全量拉树 → 逐实体 `get_entity` 找异常——mesh/material 引用空串（资产丢失）、重名实体、scale 为负、Renderable 挂了但 visible=false 的「僵尸」、Environment 重复挂（schema Helper 已注明全局单例语义）→ 产报告 → 人确认后 `begin_undo_group` 批量修复 → 截图前后对比。
- **能力**：get_scene_info / get_entity / list_component_types（读 schema 元数据辅助判断 range 越界）/ set_field / delete_entity / undo group。**产出**：场景健康报告 + 一条可整体回退的修复记录。

### UC-7 · AI 创作关键帧动画（P2）

- **人**：「给这个平台做一个 4 秒往返浮动，缓入缓出」。
- **AI**：`get_animation_clip` 读现有 .anim（或从模板起）→ 按 B2 已落地的 clip 数据模型写 position.y 轨道关键帧 + Bezier 缓动 → `set_animation_clip` 经 SetAnimationClipCommand 写回（可 undo）→ `preview_animation` 触发编辑期预览 tick（EditorAnimationPreviewState，与 Play 互斥）→ 连续 `capture_viewport` 抽帧确认运动曲线 → 保存 .anim。
- **能力**：get/set_animation_clip / preview_animation / capture_viewport。**产出**：关键帧动画创作不再只能靠 timeline GUI 手拖；B2 GUI 的数据层（headless 已锁）获得第二个消费者。

### UC-8 · AI 视觉回归扫描（dogfood 放大器，贯穿各期）

- **AI**（定期/被指派）：对 `docs/dogfood-checklist.md` 上「数据可驱动 + 结果可截图」的条目逐项跑——如「动画呼吸视觉」（挂 Animator → preview → 抽帧比对）、「多材质 sub-mesh 渲染」（摆 fixture mesh → 截图查色块）、「prefab override 蓝条背后的数据」（改实例字段 → 查 overriddenPaths）→ 把「AI 已验 / 仍需人验手感」分类回写报告。
- **人**：只 dogfood 剩下的纯交互手感项（拖拽、hover、键位）。
- **能力**：几乎全部读类 tool + capture_viewport。**产出**：dogfood 积压从结构性瓶颈降为「交互手感专项」。这是 MCP 对项目成熟度的最大杠杆（ADR-020 Consequences）。

---

## 4. 功能需求（tool 全景，P0/P1/P2）

### 4.0 通用契约约定（适用于所有 tool）

以下契约为**每个 tool 的默认行为**，各表只标注偏离项：

- **实体句柄一律 EntityGuid 字符串**（ADR-020 决策 4）。guid 失效（实体已删/场景已换）→ 返回 `ok:false, error:"entity not found"`，不崩、不部分执行。命令端在 `get_scene_info` / 启动时跑幂等 `EnsureEntityGuids`，保证所有实体可寻址。
- **组件与字段寻址一律走 `ComponentSchemaRegistry`**（决策 5 / schema-first 纪律）：`component` 参数 = schema typeName（"Transform"、"PointLight"…），`field` 参数 = schema 字段 name（"position"、"desc.emissionRate"…）。**禁止**任何 per-component 专用 tool / 专用分支——新增组件类型 MCP 自动支持，不改 MCP 代码。
- **值编解码按 PropertyType 分派**（15 种：Float/Int/UInt/Bool/Vec2/Vec3/Vec4/Quat/String/Enum/EntityRef/AssetRef/AssetRefArray/PolygonVertices/EdgeChainVertices）：数值/布尔 → JSON 原生；向量/quat → number array；Enum → 字符串（按 enumNames）；AssetRef → 资产路径字符串；EntityRef → 目标实体 guid 字符串；两类顶点表 → number array。首版顶点表可只读（写留 P1）。
- **写操作返回 `undoable: true|false` 字段**：走命令栈的为 true；现状无法 undo 的操作（delete_entity、prefab apply/revert、资产文件 IO）必须显式返回 false，让 AI 在执行前知情并向用户声明。
- **错误模型**：未知 op / 参数缺失或类型错 / 越界值 / 失效引用，一律 `ok:false` + 人类可读 error，编辑器不崩、World 不进入半改状态。
- **幂等性**：读类 tool 全部幂等；写类 tool 除注明者外**不**幂等（重复调用产生重复实体/重复命令记录）。
- **执行时机**：所有命令在主线程帧末执行（决策 2），响应在执行完成的那帧返回——AI 拿到结果即已落地，紧接着的读操作能看到刚才的写。

### 4.A 会话与能力发现

| tool | 用途 | 输入 | 输出 | 错误/边界 | 命令栈 | 幂等 | 优先级 |
|---|---|---|---|---|---|---|---|
| `ping` | 连通性/版本握手 | — | `{editorVersion, protocolVersion, sceneName}` | — | 否 | ✅ | **P0**（M0） |
| `list_component_types` | 枚举 schema 注册表：全部组件 typeName + 每字段 {name, type, range, enumNames, assetKind} + addable/removable | — | 组件元数据数组 | — | 否 | ✅ | **P0** |
| `get_editor_state` | 轻量状态快照：playState(Edit/Play/Paused)、currentScenePath、dirty、当前选中 guid、gizmo 模式 | — | 状态对象 | — | 否 | ✅ | P1 |

> `list_component_types` 是 schema-first 的自然产物（注册表已含全部元数据），让 AI **自发现**能力面——当前约 17 个内置组件（Name/Transform/Hierarchy/Renderable/SubMeshMaterials/DirectionalLight/PointLight/SpotLight/Environment/PostProcess/ParticleEmitter/RigidBody/Collider/AudioSource/Animator/Camera/Script）+ 游戏侧扩展（HealthComponent demo 同路径）。新组件注册即出现在结果里，AI 不需要硬编码任何组件知识。

### 4.B 场景查询（读）

| tool | 用途 | 输入 | 输出 | 错误/边界 | 命令栈 | 幂等 | 优先级 |
|---|---|---|---|---|---|---|---|
| `get_scene_info` | 实体树：每实体 {guid, name, parentGuid, 组件名列表} | — | 树/扁平数组 | 大场景需上限+truncated 标记（§5 性能） | 否 | ✅ | **P0** |
| `get_entity` | 单实体全字段：遍历 has==true 的 schema，逐字段 get 编 JSON（ADR-020 Q3 选 A：结构化 `{component:{field:value}}`） | `guid` | 组件字段字典 | guid 失效→error | 否 | ✅ | **P0** |
| `find_entities` | 按条件过滤：名字子串 / 含某组件 / 父子树范围 | `name?`, `component?`, `underGuid?` | guid+name 数组 | 无命中→空数组非 error | 否 | ✅ | P1 |

### 4.C 可视反馈（截图与相机）

| tool | 用途 | 输入 | 输出 | 错误/边界 | 命令栈 | 幂等 | 优先级 |
|---|---|---|---|---|---|---|---|
| `capture_viewport` | 离屏渲染当前场景并回读像素，MCP 侧转 PNG image content | `width?`, `height?`（默认 viewport 当前尺寸） | 图像 | 尺寸上限（如 4096²）防 w*h*4 溢出/显存爆；含 WaitIdle 的帧级卡顿可接受、不可高频轮询 | 否 | ✅ | **P0**（M1） |
| `frame_entity` | 相机对准实体（复用 FrameSelectedCamera；不传 guid = Frame All） | `guid?` | 新相机参数 | guid 失效→error；无 AABB 实体取 position | 否（相机非场景数据） | ✅ | P1 |
| `get_camera` | 读编辑器相机（pivot/azimuth/elevation/radius/fov） | — | 相机参数 | — | 否 | ✅ | P1 |
| `set_camera` | 写编辑器相机（AI 自主构图截图；亦覆盖 Standard Views 六向） | 相机参数（部分可省） | 生效后参数 | 非法值 clamp 到合法域 | 否（相机非场景数据，不产 undo 记录，与手动转相机一致） | ✅ | P1 |

> **已知限制（需求级声明）**：首版截图走 `Pipeline::RenderToTexture` = PBR 直出，不含 bloom/tonemap 等后处理（ADR-020 Q7）。看几何/布局/材质/光照方向足够；像素级一致（调后处理氛围必需）列为 P1 演进项「`capture_viewport` 含后处理路径」，不是新 tool。

### 4.D 实体生命周期

| tool | 用途 | 输入 | 输出 | 错误/边界 | 命令栈 | 幂等 | 优先级 |
|---|---|---|---|---|---|---|---|
| `create_entity` | 建空实体（Name+Transform+Hierarchy+guid），可指定父 | `name?`, `parentGuid?` | 新实体 `guid` | 父 guid 失效→error 不建 | ✅ CreateEntityCommand | ❌ | **P0** |
| `delete_entity` | 删实体及子树 | `guid` | `{undoable:true}` | guid 失效→error | ✅ **已命令化可 undo**（2026-06-12：editor pendingDelete 消费路径改为 SaveSubtreeToString + do=DestroySubtree / undo=LoadFromString 精确复位，不再 Clear 栈）；契约由 undoable:false 升级为 true（ADR-020 Q5 预留的"命令化后升级"，协议字段不变纯行为增强）。极端兜底：子树序列化失败时该帧退化为不可 undo 的直接销毁 | ✅（重复删已删→error 无副作用） | **P0** |
| `duplicate_entity` | 复制子树（复用 Duplicate 路径：序列化+SeparateClonedIdentities+ReassignEntityGuids，克隆体新 guid） | `guid` | 新根 `guid` | guid 失效→error | ✅ | ❌ | P1 |
| `reparent_entity` | 改父（复用 EditorHierarchy keep-world 变体，世界位姿保持） | `guid`, `newParentGuid?`(空=提为根), `keepWorld?=true` | ok | 环检测（把祖先挂到后代下）→error | ✅ | ❌（同参重复=有序无变化，可视为幂等） | P1 |
| `set_entity_order` | 根级 sibling 重排（sortIndex，对应右键 Move Up/Down） | `guid`, `direction` 或 `index` | ok | 非根实体→error | ✅ | ❌ | P2 |

### 4.E 组件与字段（写核心）

| tool | 用途 | 输入 | 输出 | 错误/边界 | 命令栈 | 幂等 | 优先级 |
|---|---|---|---|---|---|---|---|
| `set_field` | 改任意组件任意字段（schema get/set → SetFieldValueCommand\<T\>，与 Inspector 完全同路径） | `guid`, `component`, `field`, `value` | 旧值+新值 | 组件未挂/字段不存在/类型不匹配/Enum 名非法→error；有 range 的字段 clamp 或 error（建议 clamp+返回实际值） | ✅（连续同字段微调经 Merge coalesce） | ✅（同值重写无净效果） | **P0** |
| `add_component` | 挂组件（schema.add；Renderable 走 AddableWith 预置 cube+PBR 材质，与 Inspector +Add 一致） | `guid`, `component` | ok | 已挂→error；schema 无 add（Hierarchy/Name/SubMeshMaterials）→error 并列出可加项 | ✅ | ❌（重复加→error） | **P0** |
| `remove_component` | 卸组件（schema.remove，Removable 者） | `guid`, `component` | `{undoable}` | 未挂/不可移除→error | ✅（复用 Inspector Remove 的状态快照还原路径） | ❌ | P1 |
| `begin_undo_group` | 开命令组：之后的写操作合并为单条 undo 记录（CommandStack::BeginGroup） | `label?` | ok | 嵌套开组→error；**护栏**：组打开超时（如 30s）或连接断开自动 EndGroup，防 AI 忘关把栈卡死 | ✅ | ❌ | P1 |
| `end_undo_group` | 合组（CommandStack::EndGroup） | — | 组内命令数 | 无打开的组→error | ✅ | ❌ | P1 |

> **不提供 `undo` / `redo` tool（决策建议，待拍板，§8）**：命令栈是 AI 与用户共享的——AI 调 undo 可能撤掉用户刚做的手动操作。AI 改错的恢复路径 = ① 用户 Ctrl+Z（AI 操作在同一栈里）；② AI 用 set_field 反向写回（它拿到过旧值）。

### 4.F 选中与场景文件

| tool | 用途 | 输入 | 输出 | 错误/边界 | 命令栈 | 幂等 | 优先级 |
|---|---|---|---|---|---|---|---|
| `select_entity` | 设编辑器选中（让用户看到 AI 正在操作谁；Inspector 联动显示） | `guid`（空=清空选中） | ok | guid 失效→error | 否（UI 状态） | ✅ | **P0** |
| `save_scene` | 保存场景（复用 pendingSceneOp=Save；path 非空先设 currentScenePath 走 SaveAs 语义） | `path?` | 实际保存路径 | 写盘失败→error；保存成功 dirty 自动清零 | 否（文件 IO） | ✅ | **P0** |
| `open_scene` | 打开 .scene.json（复用 SceneOp::Open 同款 + 双击打开路径） | `path`, `force?=false` | ok | 文件不存在→error；**dirty 保护**：dirty 且未传 force→error "unsaved changes"（防 AI 默默丢掉用户未保存工作，§8 Q-c） | 否 | ✅ | P1 |
| `new_scene` | 新建空场景 | `force?=false` | ok | 同上 dirty 保护 | 否 | ❌ | P2 |

### 4.G Play 调试（P1 epic；脚本 tick 依赖 B1 EnterPlay 接 ScriptSystem——该接线已留待用户 session，MCP 不抢跑）

| tool | 用途 | 输入 | 输出 | 错误/边界 | 命令栈 | 幂等 | 优先级 |
|---|---|---|---|---|---|---|---|
| `play` | Edit→Play（复用 PlayOp::EnterPlay 帧末管道：World 快照落盘+物理/VFX/动画开 tick） | — | ok | 已在 Play→error | 否（模式切换；快照机制保证 Stop 还原） | ❌ | P1 |
| `pause` / `resume` | Play↔Paused（AI 可暂停在某帧 get_entity+截图细看） | — | ok | 状态不符→error | 否 | ❌ | P1 |
| `stop` | 还原快照回 Edit（Play 期全部改动丢弃——A2.3 已锁 guid 经快照往返稳定，AI 持有的 guid 跨 Play 有效） | — | ok | 已在 Edit→error | 否 | ❌ | P1 |

> Play 期的读类 tool（get_entity / capture_viewport）照常工作——这正是「观察玩法行为」的核心。**Play 期写操作（set_field 等）的语义是开放问题**（改动会被 Stop 还原，AI 可能误以为改动持久）：建议默认拒绝写、返回 "in play mode"，留 `allowInPlay` 逃生门（§8 Q-b）。

### 4.H 资产（P1/P2）

| tool | 用途 | 输入 | 输出 | 错误/边界 | 命令栈 | 幂等 | 优先级 |
|---|---|---|---|---|---|---|---|
| `list_assets` | 枚举 assets/ 下资产，按 AssetKind 过滤（Mesh/Material/Texture/Scene/Sound/AnimationClip）——AI 给 AssetRef 字段赋值前必须能查到合法路径 | `kind?`, `pathPrefix?` | 路径数组 | — | 否 | ✅ | P1 |
| `import_asset` | 导入外部资产（复用 ImportDispatcher：.png/.jpg/.jpeg/.tga/.hdr/.obj/.gltf/.glb/.fbx；经 pendingImports 帧末队列，与拖拽同路径） | `srcPath`, `scale?`（FBX importScale，CLI --scale 同款，默认 1.0） | 产物资产路径 | 不支持的扩展名→error 列出支持表；导入失败透传 importer 错误 | 否（资产文件 IO） | ❌（重复导入覆盖/重名由 importer 现行为决定） | P1 |
| `create_material` | 建 .material（MaterialFileIO + 模板：选 shader template、写默认 uniforms） | `path`, `templateName` | 资产路径 | 模板不存在→error | 否（文件 IO，undoable:false） | ❌ | P2 |
| `set_material_param` | 改 .material 的 uniform/贴图槽并落盘（Material Inspector 数据侧同路径） | `path`, `param`, `value` | ok | 参数不在模板 meta 内→error | 否（资产 IO，undoable:false；viewport 热生效与否按现状如实返回） | ✅ | P2 |

### 4.I Prefab（P2 epic；引擎层 C1 已闭环，UI dogfood-pending）

| tool | 用途 | 输入 | 输出 | 错误/边界 | 命令栈 | 幂等 | 优先级 |
|---|---|---|---|---|---|---|---|
| `create_prefab` | 子树落盘 .prefab.json（复用 CommitNewPrefabFile） | `rootGuid`, `path` | 资产路径 | guid 失效/写盘失败→error | 否（文件 IO） | ❌ | P2 |
| `instantiate_prefab` | 实例化（InstantiatePrefabCommand，ReassignEntityGuids 防碰撞） | `path`, `parentGuid?` | 新根 guid | 文件不存在→error | ✅ | ❌ | P2 |
| `get_prefab_status` | 实例的 override 状态：模板路径 + overriddenPaths 列表（消费 IsPathOverridden 持久化集） | `guid` | override 路径数组 | 非 prefab 实例→error | 否 | ✅ | P2 |
| `revert_override` / `apply_instance` | 单路径回模板值 / 实例推回模板（复用 PrefabOverrideUI 动作） | `guid`, `path?` | `{undoable:false}` | **现状不可 undo**（资产 IO / 缺 typed by-path 原语），必须显式声明 | ⚠️ 否 | ❌ | P2 |

### 4.J 动画创作（P2 epic；B2 数据层 headless 已锁）

| tool | 用途 | 输入 | 输出 | 错误/边界 | 命令栈 | 幂等 | 优先级 |
|---|---|---|---|---|---|---|---|
| `get_animation_clip` | 读实体 Animator 当前 clip 或 .anim 文件（schema `animation/Clip` v1.1 JSON：tracks/keyframes/interp/events） | `guid` 或 `path` | clip JSON | 无 Animator/非 clip backend→error | 否 | ✅ | P2 |
| `set_animation_clip` | 整 clip 写回（经 SetAnimationClipCommand copy-modify-SetClip，与 timeline GUI 同一命令） | `guid`, `clipJson` | ok | clip JSON 校验失败（key 乱序/未知 interp）→error 不落 | ✅ | ✅ | P2 |
| `preview_animation` | 编辑期预览（EditorAnimationPreviewState：play/pause/seek 单 animator tick；与 PlayState::Play 互斥） | `guid`, `action`(play/pause/seek), `time?` | 当前预览时间 | Play 模式中→error（互斥） | 否（预览态不改场景数据） | seek ✅ | P2 |

> AnimFsm 状态机图编辑（AnimFsmCommands 全套已可 undo）暂不入清单——AI 创作 FSM 的需求等首游玩法真实拉动再立项，避免设计 AI 用不到的能力。

### 4.K C# 脚本协同（P2；依赖 B1）

| tool | 用途 | 输入 | 输出 | 错误/边界 | 命令栈 | 幂等 | 优先级 |
|---|---|---|---|---|---|---|---|
| `set_script_field` | 改 ScriptComponent fieldOverrides（B1.3 引擎层已落：C# 反射 SetInstanceField）——UC-5 调玩法参数的入口 | `guid`, `fieldName`, `value` | `{undoable}` | 无 ScriptComponent/字段名不存在→error；fieldOverrides 当前 Inspector 路径不可 undo，如实返回 | ⚠️ 按落地现状 | ✅ | P2 |

### 4.L 可观测性

| tool | 用途 | 输入 | 输出 | 错误/边界 | 命令栈 | 幂等 | 优先级 |
|---|---|---|---|---|---|---|---|
| `get_editor_log` | 拉取编辑器最近日志/错误（AI 操作触发的引擎告警可见——如资产加载失败、shader 告警） | `lines?`, `minLevel?` | 日志行数组 | — | 否 | ✅ | P2 |

### 4.M 优先级汇总

| 优先级 | tool 数 | 清单 |
|---|---|---|
| **P0（首版读写闭环，M0–M2）** | **11** | ping, list_component_types, get_scene_info, get_entity, capture_viewport, select_entity, create_entity, delete_entity, set_field, add_component, save_scene |
| **P1** | **17** | get_editor_state, find_entities, frame_entity, get_camera, set_camera, duplicate_entity, reparent_entity, remove_component, begin/end_undo_group, open_scene, play, pause, resume, stop, list_assets, import_asset（+演进项：截图含后处理） |
| **P2** | **14** | set_entity_order, new_scene, create_material, set_material_param, create_prefab, instantiate_prefab, get_prefab_status, revert_override, apply_instance, get_animation_clip, set_animation_clip, preview_animation, set_script_field, get_editor_log |

---

## 5. 非功能需求

| # | 需求 | 契约 |
|---|---|---|
| NF-1 | **实时性** | 普通命令在请求到达后的下一个帧边界执行（60fps 下端到端延迟典型 < 50ms，含 socket 往返）。`capture_viewport` 例外：含离屏渲染 + WaitIdle，单次可达数百 ms 且让该帧卡顿——可接受，但 AI 侧 tool 描述需注明「非高频操作」。命令队列堆积不得阻塞 ImGui 帧（drain 在帧末，单帧 drain 全部积压）。 |
| NF-2 | **线程安全（单线程红线）** | socket 线程**永不**触碰 World/ImGui/Vulkan/EnTT；只读写 mutex 保护的 in/out 队列（ADR-020 invariant ①）。这是约束不是偏好，违反 = 架构 bug。 |
| NF-3 | **undo 一致性** | 全部场景数据写操作走 `cmdStack`（invariant ②）；AI 与用户操作在同一栈、统一 Ctrl+Z；dirty 标志经 CommandStack onChanged 自动联动。无法走命令栈的操作必须 `undoable:false` 显式声明（§4.0）。 |
| NF-4 | **句柄稳定性** | 实体引用一律 EntityGuid（A2/ADR-018 成果）：跨命令、跨场景 Save/Load、跨 Play 快照往返（A2.3 已锁）稳定。EnTT id / 实体名不得作为跨命令句柄。 |
| NF-5 | **安全** | `--mcp-port` 默认不传 = 零监听零行为变化；仅绑 127.0.0.1；无鉴权（本机单用户，对标 Blender MCP）；未来跨机需求再加 token，不在本规格内。 |
| NF-6 | **健壮性** | 任意非法输入（畸形 JSON、未知 op、错类型参数、越界尺寸、失效 guid、半截断连接）不得让编辑器崩溃或 World 半改。重点边界（对照本仓 bug-hunt 已知类别）：截图 w*h*4 整数溢出、数值字段无范围校验、字符串字段超长、命令组未闭合再入。命令端对每条命令 try/catch 级兜底，单条失败不影响后续命令与编辑器本身。 |
| NF-7 | **可观测性（用户视角）** | 用户必须能看见「AI 正在做什么」：① MCP 连接状态在编辑器 UI 可见（如状态栏指示）；② AI 的写操作出现在命令栈（Edit 菜单 Undo 标签即操作名）；③ MCP 命令流水写编辑器日志（op + 参数摘要 + 结果），出问题可回放定位。 |
| NF-8 | **协议向后兼容** | 请求/响应带协议版本（`ping` 握手协商）；命令端对未知 op 返回 error 而非断连——旧编辑器 + 新 Python server（或反之）的组合 graceful 降级，不强制同步升级。命令集只增不改语义（同 schema version 出厂冻结纪律的精神）。 |
| NF-9 | **性能（大场景）** | `get_scene_info` / `find_entities` 对大场景（千级实体）设上限 + `truncated` 标记 + 分页参数，防单响应爆 token / 爆帧预算；遍历是只读 registry 扫描，不得引入每帧常驻成本（无 MCP 连接时零开销）。 |
| NF-10 | **schema-first 纪律** | MCP 不得给 EditorState/mega-class 加 per-component 分支（invariant ③）；组件读写、能力发现一律经 ComponentSchemaRegistry。验收信号：新增一个组件 schema 后，`list_component_types` / `get_entity` / `set_field` / `add_component` 对它**零代码改动**即工作。 |

---

## 6. 分期与优先级

### 首版（P0 = 读写协同闭环，对齐设计文档 M0→M1→M2）

| 期 | 内容 | 交付判据 |
|---|---|---|
| **M0 · spike 通路** | `--mcp-port` flag + 后台 socket 线程 + in/out 队列 + `ApplyPendingMcpCommands` + `ping`/`get_scene_info`；Python 最小 MCP server 连通 | 端到端打通（技术最高风险点前置：winsock 线程 + 帧末 marshal + Python 桥） |
| **M1 · 读闭环** | `get_entity`（schema 逐字段）+ `list_component_types` + `capture_viewport`（含 `Pipeline::CaptureToCpu` 引擎 API） | AI 能完整「看」：场景结构 + 画面 |
| **M2 · 写闭环** | `create_entity` / `set_field` / `add_component` / `delete_entity` / `select_entity` / `save_scene`（全走命令栈，delete 标注不可 undo） | AI 能「改 + 截图自验」；**首版完整** |

### 后续 epic（每条一句话需求 + 依赖）

| epic | 一句话需求 | 优先级 | 依赖 |
|---|---|---|---|
| **E1 · 协同效率包** | find/duplicate/reparent/remove_component/undo group/相机控制/get_editor_state——把 UC-1/2/6 从「能做」提到「顺手」 | P1 | 首版 |
| **E2 · 资产管线** | list_assets + import_asset，打通 Blender MCP → OrangeEditor 双 MCP 工作流（UC-4） | P1 | 首版；复用 ImportDispatcher 现状 |
| **E3 · Play 调试协同** | play/pause/resume/stop + Play 期读与截图（UC-5） | P1 | **B1 EnterPlay 接 ScriptSystem**（用户 session）；无脚本时仅物理/VFX/动画 tick 也已有观察价值 |
| **E4 · 截图后处理一致** | capture_viewport 走含 bloom/tonemap 的路径，AI 所见 = 用户所见（UC-3 完全体） | P1 后段 | 引擎 Pipeline 截图路径扩展（Q7） |
| **E5 · prefab 协同** | create/instantiate/override 状态读 + revert/apply（标注不可 undo） | P2 | C1.1 UI dogfood 通过（数据层已齐） |
| **E6 · 动画创作协同** | clip 读写 + 编辑期预览驱动（UC-7） | P2 | B2 数据层（已齐）；预览互斥语义 |
| **E7 · 脚本字段协同** | set_script_field 调玩法参数 | P2 | B1.3（引擎层已落）+ E3 |
| **E8 · 可观测/杂项** | get_editor_log、set_entity_order、材质资产编辑、new_scene | P2 | 各自独立 |

> 排期原则：每期独立可 dogfood；P1 内 E1/E2 不依赖任何未落地引擎能力可先行，E3 卡 B1 接线、E4 卡 Pipeline 扩展。**MCP 自身全程零跨仓**（截图 readback OrangeRender RHI 已公共 API）。

---

## 7. 每期验收标准（可观察，多数 dogfood-gated）

MCP 是跨进程 + GUI + 网络行为，headless 测不到核心路径，验收以「AI 做 X → 用户在编辑器看到 Y」的形式定义；落地时逐批登记 `docs/dogfood-checklist.md`。

### M0
- [ ] 不带 `--mcp-port` 启动：行为与现状完全一致，`netstat` 无新监听端口。
- [ ] 带 flag 启动 + Claude 连上 orange-mcp：`ping` 返回版本；`get_scene_info` 返回的实体树与 Hierarchy 面板**逐项一致**（用户对照）。
- [ ] Python server 断开/重连，编辑器不崩、不卡帧。

### M1
- [ ] AI `get_entity` 读某实体，回报的 Transform/组件字段值与 Inspector 显示一致（含 Enum 名、AssetRef 路径）。
- [ ] AI `capture_viewport` 后能**正确描述用户屏幕上看到的场景内容**（用户对照判定——这是「AI 看见了」的直接证据）。
- [ ] 新挂一个此前没读过的组件类型，`get_entity` 无需改 MCP 代码即正确返回其字段（NF-10 验收信号）。

### M2（首版完整）
- [ ] AI `create_entity` + `add_component`(Renderable)：用户**实时**看到 viewport 出现白色 cube、Hierarchy 出现新节点。
- [ ] AI `set_field` 改 Transform.position：viewport 实体移动；用户 **Ctrl+Z 撤销 AI 的改动**成功；Ctrl+Y 重做成功。
- [ ] AI 连续微调同一字段 10 次：undo 栈合并为一条（coalesce 生效），不是 10 条。
- [ ] AI `set_field` 传错组件名/字段名/类型：返回 error，编辑器无感知、不崩。
- [ ] AI `delete_entity`：实体消失，tool 返回 `undoable:false`。
- [ ] AI 改动后场景标题出现 dirty 标记；`save_scene` 后消失、文件落盘 reload 验证。
- [ ] 走一遍 UC-1：人口述布局 → AI 搭灰盒 → 截图 → 人微调 → 保存，全程无需人替 AI 执行任何编辑操作。

### E1–E3（P1，节选关键判据）
- [ ] E1：AI `begin_undo_group` 摆 20 个实体 `end_undo_group`，用户一次 Ctrl+Z 全部回退；组打开 30s 未关自动闭合。
- [ ] E1：AI `reparent_entity(keepWorld)` 后实体世界位姿不动（viewport 无跳变）。
- [ ] E2：走一遍 UC-4——Blender MCP 导出 .glb → orange-mcp `import_asset` → 挂实体 → 截图与 Blender 渲染对照，贴图/多材质正确。
- [ ] E3：AI `play` → 用户看到 toolbar 进 Play、物理开始 tick → AI 截图 + 读实体位置 → `stop` → 场景还原且 AI 持有的 guid 仍然有效（A2.3 性质的端到端复验）。
- [ ] E3：Play 期 AI `set_field` 默认被拒绝并收到 "in play mode" error（按 §8 Q-b 拍板结果调整）。

### E4–E8（P2，定型时细化）
- E4：同一场景「AI 截图」与「用户屏幕截图」逐像素色调一致（容差比对，可借 OrangeRender golden-image 双阈值方法）。
- E5/E6/E7：各自完成 §3 对应用例（UC-7 等）的完整走查 + 不可 undo 操作全部正确声明。

---

## 8. 风险与开放问题

### 8.1 技术风险（沿架构文档，需求视角补充）

| 风险 | 需求侧含义 / 缓解 |
|---|---|
| winsock 后台线程是编辑器**首个真后台线程** | M0 单独成期、先 spike；NF-2 红线 + ADR-020 invariant ① 守住；mutex 只围 in/out 队列 |
| 截图与 viewport 最终画面有后处理色差（Q7） | 需求级声明（§4.C）：P0 接受、AI 侧 tool 描述注明；E4 收敛；**风险**：AI 在 P0 期被派去调 bloom 类参数会自验失真——用例分配上 UC-3 的后处理部分推迟到 E4 |
| delete 不可 undo（Q5） | `undoable:false` 显式契约 + tool 描述要求 AI 删前确认；删除命令化落地后 MCP 契约升级为 true（协议字段不变，纯行为增强） |
| AI 高频轮询截图拖垮编辑器帧率 | NF-1 注明非高频 + 可考虑命令端对 capture 限频（如最小间隔 500ms），P1 视 dogfood 体感决定 |
| 大场景 get_scene_info 爆 token / 爆帧 | NF-9 上限+分页；AI 工作流上引导用 find_entities 缩范围 |
| AI 与用户**同帧并发编辑同一字段** | 命令栈天然串行化（都在主线程帧末），无数据竞争；但语义上可能互相覆盖——可观测性（NF-7）让用户看见 AI 动了什么，P1 后视实际摩擦决定是否加「AI 操作高亮」类 UI |

### 8.2 产品开放问题（需求层面，最值得拍板的标 ★）

> **2026-06-12 用户拍板：以下 7 问全部按「建议」列采纳为最终裁定**（★ 三项 = 不给 undo/redo tool / Play 期写默认拒绝 + `allowInPlay` 逃生门 / 批量用 `begin/end_undo_group` + 超时护栏）。同步固化于 [ADR-020](../../Orange-Wiki/case-studies/orange-engine/decisions/ADR-020-editor-mcp-realtime-coediting-bridge.md) Decision 末表。

| # | 问题 | 建议（= 最终裁定） |
|---|---|---|
| ★ Q-a | **是否给 AI `undo`/`redo` tool？** 共享命令栈意味着 AI 的 undo 可能撤掉用户的手动操作；但没有它，AI 撤错只能反向重写。 | **不给**（§4.E）：AI 改错由用户 Ctrl+Z 或 AI 反向 set_field 恢复；undo group 让「整组回退」由用户一键完成。若实践中 AI 频繁需要自撤，再以「只能撤 AI 自己最近命令」的受限形式引入。 |
| ★ Q-b | **Play 期写操作语义**：Play 中 set_field 的改动会被 Stop 还原，AI（和用户）可能误以为改动持久。 | 默认拒绝（"in play mode" error），保 AI 心智模型简单；保留 `allowInPlay:true` 逃生门给「Play 中实时调参观察手感」的真实需求（Unity 同款心智：Play 改动丢失是新手第一坑，对 AI 同样成立）。 |
| ★ Q-c | **批量操作 = undo group 工具，还是单条 batch 命令？** UC-1/UC-6 都需要「N 个操作一条 undo」。 | 提供 begin/end_undo_group（复用 CommandStack 现成机制，灵活）+ 超时/断连自动闭合护栏；不另设 batch mega-tool（会催生绕过 schema 的复合协议）。 |
| Q-d | open_scene 的 dirty 保护：拒绝 vs force flag vs 自动 save？ | dirty 且无 force → error（§4.F）；绝不自动 save（AI 不该替用户决定落盘）。 |
| Q-e | `get_scene_info` 是否内联每实体的 Transform 摘要（position）？ | 建议内联 position（AI 空间推理几乎每次都要，省一轮 get_entity 风暴）；其余字段仍走 get_entity。 |
| Q-f | MCP 操作是否触发 autosave 节流计时？ | 与手动操作一致（dirty 即触发现有 autosave 机制），不特殊化。 |
| Q-g | 顶点表字段（PolygonVertices/EdgeChainVertices）写支持时机？ | 首版只读；首游灰盒地形若真用 EdgeChain 碰撞链创作，再升 P1（按拉动，不预做）。 |

### 8.3 需求级 anti-goals 重申

不设计以下 tool（评估过、明确排除）：模拟鼠标/键盘事件（§2.3）；逐组件专用读写 tool（违反 schema-first）；绕命令栈的「快速写」通道（违反 invariant ②）；编辑器内执行任意脚本/eval（安全 + 与 B1 脚本体系职责混淆）；AnimFsm 图编辑 tool（无真实用例拉动，§4.J）。

---

## 9. 与现有 epic 的关系（速查）

- **依赖 A2 EntityGuid（已闭环 ✅）**：句柄稳定性的全部前提（NF-4）。
- **协同 B1 PIE**：E3/E7 是 B1 的外部 AI 入口；B1 的 EnterPlay-ScriptSystem 接线先行，MCP 不抢跑（per-session 纪律）。
- **协同 B2 动画 / C1 prefab**：E5/E6 消费其已落地的数据层（SetAnimationClipCommand / overriddenPaths），并为其 GUI 提供第二消费者与数据侧回归手段。
- **放大 dogfood-checklist**：UC-8 是 MCP 对项目成熟度的最大杠杆。
