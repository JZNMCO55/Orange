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

**版本基线更新（2026-05-17）**：v0.6.5 启动前补一组 Cocos Creator **3.8.8** 深色主题截图（Animation / Animation Graph / Assets / Assets Preview 四个底部 tab 状态），覆盖了 3.6.0 截图未覆盖的若干 panel 细节；本节配色 / 控件细节按 3.8.8 截图采色，与 3.6.0 整体布局基准不冲突（Cocos Creator 3.6 → 3.8 视觉演化为微调，未改变 UI 大局）。

#### D5.1 · v0.6.5 视觉体系落锚（2026-05-17 决策）

v0.6.5 启动 ritual 前定下来的具体视觉决策。后续 v0.6.5 之内的 token 化工作 + v0.7 起的所有 UI 表面都照这套办，不再回头讨论"整体走哪条路"——只在 token 内具体值（圆角 / 字号 / per-type 色带）上微调。

**决策清单**：

| 项 | 选择 | 决策由来 |
|----|------|--------|
| **整体方向** | **方向 C** —— Cocos 布局气质（间距 / 圆角 / 配色克制）+ Godot 的"selection 明确性"（但用半透不用实色）+ Inspector component header 左侧 4px 彩色色带作为"识别度补丁"，不依赖全套彩色 icon | 节点树仍单色（接受），Inspector 是实际工作流主战场，色带成本 = EditorTheme 加一组 per-component-type token，比做整套彩色 icon 便宜 90% |
| **toolbar 布局** | **行 1 = menu bar**（File / Edit / View / Help + scene path indicator）；**行 2 = 独立 toolbar**（Save 靠左 / Play + Pause + Stop 中央 / [State] 靠右），紧贴 menu bar 下方、跨 dock root 整宽 | v0.6 c3 deliverable 原意是"聚到一条 toolbar 上"，当时实现为"挤进 menu bar 右侧"是技术妥协，未对齐 deliverable；本 milestone 补完。中央放 Play 控件参 Cocos Creator 3.8.8 + Unity / Unreal 同款工业惯例（Play 是最高频读 + 写控件） |
| **背景色基调** | Cocos 路线，**#2A2A2A ~ #2C2C2C** 区间（亮度 ~17%），具体值由 v0.6.5 开工时从 Cocos Creator 3.8.8 截图采色定 | 暗色编辑器背景走炭灰而非纯黑是工业惯例（VS Code / JetBrains / Blender / Maya / Unity / Unreal 全在 17–20% 亮度区间）；纯黑与环境（白墙 / 显示器边框 / 白天光）对比度过高，瞳孔反复调节疲劳更快。OrangeEditor 当前的"偏黑"是 ImGui `StyleColorsDark()` 默认 ~#15151E 直接套用的结果，v0.6.5 抛弃默认 |
| **主 accent 色** | **橙 #FF8A3D**（与 OrangeEngine brand 一致，保留鲜亮饱和度） | 选橙是 brand 一致性决策；保留高饱和度但配套半透 selection 是为了规避橙色作为大色块的眼睛刺激 |
| **selection 表现** | **半透叠加**（橙色 30% alpha overlay），**不**用 Godot 那种整行实色填充 | 橙色处于警告色波段，人眼对其本能更敏感（消防 / 施工 / 警示牌同因）；大面积饱和橙整行长时间盯易疲劳。橙的辨识度优势在"小面积跳出来"——把它当 focus outline / active tab 下划线 / Save dirty 高亮 / Play indicator 等"小面积高对比"用反而更体现 brand |
| **alert 色系**（与 accent 共存） | 待 v0.6.5 开工时具体化；候选：warn = 黄 / 暖橙偏黄（与主 accent 橙做饱和度 / 色相区分）；error = 红；success = 绿 | alert 不与主 accent 同色，否则失去"状态提示"功能；橙作为 brand accent 时 warn 必须明显偏黄或换色系 |
| **icon font** | **Codicons**（VS Code 同款，MIT 许可证，~500 图标，IconFontCppHeaders 头文件直接接入） | 三候选（Codicons / Lucide / FA6）中 Codicons 与 Cocos 工具感方向匹配度最高；图标数量精准覆盖 OrangeEditor 当前 UI 表面所需 30–40 个；微软 VS Code 用户认知度高，"看起来专业工具"第一印象 |
| **图标使用边界** | **不**直接拷贝 Cocos Creator 或 Godot 的 PNG 图标资产 | Cocos Creator 是专有 EULA（非 MIT，与 cocos-engine MIT 不同），其图标资产不能重分发；Godot 图标 MIT 兼容但混用 Godot 图标 + Cocos 风格布局会破坏 v0.6.5 "视觉统一" 目的，且长期沾染"看起来像 Godot"印象。Codicons 一步到位，无过渡期 |
| **圆角半径** | 倾向 **0 ~ 1px**（跟 Cocos 路线）；具体值 v0.6.5 开工时定 | Cocos 控件方正接近 0px 圆角，整体"工具感"；Godot 用 2–3px 略柔。OrangeEditor 走 Cocos 工具感所以倾向 0 ~ 1px |
| **字号档位** | 基于 v0.4.5 已落的 DPI scale 18px baseline 派生 H1 / H2 / Body / Caption 四档；具体值 v0.6.5 开工时定 | v0.4.5 已落的 `glfwGetWindowContentScale + ScaleAllSizes(scale)` 是 baseline，本期只是在它之上分档 |
| **per-component-type 色带颜色** | 待 v0.6.5 开工时具体化；每种内置 component 一个低饱和色（Transform / Renderable / DirectionalLight / RigidBody / Collider / ParticleEmitter / Animator / Name） | 低饱和度避免与 brand 橙 accent 互相干扰；色带本身只 4px 宽不抢戏 |

**红线**（与既有 v0.6.5 红线节并存，沉淀到 CLAUDE.md "OrangeEditor 架构纪律" 同级严肃）：

- **禁止直接调** `ImGui::PushStyleColor(ImGuiCol_xxx, ImVec4(0.x, 0.x, 0.x, 1.0f))` 字面量 RGBA —— 必须经 `EditorTheme` token；加进 `scripts/check_invariants.py` 作为编辑器侧新 lint 规则（v0.6.5 c? 落地时同步实装）
- **禁止 selection 整行实色填充** —— 必须用半透叠加，违反者视为今天讨论清楚的决策被推翻，需要重新立项讨论
- **禁止直接拷贝 Cocos Creator / Godot 的 PNG 图标资产**进 OrangeEditor 仓库
- **禁止裸文字按钮**（如 `"X"` / `"+"` / `"▼"` 字面量）—— 必须用 Codicons codepoint

**v0.6.5 开工时需要具体化的小项**（不在本节定，留给 commit-plan 内处理）：

- 背景 / panel 一档亮 / panel 二档亮 / 输入框 / 分隔线 / 字色 / 控件三态色 的具体 RGB（从 Cocos 3.8.8 截图采色）
- 圆角具体 px（0 / 1 / 2 三选）
- 字号档位具体 px
- 8 个内置 component 的色带具体色
- alert 色系（warn / error / success）具体色

**关联 wiki 缺页**（按 CLAUDE.md 纪律，wiki 维护另开 wiki session 处理；本节内**不**抄通用知识，仅留 anchor 指向后续 ingest 候选）：

- `concepts/editor-ui/dark-theme-base-color.md` —— 暗色编辑器背景走炭灰的人因学根据 + 跨编辑器实测对比
- `concepts/editor-ui/accent-color-budget.md` —— accent 用量 ~4% 设计原则、暖色 vs 冷色 accent 的人眼疲劳差异
- `techniques/editor-ui/icon-font-integration.md` —— ImGui icon font merge 方案，Codicons / Lucide / FA6 对比
- `comparisons/editor-ui/cocos-vs-godot-style.md` —— Cocos 3.8.8 vs Godot 4.6.2 视觉风格对比表

