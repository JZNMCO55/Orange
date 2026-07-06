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
      - **gizmo（变换） / physics 待切 → 已出精确 spec**：`docs/A1-gizmo-physics-hierarchy-followup.md`（基于真实代码读后写：transform gizmo 的"position 当 world"假设 + 改点行号 + world→local 数学 + group/rotate/scale 类比 + physics 双向 + 零回归性质 + dogfood 计划 + 实施顺序）。两项都需 world→local on write/apply，且 **GUI/双向无法 headless 验证 + 是编辑器主操作工具/Play 核心，盲改风险高**，剥离到专门 session + dogfood。dogfood item 31「已知未切」。**world→local 引擎层数学已落地（2026-06-02）**：`Scene::DecomposeToLocalTransform`/`ComposeLocalMatrix`（`scene/TransformMath.h`，header-only 纯数学，`transform_math_test` 5 例含 identity 父零回归）——gizmo session 直接调它写回 local，无需编辑器侧重写矩阵分解；剩纯 GUI 交互。
      - **小结**：**所有「读」类位置/方向 consumer + 全部装饰 overlay + importer 灯光方向已切/修**（render mesh / picking / 3 类光源 + halo + postprocess volume + 6 个 gizmo overlay + glTF 灯 R-bridging），root 实体全零回归。**只剩 transform gizmo + physics 两个「写/apply」类**（world→local 方向，intricate + GUI/双向无法 headless 验证，盲改风险高，留专门 session + dogfood）。A1 ~92% 落地。
  - **A1.2 内容迁移**：committed 场景（pbr_showcase/demo）父全在原点 → **无需迁移**（累积==local）。**glTF scene import 已回退 world-bake → local TRS ✅ 2026-06-02**（end-to-end 测验 local+累积=正确 world）。
  - **A1.3 编辑器 reparent 保持世界位姿 ✅ 2026-06-02**（主 DnD reparent 路径）：`EditorHierarchy::*KeepWorld` 变体（捕获旧 world → 改链 → 重算 local = inverse(新父 world)×旧 world）；keep-world 自逆，命令 do/undo 都调它。`TestReparentKeepWorld`。剩余 reparent 站点（duplicate/clone）clone 已复制正确 local 不需。
- **跨仓**：否（引擎 Scene/Render 交界）。**headless 可测**：是（world matrix 数值 + 嵌套累积）。**dogfood**：移动父节点子节点跟随。
- **风险**：会和本 session 的 world-bake scene import 设计交互（A1.2 要回退它）；改 drawable 收集是热路径，注意性能。

### A2 · Stable EntityGUID 消费闭环

