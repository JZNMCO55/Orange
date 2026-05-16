# OrangeEditor 路线图（独立立项）

- 基准日期：2026-05-11
- 基准状态：OrangeEditor 0.0.2；v0.1 收尾中（对应 `docs/roadmap.md` Phase 6 Task 06-01 ~ 06-09，其中 06-09 仅完成 S1 状态机骨架）
- 范围：从 v0.1（Phase 6 闭环）**之后**的编辑器演进路线；编辑器按自身 semver 独立演进，**不抢占** `docs/roadmap.md` 的 Phase 编号
- 任务字段沿用 `design-plan.md` 风格（描述 / 输入 / 输出 / 影响模块 / 前置 / 实现要点 / 验证 / 验收 / Critical Path），具体到 milestone 拆 task 在进入该 milestone 时再展开

## 与主路线图的关系

| 文件 | 职责 |
|------|------|
| `docs/design-plan.md` | 引擎 Phase 1–5.5 的施工计划（task 级，已基本完成） |
| `docs/roadmap.md` | 引擎 Phase 6+ 的长期路线（C# / Hot reload / ACP / 渲染深化 / 网络 / 跨平台 / 地形）。**Phase 6 编辑器条目仅作为 v0.1 历史快照**，后续演进迁来本文件 |
| `docs/editor-roadmap.md`（本文件） | OrangeEditor v0.2 起的所有里程碑 |

引擎 critical path（C# / Hot reload / ACP / 网络 / 跨平台）仍属 `docs/roadmap.md` Phase 7+；编辑器消费这些能力时通过"前置：主 roadmap Phase N"显式标注（见末尾依赖锚点表）。

## 核心架构决策（D1 / D2 / D3）

参考 `vendor/Orange-Wiki/wiki/concepts/gameplay/game-world-editor.md`（Gregory 2018 §15.4 的整理），任何 world editor 立项需要在三个 axis 上落锚。OrangeEditor 当前的选择 + 演进方向：

### D1 · Tool-side model vs Runtime model 映射

wiki 给三种映射（按解耦度递增）：

1. Tool-side C++ object = runtime 类（最简，但 tool 与 game 强耦合）
2. Tool-side GO → 多个 runtime 组件（组件化分解）
3. Tool-side GO = 唯一 id + property 表（property-centric，编辑器与运行时完全解耦）

**v0.1 选择（事后追评：失误）**：第 1 种 —— Inspector 直接 `reg.get<TransformComponent>(e)` 读写引擎 component，组件类型清单**硬编码**在 `EditorRenderLayer` 的 9 个 `DrawInspectorXxx` 成员里。当时担心"反射库被禁就只能 hardcode"，但忽略了同栈参考引擎的事实做法：

- `vendor/LumixEngine/src/engine/reflection.h` —— 模板 + 宏的 Builder API，编译期注册，零运行时反射库依赖
- `vendor/godot/core/object/class_db.h` —— GDCLASS 宏 + ClassDB hash 表，同样是手写注册

这两套都符合 CLAUDE.md "Serialization and reflection" 节禁令的本意（禁的是 `entt::meta` / RTTR / cereal-with-reflection / AST codegen，**不是**手写宏注册）。

**v0.2.5 整骨纠偏（schema-first，不是"保留 fast path + 新增 schema"）**：v0.2.5 milestone 引入 `PropertySchema` + `IComponentSchemaProvider`，**所有**内置组件改 schema 驱动渲染；硬编码 `DrawInspectorXxx` 路径整体清除，**不**保留 fallback。理由：调研 Lumix + Godot 后确认 schema-first 是同栈工业标准，"保留 fast path" 只是给未来再来一次硬编码留口子，没有工程价值；hardcode 禁令同步沉淀进 CLAUDE.md "OrangeEditor 架构纪律" 节。

**v0.3 角色变更**：v0.3 不再是"新增 schema 系统"，而是"基于 v0.2.5 落地的 schema 基础设施，让游戏侧自定义 component 也能注册 schema 显示在 Inspector + 第一个真实的 IEditorInspectorPlugin case"。

**绝不引入**：`entt::meta` / RTTR / cereal / clang AST codegen —— 违反 CLAUDE.md 项目级 invariant。手写宏 + 模板特化的 Builder API **不在禁令之列**。

### D2 · ACP 集成时机

wiki 给三档：

| 策略 | 代表 | level 迭代速度 | 换源资产代价 |
|------|------|----------------|--------------|
| Import-time | UnrealEd | 快（已优化） | 慢（重新 import） |
| Bake-time | Source / Quake | 中（每次 bake） | 快 |
| Lazy load + cache | Halo | 最灵活 | 快（首次付代价，后续 cache） |

**当前状态**：OrangeEngine **完全没 ACP**（主 roadmap Phase 9 才做）。编辑器吃 source asset 直接走 `AssetRegistry::Load<T>`：

- Shader：已限定 .spv 路径（Phase 3 Task 04 决策，运行时不编译 GLSL）
- Mesh：内置 `cube` / `plane` 等程序化生成，外部 .obj / .gltf loader 在 Phase 2 Task 02 已落
- Texture：stb_image 加载，无中间格式
- Material：JSON 文件（Phase 3 Task 01）

**v0.x 期内策略**：**显式接受"没 ACP"作为已知限制**。编辑器侧 asset 浏览器（v0.5）只浏览 source asset，不引入中间格式；等主 roadmap Phase 9 落地后，editor v1.x 接 lazy load + cache 模式。

### D5 · UI 风格基准