wiki ingest 完成后，本节"决策由来" 列应改为引用 wiki 页相对路径，删除复述的通用知识。

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
| L7 | **Animator backend 切换 + 状态机图不可编辑** —— Inspector 看不到 Animator 内部 | ✅ v0.7（c1 backend Combo + c2 状态机图全套 + c3 DragonBones metadata + c4 Procedural channel name 浏览） |
| L8 | **Console 面板只显示帧统计** —— 没接 Core::Log | v0.8 |
| L9 | **快捷键硬编码** —— F2 / Del / Esc 等不可配置 | v0.8 |
| L10 | **没 Profiler / Debug Draw** | v0.9 |
| L11 | **没 ACP** —— mesh / texture 走 source asset | v1.x（依赖主 roadmap Phase 9） |
| L12 | **没热重载** —— shader / scene 改了要重启 | v1.x（依赖主 roadmap Phase 8） |
| L13 | **没 Editor Settings 系统** —— gizmo 线宽 / handle 长度 / 颜色等视觉常量散落在各 cpp anonymous namespace 的 constexpr，不可在运行时调整 | v0.8（v0.6.5 先集中到 `EditorTheme.h`，v0.8 迁入 EditorSettings） |
| L14 | **视觉风格不统一** —— v0.4 ~ v0.6 各 UI 表面功能优先落地，配色 / 图标 / 控件三态散乱；v0.4 收尾时被用户当场指出 viewport 工具栏不够美观以致没法做完整功能验证 | v0.6.5 |
| L15 | **Schema 注册依赖文件作用域 static 指针** —— v0.5 c1 在 `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp` 引入 `gpAssetRegistry` / `gpNamedMaterialInstances` + 外部 setter（`SetAssetRegistryForSchema` / `SetNamedMaterialInstancesForSchema`），让 `FieldAssetRef` 的 getFn / setFn 能拿运行时数据。技术上不违反公共头 isolation，但走 hidden global state 违背 v0.2.5 EditorHost 单例 hub "消除散落 global" 的设计本意；新增任一需要运行时上下文的 schema 字段都会被诱导继续加 static 指针 | v0.8（与 EditorSettings 整骨同期；让 `FieldAssetRef` / 任意需要运行时上下文的 schema 注册入口接受 `EditorHost&` 或对应子 context 引用作为显式参数，删除 `gpAssetRegistry` / `gpNamedMaterialInstances` + setter） |
| L16 | **Inspector 顶部分发缺 IEditorAssetInspectorPlugin 抽象** —— v0.5 c5 在 `InspectorPanel.cpp` 顶部加 `IsMaterialAssetSelected → DrawMaterialSubMode` if 分支接管 Asset 浏览器选中。当前只一条特殊路径，不构成 god if/else；但 v0.7 Animation 子模式（选中 `.anim_fsm`）+ 后续可能的 scene preview（选中 `.scene.json`）都是同款"按选中资源类型切 Inspector 内容"路径，再加两条就会演化成 if/else 链，正中 v0.2.5 整骨禁令 | ✅ v0.7 c0（`tools/OrangeEditor/plugin/IEditorAssetInspectorPlugin.h` 接口 + `EditorHost::assetInspectorPlugins` 注册表 + `MaterialAssetInspectorPlugin` 第一个真实 case；InspectorPanel.cpp 顶部 hardcode if 改为遍历 plugin 注册表，未来 `.anim_fsm` / `.scene.json` 子模式只需注册 plugin） |
| L17 | **Pick 按钮对 `.material` 字段不可用** —— v0.5 验收 patch（B3 互斥选择修复）副作用：点 `.material` 文件 → Material 子模式接管 → 实体 Inspector 不画 → 字段 Pick 按钮永远不显示。 | ✅ v0.6 c7（Asset 浏览器右键菜单 "Pick to Renderable.mesh" / "Pick to Renderable.material"；RMB 路径不触发 left-click selectedAssetPath 改写，所以选中 entity 不被清，绕过 Material 子模式接管） |

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

### v0.4.5 · UI DPI 自适应 + Inspector / Panel widget 比例化 ✅

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

### v0.5 · Asset 浏览器 + Material 子模式 ✅

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

### v0.6 · 多 chunk / per-layer + dirty 状态 ✅

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

### v0.6.5 · 视觉统一与主题打磨 ✅

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
- **顶部全局 toolbar 抽象 + polish（v0.6 c3 收尾）**：v0.6 c3 当时把 Save / Play / Pause / Stop 妥协塞进 menu bar 右侧（同行与 File/Edit/View/Help 挤在一起），与 v0.6 c3 deliverable 原意"聚到一条 toolbar 上"未对齐。本 milestone 抽出**独立 toolbar 行**（紧贴 menu bar 下方、跨 dock root 整宽、固定高度 `GetFrameHeight() * 1.2`），Save / Play / Pause / Stop / [State] 从 menu bar 迁出，**Play / Pause / Stop 在 toolbar 中央**（参 Cocos Creator 3.8.8 布局），Save 靠左，[State] 靠右。Save 在 dirty 时按 EditorTheme accent 橙高亮（小面积，不整 button 填充）；Play / Stop 用对比色（绿 success / 红 error）易识别
- **panel 视觉统一**：标题栏 / 分隔条 / 折叠箭头 / Inspector component header 折叠图标 / Entity Tree 行 hover/select 状态全部按 EditorTheme 重画
- **Inspector 控件三态**：DragFloat / SliderFloat / Combo / Button hover / active / disabled 三态颜色一致
- **acceptance scene**：在 demo.scene 上完整跑一遍"开场景 → 选实体 → 改 Inspector → gizmo 拖 → Save → Play → Stop → Build" 路径，全程视觉风格一致，无 ImGui 默认深蓝 / 灰白色块漏出
- **附带：v0.4 c5 工具栏 8 项遗留功能验收**——`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v0.4-acceptance-checklist.md` c5 段共 8 项 `[ ]`（Gizmos 总开关 / disabled tooltip / Camera frustum / 多 plugin 并存 / Play vs visible 正交 等）。v0.4 期用户因工具栏不美观推迟，本 milestone 美化完成后一并跑；跑完把 c5 段父级标记勾 ✅，节点 A 大回归（P2）覆盖剩余

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

### v0.7 · Animation 子模式 ✅

**关键 deliverables**：

- **c0 前置整骨（消除 L16）**：抽 `IEditorAssetInspectorPlugin` 接口 + 注册表（与 `IEditorInspectorPlugin` 对偶，按选中资源扩展名 / kind 分派）；v0.5 c5 Material 子模式从 `InspectorPanel.cpp` 顶部 `IsMaterialAssetSelected` 分支迁出为第一个 plugin case。本条不引入 v0.7 新功能，纯为后续两条 deliverables 让路；落地后 InspectorPanel 顶部 if 分支消失，新增 asset 子模式（.anim_fsm / .scene.json）只需注册 plugin
- Inspector 的 Animator 段加 backend 切换（Skeletal / Procedural）
- Skeletal animation state machine **图编辑**（节点 = state，边 = transition + condition），保存到 .anim_fsm —— 通过 c0 注册的 plugin 接管 `.anim_fsm` 选中态，作为抽象的第二个真实 case
- DragonBones 资源浏览 + 单 clip 预览（独立窗口）
- Procedural Animator 的 channel 配置面板（fn 名 + 目标 uniform）

**前置**：v0.5（资源浏览器）+ v0.2（图编辑改动走命令）

**与引擎关系**：消费 Phase 4 Task 01 的 AnimationStateMachine + IAnimator + AnimatorRegistry；Phase 4 Task 03/04 的 Skeletal / Procedural 后端