- **现状/缺口**：EntityGuid core 已落地（ADR-013，见 [[project_entityguid_core_landed]]），但 persistentId 在序列化里仍主要是 remap key；prefab 链接 / 跨会话身份 / re-import override 都需要把 EntityGuid 当**稳定身份**贯通。
- **里程碑**：A2.1 scene 序列化以 EntityGuid 为持久身份（而非顺序 remap key）✅；A2.2 prefab 实例链接用 EntityGuid 锚定 ✅；A2.3 PIE world-clone 用 EntityGuid 保稳定引用 ✅。
- **设计提案已出 📄 2026-06-02**：`docs/a2-entity-guid-stable-identity-design.md`（扎在真实序列化代码：现状=顺序 int 主键〔SaveImpl ~193〕+ parent/sibling 用顺序 int 互链〔WriteHierarchy ~173〕+ guid 仅可选 component 不当主键〔WriteGuid ~293〕+ guid 零散非普遍〔Save 是 const World& 不自动补〕；断裂面=re-save id 漂移/跨文件锚定/跨会话引用/re-import override；**推荐选项 B**=guid 当主键 + 顺序 int 降级本地序号 + 双键过渡〔向后兼容、字节稳定保留、schema 1.12→1.13 minor〕；**headless 安全先行 slice** S1 Save 前普遍补 guid / S2 FindEntityByGuid 索引 / S3 不变性测试；待裁定 5 问留 A2.0 ADR；实施顺序）。**✅ 2026-06-03 落地**：**A2.0 = ADR-018 accepted**（用户拍板选项 B + 5 问保守默认；schema 实际 1.15→1.16 非提案的 1.13——ScriptComponent 已推到 1.15）。**S1+S3+A2.1 主键迁移已实现**（headless）：S1 `SaveOptions.ensureGuids`（非 const `Save(World&)` 重载先 EnsureEntityGuids，const 重载并存零回归）；S2 FindEntityByGuid 早落（b2cad7b）；S3 scene 级不变性测试；A2.1 = WriteHierarchy 增写 `parent/firstChild/nextSibling/prevSiblingGuid`（保留 int 本地序号）+ ReadHierarchy `ResolveEntityRef` 优先 guid 回退 int + Load 期从 JSON 建 guid→entity 索引（避免给 Core 加 std::hash<Guid>）+ scene/world 1.16。9 新测试（S1×3/S3×1/A2.1×5：新写 guid/旧纯 int 1.15 回退/混合/字节稳定/guid 胜过故意写错 int）；**全量 ctest 88/88 零回归**（含所有现存 scene/prefab/gltf round-trip = 向后兼容验证）。**A2.2 ✅ 2026-06-03**（prefab 实例↔模板 guid 锚定）：`PrefabInstanceComponent` 加 `templateEntityGuid`；`InstantiatePrefab` **在 ReassignEntityGuids 之前**捕获每个 created 实体的模板 guid（此刻 blob 保真）、按 index 配对填入；序列化 additive（graceful 旧数据空 guid）；scene schema 1.16→1.17。测试验**实例↔模板经 FindEntityByGuid 双向锚定**（C1 override 地基）+ round-trip + 向后兼容；ctest 88/88 零回归。**A2.3 ✅ 2026-06-11**（PIE world-clone guid 稳定）：**调研判定为情况 A——经 A2.1 序列化路径已事实满足，无需修复，交付 = headless 测试锁住不变量**。机制确证：编辑器 EnterPlay（`EditorRenderLayer.cpp` ~1568）进 Play 把 editing world 经 **`Scene::Save(World&, path)` 非 const 入口**（`SaveOptions.ensureGuids` 默认 true）落盘成磁盘快照；Stop（~1713）经 `Scene::Load(path, newWorld)` 还原成全新 World 替换 editing world。这是**序列化往返**而非内存 clone，且非 const Save 入口会 `EnsureEntityGuids` 普遍补全 guid + A2.1 让 Hierarchy 用 guid 主键锚定——故进 Play 的 runtime world 实体 guid 与 editing world **相同（稳定，无重分配/漂移）**，**不**走 `SeparateClonedIdentities`/`ReassignEntityGuids` 这种"分离克隆体身份"的换新路径（那只用于 Duplicate/Paste/prefab 实例化）。新测试 `scene_play_snapshot_guid_test`（`tests/scene/PlaySnapshotGuidTest.cpp`，2 用例）精确复刻 EnterPlay→Stop 的 Save→Load 路径锁住：① 从零搭、**预先无 GuidComponent** 的实体经快照往返后，原 editing guid 在还原 world 用 `FindEntityByGuid` 命中同一逻辑实体（且命中实体 Transform/Name/父子层级一致）；② 反复进出 Play（含 Play 期改 Transform 后被快照还原丢弃）guid 不漂移（EnsureEntityGuids 幂等保已有 guid 不换新）。**全量 ctest 94/94 零回归**。**A2 epic 至此全闭环**（A2.0 ADR-018 + S1/S2/S3 + A2.1/A2.2/A2.3）。
- **跨仓**：否。**headless 可测**：是。**前置**：C1 prefab override + B1 PIE world clone 都依赖它。

---

## Phase B · 两大重点 epic（用户重点，可并行两轨）

### B1 · Play-in-Editor / 玩法闭环 ★重点

