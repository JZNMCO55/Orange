# 引擎成熟度排期（vs 成熟引擎）

- 基准日期：2026-06-02
- 来源：`docs/editor-capability-gap-vs-mature.md` + `docs/editor-hierarchy-gap-vs-lumix.md` + `docs/engine-known-gaps.md` + 2026-06-02 成熟度复盘
- 用途：把"和 Unity / Unreal / Godot / Lumix 的结构性差距"排成依赖有序、可执行的阶段表。**这是建议排序**——Phase 6+ 本就可乱序/并行（见 `roadmap.md` 开头），实际节奏按拉动调整。

## 范围边界（用户 2026-06-02 拍板）

- ❌ **非目标**：大世界 / 开放世界流式加载；网络 / 多人 netcode。**永不排期**。
- ⏬ **垫底**：3D 物理（引擎定位 2D/2.5D，Box2D 已够；3D 留到最后，看是否真拉动）。
- ✅ **本次全部排期**，**重点 = Play-in-Editor + 动画时序编辑**。

## 已经成熟、不在本排期内（校准用）

避免把已达标的当差距：渲染真实感（PBR+IBL / PCSS 软阴影 / contact shadow / CSM / 后处理链 / 多光源）、编辑器数据编辑骨架（schema-first Inspector / 命令栈 coalesce / DCC import 4 件套 / dock 持久化 / 资产增删改查 / 多选群组变换 / gizmo snap+local-world / 缩略图 ThumbnailService / debug 渲染视图 5 mode）。差距**不在画面和数据编辑**，在下面的结构性大块。

---

## 排期总览（依赖排序）

| Phase | 能力 | 规模 | 跨仓 | ADR | 主 dogfood | 依赖 |
|---|---|---|---|---|---|---|
| **A 地基** | A1 Transform 层级传播 | L | 否 | 是 | viewport reparent | — |
| | A2 Stable EntityGUID 消费闭环 | M | 否 | ADR-013 续 | 序列化往返 | — |
| **B 重点 epic** | B1 **Play-in-Editor / 玩法闭环** | XL | 可能 | 是（大） | 核心 | A2 |
| | B2 **动画时序编辑** | XL | 是（GPU skinning） | 是 | 核心 | A1 部分 |
| **C 内容规模化** | C1 Prefab override / 嵌套 / 变体 | L | 否 | 是 | 编辑器交互 | A2 |
| | C2 资产格式广度（FBX/压缩纹理/file watcher + glTF G2/G3） | M each | 部分 | 否 | 导入视觉 | — |
| | C3 Shader 热重载 | L | 是 | 是 | shader 即时生效 | — |
| **D 长尾** | D1 3D 物理 | XL | 是 | 是 | — | — |
| | D2 节点式材质图 | XL | 是 | 是 | — | C3 |

> 规模口径：S=1 commit / M=单 session / L=多 session / XL=epic（多里程碑跨多 session）。

---

## Phase A · 引擎地基（建议先行）

> 这两条是后面所有 epic 的隐性前置。越晚做，越多内容/代码建立在"扁平 world transform"和"非稳定 id"的假设上，迁移成本越高。

### A1 · Transform 层级传播

- **现状/缺口**：`RenderScene::Collect` 对每个 entity 直接 `ComposeWorldMatrix(自身 local Transform)`，**不沿 HierarchyComponent 累积父变换**——parenting 对渲染位置无效（`GAP-2026-06-02-hierarchy-transform-not-propagated`）。与 Unity `localToWorld` / Godot `global_transform` / Lumix universe transform 传播相悖。
- **里程碑**：
  - **A1.0 ADR ✅**（2026-06-02 落地，**ADR-016**）：world matrix 计算策略选型 → **选方案 A：每帧 `TransformSystem` 自顶向下 DFS 重算并缓存 world matrix**（非 dirty-flag，避免隐蔽派生状态 bug；2.5D 中等规模全量重算成本可忽略）。dirty-flag 留未来 profiling 拉动。详见 `../Orange-Wiki/case-studies/orange-engine/decisions/ADR-016-transform-hierarchy-propagation.md`。
  - **A1.1 引擎核心**（分两步降风险）：
    - **step 1 ✅ 2026-06-02**：加 `TransformSystem::PropagateWorldTransforms` + `WorldTransformComponent`（派生 cache）——每帧从 hierarchy 自顶向下累积 world matrix。**additive、零消费者、零行为变化**（只产 cache，渲染/光源仍走旧路径）。headless `scene_transform_system_test`（3 层累积 / 旋转父真矩阵乘 / flat entity / 幂等）；ctest 76/76 零回归。
    - **step 2 进行中**：drawable / light / physics / gizmo **逐个**改读 `WorldTransformComponent`：
      - **drawable/mesh ✅ 2026-06-02**（RenderScene.cpp）：`Collect` 顶部跑 `PropagateWorldTransforms`，drawable 读 world cache（fallback local）。mesh parenting 生效；committed 场景父全在原点 → 零行为变化（ctest 76/76 全 render/light/shadow 测试零回归印证）。
      - **light 方向 / physics / gizmo 待切**：仍读 entity local；非原点父下灯/collider 不随父动（mesh 随）—— dogfood item 31「已知未切」。
  - **A1.2 内容迁移**：committed 场景（pbr_showcase/demo）父全在原点 → **无需迁移**（累积==local）。**glTF scene import 已回退 world-bake → local TRS ✅ 2026-06-02**（end-to-end 测验 local+累积=正确 world）。
  - **A1.3 编辑器 reparent 保持世界位姿** ★A1.1 mesh 切换后**变必需**：reparent mesh 到非原点父会跳位（local 被当相对父解释），需 keep-world 重算 local。**下一步开工**。