**Critical Path**：否（第一款游戏可以先用 Inspector 字段编辑临时凑）

### v0.8 · 编辑器 Log + 输入扩展 + Settings ✅

**关键 deliverables**（2026-05-19 跨仓 session 全数落地）：

- ✅ Console 面板接 `Core::Log`（c2）—— 加 `SetLogSink` API 到 OE `Core::Log`，编辑器侧 sink callback push ring buffer（cap 1024）+ level filter + search + auto-scroll
- ✅ Keybinding 自定义（c4）—— `EditorKeybindings` struct 含 5 条常用快捷键（W/E/R + F2 + Delete），Settings 面板 "Keybindings" 段含 [Rebind] 按钮，Esc 取消；持久化与 EditorSettings 共享 `editor_settings.json`
- ✅ 多选 / Ctrl-click 多选实体（c3）—— `EditorSelection::additionalSelectedEntities` + IsSelected / SelectedCount / ToggleAdditional helper；EntityTreePanel Ctrl-click 进入 toggle 路径
- ✅ 多选 Inspector（c3）—— primary 仍渲染，多选时上方加 banner "N entities selected (showing primary) [Multi-edit not yet wired]"；完整异构多选 → 共有属性写广播留 v0.9 拓展（需 schema 路径扩 "属性写广播" 入口）
- ✅ **Editor Settings 系统**（c1，消除 L13）—— 14 字段（6 gizmo 线宽 + handleLength + hitThreshold + 6 gizmo 配色）+ `editor_settings.json` 持久化 + Settings 面板 sliders / color pickers + "Reset to defaults"
- ✅ **Schema 注册 context 注入整骨**（c5，消除 L15）—— 单一 `SetEditorAssetContextForSchema(&editorHost.assets)` 注入路径替代原两个独立 setter；`gpAssetContext` 单指针替代 `gpAssetRegistry` + `gpNamedMaterialInstances` 两个文件作用域静态；`namedMaterialInstances` 移入 EditorAssetContext 自身。完整 `(Component&, const EditorAssetContext&)` get/set 签名整骨留 v0.9（PropertyDescriptor + SchemaInspector dispatch 体量较大）

**完工记录**：见 `vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v0.8-acceptance-checklist.md`。本期由 `/goal` 跨仓 session（解决 OE / OR / Editor 全 GAP + v0.8）顺势串完，与 Phase 6.5 整体 ✅ 在同一 session 收尾。

**前置**：v0.2

**与引擎关系**：消费 `Core::Log`（Phase 1 Task 04 已落，**v0.8 c2 内顺手加 SetLogSink API**）+ Input 模块（Phase 4 Task 08 已落）

**Critical Path**：否

### v0.8.5 · 编辑器视觉基线（sky / grid / tonemap / PBR showcase） ✅

**关键 deliverables**（2026-05-19 单 commit `ca0f112` 落地；后置 patch milestone 锚点）：

- ✅ **sky-dome pass**：`Pipeline::SetSkyEnabled / IsSkyEnabled` 公共 API；`bakedEnvCube` 有时走 cubemap 采样，无时走 procedural 3 色 horizon gradient + 太阳 disc（DirectionalLight 驱动方向 / 颜色 / 强度）
- ✅ **viewport grid pass**：`Pipeline::SetEditorGridEnabled` 公共 API；fullscreen quad + Ben Golus PristineGrid（细线 1m / 粗线 10m / 20→80m 淡出）+ sceneDepth 比较 + discard 处理几何遮挡（不走 `gl_FragDepth` 避撞精度坑）
- ✅ **ACES tonemap**：`passthrough.frag.glsl` 自带 Narkowicz fit；编辑器 RenderOffscreen 路径直读 ACES（不消费 PostProcessChain bloom+tonemap，HDR > 1 像素 shader 内自压回 [0,1]）
- ✅ **dummy IBL ambient**：从全 0 提到 (0.25, 0.25, 0.25) 灰；未挂 EnvironmentComponent 时 PBR 物体不再显灰白塑料
- ✅ **viewport clear color**：深蓝 (0.05, 0.07, 0.10) → 中性灰 (0.12, 0.12, 0.13)；sky 关时背景克制
- ✅ **PBR showcase 场景**：`assets/scenes/pbr_showcase.scene.json`（24 entity = Root + Camera + Sun + Environment + 两组 3×3 球阵）+ 18 个 `pbr_showcase/{warm,white}_m{0-2}r{0-2}.material` + `assets/meshes/sphere.mesh`（32×16 UV-sphere v3 含 normal）；启动期 lazy seed + Save 落盘，File → Open Scene 加载
- ✅ ScenePanel toolbar 加 Grid / Sky checkbox；Add Component → Renderable 默认材质 `default.material` → `pbr.material`（与 Cocos 默认 cube 手感一致）
- ✅ **顺路 bug fix**：GAP-2026-05-19-editor-environment-component-wiring 再修（漏注册 TextureLoader）+ cube.mesh face normal 朝向修（quad 顶点序与 cross 约定反向）+ EnsureHdrTarget 重建 sceneDepth 漏 reset ShaderReadOnly layout flag（pre-existing）

**已知归属债**（v0.8.5 验收通过但已显式登记，下次相关 session 解套）：

- grid pass + dummy IBL ambient 默认值 + clear color 是 **编辑器审美决定**，当前塞在 engine `Pipeline` 公共面 → `docs/engine-known-gaps.md` GAP-2026-05-19-editor-aux-passes-in-engine-pipeline
- PBR push constant 160 B 撞 Vulkan 规范保证下限 128 B（桌面 GPU 普遍 256 B，老 Intel iGPU / 移动端会 fail）→ `docs/engine-known-gaps.md` GAP-2026-05-19-pbr-push-constant-exceeds-spec-min；已在 `Pipeline::SetupRhiResources` 加 init-time 校验，devicelimit 不足时日志告警

**完工记录**：见 `vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v0.8.5-acceptance-checklist.md`。

**前置**：v0.8（消费 `Core::Log` SetLogSink）+ Phase 6.5（消费 `EnvironmentComponent` / `Pipeline::BakeIblFromWorld` / PBR 材质路径）

**与引擎关系**：sky-dome 与 ACES tonemap 是引擎 Pipeline 永久能力（游戏侧也消费）；grid pass + ambient 默认值 + clear color 是已登记的 editor 归属债，迁回 editor 端走独立 session

**Critical Path**：否

### v0.9 · Profiler / Debug Draw 集成 ✅

**关键 deliverables**（2026-05-19 跨仓 session 全数落地，c0–c8 commit 序列）：