- **现状/缺口**：无脚本运行时 / 无 C++ 模块热加载 / 无 PIE / 无 workspace 项目模型——编辑器只 tick 引擎内置子系统，加载不了游戏玩法代码（`GAP-2026-05-27-play-in-editor`）。这是 OrangeEditor 仍是"数据编辑器"而非"游戏制作环境"的根本原因。
- **里程碑**（XL，多 session）：
  - **★ 总路线修订 ✅ 2026-07-06 = [ADR-021](../../Orange-Wiki/case-studies/orange-engine/decisions/ADR-021-pie-gamemodule-dual-language-hosting.md) accepted（PIE 双语言玩法宿主）**：2026-06-02 的"脚本运行时 / C# 单轨"拍板修订为 **IGameModule 统一生命周期接口 + 双宿主**——C++ = 编辑器 lib 化 + per-game editor 静态链入起步（DLL 热加载留 M7）；C# = 下述 B1.0~B1.3 已落地的 ScriptSystem 收编为同一接口第二实现（ADR-017 不废）。触发 = 首游实现语言 2026-07-02 拍板 C++、OG spike-01 需进编辑器。三仓实况探查修正：Play 状态机/快照/guid **已全量存在**（B1.3 所述"部分已存在"实为全量，只差挂接）；新增硬阻塞 = **编辑器离屏路径不执行 InsertPass**（`Pipeline.h:131` S1 契约，史莱姆 SDF 进视口会消失，列 M1 先行）。**执行计划 = `docs/pie-gamemodule-roadmap.md`（M0-M10）**，本节各子项落点见该文档 §6 对照表。
  - **B1.0 ADR（大决策）✅ 形态已拍板**：玩法逻辑形态 = **脚本运行时，语言 = C#**（用户 2026-06-02 定，参 Unity）。**剩待 ADR 细化** = C# runtime 选型（.NET CoreCLR via hostfxr / Mono 嵌入 / NativeAOT-interop）+ C++↔C# 互操作层（component/system 绑定、marshaling、GC 与 ECS 生命周期）+ 玩法 assembly 热加载机制。📄 **设计提案地基已出**：`docs/pie-csharp-scripting-design.md`（扎在真实 PlayState/EnterPlay 代码 + Wiki scripting-system 页：runtime host 三方案对比〔推荐 CoreCLR + 可卸载 ALC 热重载〕 + binding 层 + ScriptComponent 模型 + 挂进现有 EnterPlay-snapshot 的生命周期 + 5 步分期 + 待裁定开放问题）。PIE 真启动时据此开 B1.0 ADR。**✅ 2026-06-03 更新**：B1.0 ADR 已落定 = **ADR-017 accepted**（CoreCLR via nethost/hostfxr + 可卸载 ALC；host 层经 CoreCLR vs Mono 详比确认 CoreCLR；**热重载 MVP 先 de-scope**〔Enter Play 加载一次，改脚本 Stop→重编→再 Play，绕开 ALC 卸载陷阱，完整热重载留 B1.4〕；**绑定走集中登记表**）。用户装好 .NET 9 SDK，`csharp/OrangeScriptSDK/` 编译验证通过。**B1.0 host spike 已落地**：`include/orange/engine/script/ScriptHost.h`（公共门面零 CLR 类型）+ `src/script/dotnet/ScriptHost.cpp`（唯一 CLR hosting 消费区，新 header-isolation invariant）+ 托管 probe fixture + `dotnet_host_test`（端到端：init runtime→加载 assembly→取 `[UnmanagedCallersOnly]` 函数指针→`Run(41)==42` + 错误路径不崩；CoreCLR 进程单次启动约束已编码进测试顺序）。`ORANGE_ENGINE_WITH_DOTNET` CMake option 默认 OFF（现有构建零影响）；ctest 82/82；本地未 push。**B1.1 binding 最小集 + 生命周期 ✅ 2026-06-03**：C# 脚本经函数指针表（Pattern A，非 DllImport-against-exe）真操作引擎实体——`ScriptBindings`（Entity get/set position + IsValid + Input axis，操作"当前脚本 World"，entity id +1 codec）+ `ScriptRuntime`（建在 ScriptHost 上：Bootstrap 推绑定表 / 加载 game assembly + Activator 实例化 OrangeScript 子类 + GCHandle 保活 + OnStart/OnUpdate/OnDestroy 回调 + 异常边界 catch）+ `EngineInterop` 改函数指针表；`script_runtime_test` 端到端（Mover 脚本 OnUpdate 改 TransformComponent.position，C++ 断言真变）；ctest 83/83。**B1.2 ScriptComponent + ScriptSystem headless ✅ 2026-06-03**：`ScriptComponent`（纯数据 assemblyPath+typeName，序列化进 scene；scene/world schema bump + 组件自带 `component/Script` 1.0 解耦 schema；含脚本场景在无 dotnet 构建也 round-trip）+ `ScriptSystem`（建在 ScriptRuntime 上：StartWorld 批量实例化+OnStart / Tick OnUpdate / StopWorld OnDestroy+Release；entity→handle map + 析构兜底 + 失败跳过 + runtime-outlives-system 契约）；`script_system_test` 端到端（Mover 经系统驱动改 TransformComponent）+ 序列化 round-trip + 向后兼容测试；ctest 84/84。**剩（dogfood / editor-gated，自主到此为止）**：EnterPlay S4 实际调 ScriptSystem 的编辑器接线（**dogfood**：Play 看实体动、Stop 还原）/ ScriptComponent Add-Component + Inspector tweakable（C# metadata 反射）/ workspace 项目模型（开外部游戏项目）/ B1.4 完整热重载（collectible ALC）。
  - **B1.1 workspace / 项目模型**：编辑器能"打开外部游戏项目"（当前焊死自己仓 `assets/`）。~~**硬前置**~~ → **2026-07-06 降级**（ADR-021）：per-game editor 编译期即知项目根，M3 以 `EditorAppConfig` 参数注入起步；完整 `.orangeproject` 项目模型 = roadmap M6（ADR-022 届时拍）。
  - **B1.2 游戏侧自定义 system / component 被编辑器发现**（与 schema-first / plugin-first 架构 + custom-component 扩展点对齐）。
  - **B1.3 Play/Pause/Stop 状态机 ⚠️ 部分已存在**：编辑器**已有** `PlayState`(Edit/Play/Paused) 状态机 + `EnterPlay` 的 World 快照落盘/Stop 还原（`EditorRenderLayer.cpp:1488` S2）+ Play tick 跑 physics（`:223`）——脚本生命周期挂进这套即可，不必重造。剩 = 进 Play clone 的 EntityGuid 稳定性（**依赖 A2**）+ 脚本 OnStart/OnUpdate/OnDestroy 接入。
  - **B1.4 输入/相机 editing vs play 模式切换**（编辑期 fly-cam+gizmo / play 期游戏相机+输入栈）。
  - **B1.5 运行时落地**：~~按 B1.0 选脚本运行时 OR dll 热加载~~ → **2026-07-06 具体化**（ADR-021）：双宿主 = roadmap M2（IGameModule 接口 + Play 挂接）→ M3（editor lib 化）→ M4（OG SlimeGameModule 首个 C++ 消费者）→ M5（C# ScriptSystem 收编接线）；dll 热加载 = M7；发布 runtime = M10。
  - **C# 脚本 SDK 骨架 ✅ 已落地 2026-06-02**：`csharp/OrangeScriptSDK/`——`OrangeScript` 基类（OnStart/OnUpdate/OnDestroy + Entity，对标 MonoBehaviour）+ 托管 `Entity` 句柄（uint64，不持裸指针）+ `Vec3`（blittable 同 glm 布局）+ `EngineInterop` P/Invoke 绑定声明 + net8.0 csproj + 示例。脚本侧 API 契约就位；C++ CLR host + 导出符号是 B1.0/B1.1（本机无 .NET SDK，未 dotnet build 编译验证，随 host spike 一起编）。