- **跨仓**：否（引擎 Scene/Render 交界）。**headless 可测**：是（world matrix 数值 + 嵌套累积）。**dogfood**：移动父节点子节点跟随。
- **风险**：会和本 session 的 world-bake scene import 设计交互（A1.2 要回退它）；改 drawable 收集是热路径，注意性能。

### A2 · Stable EntityGUID 消费闭环

- **现状/缺口**：EntityGuid core 已落地（ADR-013，见 [[project_entityguid_core_landed]]），但 persistentId 在序列化里仍主要是 remap key；prefab 链接 / 跨会话身份 / re-import override 都需要把 EntityGuid 当**稳定身份**贯通。
- **里程碑**：A2.1 scene 序列化以 EntityGuid 为持久身份（而非顺序 remap key）；A2.2 prefab 实例链接用 EntityGuid 锚定；A2.3 PIE world-clone 用 EntityGuid 保稳定引用。
- **跨仓**：否。**headless 可测**：是。**前置**：C1 prefab override + B1 PIE world clone 都依赖它。

---

## Phase B · 两大重点 epic（用户重点，可并行两轨）

### B1 · Play-in-Editor / 玩法闭环 ★重点

- **现状/缺口**：无脚本运行时 / 无 C++ 模块热加载 / 无 PIE / 无 workspace 项目模型——编辑器只 tick 引擎内置子系统，加载不了游戏玩法代码（`GAP-2026-05-27-play-in-editor`）。这是 OrangeEditor 仍是"数据编辑器"而非"游戏制作环境"的根本原因。
- **里程碑**（XL，多 session）：
  - **B1.0 ADR（大决策）✅ 形态已拍板**：玩法逻辑形态 = **脚本运行时，语言 = C#**（用户 2026-06-02 定，参 Unity）。**剩待 ADR 细化** = C# runtime 选型（.NET CoreCLR via hostfxr / Mono 嵌入 / NativeAOT-interop）+ C++↔C# 互操作层（component/system 绑定、marshaling、GC 与 ECS 生命周期）+ 玩法 assembly 热加载机制。PIE 真启动时开 B1.0 ADR 落这些。
  - **B1.1 workspace / 项目模型**：编辑器能"打开外部游戏项目"（当前焊死自己仓 `assets/`）。**硬前置**。
  - **B1.2 游戏侧自定义 system / component 被编辑器发现**（与 schema-first / plugin-first 架构 + custom-component 扩展点对齐）。
  - **B1.3 Play/Pause/Stop 状态机**：进 Play 时 clone editing world → runtime world（复用 scene 序列化深拷贝），退出还原编辑前状态。**依赖 A2**（EntityGuid 稳定 clone）。
  - **B1.4 输入/相机 editing vs play 模式切换**（编辑期 fly-cam+gizmo / play 期游戏相机+输入栈）。
  - **B1.5 运行时落地**：按 B1.0 选脚本运行时 OR dll 热加载。
- **跨仓**：路线 (a) 脚本可能引擎侧；(b) dll 热加载属新架构方向。**dogfood**：核心（PIE 是交互闭环）。**验收**：编辑器摆挂游戏侧自定义 system 的场景 → Play → 视口内 system 真 tick → Stop → 还原。

### B2 · 动画时序编辑 ★重点