- ✅ **Debug 绘制 API**（c1 + c2）—— `Orange::Engine::Render::DebugDrawScene` 公共面 wrap `OrangeRender::Renderer::DebugDraw`：AddLine / AddAabb / AddSphere / AddTriangle 四条 immediate-mode 入口 + Pipeline 自管 RecordDebugDrawPass（grid 之后、passthrough 之前，always-on-top 不写深度）+ ScenePanel toolbar Debug Draw checkbox + 选中实体黄色 sphere overlay 试点。sample 15 `samples/15_debug_draw_minimal/` 端到端验证（line/aabb/sphere/triangle 全覆盖）。**Text 暂未提供**——ImGui DrawList screen-space overlay 已覆盖 gizmo label / panel 标签等 text 用例；3D world-space text 留 v1.x 按需扩
- ✅ **Profiler 面板自实现 AutoProfile RAII**（c3 + c4 + c5）—— ADR-003 选定方案 B（自实现 in-game 路径，不接 Tracy）：`include/orange/engine/core/Profiler.h` AutoProfile RAII + 分层 sample bin（parent 关系树）+ FinalizeFrame 算 exclusive。`ORANGE_PROFILE_SCOPE` / `ORANGE_PROFILE_DECLARE_BIN` 宏；AppHost 主循环帧末 FinalizeFrame；Pipeline 内置 9 个 bin（Frame / PollEvents / LayerUpdate / Render / Shadow / Sky / MainPass / Particles / Bloom / Tonemap / DebugDraw / Grid）。ProfilerPanel Performance tab：帧耗时 PlotLines（128 帧 ring）+ sample bin TreeNode 表格（Name / Inclusive / Exclusive / Calls）。**Tracy gate 保留 CMake stub 不接通**——双轨升级路径走 superseding ADR
- ✅ **内存统计 per-module**（c6）—— `include/orange/engine/core/Memory.h` per-category 字节级 counter API（RegisterCategory / AddBytes / SubBytes / Snapshot，含 current / high-water / alloc/free count 四件套）。ProfilerPanel Memory tab：Tracked Categories（Core::Memory opt-in，模块未来 instrument）+ Module Counts（走现有 introspection API：World::Size / AssetRegistry::Size / Pipeline::TemplatePipelineCount / Pipeline::BloomMipCount / Profiler::BinCount / Memory::CategoryCount）。byte-accurate per-allocator hook 是非平凡 refactor，留未来工作

**v0.8 c5 自承诺的"完整 `(Component&, EditorAssetContext&)` 签名整骨"**：调研发现是 GetFn/SetFn 签名扩 context 参数 + 20+ lambda 重写 + dispatch 改造，半天+ 工作量，与 v0.9 主线（DebugDraw + Profiler + Memory）正交且不阻塞用户体验。重新评估推延到 **v0.9.5 patch milestone**（先例：v0.6.5 / v0.8.5 都是从大 milestone 拆出的 patch）。

**完工记录**：见 `vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v0.9-acceptance-checklist.md`。ADR-003（Profiler 后端选型）在 v0.9 完工后切 status: accepted。

**前置**：v0.2（Editor 命令栈基础）+ v0.8 c2（SetLogSink 同节奏的 engine→editor 数据流模式 reuse）

**与引擎关系**：DebugDrawScene 是 engine 端 wrap（Header isolation 守住）；Profiler / Memory 是 Core 模块新公共面（零第三方 include）。Pipeline 内置 PROFILE_SCOPE 让游戏侧消费 Pipeline 时也能看到 frame breakdown。

**Critical Path**：否

### v0.9.5 · Schema dispatch 整骨（v0.8 c5 → v0.9 → v0.9.5） ✅

**触发**：v0.8 c5 自承诺留 v0.9；v0.9 完工评估后推延到本 patch milestone。

**关键 deliverables**（2026-05-19 落地，c1–c4 commit 序列）：

- ✅ **AssetRefAccessor 专用槽位**（c1）—— `PropertyDescriptor` 加 `AssetRefGetFn / AssetRefSetFn`（带 `const EditorAssetContext&` 参数）+ `assetRefGet / assetRefSet` 字段，与既有 `get/set` 槽位并存
- ✅ **SchemaInspector AssetRef case 双路径 dispatch**（c2）—— `useCtxAccessor` 分支判定：assetRefGet/Set 非空走新路径 + `MakeAssetRefFieldApply` 命令栈 replay；否则回退旧 get/set + `MakeFieldApply`（外部 plugin 兼容防御）
- ✅ **内置 lambda 全数迁移 + 下架 gpAssetContext**（c3）—— `ComponentSchemaBuilder::FieldAssetRef` 改签名收新 accessor；Renderable mesh/material + Environment cubemap 共 6 个 lambda 改走 ctx 参数；`gpAssetContext` 静态指针 + `SetEditorAssetContextForSchema` setter + main.cpp 启动期注入 同步下架
- ✅ **ADR-004 + acceptance-checklist**（c4）—— 选型决策（方案 B 专用槽位 vs 方案 A 扩全字段加 ctx）+ 升级到方案 A 的 trigger 条件 + 5 项编辑器手动验收回归项

**与原描述的偏差**：原 deliverable 提两条候选（"扩 GetFn/SetFn 全字段加 ctx" vs "引入 AssetRefAccessor 槽位"），ADR-004 选定后者。理由：当前只有 AssetRef 一类需 ctx，方案 A 为"假设可能出现的扩展"提前付 40+ 站点改动成本不对称；方案 B 与 Lumix 模式一致（同栈参考引擎 setter 不带 editor ctx），未来升级到方案 A 仍可行（ADR-004 Notes 段记录升级路径）。

**完工记录**：见 `vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v0.9.5-acceptance-checklist.md`。ADR-004（Schema AssetRef accessor 选型）在本 milestone 完工同 commit 切 status: accepted。

**前置**：v0.2.5（schema-first 基础）+ v0.9（无依赖，正交）

**与引擎关系**：纯编辑器侧 refactor，不动 engine 公共面。

**Critical Path**：否（同 v0.6.5 / v0.8.5 节奏，patch milestone 不在 v1.0 critical path 上）

### v1.0 · 验收里程碑 ✅

v0.1 ~ v0.9.5 全部 ✅。验收路径：邀请非程序员（如美术 / 关卡设计师）跑一个 30 分钟典型任务 —— 搭场景 / 调材质 / 摆灯光 / 跑 Play Mode 看效果 —— 不需要程序员介入。

通过即声明 Phase 6 当初的目标 "**足以让美术 / 关卡设计师不写代码完成日常工作**" 闭环。

**触发**：v0.9.5 完工后 2026-05-22 由项目作者本人跑验收。作者背景"开发了引擎本身 / 没做过游戏开发 / 没深入用过任何引擎搭场景" —— 与 Phase 6 目标用户画像（不熟悉怎么搭场景反而是优势，不会用 power-user shortcut 绕过 UX 漏洞）实质等同；唯一克制点：遇到卡点不许开 IDE 改代码，开 = 该步 fail。

**关键 deliverables**（2026-05-22 落地，同 session 完成）：

- ✅ **v1.0 验收任务脚本** —— 4 段 30 分钟脚本：A 空场景 / B 加光物件 / C 物理粒子 / D 序列化 round-trip（`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.0-acceptance-task-script.md`）
- ✅ **跑通验收** —— 11 项验收点全数 PASS；2 段 Friction fail（A2 New Scene 误 reseed demo / B1 DirectionalLight Inspector 缺方向 helper）按"Critical / Friction / Visual"三档纪律不阻塞 ✅
- ✅ **同 session 修 v1.0 阻塞 Critical fail** —— `GAP-2026-05-22-new-scene-actually-seeds-demo` G1 落地（`tools/OrangeEditor/EditorRenderLayer.cpp::ApplyPendingSceneOp` 移除 `SeedDemoWorld(mHost)` 调用 + 同步删除 dead include + log message 改 "new empty scene"）
- ✅ **3 个 v1.0 验收期间发现的 Friction GAP 留 v1.x batch** —— directional-light-inspector-direction-helper-missing (P1) / multi-directional-light-semantics-undefined (P2，工业对照 Unity / Unreal / Godot / Lumix 均允许多个，归属"补足主光语义 + UI 反馈"而非"禁止多个") / shadow-not-tracking-directional-light-direction（撤回，合并到前者 G2，UX 误解非真 bug）
- ✅ **acceptance-checklist + 版本号 bump** —— OrangeEditor CMake `project(...) VERSION` 0.0.2 → 1.0.0 第一次与 roadmap 概念对齐（v0.1~v0.9.5 期 CMake VERSION 字段未维护；v1.0 验收同 session 拉齐）；详见 `vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.0-acceptance-checklist.md`