- **跨仓**：路线 (a) 脚本可能引擎侧；(b) dll 热加载属新架构方向。**dogfood**：核心（PIE 是交互闭环）。**验收**：编辑器摆挂游戏侧自定义 system 的场景 → Play → 视口内 system 真 tick → Stop → 还原。

### B2 · 动画时序编辑 ★重点

- **现状/缺口**：runtime 齐全（骨骼/程序化/状态机都真实现，见 [[reference_orangeengine_animation_state]]），但**创作侧几乎为零**——零时间轴/dopesheet/曲线编辑器/状态机图编辑器，动画退化成"start→end 线性"或 C++ lambda channel；**无 GPU skinning**（骨骼动画动不了 mesh，大概率跨 OrangeRender）；AnimatorComponent 还不能 Add-Component（`GAP-2026-06-01-animation-editor-integration-missing`）。
- **里程碑**（XL，多 session）：
  - 📄 **设计提案地基已出**：`docs/b2-animation-authoring-design.md`（读真实头文件后：现状盘点〔Procedural channel 是 lambda 非数据 / FSM 已有数据底座〕 + B2.1 clip/track/keyframe 数据结构 + 采样算法 + 与现有 animator 接通〔数据 channel 与 lambda 并存零改 runtime〕 + UI 线 + 实施顺序）。
  - **B2.1 动画数据模型 ✅ 已落地（数据模型 + animator 接入 + 编辑器数据原语）2026-06-02**：`AnimationClip`/`AnimationTrack`/`Keyframe` + `InterpMode`{Step/Linear/Bezier} + header-only `SampleTrack`（二分 + 插值 + clamp，`include/orange/engine/animation/AnimationClip.h`）；`ProceduralAnimator::AddDataChannel`（接现有 channel，runtime 零改）；**编辑器数据层原语齐备**：Sort/IsSorted（升序不变量）+ ComputeClipDuration/WrapClipTime（playback loop/clamp）+ AddKeyframeSorted（打键 C/U）+ Find/RemoveKeyframe（选/删）+ MoveKeyframeTime（dopesheet 拖）。headless 全测（AnimationClipTest 13 例 + ProceduralAnimator 数据 channel 例），ctest 77/77。
  - **B2.1b 运行时消费 + 序列化 ✅ 已落地 2026-06-02**（commit 提交信息里标 "B2.2"，指 B2.1 列出的"剩"项增量，**与下方 roadmap 编号 B2.2=GPU skinning 不同**，勿混淆）：
    - **`ClipAnimator`**（`include/orange/engine/animation/ClipAnimator.h` + `src/animation/ClipAnimator.cpp`，commit `0951a50`）：IAnimator 后端，把 AnimationClip 的 track 按 `targetName` 约定名（`position[.xyz]` / `rotation(.euler)` / `scale[.xyz]` / `scale.uniform`，`ParseTransformTarget` 解析）采样写进实体**本地** TransformComponent；只写 local，TransformSystem 的 PropagateWorldTransforms（ADR-016）照常累积父变换 → 子节点跟随（与 A1 正交）。播放控制 Play/Pause/Stop/Seek/loop/clamp + IsFinished + **Progress()〔进度 0..1〕 + SetSpeed()〔速率，含倒放〕 + CrossFadeTo()〔**真两-clip cross-fade**（2026-06-02 从冻结-pose MVP 升级）：出场 clip 在 fade 期间按同一 speed 继续推进、逐帧实时采样（run cycle 腿摆在淡入 jump 时仍动），叠加冻结基线供未驱动字段；position/scale lerp + rotation slerp，idle↔walk↔attack 平滑切换〕**。`clip_animator_test` 19 例（含真两-clip 推进 + 未驱动字段基线）。
    - **动画事件**（commit `4e3afee`，面向首游 boss 战"攻击判定按时刻触发"硬需求）：`AnimationClip::events`{time,name} + `ClipAnimator::SetEventCallback`；正向播放越过 event.time 触发（半开 (oldT,newT]；**仅正向**，倒放/Seek scrub/暂停不触发，Unity 同款；**loop 跨界**拆两段；advance>=dur 全触发一次）。
    - **`.anim` JSON 序列化**（`AnimationClipSerialization.{h,cpp}`，同 session commit）：走 Core::Serialization，schema `animation/Clip` **v1.1**（minor 1 加 events；旧 minor 0 文件向后兼容读为空）；enum 按字符串（未知 fail-soft）、数值按定长数组；`AnimationClipToJson`/`FromJson` + `Save`/`LoadAnimationClip`。`animation_clip_serialization_test` 10 例 round-trip。
    - **clip 编辑原语补充**：clip 级 track 管理（Find/Upsert/Remove/UpsertKeyframe）+ `RecomputeClipDuration`（编辑后刷新时长）+ `ProceduralAnimator::AddClipChannels`（同 clip 驱动 material uniform）。
    - **scene round-trip**（commit `a79cef3`）：AnimatorComponent 的 "clip" backend 例外——Save 嵌入 clipJson（形态 B），Load pass 2 旁路 AnimatorRegistry 重建 ClipAnimator + SetTarget 到 entity 自身 Transform。**这让 ClipAnimator 跨 Enter-Play 快照/Stop 还原稳健**（此前只持 backend 名、靠 factory 重建 → clip 数据 per-entity 无 factory 会悬空）。`scene_serialization_test::TestClipAnimatorRoundTrip`。
    - **编辑器可见 demo**（commit `ddc2e3c`）：`SeedDemoWorld` 加 "Animated Cube (clip)"（bob position.y + spin rotation.euler，2s loop）；视觉待 dogfood（[dogfood-checklist](dogfood-checklist.md) item 33，含 demo.scene.json 需挪开触发 SeedDemoWorld 的指引）。ctest 79/79（含 editor_build_smoke）。
    - **`.anim` 接 AssetRegistry ✅**（`AnimationClipLoader : IAssetLoader<AnimationClip>`，animation 模块；AnimationClip 直接当 asset 无 wrapper）：`AssetRegistry::Load<AnimationClip>(path)` → loader → `LoadAnimationClip`。`animation_clip_loader_test` 3 例（注册/Load/Get + dedup + 缺文件 Err）。
    - **✅ 动画 clip 创作 GUI 三件套已实现（dogfood-pending）2026-06-03**：**B2.6**（OE `8e43084`）AnimatorComponent "clip" 可 Add-Component（c10 路径）+ Inspector FieldAssetRef 引用 `.anim` + loop + Play/Pause/scrub（plugin）+ **编辑期预览 tick 新机制**（EditorAnimationPreviewState 子 context，Edit 模式只 tick 被预览 animator，与 PlayState::Play 互斥）；**B2.3**（OE `98d543c`）timeline/dopesheet（ImDrawList 自绘轨道/菱形 key/playhead/事件 marker + hit-test，scrub→Seek/打键/拖键/删键/事件，**SetAnimationClipCommand** copy-modify-SetClip 整快照 + merge，资产化 clip「Save to .anim」写回）；**B2.4**（OE `8070ac8`）曲线编辑器（Dopesheet↔Curve 切换，**SampleTrack 画曲线保 display==playback**，Bezier in/out 手柄拖拽改缓动 + 右键切 InterpMode）。三件全 schema-first/无 hardcode，数据层 headless 全锁（ClipAnimatorAssetReassign/TimelineEditPrimitives/CurveEditorPrimitives），ctest 87/87。**所有 ImGui 交互 dogfood-pending**（dogfood-checklist item 37-48），headless 测不到，待真机一次性验。剩 **B2.5 状态机图编辑器**（独立编辑器，编 `.anim_fsm` 非 clip）+ **B2.2 GPU skinning**（跨 OrangeRender）。
  - **B2.2 GPU skinning**：骨骼蒙皮上 GPU（**跨仓 OrangeRender**——bone matrix palette + vertex skinning shader；当前骨骼动画在 viewport 看不到形变）。**按 ADR-009 三段式**：OrangeRender session 落地 → umbrella bump → 引擎消费。
  - **B2.3 Timeline / dopesheet UI**（编辑器层 ImGui）——轨道 + 关键帧拖动 + scrub。**精确 spec 已出**：`docs/b2.3-timeline-dopesheet-spec.md`（接入点 `DrawAnimationPanel` 空壳 EditorRenderLayer.cpp:2966 + 数据原语↔交互映射表〔scrub→Seek/打键→UpsertKeyframe/拖 key→MoveKeyframeTime/...〕全是已落地原语的可视化壳 + 3 设计点〔clip 可变访问走 copy-SetClip 命令栈 / 编辑期单 animator tick 预览 / .anim 写回〕 + 实施顺序 + dogfood）。数据层零缺口，纯 UI 壳。
  - **B2.4 曲线编辑器**（curve editor，缓动/Bezier handle）。**runtime 缓动模型已落地 ✅ 2026-06-02**：`InterpMode::Bezier` 从只抬升值的 MVP 升级为真 CSS cubic-bezier 时序缓动（`CubicBezierEase`，AnimationClip.h header-only：把相邻两帧过渡看成单位三次 Bezier，outTangent/inTangent 作 2D 控制柄，Newton+bisection 反解时间 → 支持 ease-in/out/in-out 非线性时序 + 值方向 overshoot 回弹）。切线字段/序列化不变（已 round-trip），无 .anim 用 Bezier → 零数据风险；`animation_clip_test` 加 6 例时序覆盖。**剩纯 GUI**：曲线编辑器画布 + 拖控制柄（消费已就绪的 CubicBezierEase + 切线数据）。与 B2.1（数据/采样先于 timeline GUI）同款 runtime-first 节奏。
  - **B2.5 状态机图编辑器 ✅（2026-06-12 核实：UI 早已落地）**：节点图 UI **早在 v0.7（c2-3~c2-8）完整实现并 commit**——节点画布（ImDrawList 自绘矩形节点 + transition 边 + 箭头 + 自循环 + initial state 高亮）/ 拖节点改 layout（`AnimFsmMoveStateCommand`）/ 右键空白建 state / 右键节点删改设初始 / 拖边建 transition / condition 列表编辑全在；`layout` 坐标存储是 `.anim_fsm` schema **v1.1 既有 shipped 字段**（非"待补"）。spec `docs/b2.5-state-machine-graph-spec.md` 写"只缺节点图 UI / 唯一缺口节点坐标存储"**已严重过时**（待 doc session 更新）。**2026-06-12 补 `anim_fsm_file_io_test`** 守护此前无 headless 测的 IO 层（全量 round-trip + layout 对称 + 向后兼容 + 错误路径，schema 不 bump）。**剩纯 GUI dogfood**（节点拖拽/建删/拖边交互真机验）+ 可选小增量（transition 边上 condition label / Play active state 高亮）。
  - **B2.6 AnimatorComponent Add-Component + clip 引用 + channel 可视化创作**（当前 channel 只能 C++ lambda；接 schema 注册 + UI 选 target/channel）。**精确 spec 已出**：`docs/b2.6-animator-clip-authoring-spec.md`（基于真实 schema 代码：AnimatorComponent "clip" backend 可 Addable〔Renderable c10 自定义 add 路径〕+ `AssetKind::AnimationClip` + FieldAssetRef 拖 .anim + ClipAnimator 记来源路径 + Inspector 播放控制/编辑期预览 tick；改点 file:line + 零回归 + dogfood 计划 + 实施顺序）。下个编辑器 session 照此执行。