**参照系**：[Cocos Creator 3.6.0](https://www.cocos.com/creator)（用户提供截图所示版本）。Cocos Creator 是成熟的 3D 引擎编辑器，其 UI 布局是工业级参照。

**对标范围**（仅布局 + 交互手感）：

- 左侧 Hierarchy + Assets 上下分栏
- 中央 Scene viewport + 顶部 viewport 工具栏（视图模式 / Shaded / Wireframe / Camera mode）
- 右侧 Inspector（Node + 多 Component 折叠区块）
- 底部 tab 容器（Assets Preview / Console / Animation 三 tab）
- 顶部全局 toolbar（Save / Build / Play / Pause / Stop / 状态 indicator）
- 整体配色走深色（dark theme）

**不对标**：

- **场景内容**：Cocos 演示场景（Bistro / 高 PBR）是数十人团队多年积累；OrangeEditor demo scene 走 D4 路径（让 Phase 1–5 视觉栈在编辑器内可见，不追美术水准）
- **Inspector 字段密度**：Cocos `cc.DirectionalLight` 13 字段反映的是 Cocos 引擎已有的 shadow / 色温 / CSM / occluder 能力，**字段差距是引擎渲染深化的事，不是编辑器 UI 风格的事**
- **资源管理深度**：Cocos 的 UUID 系统 + 资产数据库是 import-time ACP 方案，与 OrangeEngine D2 决策（lazy load + cache）不同路径
- **多平台 Build 系统**：Cocos 一键发布 Web / Android / iOS / 微信小游戏；OrangeEngine 是 Windows-first，Build 按钮在 editor-roadmap v0.6 仅做 "导出 standalone Windows exe" 一种

**词条小注**：用户最初提到的"cocos2D"按上下文（提供的截图为 Cocos Creator 3.6.0）解释为 Cocos Creator；如果实际指 cocos2d-x（旧 2D 框架，无此种 3D 编辑器 UI），本节需重写。

### D4 · 演示场景（Editor Demo Content）的角色

**问题**：编辑器开发期，"打开 OrangeEditor 看到了什么" 直接决定了对引擎能力的主观感受。当前 `tools/OrangeEditor/DemoWorld.cpp::SeedDemoWorld` 只种了 7 个占位 entity（Root / Camera / Light / Geometry / Floor / Wall / Misc），mesh handle 全 Invalid、Material default、没体积光、没粒子、没真实灯光 —— 这让编辑器看起来像学院派 UI 占位，**完全没展示 Phase 1–5 已落地的视觉栈**（HDR / Bloom / Tonemap / 软阴影 / 体积光 / Material 模板 / VfxSystem / DragonBones）。

**决定**：把 "编辑器内的 demo scene" 视为**第一公民产物**，与 milestone 并行维护。每次引擎新增视觉能力（粒子模板 / 材质模板 / 光照特性 / 后处理）→ 同期把它加进 demo scene，让编辑器视觉随引擎能力同步演进。

**演进锚点**：v0.1 收尾时跟随一个 Editor Demo Scene v2（见 milestone "v0.1.5"），把现有 Phase 1–5 视觉能力组合进编辑器内可见的一个场景。后续每个 milestone 自带 "demo scene 应展示什么" 一项。

**与 Cocos Bistro 的距离**：**不对标 Cocos / Unity 演示级 3D 场景**。Bistro 是数十人团队 + Amazon Lumberyard / Unity HDRP 数年积累；OrangeEngine demo scene 的目标是 "**让 Phase 1–5 已有能力在编辑器里可见**"，不追求美术水准。

### D3 · Rapid iteration 模式

wiki 给三种：

1. **Offline + 文件 watch**：editor 改文件 → 引擎检测 mtime → 热重载（轻量，但延迟可见）
2. **Live IPC**：editor 与 game 并行运行，socket 通信（最灵活，工程量大）
3. **In-engine WYSIWYG**：editor 内嵌引擎 runtime，所见即所得（最重，但反馈最直接）

**v0.1 选择**：**in-engine WYSIWYG** —— Task 06-08 把 engine Pipeline 接入 viewport，Task 06-09 引入 Edit / Play / Paused 状态机让物理 / 粒子 / 动画在编辑器内 tick。

**wiki 陷阱回避**：wiki "陷阱"第一条 "过早追求 in-engine editor 初期成本极高"——已经付了这个代价，所以后续 milestone **必须充分利用 in-engine 优势**（gizmo 直接 hit-test ECS / Play Mode 即时反馈 / Inspector 改值立即看到 viewport 变化），不退回到 offline reload。

## 已知限制（开发期容忍，按 milestone 消除）

| # | 限制 | 待消除于 |
|---|------|---------|
| L1 | **没 Undo/Redo** —— 所有 mutate 直接生效，关闭无确认 | v0.2 |
| L2 | **游戏侧自定义 component 不显示** —— Inspector 硬编码内置组件清单 | v0.2.5（架构整骨：内置组件转 schema）+ v0.3（游戏侧 schema 注册落地） |
| L3 | **viewport 内无 gizmo** —— Transform 只能通过 Inspector 拖 DragFloat 改 | v0.4 |
| L4 | **mesh / material 字段编辑要靠手敲 handle id** —— 没资源浏览器 | v0.5 |
| L5 | **没 dirty 状态指示** —— File / Open 不提示未保存改动 | v0.6 |
| L6 | **没 per-layer / 多 chunk 协作** —— 一个 .scene.json 整个 chunk 一文件，多人编辑 merge 困难 | v0.6（按需） |
| L7 | **Animator backend 切换 + 状态机图不可编辑** —— Inspector 看不到 Animator 内部 | v0.7 |
| L8 | **Console 面板只显示帧统计** —— 没接 Core::Log | v0.8 |
| L9 | **快捷键硬编码** —— F2 / Del / Esc 等不可配置 | v0.8 |
| L10 | **没 Profiler / Debug Draw** | v0.9 |
| L11 | **没 ACP** —— mesh / texture 走 source asset | v1.x（依赖主 roadmap Phase 9） |
| L12 | **没热重载** —— shader / scene 改了要重启 | v1.x（依赖主 roadmap Phase 8） |
| L13 | **没 Editor Settings 系统** —— gizmo 线宽 / handle 长度 / 颜色等视觉常量散落在各 cpp anonymous namespace 的 constexpr，不可在运行时调整 | v0.8（v0.6.5 先集中到 `EditorTheme.h`，v0.8 迁入 EditorSettings） |
| L14 | **视觉风格不统一** —— v0.4 ~ v0.6 各 UI 表面功能优先落地，配色 / 图标 / 控件三态散乱；v0.4 收尾时被用户当场指出 viewport 工具栏不够美观以致没法做完整功能验证 | v0.6.5 |

## 里程碑

> v0.2 是 critical foundation —— 后续所有 milestone 都假设命令系统存在。**v0.2 完成前不开 v0.3+**。其余 milestone 之间可乱序、跳跃（同主 roadmap Phase 6+ 节奏纪律）。

### v0.1 · 基础闭环 ✅

**对应**：`docs/roadmap.md` Task 06-01 ~ 06-09

scaffold + ImGui dock + 实体树 + Inspector + Scene 保存/加载 + viewport + Play Mode 状态机。06-08 / 06-09 收尾后整个 v0.1 标 ✅。

详细任务历史保留在 `docs/roadmap.md` Phase 6 内（不重复抄）。

### v0.1.5 · Editor Demo Scene v2 ✅

**为什么单独立条**：v0.1 收尾时编辑器视觉与引擎能力**严重失配** —— Phase 1–5 全 ✅ 但编辑器内只有 7 个 placeholder。这条 milestone 不引入新 engine / editor feature，只是**把已落地能力组合成一个像样的演示场景**。**这是消除主观"引擎不行"感受的最高 ROI 动作**。

**关键 deliverables**：

- 替换 `tools/OrangeEditor/DemoWorld.cpp::SeedDemoWorld` 或新增 `assets/scenes/demo.scene.json`：
  - 5–10 个真实 mesh（cube / plane / sphere / 内置 model）摆出一个小室内 / 户外切片
  - 真实 Material：toon + rim_light（Phase 3 Task 02）/ dissolve / emissive（Phase 5 Task 02b）至少各一个实例
  - DirectionalLight + 软阴影开启（Phase 3 Task 05）
  - VfxSystem 粒子 emitter 至少一个（雪 / 萤火 / 烟 任选 —— Phase 5 Task 02a）
  - 体积光（screen-space god rays）至少一个方向光启用（Phase 5 Task 03）
  - HDR / Bloom / ACES Tonemap 默认开启（Phase 3 Task 03 已自动）
  - 一个 DragonBones 角色（Phase 4 Task 02-03）—— 用引擎自带 sample 资源
  - 2.5D 视角 camera（透视 + 锁 X/Y 平面 + 轻微 parallax）
- 加载机制：编辑器启动时检测 `assets/scenes/demo.scene.json` 存在则自动 Load，否则 fallback 到当前 SeedDemoWorld
- 验收：打开 OrangeEditor 看到的画面要让人**直观感受 Phase 1–5 视觉栈在工作**，而不是几个白色 placeholder cube

**已知不能展示（依赖引擎缺口）**：

- **史莱姆发光照亮黑暗环境** / 灯泡照明 —— 需要 PointLight 公共接口 + 多 light 支持，引擎缺口登记于 `docs/engine-known-gaps.md` 的 `GAP-2026-05-11-point-light-and-visible-halo`。本 milestone **不**包含这条；待 GAP 落地后 demo scene 升级

**前置**：v0.1 viewport 可渲染 + Scene 序列化（06-07）—— 已满足。粒子 / 动画要求**动态 tick**，因此 06-09 S4（VfxSystem / Animator 接入 Play Mode tick）是本 milestone 的真实前置；v0.1.5 须在 06-09 全部收尾后开工

**与引擎关系**：仅消费已有 engine API，不新增任何 engine 能力

**Critical Path**：是（针对主观体验感，比 v0.2 命令系统更紧急）

**落地状态（2026-05-12）**：

- `SeedDemoWorld` 重写：13 个实体，5 种内置材质各至少一个实例（textured / toon×3 / rim_light / dissolve / emissive），DirectionalLight.castsShadow=true，两个 `ParticleEmitterComponent`（火焰 + 萤火），2.5D EditorCamera 默认值更新
- `InitializeEditorAssets` 新增 toon / rim_light / dissolve 三个 `MaterialInstance` 字段到 `EditorState`
- `main.cpp` 自动加载机制：尝试 `Scene::Load("assets/scenes/demo.scene.json")`，失败回退 SeedDemoWorld
- **未能展示**：DragonBones 角色——`DragonBonesContext` 在 `src/` 私有头，editor CMake target 无法访问；体积光（god rays）依赖 Pipeline 在有 castsShadow 光时自动开启，运行时验证后确认
- 编译：`cmake --build build --config Debug --target OrangeEditor` 7 TU 全绿（2026-05-12）

### v0.2 · Command System + Undo/Redo ✅

**为什么是 critical**：

- 现在所有 mutate（Inspector 字段改 / DnD reparent / Create / Delete / Rename）**直接** mutate ECS；任何一步操作都不可撤销。编辑器无 undo 是非程序员用户**最高频抱怨**
- v0.3 的 Property Schema、v0.4 的 gizmo 拖动、v0.6 的 dirty 标志、v0.9 的 batch 操作，**都需要命令为基础**
- Play Mode（Task 06-09）的 World 快照机制与 Command Stack 的 capture/restore 是**同一抽象**；统一在 v0.2 解决

**关键 deliverables**：

- `editor/command/ICommand.h`：execute / undo / coalesce 接口
- `editor/command/CommandStack.h`：push / undo / redo / clear / 容量上限
- 内置命令：SetFieldValue<T>、CreateEntity、DestroySubtree、Reparent、Rename、AddComponent、RemoveComponent
- Inspector 所有字段控件包成 "拖期间合并 coalesce → 释放鼠标 commit 命令" 模式（避免 DragFloat 每帧 push 一条）
- Ctrl+Z / Ctrl+Y 全局快捷键（暂不可配，v0.8 接入 keybinding 系统时再开放）
- Play Mode World 快照（Task 06-09 S2/S3）**改成走 Command Stack 的 capture/restore primitive**，而非独立 stringstream serialize

**前置**：v0.1

**与引擎关系**：纯编辑器侧实现；引擎 component 不感知命令。Inspector 通过 `World::Patch(entity, fn)` 风格 wrapper 写入（这个 wrapper 也只在编辑器侧加，不污染引擎公共 API）

**Critical Path**：是（后续所有 milestone 的地基）

### v0.2.5 · 架构整骨（Schema-first + Plugin 抽象 + EditorHost 拆分） ✅

**为什么单独立条 + Critical Path**：

v0.1 + v0.1.5 + v0.2 收尾后回看 OrangeEditor 当前结构，对照 wiki `vendor/Orange-Wiki/wiki/concepts/gameplay/game-world-editor.md` + 调研 `vendor/LumixEngine/src/editor/*` + `vendor/godot/editor/*`，**当前架构在 5 个具体维度上是 hardcode / god class / 缺抽象**。这些不在 v0.3（schema 应用）/ v0.4（Gizmo）顺手做的范围内，需要专门 milestone 处理。继续在现状上加 v0.3 / v0.4 等于把 hardcode 路径再叠一层，未来要还的债更大。

**"随意"的 5 条具体诊断（2026-05-12）**：

1. **EditorRenderLayer 是 god class** —— `OnUpdate` + 9 个 `DrawInspectorXxx` 成员（Transform / Hierarchy / DirectionalLight / Renderable / RigidBody / Collider / ParticleEmitter / Animator / Name）+ 5 个 panel 函数 + Play Mode 入口全塞一个类。切到多 TU 不解决类自身的巨型问题
2. **EditorState 是 god struct，单调增长** —— 当前 19 个字段（World / selection / scenePath / pendingSceneOp / playState / pendingPlayOp / snapshot path / rename 三件套 / pendingDelete / pendingReparent / pendingCreate / Euler cache / AssetRegistry / MaterialSystem / 2 个 MeshHandle / 7 个 MaterialInstance / CommandStack / EditorCamera）；每个 milestone 加字段没有任何子域切分
3. **没有 IEditorInspectorPlugin / IEditorGizmoPlugin 抽象** —— 游戏侧自定义 component 要显示在 Inspector，只能改 EditorRenderLayer 源码新增 `DrawInspectorXxx`；正中 wiki `game-world-editor.md §陷阱` 第 2 条 "Per-type property hardcode → schema 驱动"
4. **CommandStack lambda 捕获 World\*** —— scene swap / 破坏性操作必须 `Clear()`；Godot `EditorUndoRedoManager` 用 `ObjectID` 弱引用 + per-scene history 隔离的做法是更稳的抽象
5. **没有 EditorHost / EditorApp 一层** —— `main.cpp` 直接构造 AppHost + EditorRenderLayer + EditorState，编辑器没有"自己的应用入口"概念；Lumix `StudioApp` / Godot `EditorNode` 都是单例 hub，OrangeEditor 缺这一层意味着任何全局编辑器服务（剪贴板 / quick search / preferences / 后续 plugin registry）只能继续塞 EditorState

**关键 deliverables**：

- **EditorHost 单例 + EditorState 子域拆分** —— EditorState 按域拆 4 个 context：
  - `EditorSelection`（selectedEntity / 多选 / pendingDelete / pendingReparent / pendingCreate / rename 三件套）
  - `EditorSceneContext`（World 所有权 / currentScenePath / pendingSceneOp / playState / pendingPlayOp / playSnapshotPath）
  - `EditorAssetContext`（AssetRegistry / MaterialSystem / 内置 mesh / material handles）
  - `EditorCameraState`（轨道相机 + Euler cache）
  - `EditorHost` 单例聚合 4 个 context + CommandStack + 后续 plugin registry；EditorState 退化为薄壳或直接删除（按重构方便程度二选一）
- **PropertySchema + IComponentSchemaProvider** —— 参考 Lumix Builder API（`vendor/LumixEngine/src/engine/reflection.h`）和 Godot ClassDB（`vendor/godot/core/object/class_db.h`）的宏 + 模板特化注册：每个内置 component 通过 `static const PropertySchema& Schema()` 或等价注册入口提供字段元数据（type / label / tooltip / range / display flags）；Inspector 通过 visitor pattern 遍历 schema 生成 ImGui 控件
- **内置组件 schema 化（全量，无 fallback）** —— 9 个 `DrawInspectorXxx` 全部改 schema 驱动；硬编码路径**整体清除**，不保留 fast path。Add Component 菜单也改 schema 注册表枚举驱动，不再 hardcode `if/else` 列表
- **IEditorInspectorPlugin 接口（声明 + 注册表，本 milestone 不实现真实 plugin case）** —— 游戏侧或后续编辑器扩展可注册 plugin 截获特定 component 提供自定义 UI（参 Godot `EditorInspectorPlugin::parse_property()`）；本 milestone 只定义接口 + EditorHost 里的 plugin registry，v0.3 出第一个真实 case
- **IEditorGizmoPlugin 接口（仅声明，v0.4 实现）** —— 同上 plugin 抽象的对偶；接口签名定下来让 v0.4 直接消费，不再回头改抽象
- **CommandStack 增 BeginGroup / EndGroup** —— 参 Lumix `beginCommandGroup()`，支持原子多命令组合（一次 gizmo 拖动 = group 内多条 SetField 命令）。MergeMode 扩展为三档（`Disable` / `Ends` / `All`）参 Godot
- **CommandStack 解 World\* 强耦合** —— 命令存 entity id 而非 lambda 捕获 World\*；改走 `EditorHost::GetSceneContext().GetWorld()` 解引；scene swap 时仅需让 EditorSceneContext 失效，不必 Clear 整栈（但保留 `Clear()` 给真正不可恢复的场景切换）

**前置**：v0.2（命令栈基础已有）+ v0.1.5（Demo scene 验证视觉栈未坏的 baseline）；均已 ✅

**与引擎关系**：纯编辑器侧重构；引擎 component 不感知 schema 注册。schema 注册代码位于编辑器 target 内（建议 `tools/OrangeEditor/schema/*` 新目录），引擎公共 API 零改动

**Critical Path**：是（v0.3 之后所有 milestone 的真正地基；不先做 v0.2.5，v0.3 等于在 hardcode 上再叠一层 + CLAUDE.md "OrangeEditor 架构纪律" 直接违反）

**禁止条款（同步沉淀进 CLAUDE.md "OrangeEditor 架构纪律" 节）**：
- v0.2.5 之后所有编辑器代码：**禁止**新增"加一个 component 类型就改 `EditorRenderLayer` 源码"路径；必须走 schema 注册
- **禁止**把 per-component 的 Inspector / Gizmo / 序列化 UI 逻辑写进任一 mega-class；必须以独立注册项形式存在
- **禁止**在 EditorState / 任一子 context 上无脑加字段；新功能找对应 context 加，没有合适 context 就先拆 context

**验收**：

- `tools/OrangeEditor/EditorRenderLayer.cpp` 中所有 `DrawInspectorXxx` 删除
- 新增任意内置 / 假设的"游戏侧" component 只需写 schema 注册 + 序列化 `Read/Write`，**不需要**改 `EditorRenderLayer` / `EditorHost` / 任一 context
- CommandStack `BeginGroup` / `EndGroup` + `MergeMode` 三档单元测试通过
- v0.1 ~ v0.2 已有功能（场景保存 / 加载 / Undo / Redo / Play Mode / Demo scene）回归测试通过 —— 整骨不允许损失已落地能力
- Editor 启动 + Demo scene 加载 + 选中实体 + 改 Inspector 字段 + Undo + Save 路径全绿
- `EditorState` 字段数从 19 降到 ≤ 3（仅薄壳聚合）或直接删除；4 个子 context 文件存在且单文件 < 200 行

### v0.3 · 游戏侧 Schema 注册落地 ✅

**目标**：基于 v0.2.5 已落地的 `PropertySchema` / `IComponentSchemaProvider` / `IEditorInspectorPlugin` 基础设施，让游戏侧自定义 component 通过同一套 schema API 显示在 Inspector，并出第一个真实的 plugin case 验证抽象边界。

**关键 deliverables**：

- 游戏侧 API：`OrangeEditor::RegisterComponentSchema<T>(schemaDesc)` —— 在编辑器 main 启动期或独立 plugin 入口注册
- 与引擎 `Read/Write` 序列化共用字段清单：手写一份 schema 描述 + 一份 `Read/Write`，靠 review 保一致（**不**引入 codegen）
- 控件类型扩展：拖拽 / 数值控件 / 颜色 / 资源 ref / enum 下拉 / nested struct 折叠等控件类型枚举完整化，schema entry 选择
- 示例：在 `samples/` 或 `tests/` 中注册一个伪"游戏侧" component（如 `HealthComponent`），验证从 schema 注册 → Inspector 显示 → Undo / Redo → 序列化保存 / 加载完整链路无需改 editor 源码
- 第一个真实 `IEditorInspectorPlugin` case：覆写某 component 的默认 schema 渲染（候选：Animator 加 mini-preview / Material 加资源预览缩略图）—— 验证 plugin 能在 schema 默认行为上叠加 UI 而不替换整段

**前置**：v0.2.5（schema 基础设施 + plugin 接口已落）

**与引擎关系**：仅在编辑器侧 + sample / test 侧；引擎不感知 schema 的存在

**Critical Path**：否（v0.2.5 落地后，本 milestone 算扩展点真实性验证；v0.4 / v0.5 / v0.6 不依赖游戏侧 schema 已注册，只依赖 v0.2.5 的接口）

### v0.4 · Gizmo & 特殊对象可视化 ✅

**目标**：viewport 内能直接拖实体，对应 wiki `game-world-editor.md` §核心功能 4（Selection）+ 5（Layers）+ 7（特殊对象类型）。

**关键 deliverables**：

- **Transform gizmo**：translate / rotate / scale 三种 mode（W / E / R 切换），world / local 切换
- **Picking**：viewport 点击 → ray cast 进 ECS → 选中 entity（与 Entity Tree 双向高亮）
- **Light gizmo**：DirectionalLight 显示图标 + Direction 箭头可拖
- **ParticleEmitter gizmo**：spawn offset box + 初速度向量可视化
- **Camera frustum**：选中带 Camera 组件的实体时显示 frustum 线框
- **Viewport 工具栏**（参 Cocos 截图）：视图模式（Persp / 2D Lock）/ 显示模式（Shaded / Wireframe / Shaded+Wireframe）/ Camera mode（Design Resolution 锁定）/ Gizmo on-off 总开关。挂在 Scene 面板顶部
- **TODO（按需补）**：Region (trigger) / Spline / Sound radius / Nav mesh（这些都是 wiki §特殊对象类型表里的，但取决于第一款游戏是否真用到）

**前置**：v0.2（gizmo 拖动必须产生命令，单次拖 = 一条 coalesce 后的 SetTransform 命令）

**与引擎关系**：

- gizmo hit-test 全在编辑器侧（射线 + AABB 测试）
- 消费 `RenderableComponent.aabb`（Phase 2 已有）+ `TransformComponent`（Phase 1 已有）
- viewport overlay 渲染线段 / 图标：编辑器 v0.1 已经有 viewport pipeline，加一条 line / billboard 子 pass

**Critical Path**：是（"美术能拖东西"是编辑器最强烈的用户期待）

### v0.4.5 · UI DPI 自适应 + Inspector / Panel widget 比例化

**为什么单独立条**：

v0.4 收尾后在另一台不同分辨率 / 缩放比的机器上跑 OrangeEditor，发现 Inspector 右侧字段名被截成 `Enti...` / `Sta...` / `Par...`，一排 Remove Component 红 X 挤在一起。诊断（2026-05-15）：

- 你这台机 4K × 175%（逻辑分辨率 ~2194×1234）一切正常
- 另一台 2520×1680 × 150%（逻辑分辨率 1680×1120）—— 宽度只有你 76%
- 根因有二：
  1. **`main.cpp` 字号写死 `kDefaultFontSizePx = 36.0f`，ImGui style padding / spacing 完全没按 DPI 缩放** —— 任一窄屏机器都装不下
  2. **Inspector / Panel 内部所有 widget 的列宽 / 按钮 / table column 都按绝对像素布局**，dock 比例化（v0.4 起 left 20% / right 25% / bottom 30%）只解决 panel 外框，没解决 panel 内 widget

(1) 已经在 v0.4 post-milestone 一次性 hotfix 落地（`main.cpp` 的 ImGui Init 改为 `glfwGetWindowContentScale` + `ScaleAllSizes(scale)` + 字号基准从 36 → 18 × scale）—— 但这只兜底，未根治 (2)。本 milestone 处理 (2)。

**关键 deliverables**：

- **Inspector schema 控件比例化**：`tools/OrangeEditor/schema/SchemaInspector.cpp`（v0.2.5 整骨产物）里所有 ImGui 控件改成 ratio / min-size 驱动；`ImGui::CalcTextSize` 主动测列名宽度作为 column min，剩余空间给 value 控件；component header（Remove 按钮、folding triangle、name）整体用 `ImGui::PushItemWidth(GetContentRegionAvail().x * ratio)` 限定，不依赖 ImGui 默认行宽
- **Inspector 的标签 / 值两列改 `ImGui::Table` + `ImGuiTableColumnFlags_WidthStretch`**：标签列设 minWidth = `CalcTextSize("最长内置 label 名").x`，值列 stretch；放弃当前 `ImGui::Columns` / 隐式 column 路径
- **EntityTreePanel / ScenePanel / Console / Assets 同步处理**：树状缩进尺寸用 `style.IndentSpacing`（已被 ScaleAllSizes 缩了），不再写绝对像素 padding；ScenePanel 顶部工具栏 `BeginChild` 高度用 `GetFrameHeightWithSpacing()` 而不是数字
- **acceptance scene**：在两台机器（4K × 175% 和 1680×1120 × 150%）分别全屏跑 demo.scene，Inspector 选中带 5+ component 的 entity，无任何 label 出现 `...` 省略；component remove 按钮可点击，行高一致
- **回归**：v0.4 gizmo 拖动、v0.3 Inspector 字段编辑、v0.2 Undo / Redo、v0.1.5 demo scene 加载——全部回归通过

**前置**：v0.2.5（schema-first Inspector 已落，所有 widget 都过 `SchemaInspector`）+ v0.4 post-fix DPI scale 已落（本条由 v0.4 ✅ 后的 hotfix commit 处理）

**与引擎关系**：纯编辑器侧；引擎公共 API 零改动

**Critical Path**：否（功能上 v0.5 / v0.6 不依赖；但任何团队成员换机器跑 OrangeEditor 都会撞，体验级 P1）

**红线**：本 milestone 内**禁止**给某个 component / panel 在 widget 层 hardcode "magic number 像素列宽" 来"先用着" —— 这正是 v0.4 期撞上本问题的原因。所有列宽必须来自 `CalcTextSize` 或 `style.*` 或 dock cell 比例。

**验收**：

- 上述 acceptance scene 在两种 DPI / 分辨率配置下全绿
- `Grep` `SchemaInspector.cpp` / `InspectorPanel.cpp` / 其他 panel 找不到字面量像素列宽（除 `style.*` / `CalcTextSize` / `GetContentRegionAvail` 派生外）—— 加进 `scripts/check_invariants.py` 作为编辑器侧新 lint 规则
- 编辑器 milestone-end-checklist 走完，含两台机器实测截图存档

### v0.5 · Asset 浏览器 + Material 子模式

**对应**：`docs/roadmap.md` Task 06-06（材质编辑器子模式）的真正落地

**关键 deliverables**：

- **资源面板**：浏览 `assets/` 目录树，预览 mesh 缩略图 / texture 缩略图 / material 颜色块
- **Material 子模式**：MaterialTemplate 选择 + uniform 调参（参 wiki D1 schema）+ 纹理槽指派 + 保存到 .material（Phase 3 Task 01 已有序列化）
- **Inspector 的 Renderable.mesh / .materialInstance 字段**改成"拖资源到字段 + 浏览器里点选"
- **底部 tab 容器**（参 Cocos 截图）：把 Assets Preview / Console / Animation（v0.7 才进）做成同 tab 容器，与 Cocos 底部三 tab 布局对齐。本期建好容器，Animation tab 留空 placeholder
- **重要**：仍**只浏览 source asset**（参 D2 已知限制 L11）。资源类型由 v0.3 schema 描述

**前置**：v0.3（资源类型 ref 控件需要 schema 描述 + v0.2 命令系统）

**与引擎关系**：消费 Phase 3 Task 02 的 toon / rim_light 内置模板 + Phase 5 Task 02b 的 dissolve / emissive 模板；Material 序列化已存在

**Critical Path**：否（手敲 handle 也能用，但烦扰极高）

### v0.6 · 多 chunk / per-layer + dirty 状态

**对应**：wiki §保存加载 + §陷阱 "chunk 粒度与 VCS 冲突"

**关键 deliverables**：

- Scene 序列化扩 SchemaVersion（参 CLAUDE.md "Serialization and reflection"：不改已发版本，新版本 + migrator）
- Scene 拆 chunk → layer 两级；per-layer 落盘 + 单独加载 / hide / show
- 顶部 menu bar 显示 dirty 状态（标题 `*` 后缀）
- 关闭未保存确认对话框
- 多 scene tab（参 Task 06-07 Out-of-scope）
- **顶部全局 toolbar**（参 Cocos 截图）：把 Save / Build / Play / Pause / Stop / View Mode 切换从 File 菜单 + Console 面板按钮聚到一条 toolbar 上。Save 按钮 dirty 时高亮

**前置**：v0.2（dirty = command stack 与 last-save 标记之间有命令）

**条件触发**：若第一款游戏单关卡单文件 + 单人编辑可承受，可推迟到游戏侧反馈拉动时再做

**与引擎关系**：Scene 序列化 SchemaVersion bump + migrator（Phase 5 Task 01 已有 migrator 机制）

**Critical Path**：否（仅协作场景必须）

### v0.6.5 · 视觉统一与主题打磨

**为什么单独立条**：

v0.4 ~ v0.6 把编辑器的主要 UI 表面陆续摆齐 —— viewport 工具栏（v0.4）/ Inspector & Panels（v0.2.5 schema）/ 资源浏览器 + 底部 tab 容器（v0.5）/ 顶部全局 toolbar（v0.6）。但这一路是**功能优先**，每条 milestone 只保证"能用 + 不丑得离谱"，没有任何一条专门处理整体视觉一致性。结果在 v0.4 收尾时被用户当场指出 viewport 工具栏不够美观以致没法做完整功能验证 —— 单纯 toolbar 调整不够，需要把整套视觉 token（间距 / 配色 / 图标 / 控件三态）统一打磨一遍。

**为什么放在这里（不更早 / 不更晚）**：

- **不更早**：v0.6 要落"顶部全局 toolbar"（Save/Build/Play/Pause/Stop + dirty 高亮）—— 另一条主 UI 表面。在 v0.6 之前美化会被 v0.6 新增 toolbar 推翻配色 / 间距 / 图标体系
- **不更晚**：v0.7 要画 Animation 状态机图编辑器（节点 / 边 / 高亮）—— 又一大块新视觉表面。本 milestone 在 v0.7 之前定下视觉 token，v0.7 直接按 token 画状态机，避免后续 polish 时返工
- **v0.8 EditorSettings 不是前置**：本 milestone 可以先把视觉常量硬编码到一处 `EditorTheme.h`，v0.8 落 Settings 系统时把这些常量迁入 `EditorSettings` —— 恰好对偶 L13 的迁移路径

**关键 deliverables**：

- **`tools/OrangeEditor/theme/EditorTheme.h`**：集中所有视觉 token —— 间距单位 / 圆角 / 边框宽度 / 配色 palette（深色主题，参 Cocos Creator 3.6.0 截图配色）/ 字号档位（H1 / H2 / Body / Caption）/ 控件三态色（idle / hover / active / disabled）/ icon 尺寸档位
- **图标 font 接入**：选 Font Awesome 6 Free / Lucide / Codicons 之一（按 license 与覆盖度二选一），通过 ImGui font merge 加载到默认字体，文字按钮全部替换为 icon + tooltip
- **viewport 工具栏 polish**：按 EditorTheme 重画 v0.4 已落的视图模式 / 显示模式 / Camera mode / Gizmo on-off 按钮；状态切换有视觉反馈
- **顶部全局 toolbar polish**：v0.6 落地后跟随本 milestone 重画 Save / Build / Play / Pause / Stop 按钮；Save 在 dirty 时按 EditorTheme accent 色高亮；Play / Stop 用对比色（绿 / 红）易识别
- **panel 视觉统一**：标题栏 / 分隔条 / 折叠箭头 / Inspector component header 折叠图标 / Entity Tree 行 hover/select 状态全部按 EditorTheme 重画
- **Inspector 控件三态**：DragFloat / SliderFloat / Combo / Button hover / active / disabled 三态颜色一致
- **acceptance scene**：在 demo.scene 上完整跑一遍"开场景 → 选实体 → 改 Inspector → gizmo 拖 → Save → Play → Stop → Build" 路径，全程视觉风格一致，无 ImGui 默认深蓝 / 灰白色块漏出

**前置**：v0.6（全局 toolbar 已落，所有主 UI 表面齐全）；v0.4.5（DPI 自适应已落，token 化的间距 / 字号才有意义）

**与引擎关系**：纯编辑器侧；引擎公共 API 零改动；不消费任何新 engine 能力

**Critical Path**：否（功能上 v0.7 / v0.8 / v0.9 不依赖 EditorTheme；但用户体验级 P1，且推迟会让 v0.7 状态机图编辑器再来一次返工）

**红线**：

- 本 milestone **禁止**新增 / 修改任何 editor 功能行为 —— 只动视觉。任何"顺手把 X feature 也改一下" 都拆出去单独 commit / milestone
- **禁止**直接调 `ImGui::PushStyleColor(ImGuiCol_xxx, ImVec4(0.2f, 0.4f, 0.7f, 1.0f))` 这种字面量 RGBA —— 必须经 EditorTheme token；加进 `scripts/check_invariants.py` 作为编辑器侧新 lint 规则（与 v0.4.5 的字面量像素列宽 lint 同期上）
- **禁止**为单个 panel / 控件硬编码"特殊"颜色 —— 不在 token 里就先加 token；token 不够用是 EditorTheme 的设计 bug，不是 panel 的自由度

**不做**：

- **可切换主题**（亮色 / 自定义 palette）—— 留给 v0.8 EditorSettings 接入主题切换
- **自定义 dock layout 模板**（保存 / 加载多套 layout 预设）—— 与本 milestone 视觉打磨正交，按需另立
- **动画过渡 / tween**（按钮 hover 渐变、panel 折叠动画）—— ImGui 不擅长，硬做易撞 frame pacing 问题

**验收**：

- 上述 acceptance scene 在两种 DPI / 分辨率配置（v0.4.5 acceptance 同款机器）下视觉一致
- `Grep` `tools/OrangeEditor/` 找不到字面量 `ImVec4(0.\d+f, 0.\d+f, 0.\d+f, ` 形式的 RGBA（除 EditorTheme.h / EditorTheme.cpp 自身外）
- `Grep` 找不到字面量 button label 是裸文字符号（`"X"` / `"+"` / `"▼"` 等）—— 必须用 icon font codepoint
- 编辑器 milestone-end-checklist 走完，含 v0.4 ~ v0.6 全 panel polish 前后对比截图存档

### v0.7 · Animation 子模式

**关键 deliverables**：

- Inspector 的 Animator 段加 backend 切换（Skeletal / Procedural）
- Skeletal animation state machine **图编辑**（节点 = state，边 = transition + condition），保存到 .anim_fsm
- DragonBones 资源浏览 + 单 clip 预览（独立窗口）
- Procedural Animator 的 channel 配置面板（fn 名 + 目标 uniform）

**前置**：v0.5（资源浏览器）+ v0.2（图编辑改动走命令）

**与引擎关系**：消费 Phase 4 Task 01 的 AnimationStateMachine + IAnimator + AnimatorRegistry；Phase 4 Task 03/04 的 Skeletal / Procedural 后端

**Critical Path**：否（第一款游戏可以先用 Inspector 字段编辑临时凑）

### v0.8 · 编辑器 Log + 输入扩展 + Settings

**关键 deliverables**：

- Console 面板接 `Core::Log`（参 Task 06-02 Out-of-scope）—— filter by level / tag / search
- Keybinding 自定义（hot key editor），保存到 `editor_keybindings.json`
- 多选 / Shift / Ctrl-click 多选实体（参 Task 06-03 Out-of-scope）
- 多选 Inspector：wiki §6 property grid "异构多选 = 只显示所有类型共有的属性"
- **Editor Settings 系统**（消除 L13）：把现在散落在 cpp anonymous namespace 的视觉常量集中到一个 `EditorSettings` 结构 + Settings 面板调整 + 保存到 `editor_settings.json`。首批纳入：gizmo 线宽（translate / rotate / scale 各 idle + highlight 共 6 个值）、gizmo handle 屏幕长度 `kHandleScreenLengthPx`、hit threshold、gizmo 配色。架构参 `vendor/LumixEngine/src/editor/settings.h`（同栈手写注册，符合 CLAUDE.md "禁止 hardcode" 纪律）

**前置**：v0.2

**与引擎关系**：消费 `Core::Log`（Phase 1 Task 04 已落）+ Input 模块（Phase 4 Task 08 已落）

**Critical Path**：否

### v0.9 · Profiler / Debug Draw 集成

**关键 deliverables**：

- Debug 绘制 API 在 viewport 显示（aabb / line / sphere / text），面板开关
- Profiler 面板：接 `ORANGE_ENGINE_WITH_TRACY` 或自实现 in-game profiler（wiki `techniques/debugging/in-game-profiler.md`）
- 内存统计（per-module）

**前置**：v0.2

**与引擎关系**：消费引擎已有 tracy gate；调试绘制 API 走编辑器侧 viewport overlay sub-pass

**Critical Path**：否

### v1.0 · 验收里程碑

v0.1 ~ v0.9 全部 ✅。验收路径：邀请非程序员（如美术 / 关卡设计师）跑一个 30 分钟典型任务 —— 搭场景 / 调材质 / 摆灯光 / 跑 Play Mode 看效果 —— 不需要程序员介入。

通过即声明 Phase 6 当初的目标 "**足以让美术 / 关卡设计师不写代码完成日常工作**" 闭环。

### v1.x · 长尾（按需触发，不进 v1.0 critical path）

| 条目 | 依赖 |
|------|------|
| Hot reload（编辑器内改 shader / scene → 即时生效） | 主 roadmap Phase 8 |
| ACP 集成（lazy load + cache 模式） | 主 roadmap Phase 9 |
| C# 脚本组件可视化 | 主 roadmap Phase 7 |
| 地形 / 植被工具 | 主 roadmap Phase 13 |
| DCC 集成（Blender / Maya plugin） | 单独立项 |

### v1.x · Ori-like 视觉子模式（按第一款游戏拉动）

这一组特性是 2.5D 平台跳跃**特有**的编辑器子模式，区别于通用 3D 编辑器需求。**只在第一款游戏明确需要时**展开为完整 milestone，目前仅登记需求：

| 条目 | 编辑器侧 | 引擎侧依赖 |
|------|---------|----------|
| **Camera Cinematic 时间线** | 关键帧编辑器（position / target / FOV / depth-of-field 随时间动画），保存到 `.cinematic` 资源；可视化路径曲线 | 引擎需要 `CameraAnimatorComponent` + spline 求值（当前无） |
| **Parallax Layer 编辑** | 给 entity 标 `parallaxDepth` 字段，viewport 按 depth 分组可视化；预览 camera 视差滚动效果 | 引擎需要 `ParallaxComponent`（depth + scrollScale）+ 渲染端按 depth 分组提交（当前无） |
| **Ambient Weather Particle 模板** | 内置 emitter blueprint（雪 / 雨 / 风 / 雾 / 飞舞光斑），拖到场景即用；区域绑定（particle 仅在 camera 视野 + 一定 region 内 spawn） | 引擎已有 VfxSystem（Phase 5 Task 02a） + 体积光（Phase 5 Task 03）；可能需要 `ParticleRegionComponent` 限定 spawn 范围 |
| **远近景过场编辑** | 时间线 + Camera 关键帧 + Depth-of-field 调度 + 触发条件（玩家进 region / event 触发） | 复用 Camera Cinematic + 事件系统（事件系统当前未规划，可能要走 Phase 7+ 或 game-side） |

**触发节奏**：建议在 v0.4 (Gizmo) + v0.5 (Asset 浏览器) 完成后，第一款游戏 fork 启动；游戏侧第一次真实用到「远近景过场」或「气候粒子」时，把对应 v1.x 条目升格为编辑器子 milestone，同时给引擎侧加对应 component（Parallax / CameraAnimator）。在那之前**仅记录需求，不主动开发**。

**关联引擎缺口**：

- **PointLight + 可见光晕**（史莱姆发光 / 灯泡照明场景）→ `docs/engine-known-gaps.md` GAP-2026-05-11-point-light-and-visible-halo
- **CameraAnimatorComponent + spline 求值** → 未登记（远近景过场触发时再登记）
- **ParallaxComponent + 渲染端按 depth 分组** → 未登记（视差层 v1.x 触发时再登记）

引擎缺口落地节奏跟 editor-roadmap 解耦；每条缺口独立 session 处理（参 `docs/engine-known-gaps.md` 处理纪律）。

## 与主 roadmap 的依赖锚点

| editor milestone | 依赖主 roadmap |
|------------------|----------------|
| v0.1 ~ v0.4 | 仅 Phase 1–5（全部已落地） |
| v0.5 Material 编辑 | Phase 3 Material（已落地） |
| v0.6 layer 序列化 | Phase 5 SchemaVersion + migrator（已落地） |
| v0.6.5 视觉统一 | 无引擎依赖（纯编辑器侧） |
| v0.7 Animation 子模式 | Phase 4 AnimationStateMachine（已落地） |
| v0.9 Profiler | Phase 1 tracy gate（已落地） |
| v1.x Hot reload | 主 roadmap Phase 8 |
| v1.x ACP 集成 | 主 roadmap Phase 9 |
| v1.x C# 组件可视化 | 主 roadmap Phase 7 |

## 不做的事（明确排除）

- **多人协同编辑**（同一 chunk 多客户端 live merge / OT / CRDT）—— 复杂度过高；per-layer 文件 + VCS 是工程上限
- **完整 DCC 集成**（Blender / Maya / 3ds Max plugin）—— 每个都是独立工程，需要美术验证后再投入；v1.x 长尾不进 critical path
- **VR / AR 编辑视图**
- **编辑器内置脚本控制台**（Lua / Python REPL）—— 主 roadmap Phase 7 决定脚本语言前不做
- **资源自动 import 流水线 watcher**—— ACP 来之后顺路做，不单独立项
- **基于反射的 Inspector 自动生成** —— 违反 CLAUDE.md 项目级 invariant；v0.3 Property Schema 是手写替代方案
- **跨引擎导出**（USD / glTF 编辑器导出）—— 不在游戏 ship critical path 上

## Self-Check

- **v0.1.5 (Editor Demo Scene v2) 是最高 ROI 动作** —— 不引入新能力，把 Phase 1–5 已落地视觉栈在编辑器内组合可见；建议先做这条再回到 v0.2 主线
- v0.2 是 mutate / undo 能力的地基（command system / undo / Play Mode 快照统一）
- **v0.2.5（架构整骨）是 v0.3 之后所有 milestone 的真正地基** —— schema / plugin / EditorHost / CommandStack 解耦 World\* 全在此 milestone 落地；不先做 v0.2.5，v0.3 就是在 hardcode 上叠一层，且直接违反 CLAUDE.md "OrangeEditor 架构纪律" 节；**v0.2.5 完成前不开 v0.3+**
- **v0.6.5（视觉统一）放在 v0.6 之后 / v0.7 之前** —— 必须等 v0.6 全局 toolbar 落地（否则返工），又必须在 v0.7 状态机图编辑器之前（让 v0.7 直接按 EditorTheme token 画，避免再次返工）；纯视觉打磨，不动功能
- 编辑器 semver 独立于 OrangeEngine 0.1.x：editor 升 v0.2 ≠ engine 升版
- 四个核心架构决策（D1/D2/D3/D4）显式记录，避免后期被动重做
- 与主 roadmap 的依赖以 "前置：主 roadmap Phase N" 形式标，不抢占 phase 编号
- 已落地 v0.1 任务历史仍在 `docs/roadmap.md` Phase 6 内查（不重复抄）
- 已知限制 L1–L12 显式登记 + 标定消除点；不存在"心知肚明但没写下来"的悬置项
- v1.x 长尾全部带依赖；不在 critical path 上
- "Ori-like 视觉子模式"（Camera Cinematic / Parallax / Ambient Weather）登记为需求但**不主动开发**，等第一款游戏 fork 后真实用到时再升格