**与原描述的偏差**：原文写"邀请非程序员（美术 / 关卡设计师）"，实际由作者本人跑 —— 因独立开发无外援可拉。**通过 think-aloud 守纪律 + 不开 IDE 红线 + 验收脚本压低 power-user shortcut 空间**等效模拟目标用户视角，验收信号同样有效（A2 / B1 两个 fail 是程序员视角看不见、零基础用户视角立刻显现的 UX 陷阱，完美兑现脚本设计意图）。

**前置**：v0.1 ~ v0.9.5 全部 ✅

**与引擎关系**：纯编辑器侧 + 编辑器消费引擎公共面已稳，未触发任何引擎公共头改动；同 session 唯一引擎仓修改 `tools/OrangeEditor/EditorRenderLayer.cpp` 是编辑器侧 New Scene 行为修正。

**Critical Path**：是（Phase 6 OrangeEditor 子项目终结里程碑）

**完工记录**：见 `vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.0-acceptance-checklist.md`。本 milestone ✅ 后 OrangeEditor 自身 semver 切 stable（1.0.x patch / 1.x.0 minor 行为按一般 semver 演进），编辑器公共 API 进入"破坏性改动需走 major bump"约束（与引擎本体 0.x 仍 unstable 区别）。

### v1.0.1 · Friction Patch Batch ✅

**版本**：v1.0 ✅ 后第一个 patch milestone（按 [[feedback-post-v1-versioning]] 纪律走 v1.0.xx）
**落地日期**：2026-05-22

**范围**：5 个 v1.0 验收期间 / 后续试搭场景登记的 P1/P2 friction GAP 打包，纯 UX / 视觉 polish，零新功能。

| GAP | 优先级 | 改动落点 |
|------|--------|----------|
| `editor-dock-layout-collapses-on-restore` | P1 | OnUpdate 内检测 viewport 收缩 > 25% 时 RemoveNode 让 BuildDefaultLayoutOnce 重建 |
| `editor-default-ibl-missing-causes-black-pbr-faces` | P1 | `Pipeline.cpp` dummy IBL irradiance 灰度 0.25 → 0.5（aux passes 路径） |
| `directional-light-inspector-direction-helper-missing` | P1 | ComponentSchema 加 `helperText` 字段 + `Builder::Helper(...)` API + SchemaInspector 段顶渲染；DirLight schema 加 helper 段 |
| `multi-directional-light-semantics-undefined` G1 | P2 | DirLight schema helper 合并 + Hierarchy ⚠ chip + tooltip（与 #5 共用 overflow set） |
| `multi-environment-component-semantics-undefined` G1 | P2 | Environment schema helper + 同款 Hierarchy ⚠ chip |

**验收文档**：`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.0.1-acceptance-checklist.md`

**与引擎关系**：engine 侧仅一处改动（`src/render/Pipeline.cpp` dummy IBL fallback 灰度 bump，shipping 路径不动）；其他全部在 `tools/OrangeEditor/`。CMake VERSION 1.0.0 → 1.0.1。

**Critical Path**：否（v1.0 已 ✅，后续走 patch 通道）

### v1.1 · DCC Asset Import Pipeline ✅

**版本**：v1.0 ✅ 后第一个 minor bump（按 [[feedback-post-v1-versioning]] 纪律新功能 / 大架构走 minor）

**触发 GAP**：[`GAP-2026-05-22-editor-dcc-import-pipeline-missing`](engine-known-gaps.md) —— 编辑器缺外部 DCC 资产（.obj / .gltf mesh + .png / .jpg / .tga texture）导入流水线，"美术 / 关卡设计师不写代码完成日常工作"目标最关键入口能力

**架构决策**：见 [ADR-008](decisions/README.md)（5 议题合并：A3 UX / B2 物理布局 / C2 .meta sidecar / D3 vendor / E1 mipmap / F2 scope）。本 milestone 不再单独讨论这些议题，按 ADR-008 落地

**Task 拆分**（detail 进入实施 session 时展开）：

| Task | 描述 | 影响模块 | 估时 |
|------|------|---------|------|
| T1 | TextureLoader 开 PNG/JPG/TGA decoder + texture .meta v1 schema | `src/asset/TextureLoader.cpp` + `tools/OrangeEditor/import/` | 0.5 session |
| T2 | File→Import 菜单 + Asset Browser drag-drop OS 文件路由 + 文件类型分派 | `tools/OrangeEditor/EditorRenderLayer.cpp` + `tools/OrangeEditor/import/ImportDispatcher.{h,cpp}` | 0.5 session |
| T3 | tinyobjloader vendor 接 + ObjImporter 模块 + 转 `.mesh` v3 写盘 | `vendor/tinyobjloader/` + `tools/OrangeEditor/import/ObjImporter.{h,cpp}` | 1 session |
| T4 | cgltf vendor 接 + GltfImporter 模块 + 转 `.mesh` v3 写盘（PBR material 解析延 v1.2） | `vendor/cgltf/` + `tools/OrangeEditor/import/GltfImporter.{h,cpp}` | 1 session |
| T5 | .meta sidecar 完整接通（hash 增量重 import + Inspector 显示 import 参数 readonly） | `tools/OrangeEditor/import/MetaSidecar.{h,cpp}` + `tools/OrangeEditor/inspector/*` | 0.5 session |
| T6 | `.gitignore` 规则扩展 + acceptance-checklist + Wiki cross-pollination 案例（若有）| `docs/` + `assets/` + Wiki | 0.5 session |

**前置已具备**（不必补）：
- `AssetRegistry::Insert<T>()` runtime 注入 in-memory asset 已支持
- `TextureLoader` stb_image 已编进，PNG/JPG/JPEG/TGA 仅需删 `STBI_NO_*` 行
- HDR 路径已通过 PolyHaven `assets/environments/` 验证

**引擎侧需要补的能力**（v1.1 同期 PR 但走引擎自身改动通道）：

| 模块 | 改动 | 备注 |
|------|------|------|
| `TextureLoader.cpp` | 删 `STBI_NO_PNG` / `STBI_NO_JPEG` / `STBI_NO_TGA` | 1 行级，进 v1.1 T1 |
| `MeshLoader.cpp` | 无改动 | `.mesh` v3 格式已够；importer 写出 v3 |

**新增 vendor**（CMakeLists.txt + `vendor/`）：

| Vendor | 用途 | 许可证 |
|--------|------|--------|
| tinyobjloader | `.obj` parser | MIT |
| cgltf | `.gltf` / `.glb` parser | MIT |

**Critical Path**：是（"美术工作流补完"是 v1.0 后第一个新功能 milestone，是第一款游戏 fork 启动的前置之一）

**验收**（acceptance-checklist 在 v1.1 实施完成时落 Wiki）：
- 美术 / 关卡设计师**不写代码**完成"从 Blender 导出 .obj/.gltf → 拖到 Asset Browser → 应用现有材质 → 渲染正确"完整闭环
- 同款流程对 .png/.jpg/.tga 贴图：拖入 → 入 AssetRegistry → Pick 给材质 → viewport 实时反映
- 源文件 hash 变化触发 .meta 自动更新 + 重 import（手动右键 Reimport 兜底）

**与引擎关系**：引擎 runtime 纯净保持（importer 全部在 `tools/OrangeEditor/import/`，不污染 `src/asset/`）；CLAUDE.md "OrangeEditor 架构纪律" 节增 invariant：新增任何外部资产格式必须走相同 4 件套路径（vendor 单 header + 转引擎自家二进制 + .meta sidecar + AssetRegistry Insert）。CMake VERSION 1.0.1 → 1.1.0。

**关联 GAP**：v1.1 ✅ 后 [`GAP-2026-05-22-editor-material-create-and-thumbnail-missing`](engine-known-gaps.md) 进入触发条件（material thumbnail 烘培需要真外部贴图，前置已具备）→ 建议 v1.2 / v1.x 跟进。

