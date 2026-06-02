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
    - **step 2 进行中**：drawable / picking / light / physics / gizmo **逐个**改读 `WorldTransformComponent`：
      - **drawable/mesh ✅ 2026-06-02**（RenderScene.cpp）：`Collect` 顶部跑 `PropagateWorldTransforms`，drawable 读 world cache（fallback local）。mesh parenting 生效；committed 场景父全在原点 → 零行为变化（ctest 76/76 全 render/light/shadow 测试零回归印证）。
      - **picking ✅ 2026-06-02**（EditorPicking.cpp）：ray-AABB 测 world cache（fallback local）→ parented mesh 按世界位置可选中。
      - **光源 consumer ✅ 2026-06-02**（全 3 类）：DirectionalLight 方向（Pipeline.cpp 两处）+ PointLight 位置 + SpotLight 位置/方向（PipelineShadow.cpp UpdatePointLightsUbo/UpdateSpotLightsUbo）全读 world matrix；位置/方向随父传播 + 阴影跟随；root 灯零回归（ctest 76/76）。
      - **杂项视觉 ✅ 2026-06-02**：PointLight halo + PostProcess local volume box（render 端）读 world（fallback local，root 零回归）。
      - **装饰 gizmo overlay 全切 ✅ 2026-06-02**：DirectionalLight 箭头 / PointLight 圆环 / SpotLight 锥体 / PostProcess volume box / ParticleEmitter spawn box / CameraFrustum（eye+forward+up）的 origin+方向读 world，fallback local，root 实体零回归。
      - **importer R-bridging ✅ 2026-06-02**：glTF 灯光方向编码从"world 光向直接写 local"改为 `nodeLocalRot × angleAxis(90°,+X)`（仅 -Z→-Y 桥接），匹配消费者读 world——root + 非 root〔含旋转父〕导入灯方向都对（旧法非 root 灯被父旋转二次应用得错向）。GltfSceneImportTest 第六组（旋转父下灯 → 世界光向 (-1,0,0)）锁住。
      - **gizmo（变换） / physics 待切 → 已出精确 spec**：`docs/A1-gizmo-physics-hierarchy-followup.md`（基于真实代码读后写：transform gizmo 的"position 当 world"假设 + 改点行号 + world→local 数学 + group/rotate/scale 类比 + physics 双向 + 零回归性质 + dogfood 计划 + 实施顺序）。两项都需 world→local on write/apply，且 **GUI/双向无法 headless 验证 + 是编辑器主操作工具/Play 核心，盲改风险高**，剥离到专门 session + dogfood。dogfood item 31「已知未切」。
      - **小结**：**所有「读」类位置/方向 consumer + 全部装饰 overlay + importer 灯光方向已切/修**（render mesh / picking / 3 类光源 + halo + postprocess volume + 6 个 gizmo overlay + glTF 灯 R-bridging），root 实体全零回归。**只剩 transform gizmo + physics 两个「写/apply」类**（world→local 方向，intricate + GUI/双向无法 headless 验证，盲改风险高，留专门 session + dogfood）。A1 ~92% 落地。
  - **A1.2 内容迁移**：committed 场景（pbr_showcase/demo）父全在原点 → **无需迁移**（累积==local）。**glTF scene import 已回退 world-bake → local TRS ✅ 2026-06-02**（end-to-end 测验 local+累积=正确 world）。
  - **A1.3 编辑器 reparent 保持世界位姿 ✅ 2026-06-02**（主 DnD reparent 路径）：`EditorHierarchy::*KeepWorld` 变体（捕获旧 world → 改链 → 重算 local = inverse(新父 world)×旧 world）；keep-world 自逆，命令 do/undo 都调它。`TestReparentKeepWorld`。剩余 reparent 站点（duplicate/clone）clone 已复制正确 local 不需。
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
  - **B1.0 ADR（大决策）✅ 形态已拍板**：玩法逻辑形态 = **脚本运行时，语言 = C#**（用户 2026-06-02 定，参 Unity）。**剩待 ADR 细化** = C# runtime 选型（.NET CoreCLR via hostfxr / Mono 嵌入 / NativeAOT-interop）+ C++↔C# 互操作层（component/system 绑定、marshaling、GC 与 ECS 生命周期）+ 玩法 assembly 热加载机制。📄 **设计提案地基已出**：`docs/pie-csharp-scripting-design.md`（扎在真实 PlayState/EnterPlay 代码 + Wiki scripting-system 页：runtime host 三方案对比〔推荐 CoreCLR + 可卸载 ALC 热重载〕 + binding 层 + ScriptComponent 模型 + 挂进现有 EnterPlay-snapshot 的生命周期 + 5 步分期 + 待裁定开放问题）。PIE 真启动时据此开 B1.0 ADR。
  - **B1.1 workspace / 项目模型**：编辑器能"打开外部游戏项目"（当前焊死自己仓 `assets/`）。**硬前置**。
  - **B1.2 游戏侧自定义 system / component 被编辑器发现**（与 schema-first / plugin-first 架构 + custom-component 扩展点对齐）。
  - **B1.3 Play/Pause/Stop 状态机 ⚠️ 部分已存在**：编辑器**已有** `PlayState`(Edit/Play/Paused) 状态机 + `EnterPlay` 的 World 快照落盘/Stop 还原（`EditorRenderLayer.cpp:1488` S2）+ Play tick 跑 physics（`:223`）——脚本生命周期挂进这套即可，不必重造。剩 = 进 Play clone 的 EntityGuid 稳定性（**依赖 A2**）+ 脚本 OnStart/OnUpdate/OnDestroy 接入。
  - **B1.4 输入/相机 editing vs play 模式切换**（编辑期 fly-cam+gizmo / play 期游戏相机+输入栈）。
  - **B1.5 运行时落地**：按 B1.0 选脚本运行时 OR dll 热加载。
  - **C# 脚本 SDK 骨架 ✅ 已落地 2026-06-02**：`csharp/OrangeScriptSDK/`——`OrangeScript` 基类（OnStart/OnUpdate/OnDestroy + Entity，对标 MonoBehaviour）+ 托管 `Entity` 句柄（uint64，不持裸指针）+ `Vec3`（blittable 同 glm 布局）+ `EngineInterop` P/Invoke 绑定声明 + net8.0 csproj + 示例。脚本侧 API 契约就位；C++ CLR host + 导出符号是 B1.0/B1.1（本机无 .NET SDK，未 dotnet build 编译验证，随 host spike 一起编）。