- **跨仓**：B2.2 是。其余编辑器/引擎单仓。**依赖**：A1 部分（骨骼是 transform hierarchy）。**dogfood**：核心（动画手感、timeline 交互全靠真机）。
- **建议起点**：B2.2 GPU skinning（让骨骼动画先"看得见"，是这条 epic 的视觉地基）+ B2.6（让 animator 至少能在编辑器挂上）。

---

## Phase C · 内容规模化 + 资产广度（可并行/机会主义）

### C1 · Prefab override / 嵌套 / 变体

- **现状**：prefab MVP 已落（能实例化，见 [[project_prefab_engine_mvp_landed]]），缺 override（实例改不回灌母体）/ 嵌套 prefab / apply-revert / 变体。成熟引擎靠这套批量构建内容。
- **里程碑**：C1.1 override（实例字段 delta，不回灌母体，蓝条标记）；C1.2 嵌套 prefab；C1.3 apply/revert；C1.4 prefab 变体。**依赖 A2** EntityGuid（A2.1+A2.2 已落 ✅）。规模 L。dogfood：编辑器交互。
- **设计提案已出 📄 2026-06-03**：`docs/c1-prefab-override-design.md`（现状=实例全 bake 撑不起 override；**存储模型选项 A〔全 bake + 派生 diff，零破坏 MVP〕vs B〔template+delta 自动传播但大重构〕，推荐 A 起步**；headless 安全先行件 CS1 实例↔模板 diff〔决策中立，A/B 都用得上，消费 A2.2 templateEntityGuid〕；6 问留 C1.0 ADR）。
- **CS1 ✅ 2026-06-03**（决策中立 headless 先行件，OE 待 commit）：`PrefabOverride.{h,cpp}`——`ComputeInstanceOverrides`/`ComputeEntityOverrides` field 级 diff（复用 serializer 注册表序列化单 entity + structured JSON leaf 比较 typed 防跨类型误判 + 过滤 Guid/PrefabInstance/Hierarchy 身份 component + 经 templateEntityGuid+FindEntityByGuid 配对，全失败 graceful）。`prefab_override_test` 6 例；ctest 89/89。已知局限：mesh/material asset-ref 退化对称无误报但不敏感（两侧传同 registry 即可，留 UI 期）。**CS2 ✅ 2026-06-03**（refresh-from-template，决策中立 headless）：`RefreshEntityFromTemplate`/`RefreshInstanceFromTemplate`——把实例未 override 字段从演进后模板重拉、override 字段保留。**agent 复核发现并修正为三方 merge**（base/theirs/mine）：纯"实例 vs 当前模板"二方 diff 分不清"用户真 override〔mine≠base 保留〕"vs"模板演进〔mine==base≠theirs 更新〕"——真 override 集 = CS1 的 `diff(mine,base)`；M=theirs 为底 + override 叶子用 mine 覆盖 → 写回实例（数组坑靠先 replay 模板 Write 解决）。单模板退化（base==theirs）值 no-op 安全。`prefab_refresh_test` 6 例（含 override 保留 + 模板演进字段更新的判别性 T1）；ctest 90/90。**C1.0 = ADR-019 accepted ✅ 2026-06-03**（用户拍板选项 A 全 bake + 派生 diff 起步、预留 B；持久化 overriddenPaths；§5 六问保守默认）。**overriddenPaths 持久化 ✅**：`PrefabInstanceComponent.overriddenPaths`（扁平 `"componentName/fieldPath"` 串）+ 序列化（scene 1.17→1.18）+ `RecordOverridePath`(dedup)/`IsPathOverridden`/`ClearOverridePath` helper + **`RefreshInstanceWithRecordedOverrides`**（用持久化 override 集当显式集，复用 CS2 merge 机器但更干净——单模板不需 base/三方）。`prefab_overridden_paths_test` 5 例；ctest 91/91。**C1 引擎层地基全齐**（CS1 diff + CS2 refresh + overriddenPaths 持久化）。**剩 C1.1 编辑器 UI（dogfood-gated）**：命令栈钩子（改实例字段自动 RecordOverridePath）+ Inspector 蓝条标记 override 字段（消费 IsPathOverridden）+ 右键 revert（ClearOverridePath + 单字段回模板）/ apply（推回模板 blob）/ "刷新实例"（消费 RefreshInstanceWithRecordedOverrides）。