### v1.2.6 · lazy create 应用 .template.json 默认值 ✅

**版本**：v1.2.5 ✅ 后第六个 patch
**落地日期**：2026-05-24

**范围**：修复 v1.2.5 后用户继续报"plane 没变成动态水面，材质名称倒是更改了"的真正视觉根因。

**根因**：v1.2.5 修了 Inspector 字段反查（cache 同步），但**新 lazy create 的 instance 没有 uniform override**——v1.1.1 Create Material modal 落盘的 .material 文件 uniforms 段是空的，`ApplyDataToInstance` 是 no-op。Pipeline 推全 0 push-constant：uBaseColor=(0,0,0,0) 黑色 / uMRA=(0,0,0,0) → water Wave Amplitude/Speed/Fresnel 全 0 → 视觉纯黑塑料看起来"没变水面"。

**修复**：`EnsureMaterialInstance` lazy create 后从 `.template.json` 的 `uniforms[].default` 字段读默认值调 `SetUniform` 兜底（仅当 .material 未 override 时——`HasUniformOverride` 检查保用户调过的值不被覆盖）。

**改动**：

| 文件 | 改动 |
|------|------|
| `tools/OrangeEditor/BuiltinAssets.cpp::EnsureMaterialInstance` | lazy create 后加 ~40 行 default uniforms 应用（含 vec4 / vec3 / vec2 / float / int 派发；mat4 跳过——uMVP/uModel hidden 不进路径）|
| `tools/OrangeEditor/BuiltinAssets.cpp` 顶部 | include `ShaderTemplateMetaIO.h` + glm/vec2.hpp + glm/vec3.hpp |
| `tools/OrangeEditor/CMakeLists.txt` | VERSION 1.2.5 → 1.2.6 |

**验收文档**：`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.2.6-acceptance-checklist.md`

**与引擎关系**：纯编辑器侧 ~40 行修复；CMake VERSION 1.2.5 → 1.2.6。

**Critical Path**：是（v1.2.x 材质工作流系列**真正**最后闭环——新材质拖到 plane 视觉呈现 default 水面 / PBR 灰塑料等，与 Inspector 默认值一致）

**不在本 patch 范围**（明示）：
- v1.1.1 Create Material modal 落盘时直接写 default 到 .material 文件 uniforms 段 → v1.3.0+ minor
- 每次 lazy create 调一次 LoadShaderTemplateMeta IO → 不在热路径，撞性能再加 cache

### v1.2.5 · EnsureMaterialInstance 同步 namedMaterialInstances cache ✅

**版本**：v1.2.4 ✅ 后第五个 patch
**落地日期**：2026-05-24

**范围**：修复 v1.2.4 后用户继续报"新材质无法应用到实体，Inspector 显示 None"的真正根因。

**根因**：v1.2.4 抽 `EnsureMaterialInstance` 统一 lazy create 路径，但漏更新 `EditorAssetContext.namedMaterialInstances` cache 字段。schema AssetRef `materialGet`（RegisterBuiltinSchemas.cpp:359）反查走的是 cache 而非 `BuildNamedMaterialInstances()` 函数，所以新 path 找不到 → Inspector Renderable.material 字段显示 None。`namedMaterialInstances` cache 由 `main.cpp:642-643` 启动期 one-shot 填充（仅 8 hardcode + PBR showcase 18），lazy create 之后未增量同步。

**修复**：`EnsureMaterialInstance` 内 lazy create 后加一行 `host.assets.namedMaterialInstances[materialPath] = rawPtr;` 增量同步 cache。schema 反查、Scene Save / Load 路径都立即看到新 path。

**改动**：

| 文件 | 改动 |
|------|------|
| `tools/OrangeEditor/BuiltinAssets.cpp::EnsureMaterialInstance` | lazy create 后增量更新 `host.assets.namedMaterialInstances` cache（1 行核心修复）|
| `tools/OrangeEditor/CMakeLists.txt` | VERSION 1.2.4 → 1.2.5 |

**验收文档**：`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.2.5-acceptance-checklist.md`

**与引擎关系**：纯编辑器侧 1 行修复；CMake VERSION 1.2.4 → 1.2.5。

**Critical Path**：是（v1.2.x 材质工作流系列的最后闭环——v1.1.1 Create + v1.2.2 Inspector + v1.2.3 DnD + v1.2.4 统一 lazy + v1.2.5 cache 同步，至此美术 / 关卡设计师"不写代码完成日常工作"的材质路径全打通）

**不在本 patch 范围**（明示）：
- BuildNamedMaterialInstances() 函数 vs namedMaterialInstances 字段统一为单一 source of truth → v1.3.0+ minor 架构整骨

### v1.2.4 · DnD apply 新材质修复（统一 lazy create 路径）✅

**版本**：v1.2.3 ✅ 后第四个 patch
**落地日期**：2026-05-24

**范围**：修复 v1.2.3 验收用户反馈 bug——拖未被 Inspector 选过的新 `.material` 到实体时物体材质显示 None。

**根因**：v1.2.2 lazy create 路径只在 Inspector `DrawMaterialSubMode` 触发；v1.2.3 DnD apply lambda 直接调 `BuildNamedMaterialInstances + map.find` 没经 lazy create 路径。用户拖未 Inspector 过的新 .material → BuildNamedMaterialInstances 找不到 → 设 nullptr → 显示 None。

**修复**：抽 `EnsureMaterialInstance(host, path)` helper 到 `BuiltinAssets.{h,cpp}`，含 lazy create 兜底（先查 BuildNamedMaterialInstances 已有 → 命中返回；否则 ReadMaterialFile + CreateInstance + ApplyDataToInstance + own 到 userMaterials → 返回）。Inspector + EditorAssetDropHandler::ApplyMaterial 两处统一调 helper。

**改动**：

| 文件 | 改动 |
|------|------|
| `tools/OrangeEditor/BuiltinAssets.{h,cpp}` | 加 `EnsureMaterialInstance(host, path)` helper（含 lazy create 兜底）|
| `tools/OrangeEditor/plugin/MaterialAssetInspectorPlugin.cpp` | DrawMaterialSubMode 原 namedMap.find + lazy create 替换为 `EnsureMaterialInstance(host, materialPath)` 单调用 |
| `tools/OrangeEditor/EditorAssetDropHandler.cpp::ApplyMaterial` | apply lambda 内 `BuildNamedMaterialInstances + map.find` 替换为 `EnsureMaterialInstance(*pH, p)` |
| `tools/OrangeEditor/CMakeLists.txt` | VERSION 1.2.3 → 1.2.4 |

**验收文档**：`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.2.4-acceptance-checklist.md`

**与引擎关系**：纯编辑器侧；CMake VERSION 1.2.3 → 1.2.4。

**Critical Path**：是（v1.2.3 DnD 工作流真正打通的最后一拼图）

### v1.2.3 · Asset 拖拽到实体（Hierarchy + Viewport 双路径）✅

**版本**：v1.2.2 ✅ 后第三个 patch
**落地日期**：2026-05-24

**范围**：补完 Asset Browser → 实体的拖拽工作流。以前 DnD source 只能在 Asset Browser 起拖，DnD target 只在 Inspector 字段；本 patch 加：(A) **Hierarchy 行**接受 ORANGE_ASSET payload + (B) **Viewport** 接受 + EditorPicking raycast 找命中 entity；按文件扩展名（`.material` / `.mesh` / `.obj` / `.wav` / `.ogg` / `.mp3` / `.flac`）自动分派到对应 component 字段，全部走 `SetFieldValueCommand<std::string>` 命令栈支持 Undo / Redo。

**改动**：