- **跨仓**：路线 (a) 脚本可能引擎侧；(b) dll 热加载属新架构方向。**dogfood**：核心（PIE 是交互闭环）。**验收**：编辑器摆挂游戏侧自定义 system 的场景 → Play → 视口内 system 真 tick → Stop → 还原。

### B2 · 动画时序编辑 ★重点

- **现状/缺口**：runtime 齐全（骨骼/程序化/状态机都真实现，见 [[reference_orangeengine_animation_state]]），但**创作侧几乎为零**——零时间轴/dopesheet/曲线编辑器/状态机图编辑器，动画退化成"start→end 线性"或 C++ lambda channel；**无 GPU skinning**（骨骼动画动不了 mesh，大概率跨 OrangeRender）；AnimatorComponent 还不能 Add-Component（`GAP-2026-06-01-animation-editor-integration-missing`）。
- **里程碑**（XL，多 session）：
  - 📄 **设计提案地基已出**：`docs/b2-animation-authoring-design.md`（读真实头文件后：现状盘点〔Procedural channel 是 lambda 非数据 / FSM 已有数据底座〕 + B2.1 clip/track/keyframe 数据结构 + 采样算法 + 与现有 animator 接通〔数据 channel 与 lambda 并存零改 runtime〕 + UI 线 + 实施顺序）。
  - **B2.1 动画数据模型 ✅ 已落地（数据模型 + animator 接入 + 编辑器数据原语）2026-06-02**：`AnimationClip`/`AnimationTrack`/`Keyframe` + `InterpMode`{Step/Linear/Bezier} + header-only `SampleTrack`（二分 + 插值 + clamp，`include/orange/engine/animation/AnimationClip.h`）；`ProceduralAnimator::AddDataChannel`（接现有 channel，runtime 零改）；**编辑器数据层原语齐备**：Sort/IsSorted（升序不变量）+ ComputeClipDuration/WrapClipTime（playback loop/clamp）+ AddKeyframeSorted（打键 C/U）+ Find/RemoveKeyframe（选/删）+ MoveKeyframeTime（dopesheet 拖）。headless 全测（AnimationClipTest 13 例 + ProceduralAnimator 数据 channel 例），ctest 77/77。
  - **B2.1b 运行时消费 + 序列化 ✅ 已落地 2026-06-02**（commit 提交信息里标 "B2.2"，指 B2.1 列出的"剩"项增量，**与下方 roadmap 编号 B2.2=GPU skinning 不同**，勿混淆）：
    - **`ClipAnimator`**（`include/orange/engine/animation/ClipAnimator.h` + `src/animation/ClipAnimator.cpp`，commit `0951a50`）：IAnimator 后端，把 AnimationClip 的 track 按 `targetName` 约定名（`position[.xyz]` / `rotation(.euler)` / `scale[.xyz]` / `scale.uniform`，`ParseTransformTarget` 解析）采样写进实体**本地** TransformComponent；只写 local，TransformSystem 的 PropagateWorldTransforms（ADR-016）照常累积父变换 → 子节点跟随（与 A1 正交）。播放控制 Play/Pause/Stop/Seek/loop/clamp + IsFinished + **Progress()〔进度 0..1〕 + SetSpeed()〔速率，含倒放〕 + CrossFadeTo()〔过渡混合：from-pose→新 clip，position/scale lerp + rotation slerp，idle↔walk↔attack 平滑切换〕**。`clip_animator_test` 17 例。
    - **动画事件**（commit `4e3afee`，面向首游 boss 战"攻击判定按时刻触发"硬需求）：`AnimationClip::events`{time,name} + `ClipAnimator::SetEventCallback`；正向播放越过 event.time 触发（半开 (oldT,newT]；**仅正向**，倒放/Seek scrub/暂停不触发，Unity 同款；**loop 跨界**拆两段；advance>=dur 全触发一次）。
    - **`.anim` JSON 序列化**（`AnimationClipSerialization.{h,cpp}`，同 session commit）：走 Core::Serialization，schema `animation/Clip` **v1.1**（minor 1 加 events；旧 minor 0 文件向后兼容读为空）；enum 按字符串（未知 fail-soft）、数值按定长数组；`AnimationClipToJson`/`FromJson` + `Save`/`LoadAnimationClip`。`animation_clip_serialization_test` 10 例 round-trip。
    - **clip 编辑原语补充**：clip 级 track 管理（Find/Upsert/Remove/UpsertKeyframe）+ `RecomputeClipDuration`（编辑后刷新时长）+ `ProceduralAnimator::AddClipChannels`（同 clip 驱动 material uniform）。
    - **scene round-trip**（commit `a79cef3`）：AnimatorComponent 的 "clip" backend 例外——Save 嵌入 clipJson（形态 B），Load pass 2 旁路 AnimatorRegistry 重建 ClipAnimator + SetTarget 到 entity 自身 Transform。**这让 ClipAnimator 跨 Enter-Play 快照/Stop 还原稳健**（此前只持 backend 名、靠 factory 重建 → clip 数据 per-entity 无 factory 会悬空）。`scene_serialization_test::TestClipAnimatorRoundTrip`。
    - **编辑器可见 demo**（commit `ddc2e3c`）：`SeedDemoWorld` 加 "Animated Cube (clip)"（bob position.y + spin rotation.euler，2s loop）；视觉待 dogfood（[dogfood-checklist](dogfood-checklist.md) item 33，含 demo.scene.json 需挪开触发 SeedDemoWorld 的指引）。ctest 79/79（含 editor_build_smoke）。
    - **`.anim` 接 AssetRegistry ✅**（`AnimationClipLoader : IAssetLoader<AnimationClip>`，animation 模块；AnimationClip 直接当 asset 无 wrapper）：`AssetRegistry::Load<AnimationClip>(path)` → loader → `LoadAnimationClip`。`animation_clip_loader_test` 3 例（注册/Load/Get + dedup + 缺文件 Err）。
    - **剩**：timeline/dopesheet/曲线 UI（B2.3/B2.4）+ AnimatorComponent Add-Component/Inspector 引用 `.anim` asset + 创作 clip（B2.6）——纯编辑器 GUI 层，需 dogfood。
  - **B2.2 GPU skinning**：骨骼蒙皮上 GPU（**跨仓 OrangeRender**——bone matrix palette + vertex skinning shader；当前骨骼动画在 viewport 看不到形变）。**按 ADR-009 三段式**：OrangeRender session 落地 → umbrella bump → 引擎消费。
  - **B2.3 Timeline / dopesheet UI**（编辑器层 ImGui）——轨道 + 关键帧拖动 + scrub。**精确 spec 已出**：`docs/b2.3-timeline-dopesheet-spec.md`（接入点 `DrawAnimationPanel` 空壳 EditorRenderLayer.cpp:2966 + 数据原语↔交互映射表〔scrub→Seek/打键→UpsertKeyframe/拖 key→MoveKeyframeTime/...〕全是已落地原语的可视化壳 + 3 设计点〔clip 可变访问走 copy-SetClip 命令栈 / 编辑期单 animator tick 预览 / .anim 写回〕 + 实施顺序 + dogfood）。数据层零缺口，纯 UI 壳。
  - **B2.4 曲线编辑器**（curve editor，缓动/Bezier handle）。
  - **B2.5 状态机图编辑器 ⚠️ 数据底座已就绪**：AnimationStateMachine **已有数据驱动 `.anim_fsm`**（`ConditionExpr{param,op,threshold}` 可序列化，ADR-005 v0.7）+ `AnimFsmAssetInspectorPlugin` 列表式编辑——**只缺节点图可视化 UI**（状态=节点/transition=边），不重做数据层。**精确 spec 已出**：`docs/b2.5-state-machine-graph-spec.md`（数据↔图映射 + 唯一真缺口=节点坐标存储〔.anim_fsm 加 editorPos minor bump〕 + 复用现有 AnimFsmCommands/popup id；接入 AnimFsmAssetInspectorPlugin 或 DrawAnimationPanel 子模式）。
  - **B2.6 AnimatorComponent Add-Component + clip 引用 + channel 可视化创作**（当前 channel 只能 C++ lambda；接 schema 注册 + UI 选 target/channel）。**精确 spec 已出**：`docs/b2.6-animator-clip-authoring-spec.md`（基于真实 schema 代码：AnimatorComponent "clip" backend 可 Addable〔Renderable c10 自定义 add 路径〕+ `AssetKind::AnimationClip` + FieldAssetRef 拖 .anim + ClipAnimator 记来源路径 + Inspector 播放控制/编辑期预览 tick；改点 file:line + 零回归 + dogfood 计划 + 实施顺序）。下个编辑器 session 照此执行。
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