- **现状/缺口**：runtime 齐全（骨骼/程序化/状态机都真实现，见 [[reference_orangeengine_animation_state]]），但**创作侧几乎为零**——零时间轴/dopesheet/曲线编辑器/状态机图编辑器，动画退化成"start→end 线性"或 C++ lambda channel；**无 GPU skinning**（骨骼动画动不了 mesh，大概率跨 OrangeRender）；AnimatorComponent 还不能 Add-Component（`GAP-2026-06-01-animation-editor-integration-missing`）。
- **里程碑**（XL，多 session）：
  - **B2.1 动画数据模型**：keyframe track + curve（引擎层；当前 runtime 无 keyframe 编辑数据结构）。schema-first，可序列化。
  - **B2.2 GPU skinning**：骨骼蒙皮上 GPU（**跨仓 OrangeRender**——bone matrix palette + vertex skinning shader；当前骨骼动画在 viewport 看不到形变）。**按 ADR-009 三段式**：OrangeRender session 落地 → umbrella bump → 引擎消费。
  - **B2.3 Timeline / dopesheet UI**（编辑器层 ImGui）——轨道 + 关键帧拖动 + scrub。
  - **B2.4 曲线编辑器**（curve editor，缓动/Bezier handle）。
  - **B2.5 状态机图编辑器**（AnimationStateMachine 已有 runtime，缺节点图 UI + 过渡条件可视化编辑）。
  - **B2.6 AnimatorComponent Add-Component + channel 可视化创作**（当前 channel 只能 C++ lambda；接 schema 注册 + UI 选 target/channel）。
- **跨仓**：B2.2 是。其余编辑器/引擎单仓。**依赖**：A1 部分（骨骼是 transform hierarchy）。**dogfood**：核心（动画手感、timeline 交互全靠真机）。
- **建议起点**：B2.2 GPU skinning（让骨骼动画先"看得见"，是这条 epic 的视觉地基）+ B2.6（让 animator 至少能在编辑器挂上）。

---

## Phase C · 内容规模化 + 资产广度（可并行/机会主义）

### C1 · Prefab override / 嵌套 / 变体

- **现状**：prefab MVP 已落（能实例化，见 [[project_prefab_engine_mvp_landed]]），缺 override（实例改不回灌母体）/ 嵌套 prefab / apply-revert / 变体。成熟引擎靠这套批量构建内容。
- **里程碑**：C1.1 override（实例字段 delta，不回灌母体，蓝条标记）；C1.2 嵌套 prefab；C1.3 apply/revert；C1.4 prefab 变体。**依赖 A2** EntityGuid。规模 L。dogfood：编辑器交互。

### C2 · 资产格式广度

- **现状/缺口**：只有 OBJ/glTF/GLB；无 FBX/DAE/USD；无 EXR/KTX/DDS/BC7 压缩纹理；无离线 texture cook（mipmap/压缩）；无 file watcher 自动 reimport。
- **里程碑**（各自独立，ADR-008 4 件套路径，可并行）：
  - C2.1 **glTF scene import G2/G3 补完**（per-mesh 材质 + cameras）——续本 session（G1 已落），最顺手的下一步。
  - C2.2 **FBX importer**（OpenFBX MIT vendor，hierarchy+multi-mesh+material slot）。
  - C2.3 压缩纹理 + 离线 cook（性能 milestone，部分跨仓）。
  - C2.4 file watcher 自动 reimport（平台文件监控）。
- 规模 M each。多数引擎/编辑器单仓（cook 部分跨仓）。

### C3 · Shader 热重载

- **现状/缺口**：无 shader 热重载 / runtime GLSL 编译（改 shader 要重编引擎）；template 库 8/15。
- **里程碑**：glslang vendor + runtime GLSL→SPV 编译 + Material UBO（**跨仓 OrangeRender**）。规模 L。dogfood：改 shader 即时生效。

---

## Phase D · 长尾（按拉动/最后）

### D1 · 3D 物理（用户排最后）

- 现 Box2D（2D）。3D 需新 physics backend（Jolt / PhysX / Bullet 选型）。**用户明确垫底**——引擎定位 2D/2.5D，3D 物理看首游后是否真拉动再评估。

### D2 · 节点式材质图（shader graph）

- "Phase 11+ 或永不"，数月级，不主动开工。依赖 C3。

---

## 已拍板（2026-06-02 用户决策）

1. **排序 = 地基优先**（用户"按你的节奏"→采纳推荐）：先做 **A1 Transform 层级传播**，再 A2 EntityGUID，然后并行开 B1/B2。理由：A1 越晚改迁移越贵 + 会回退本 session scene import 的 world-bake workaround；A1 纯逻辑+headless 可测，最适合自主推进。**→ 下一步 = A1.0 ADR + A1.1 实现。**
2. **B1 PIE 路线 = 脚本运行时，语言 = C#**（用户 2026-06-02 两步拍板）：① 走脚本路线（非 C++ dll 热加载）；② **脚本语言 = C#**（参 Unity；候选运行时如 .NET CoreCLR / Mono / hostfxr 嵌入 + C# 玩法程序集热加载，具体 runtime 选型留 B1.0 ADR）。架构方向定 = 嵌 C# 运行时 + 玩法 assembly 热加载，编辑器 PIE 内 tick 游戏侧 C# system/component。

> 维护：每个 epic 开工时在对应 `GAP-*` 条目记落地进度；标志变化（✅/规模/跨仓）回灌本表与 `engine-known-gaps.md`。dogfood 残留按 `docs/dogfood-checklist.md` 格式登记。