| 文件 | 改动 |
|------|------|
| `tools/OrangeEditor/EditorAssetDropHandler.{h,cpp}` | 新增单点路由 `ApplyAssetDropToEntity(host, target, assetPath)`，复用 EditorRenderLayer "Pick to Renderable.*" 同款 lambda + 命令栈 |
| `tools/OrangeEditor/panels/EntityTreePanel.cpp` | 现有 BeginDragDropTarget（行 527）扩接受 ORANGE_ASSET payload → 调 helper |
| `tools/OrangeEditor/panels/ScenePanel.cpp` | ImGui::Image 之后加 BeginDragDropTarget + AcceptDragDropPayload(ORANGE_ASSET) → 算 NDC + PickEntityAt → 调 helper |
| `tools/OrangeEditor/CMakeLists.txt` | 加 EditorAssetDropHandler.cpp 编译列表 + VERSION 1.2.2 → 1.2.3 |

**验收文档**：`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.2.3-acceptance-checklist.md`

**与引擎关系**：纯编辑器侧改动；CMake VERSION 1.2.2 → 1.2.3。

**Critical Path**：是（v1.2.2 lazy create 之后 Asset Browser → 实体的工作流闭环全打通 —— 美术 / 关卡设计师"不写代码完成日常工作"承诺再补一拼图）

**不在本 patch 范围**（明示）：
- Hover 期间视觉高亮反馈（光标拖过 viewport 时被命中 entity 实时描边）→ v1.x minor 与 outline 系统同期
- Asset Browser 拖动时缩略图反馈 → v1.x minor 依赖 OR offscreen RT

### v1.2.2 · 新建材质 lazy create live instance ✅

**版本**：v1.2.1 ✅ 后第二个 patch
**落地日期**：2026-05-24

**范围**：v1.1.1 Create Material UI 的对偶 friction 修复——新建 `.material` 后 Inspector 撞 `liveInstance == nullptr`（`BuildNamedMaterialInstances` 只 hardcode 8 个内置 + 18 个 PBR showcase）→ 用户无法调参。本 patch 加 **lazy create on first Inspector access** 路径：任何未注册的 .material 首次被 Inspector 访问时即时 `MaterialSystem::CreateInstance(templateName)` + `ApplyDataToInstance(读 .material override)` own 到新加的 `EditorAssetContext.userMaterials` map，立即可调参。

**改动**：

| 文件 | 改动 |
|------|------|
| `tools/OrangeEditor/context/EditorAssetContext.h` | 加 `std::unordered_map<std::string, std::unique_ptr<MaterialInstance>> userMaterials` 字段（own 用户创建 / lazy 触达的实例）|
| `tools/OrangeEditor/BuiltinAssets.cpp::BuildNamedMaterialInstances` | 末尾追加遍历 `userMaterials` 把每条加进返回 map（与 hardcode 同 key 时不覆盖）|
| `tools/OrangeEditor/plugin/MaterialAssetInspectorPlugin.cpp::DrawMaterialSubMode` | `liveInstance == nullptr && originalTemplate 非空` 分支加 lazy `CreateInstance + ApplyDataToInstance + 转 own 到 userMaterials` 路径 |
| `tools/OrangeEditor/CMakeLists.txt` | VERSION 1.2.1 → 1.2.2 |

**验收文档**：`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.2.2-acceptance-checklist.md`

**与引擎关系**：纯编辑器侧改动，无引擎 / 无 OR 影响。CMake VERSION 1.2.1 → 1.2.2。

**Critical Path**：是（v1.1.1 Create Material UI 闭环补完——美术 / 关卡设计师"不写代码完成日常工作"承诺的关键 friction，新建 → 调参 → Pick → 视觉 的整路径现在无断点）

**不在本 patch 范围**（明示）：
- userMaterials 析构时 .material override 自动落盘（auto save）→ v1.3.0+ minor 候选
- userMaterials 内存上限 / LRU 淘汰 → 按需触发
- 创建 Material modal 路径同步 CreateInstance 写 userMaterials（即 B 方案）—— A 方案 lazy create 已覆盖此路径，不需重复

### v1.2.1 · IAuxPassProvider Hook + 公共 API 中性化 ✅

**版本**：v1.2.0 ✅ 后第一个 patch（**校准记录**：原 commit `06b76e3` 拟 v1.3.0 minor，2026-05-24 同日用户当场指出 "2 天内 5 个 bump 太随意，1.x.0 minor 必须伴随多个完整功能落地"，按 [[feedback-minor-bump-must-carry-multiple-features]] 新规则校准为 patch —— 本次仅接口预留 + rename 一项未完整功能，不达 minor 2-3 完整功能门槛；follow-up commit 改 CMake VERSION + 重命名 acceptance-checklist + 更新本节性质，commit 历史保留）
**落地日期**：2026-05-24

**范围**：引入 `IAuxPassProvider` 公共 hook 让外部（editor / 游戏端）注入主 pass 与后处理之间的辅助 pass；引擎公共面命名中性化（`SetEditorGridEnabled` → `SetAuxGridEnabled`，"EditorGrid" 字样从 engine public API 消除）。部分关闭 [`GAP-2026-05-19-editor-aux-passes-in-engine-pipeline`](engine-known-gaps.md) G1。

**精炼 scope**：完整 G1 三阶段（接口预留 / 命名中性化 / grid pass 实际迁出）。v1.2.1 ship 前两阶段；**第三阶段 grid pass 实际迁出**到编辑器端留 v1.3.0+ minor 拉动（与 OR 端 offscreen RT / texture handle 完整暴露同节奏；届时与材质球缩略图 / Pipeline 默认值彻底中性化 / 其他功能合并 ship 凑足 minor 多功能门槛）。

**改动**：

| 文件 | 改动 |
|------|------|
| `include/orange/engine/render/IAuxPassProvider.h` | 新增 interface + `AuxPassContext` struct + RHI 类型前向声明（不破 header isolation） |
| `include/orange/engine/render/Pipeline.h` | rename `SetEditorGridEnabled`/`IsEditorGridEnabled` → `SetAuxGridEnabled`/`IsAuxGridEnabled`；加 `SetAuxPassProvider(IAuxPassProvider*)` 公共 API |
| `src/render/Pipeline.cpp` | rename impl + 加 SetAuxPassProvider 实现 + window 路径 / offscreen 路径各加 hook 调用（grid 之后、debug draw 之前） |
| `src/render/pipeline/PipelineImpl.h` | 加 `pAuxPassProvider` 字段 + `IAuxPassProvider.h` include |
| `tools/OrangeEditor/panels/ScenePanel.cpp` | rename 调用 |

**验收文档**：`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.2.1-acceptance-checklist.md`

**与引擎关系**：仅引擎公共面 + Pipeline 内部改动；编辑器侧仅 1 处 rename；CMake VERSION 1.2.0 → 1.2.1（**校准后**，原 1.3.0）。

**Critical Path**：是（v1.0 验收前预留的"API 中性化"债清理；为 v1.3.0+ minor 中 grid pass 真正迁出 + 缩略图 + 默认值中性化合并 ship 铺垫；为未来 outline / wireframe / debug overlay 等编辑器 / 游戏端辅助 pass 注入提供官方 hook）

**不在本 patch 范围**（明示）：
- Grid pass 实际迁出 + cmake gate 整体移除 → v1.3.0+ minor（与缩略图合并凑足 minor 门槛）
- Pipeline `SetAmbient` / `SetClearColor` 公共 API + 默认值彻底中性化 → v1.3.0+ minor 与 grid 迁出一起
- 材质球缩略图（GAP-2026-05-22 G2）→ 与 grid 迁出合并到 v1.3.0+ minor，依赖 OR offscreen RT API
- 多 provider 链式调用 → 真有需求拉动再加

### v1.1.1 · Create Material UI ✅