### C2 · 资产格式广度

- **现状/缺口**：只有 OBJ/glTF/GLB；无 FBX/DAE/USD；无 EXR/KTX/DDS/BC7 压缩纹理；无离线 texture cook（mipmap/压缩）；无 file watcher 自动 reimport。
- **里程碑**（各自独立，ADR-008 4 件套路径，可并行）：
  - C2.1 **glTF scene import G2 ✅ 2026-06-03**（per-mesh PBR 材质）：单 material → `Renderable.materialInstance` / 多 material → `SubMeshMaterialsComponent`（各 sub-mesh 段独立材质 + slot 0 兜底），去 G1"合并单段走默认材质"workaround；全局按 `cgltf_material*` 去重；**headless 难点解法** = sentinel `MaterialInstance(nullptr)`（unique_ptr 拥有，Save 用完即随 scratch world 析构）+ `namedMaterialInstances` 反查写 `.material` 路径进 scene.json，Load 端 `materialResolver` lazy-create 真实 instance 对称；cgltf 生命周期严守（material 解析全在 cgltf_free 前）；`GltfSceneImportTest` +3 case；ctest 84/84。
  - C2.1 **glTF scene import G3 ✅ 2026-06-03**（cameras 补完，lights 早落）：`node.camera` → `Render::Camera` component——透视 `yfov/aspectRatio/znear/zfar` 烘成 `Camera::Perspective`、正交 `xmag/ymag`（视图半宽高）映 `left/right/bottom/top` 调 `Camera::Orthographic`；aspect/zfar 缺省取 16:9 / far 1000（glTF 可选字段语义 = 跟视口 / 无限远）。**朝向决策**：查证 `CameraFrustumGizmoPlugin` 明确"相机看本地 -Z 是 OpenGL/glTF/Vulkan 同款约定"，与 glTF 一致 → 相机**不做**灯光那种 -Z→-Y 桥接，view 留单位、位姿交 entity Transform。`cameras=N` 计数穿进 importer 汇总。`GltfSceneImportTest` +1 组（透视全参 / 缺省 aspect+far / 正交，Save→Load round-trip 后逐元素对位投影矩阵 = 工厂结果 + Transform 位姿）；ctest 92/92。**C2.1 glTF scene import 至此 node payload（mesh/light/camera）端到端完整**。**已知 MVP 限制**：`Render::Camera` 只存矩阵对、无参数化 fov 字段 → 烘焙导入，re-edit 按矩阵不按 fov（真要可重编需扩 Camera + Inspector，属后续）。
  - C2.2 **FBX importer ✅ 2026-06-03**（静态 mesh + 材质 MVP）：OpenFBX MIT vendored 到 `vendor/OpenFBX/`（ofbx + libdeflate，C/C++ 混编 per-TU /W0）；`FbxImporter`（tools/OrangeEditor/import/）对标 GltfImporter——塌平合并多 mesh + 多 material 拆 sub-mesh slot + MikkTSpace 切线 + ImportTexture co-locate + `.mesh`/`.material`/`.meta` + AssetRegistry；**坐标系转换**（Z-up→Y-up `(x,y,z)→(x,z,-y)`，信任已烘米单位）；ImportDispatcher 注册 `.fbx`。Blender headless 自生成 `cube_two_material.fbx` fixture（自有几何可 commit）+ `FbxImportTest`（轴正确性靠绿 sub-mesh 法线落 ±Y 锁住、多材质 sub-mesh、确定性）；ctest 88/88。**FBX scene-level 层级导入 ✅ 2026-06-03**（对标 glTF scene import）：`FbxSceneImporter`——遍历 FBX node 层级 → 每 node 建实体（Name/Transform/Hierarchy，mesh node 挂 Renderable + per-mesh 不塌平）→ Scene::Save；**node local transform 共轭轴转换 `M_engine = R·M_fbx·R⁻¹`**（基变换相似变换，非 R·M——R·M 会让带旋转/各向异缩放子节点歪；R 纯旋转逆=转置）；抽出共享 `FbxAxisConverter`/`FbxMaterialParse`（两 FBX importer 复用，仿 GltfMaterialParse）；`import-scene` CLI 按扩展名分派 .fbx/.gltf。Blender 生成层级 fixture `cube_hierarchy.fbx`（Child 相对父非平凡 local 验共轭）+ `FbxSceneImportTest`（实体树 + 共轭后 TRS 逐位验证 + 材质）；ctest 92/92。
  - C2.2 **FBX import-scale 参数 ✅ 2026-06-03**（解决"真 cm 文件偏大 100×"）：**调查实测**——FBX 单位是经典歧义（`UnitScaleFactor=1` 的文件，顶点既可能是米〔Blender 默认 apply_unit_scale，cube_two_material.fbx 实测 USF=1+米〕也可能是 cm〔Maya/Max〕，**单凭 UnitScaleFactor 无法区分**；盲目 ×USF/100 会把 Blender 默认导出全缩 1/100）。故采工业标准（Unity/Unreal/Godot 同款）：**显式 `importScale` 参数**，默认 1.0 = 信任已烘米（对 Blender 零破坏），真 cm 文件传 ≈0.01。贯通 `MakeAxisConverter`（顶点 + node 平移单一注入点）→ 两 FBX importer → `DispatchToRegistry` → CLI（`import-mesh`/`import-scene` 都接 `--scale <f>`）；`MakeAxisConverter` 日志打印文件 UnitScaleFactor 供判断。`FbxSceneImportTest` +1 节（importScale=0.5 验 node 平移整体 ×0.5）；ctest 92/92。**剩**：skinning/animation / node geometric matrix（**Blender 不导出 FBX geometric transform，headless 无法造 fixture → 需 Maya/Max 源文件才可测**）/ FBX camera（OpenFBX 支持但有"相机默认看 +X"约定坑，需桥接 + Blender 相机 fixture，下一件干净选项）。
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