**版本**：v1.1 ✅ 后的第一个 patch milestone（按 [[feedback-post-v1-versioning]] 纪律走 v1.x.y）
**落地日期**：2026-05-24

**范围**：单一 P1 friction GAP 修复——Asset Browser 缺"新建材质"GUI 入口。补完 v1.1 "外部资产进来" 之后的对偶能力"项目内从零创建"。

| GAP | 优先级 | 改动落点 |
|------|--------|----------|
| `editor-asset-browser-create-material-missing` G1 | P1 | `EditorRenderLayer.cpp::DrawAssetFileList` 末尾挂 `BeginPopupContextWindow` 弹 `Create → Material` 菜单；DrawAssetsPanel 末尾绘制主 modal（filename + template Combo + Path 预览 + Create/Cancel）+ overwrite 二级 modal；落盘走 `MaterialFileIO::WriteMaterialFile` + 自动切 `selectedAssetPath` 让 Material Inspector 子模式接管 |

**验收文档**：`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.1.1-acceptance-checklist.md`

**与引擎关系**：零引擎侧改动；全部改动落 `tools/OrangeEditor/EditorRenderLayer.cpp` 一个文件；不引入新公共 surface（仅 UI 入口，底层 API 全已存在）。CMake VERSION 1.1.0 → 1.1.1。

**Critical Path**：否（patch，friction 修复）

**不在本 patch 范围**（明示）：
- GAP G2 自动后缀编号路径——采用 overwrite 询问取代（与 Cocos / Unity 工业惯例一致）
- GAP G3 顺路 `Create → Scene` / `Create → Folder`——单独 v1.1.x 顺位或 v1.2 minor 一起做
- `GAP-2026-05-24-material-template-library-and-custom-hook`（自动扫描 + 用户自定义 template 入口）——独立 minor milestone

### v1.2.0 · 数据驱动 Shader Template Library ✅

**版本**：v1.1 DCC Import / v1.1.1 Create Material UI ✅ 后第一个 minor bump
**落地日期**：2026-05-24

**范围**：把 6 个 hardcode shader template（pbr / textured / toon / rim_light / dissolve / emissive）改为数据驱动 `.template.json` + 自动扫描注册，配套 Material Inspector 数据驱动 widget 重构，让"用户加新 shader 不写 C++ 自动出 widget"承诺落地。**关闭** [`GAP-2026-05-24-material-template-library-and-custom-hook`](engine-known-gaps.md) G1。

**Task 拆分**（T1-T5 跨 5 session 推进）：

| Task | 描述 | 影响模块 | 状态 |
|------|------|---------|------|
| T1 | G1 框架：`assets/shaders/templates/` + 6 `.template.json` schema v1.0 + `MaterialSystem::RegisterTemplatesFromDirectory` API + 启动期路径替换 | `src/render/MaterialSystem.{h,cpp}` + `tools/OrangeEditor/BuiltinAssets.cpp` | ✅ |
| T2 | Inspector UI 数据驱动重构：schema v1.0 → v1.1 加 `editor: {}` 元数据块 + 6 widget enum + components 模式 + `ShaderTemplateMetaIO` parser + `MaterialAssetInspectorPlugin` `RenderUniformWidget` 替换 pbr hardcode | `tools/OrangeEditor/ShaderTemplateMetaIO.{h,cpp}` + `tools/OrangeEditor/plugin/MaterialAssetInspectorPlugin.cpp` | ✅ |
| T3 | 第二批新 shader：实际 ship `unlit` 1 个；其余 5 个（skybox / sprite2d / particle_cpu / particle_trail / pbr_transparent）撞 Pipeline / OrangeRender 缺口登记 backlog | `src/render/builtin_shaders/unlit.frag.glsl` + `assets/shaders/templates/unlit.template.json` | ✅ |
| T4 | 第三批新 shader：实际 ship `water_basic` 1 个；剩 2 个（decal / planar_shadow）撞缺口登记 backlog | `src/render/builtin_shaders/water_basic.frag.glsl` + `assets/shaders/templates/water_basic.template.json` | ✅ |
| T5 | 收尾：CMake VERSION 1.1.1 → 1.2.0 + acceptance-checklist + ✅ | `tools/OrangeEditor/CMakeLists.txt` + docs + Wiki | ✅ |

**实际 ship 数**：8 个 template（6 现有迁出 + unlit + water_basic）vs 原 design 15 个 = 53%。差距 7 个全部因 Pipeline / OrangeRender 端能力缺失（非数据驱动框架缺陷），按 CLAUDE.md "跨仓纪律" 登记 backlog 见 [`engine-known-gaps.md` T3/T4 backlog 登记表](engine-known-gaps.md)。

**验收文档**：`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.2.0-acceptance-checklist.md`

**与引擎关系**：`src/render/MaterialSystem.{h,cpp}` 加 `RegisterTemplatesFromDirectory` 公共 API（向后兼容，不破已有 RegisterBuiltins）；`src/render/builtin_shaders/` 加 2 个 frag shader；其他全在 `tools/OrangeEditor/`。CMake VERSION 1.1.1 → 1.2.0。

**Critical Path**：是（"美术 / 关卡设计师不写代码完成日常工作" 承诺的关键拼图——任何 shader template 加 / 改 / 删现在不动 C++）

**不在本 minor 范围**（明示）：
- 7 个 backlog shader（skybox / sprite2d / particle_cpu / particle_trail / pbr_transparent / decal / planar_shadow）—— 撞 Pipeline / OrangeRender 缺口登记 [`engine-known-gaps.md`](engine-known-gaps.md)，按需触发
- macro 守卫联动（`USE_NORMAL_MAP` 等 conditional show）—— 本 minor 8 个 baseline 无 macro 字段，等真用到时再实现
- 材质球缩略图（GAP-2026-05-22 G2）—— 独立 v1.3.0 minor
- Editor 内 GLSL 文本编辑 + runtime SPV 编译（GAP-2026-05-24 G2）—— Phase 7+ / 待 Material UBO 基础设施 +1

### v1.x · 长尾（按需触发，不进 v1.0 critical path）

| 条目 | 依赖 |
|------|------|
| Hot reload（编辑器内改 shader / scene → 即时生效） | 主 roadmap Phase 8 |
| ACP 集成（lazy load + cache 模式） | 主 roadmap Phase 9 |
| C# 脚本组件可视化 | 主 roadmap Phase 7 |
| 地形 / 植被工具 | 主 roadmap Phase 13 |
| DCC 集成（Blender / Maya plugin） | 单独立项；v1.1 已落地基础 import 流水线（.obj / .gltf / .png/jpg/tga），本条 = 反向 export plugin |
| .fbx import | tinyobjloader / cgltf 之外接 OpenFBX 或 ufbx；v1.1 已铺好 4 件套路径，加 .fbx 仅是新增 importer 模块 |
| .meta Inspector 段编辑（normalmap green invert / scale / mipmap mode） | v1.1 .meta 已存参数 readonly，v1.2 加可写 GUI |
| BC7 / KTX2 texture 压缩 import 路径 | v1.1 走 RGBA8 + 运行时 mipmap；性能 milestone 触发时切 |
| mesh normal/tangent 自动补（mikktspace）| v1.1 信任 importer 提供；缺时偏暗，v1.2 加 |

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
- 已知限制 L1–L16 显式登记 + 标定消除点；不存在"心知肚明但没写下来"的悬置项（L15 / L16 是 v0.5 retro 期新增的隐性架构债，消除点分别 v0.8 / v0.7）
- v1.x 长尾全部带依赖；不在 critical path 上
- "Ori-like 视觉子模式"（Camera Cinematic / Parallax / Ambient Weather）登记为需求但**不主动开发**，等第一款游戏 fork 后真实用到时再升格
