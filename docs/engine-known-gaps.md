# OrangeEngine 已知能力缺口

本文件登记 **编辑器 / sample / 游戏侧推进过程中发现的、需要 OrangeEngine 自身新增能力或修复的具体缺口**。与 OrangeRender 仓的 `vendor/OrangeRender/docs/incoming_feature.md` 同节奏：

- 每条以 `## GAP-<日期>-<short slug>` 开头，便于 commit / PR title 引用
- 包含：发现方 / 发现日期 / 一句话定性 / 触发场景 / 缺什么 / 期望验收 / 状态
- **处理流程**：发现 gap 的 session 只做**登记**，**不**在同一 session 里同时改引擎；引擎补强是显式独立 session 处理（精神同 CLAUDE.md "绝对不允许在同一个 session 内既向 OrangeRender 提 feature、又在本仓库消费 / 处理该 feature" —— 把同一纪律下沉到 OrangeEngine ↔ OrangeEditor / Game 关系）
- 落地后搬到本文件末尾的"处理记录"段，CHANGELOG 写详细修复点

## 登记门槛（低摩擦优先）

历史教训：本文件长期只有 1 条登记，原因不是没缺口，是登记摩擦让大量"卡了 5 分钟自己绕过去了"的缺口丢失。**摩擦门槛刻意降低**：

- **任何一次** 编辑器 / sample / 游戏侧推进**因引擎能力缺失暂停超过 5 分钟**，立即登记一条 GAP（即使后续发现是误解，留个待评审条目也比丢失信息好）
- 登记不需要写完整的"缺什么 / 期望验收"——**起初可以只写发现方 + 一句话定性 + 触发场景**，剩余字段后续 session 处理时补
- **登记 ≠ 承诺要做**。GAP 落地节奏由独立 session 评审决定，可以延后 / 合并 / 拒绝。**登记本身是几乎零成本的信息保留**
- 误登记 / 撤回成本低：发现是误解时直接在原条目下加一句"撤回原因"即可，不必删

只有同时满足两条才升级为"真正的工程负担登记"：(1) 已经独立 session 评审过 + (2) 决定要做。在那之前，GAP 条目是松散的需求收件箱。

## 与现有文档的关系

| 文件 | 职责 |
|------|------|
| `docs/design-plan.md` | Phase 1–5.5 task 级历史 |
| `docs/roadmap.md` | Phase 6+ 长期路线（前瞻、按计划） |
| `docs/editor-roadmap.md` | 编辑器路线（前瞻、按计划） |
| `docs/engine-known-gaps.md`（本文件） | **撞上即补**的具体能力缺口（响应消费者发现） |

一条 gap 落地后视情况：
- 如纯属"漏排期"的小补丁（如 `HierarchyComponent` 缺 helper），直接 commit + CHANGELOG
- 如属于完整 Phase 范围（如 PointLight + 多 light 支持），写回 `docs/roadmap.md` 对应 Phase 作为新 Task

---

## GAP-2026-05-11-point-light-and-visible-halo

- **发现方**：OrangeEditor v0.1 / v0.1.5 Demo Scene 设计
- **发现日期**：2026-05-11
- **一句话定性**：Render 公共面缺 PointLight / SpotLight 与可见光晕能力，导致 "点光源照亮黑暗环境 + 光体本身可见" 的典型场景（Ori-like 史莱姆发光 / 灯泡照明）无法实现

### 触发场景

第一款游戏构想里有 "史莱姆主角在黑暗环境中发光探索" 的核心机制：

- 史莱姆位置发出点光，照亮周围 mesh（地面 / 墙壁）
- 玩家能直接看到光本身（光晕 / 光球）
- 经典演示场景用一个灯泡 prop 来表现同一能力，使其也能作为 editor-roadmap v0.1.5 Editor Demo Scene v2 的典型展示项

当前引擎能力对照：

| 原子能力 | 现状 | 备注 |
|---------|------|------|
| DirectionalLight | ✅ | Phase 3 Task 05 |
| PointLight 公共接口 | ❌ | `include/orange/engine/render/LightComponent.h:8` 注释明说 "点光 / 聚光留给 Phase 4+"，但 Phase 4 / 5 都没做 —— 漏网项 |
| SpotLight 公共接口 | ❌ | 同上 |
| Pipeline 多 light 收集 | ❌ | 当前 Pipeline 取 first-found DirectionalLight 作主光 |
| Emissive Material + Bloom | ✅ | Phase 5 Task 02b + Phase 3 Task 03；可让物体 **看起来发光** 但 **不照亮** 周围 |
| Screen-space god rays | ✅ | Phase 5 Task 03；仅 DirectionalLight 阳光光柱，不适用 PointLight |
| Point light volumetric halo | ❌ | 真 froxel-based volumetric point light 是 Phase 10 量级；本 gap 走 billboard 近似 |

### 缺什么（按依赖拆）

#### G1 · `PointLightComponent` 公共接口

- `include/orange/engine/render/LightComponent.h` 加 `PointLight` struct：
  - position 跟 Transform 走（不存 component 上）
  - color (vec3，linear RGB)
  - intensity (float，标量乘子)
  - range (float，光照距离上限，culling 用)
  - attenuation 模型：先用经典 `1 / (1 + linear*d + quadratic*d²)` 或物理基 `1 / d² · smoothstep(0, range, d)` cutoff（实现时定一种）
  - castsShadow（保留字段但本 gap 不实现 omnidirectional shadow map）
- 序列化 Read/Write + SchemaVersion bump（参 CLAUDE.md "Serialization and reflection"：新版本 + migrator，不改已发版本）
- Inspector 段配套（属 editor-roadmap v0.4 范围，本 gap 仅引擎侧）

#### G2 · Pipeline 多 light 收集 + forward 多 light shading

- Pipeline 在 RenderScene 收集阶段把所有 PointLight 收进 light list（cap 上限暂定 8 个，超出截断 + warning log）
- per-frame UBO 把 light list 喂给 fragment shader（push-constant 容量不够，必须走 UBO）
- toon / rim_light fragment shader 加 point light loop：迭代到 cap 上限，距离衰减 + NdotL + range cutoff
- 多 light 排序 / tile-based culling / clustered shading 等高级优化 **不在本 gap 范围**，留给 Phase 10 渲染深化

#### G3 · 可见光晕（最经济版）

- **不做** froxel-based volumetric point light（Phase 10 Task 10-04 体量）
- billboard 自发光 sphere mesh + radial gradient texture + bloom 自动散光近似
- 可考虑提供内置 `PointLightHalo` 子 mesh（程序化生成 sphere + emissive material），editor 侧 v0.4 让 Light gizmo 默认挂这个 prop（gap 内只暴露 mesh / material 资源，editor 侧消费）
- 真正的 volumetric scattering 留给 Phase 10

### 期望验收

- editor-roadmap v0.1.5 Editor Demo Scene v2 中能挂一个 PointLight + halo prop，看到：
  - 史莱姆 / 灯泡 mesh 本身因 emissive + bloom 视觉发光
  - 周围 ground / wall mesh 接收 PointLight 贡献，距离衰减自然过渡
  - 黑暗环境（DirectionalLight intensity = 0 或不挂）下，整个场景仅由 PointLight 决定明暗
- 性能基线：cap=8 PointLight、1080p、Debug 配置主 pass 不显著掉帧（不要求严格 60 FPS，但不要数量级回归）
- 序列化 round-trip：PointLight scene save / load 字段无丢失

### 状态

- **登记**：2026-05-11
- **处理**：未启动；预估 1–2 个独立 session 体量（G1 + G2 一个 session，G3 一个 session）
- **关联**：editor-roadmap.md v0.1.5 demo scene / v1.x Ori-like 视觉子模式
- **归属**：`docs/roadmap.md` Phase 10 · 渲染深化 Task 10-07（2026-05-12 拍板）

---

## GAP-2026-05-14-renderable-material-instance-round-trip

- **发现方**：OrangeEditor v0.2.5 BUG-2 诊断
- **发现日期**：2026-05-14
- **一句话定性**：`RenderableComponent.materialInstance` 是裸 `MaterialInstance*`，Scene 序列化完全跳过该字段；任何 Save → Load round-trip 后 materialInstance 一律 nullptr，编辑器侧不得不靠 hardcode by-name 分派兜底——是 BUG-2（Play→Stop 后掉落物变棋盘格）的结构性根因

### 触发场景

OrangeEditor v0.2.5：

1. **启动加载**：main.cpp 跑 `Scene::Load("assets/scenes/demo.scene.json")`——demo.scene.json 内 8 个 Renderable 段全部缺 material 字段（Save 时根本写不出）。Load 返回 IsErr 后 main.cpp fallback 到 `SeedDemoWorld`，靠 SeedDemoWorld 内 hardcode `rc.materialInstance = host.assets.pXxxMaterial.get()` 挂材质——绕过了缺口但没解决
2. **Play→Stop snapshot**：EditorRenderLayer 走 `Scene::Save → temp file → Scene::Load`。Save 跳过 materialInstance，Load 还原后全 nullptr；`ReattachMaterialInstances` 按实体名 hardcode 分派挂回，未命中的（如 Dynamic Box）落 `pDefaultRenderableMaterial`（textured 棋盘格）——BUG-2 现象
3. **Save → Reopen** 用户手工保存的 scene 同理丢失 materialInstance；下次启动只能靠 hardcode 路径勉强还原

### 验证（2026-05-14 通过 grep + 阅读引擎源码确认）

- `src/scene/SceneSerialization.cpp::Save` 内 `idMap` 把 entt::entity 重映射为 0..N-1 持久 ID 写入 JSON；Load 时 create 新 entt::entity，所以 **EnTT 句柄在 round-trip 中不稳定**，无法直接用作 caller-side 还原键
- `RenderableComponent` 的 Read/Write 不写 materialInstance 字段（demo.scene.json 内 Renderable 段仅含 `castsShadow / mesh / visible`，且 `mesh` 字段是空字符串——AssetHandle 也没真正序列化）

### 缺什么（按依赖拆，v0.3+ 评审定）

#### G1 · `MaterialInstance` 命名注册 + 序列化形式

候选设计：

1. **基于 asset id 的字符串字段**：`AssetRegistry` 增加"按 string id 注册 MaterialInstance"路径；`RenderableComponent` 序列化时写 material 的 string id，Load 时反向 lookup。需要 `pFloorMaterial` / `pToonMaterial` / `pDissolveMaterial` 等内置材质在编辑器启动期注册时一并写 id（典型："builtin/floor" / "builtin/toon" / ...）
2. **`AssetHandle<MaterialInstance>` 强类型 handle**：与 `mesh: AssetHandle<MeshAsset>` 走同款路径；裸指针字段降级为运行时缓存（lazy resolve from handle）。要求 `MaterialInstance` 满足 `AssetHandle` 的 trait 约束
3. **混合**：保留裸指针字段（性能 hot path），新增可选 `materialAssetId` 兄弟字段参与序列化，Load 后调度一次 ResolvePointers pass

无论哪种，本 GAP 范围内**只**做：(a) `RenderableComponent` schema bump（SchemaVersion +1）+ Read/Write 处理新字段；(b) Save 写出 + Load 反向解析；(c) 现有 v1 数据 migrator 走老路径（裸指针保持 nullptr，编辑器侧自然兼容现有 hardcode 兜底）

#### G2 · `mesh` 字段同款修复

demo.scene.json 内 `"mesh": ""` 也是同款问题——`AssetHandle<MeshAsset>` 看似走 handle 路径但序列化是空字符串，Load 后 handle 也是 Invalid。要么 G1 落地同时把 mesh 一起补正，要么单独再开一条 GAP

#### G3 · `RigidBodyComponent.handle` / `AnimatorComponent.animator` 等同类裸字段统一治理

裸指针 / 运行时 handle 字段都是同款"运行时引用不进序列化"模式。本 GAP 主目标是 materialInstance，但顺手把同类字段处理原则在引擎 Serialization 文档写一遍，避免未来再撞同款

### 期望验收

- 在编辑器里 +Add Renderable 一个实体（预绑 cubeMesh + defaultMaterial）→ 改字段 → Save → 关闭重启 → Load → 该实体 Renderable 段 `materialInstance` **仍指向 defaultMaterial**（视觉：实体仍显示 textured 棋盘格，不是无材质 fallback）
- 同流程下 Play → Stop → materialInstance 保持原值（不再 nullptr）；BUG-2 现象天然消失
- 编辑器侧可**删除** `tools/OrangeEditor/EditorRenderLayer.cpp::ReattachMaterialInstances` 全部 hardcode by-name 分派（v0.2.5 末期标记为 "GAP-2026-05-14 落地后退役"）

### 临时方案（v0.2.5 编辑器侧）

GAP 未落地前，编辑器侧 v0.2.5 范围内的临时修复候选见 OrangeEditor 验收清单 BUG-2 段（`docs/acceptance/editor-v0.2.5-acceptance-checklist.md`）：

- **Y'** hardcode 表加 Dynamic Box 一行（最小妥协）
- **Y''''** EditorRenderLayer 加 `mPlayMaterialSnapshot` 通用 by-name snapshot/restore（约 20 行，不新增 hardcode 名字）—— 推荐

下个 milestone 评审决定走 Y' / Y'''' 还是直接等本 GAP 落地走 N

### 状态

- **登记**：2026-05-14
- **处理**：引擎侧落地 2026-05-14（G1，独立 session）
  - G1：`ComponentSerializerEntry.h` 加 `Render::MaterialInstance` forward decl + `namedMaterialInstances` 字段到 `SaveContext` / `LoadContext`；`SceneSerialization.h` 的 SaveOptions / LoadOptions 同步加字段；WriteRenderable 写出 `materialInstanceId` 字符串（O(N) 反查 namedMaterialInstances）；ReadRenderable 按 id 正向查表赋指针；SceneSchemaVersion 从 1.0 → 1.1（minor bump，可选字段，向后兼容）
  - G2（mesh 空路径）：诊断结论——不是引擎序列化 bug，是编辑器侧 builtin mesh 未通过 `AssetRegistry::Insert` 注册有效路径导致 `PathOf` 返回空；fix 属编辑器侧（确保 SeedDemoWorld / InitializeEditorAssets 时用 `registry.Insert("builtin/cube", ...)` 等有意义路径）；引擎端 WriteRenderable / ReadRenderable 逻辑本身正确，无需改动
  - G3（同类裸字段原则）：`RigidBodyComponent.handle` / `AnimatorComponent.animator` 是运行时 backend binding，不属于"内容序列化"范围，与 materialInstance 不同款；materialInstance 是内容引用（玩家看到哪个材质），应该序列化；设计原则：凡"内容引用"字段（控制运行时展现的数据）必须序列化，凡"backend binding 句柄"（物理 body handle、IAnimator 实例）在 Load 后由系统重建，不序列化
  - **编辑器侧消费**（2026-05-14）：`DemoWorld.cpp` 新增 `BuildNamedMaterialInstances(EditorAssetContext&)` → `"builtin/*"` id 表；所有 Scene::Save / Load 调用点（SceneOp::Open / Save / SaveAs + Play 快照 Save + Play Stop 快照 Load）均传入 `namedMaterialInstances`；`ReattachMaterialInstances` hardcode by-name 分派已删除；BUG-2 现象消失
- **关联**：OrangeEditor v0.2.5 BUG-2（已完整修复）；editor-roadmap.md v0.3 资产 / scene 编辑能力
- **归属**：待评审；候选挂到 `docs/roadmap.md` Phase 7+ 序列化深化 或独立小 task

---

## GAP-2026-05-14-scene-serializer-extension

- **发现方**：OrangeEditor v0.3 c2 计划（游戏侧 schema 注册落地）
- **发现日期**：2026-05-14
- **一句话定性**：`Scene::Save` / `Scene::Load` 写死 `GetBuiltinComponentSerializers()` 内置 component 列表，无任何 hook 让游戏侧 / 编辑器侧注册自定义 `ComponentSerializerEntry`——直接 block 了 editor-roadmap v0.3 "游戏侧自定义 component 走完整 schema → Inspector → Undo/Redo → **Save/Load** 链路" 这条验收路径

### 触发场景

OrangeEditor v0.3 c2 设计：演示一个伪游戏侧 `HealthComponent` 通过 `OrangeEditor::RegisterComponentSchema<T>(...)` 公共 API 注册后，**不改 editor 一行**就能在 Inspector 显示 + Undo/Redo + Save/Load round-trip。前三段（schema 注册 / Inspector 渲染 / Undo/Redo）当前架构都已具备，**只差 Save/Load**：

- `src/scene/ComponentSerializers.cpp:1025` `GetBuiltinComponentSerializers()` 返回写死 `std::vector<ComponentSerializerEntry>`
- `src/scene/SceneSerialization.cpp:108` 与 `:221` 是 Save / Load 仅有的两个调用点，都直接拿 builtin 列表，没有"额外 entry"参数
- `Scene::SaveOptions` / `LoadOptions` 当前字段：`assetRegistry` / `physicsWorld` / `animatorRegistry` —— 没有任何"自定义 serializer"扩展点

结果：
- **Save**：World 内挂的 `HealthComponent` 不在 builtin Has() 集合 → 直接被跳过，不写入 JSON
- **Load**：JSON 里如果硬塞 "Health" 段，按现有 forward-compat 规则被 warn + skip

游戏侧 / 编辑器侧无任何合法路径让 `HealthComponent` 参与 Scene 序列化。

### 缺什么（按依赖拆）

#### G1 · `LoadOptions` / `SaveOptions` 加 `extraSerializers` 字段

- `Scene::SaveOptions` 加 `std::span<const ComponentSerializerEntry> extraSerializers{};`（或等价 view 类型，避免引入 `<vector>` 到公共头）
- `Scene::LoadOptions` 同款字段
- Save / Load 内部把 builtin + extra 拼成单一 dispatch 表（线性查找即可，N ≤ 几十）
- 名字冲突处理：extra 与 builtin 同 `name` → 返回 `ResultCode::AlreadyExists`（开发期 bug，不允许覆盖 builtin）

#### G2 · `ComponentSerializerEntry` 公共面化

- 当前 `ComponentSerializerEntry` 在 `src/scene/ComponentSerializers.h`（私有头）
- 让游戏侧能填这个 struct 必须移到 `include/orange/engine/scene/ComponentSerializerEntry.h`（公共头）
- 函数指针签名：`Has(const World&, Entity)` / `Write(JsonWriter&, ..., SaveContext&)` / `Read(const JsonReader&, ..., LoadContext&)`——`JsonWriter` / `JsonReader` / `SaveContext` / `LoadContext` 也要公共化（除非这些已经在公共头中）

#### G3 · `SchemaVersion` 路径处理

- 游戏侧 component 应该自带 `SchemaVersion`（参 CLAUDE.md "Serialization and reflection" 节）
- 写入 / 读取路径是否需要在 ComponentSerializerEntry 内承载 schema version？还是约定调用方在 `Write` / `Read` 内部自己处理？两条路径都可行，G1 落地时定一种

### 期望验收

- OrangeEditor v0.3 c2b（GAP 落地后另开 session）：在 sample / test 内定义 `HealthComponent { int hp; int maxHp; }` + `Read/Write` + schema 注册
- 编辑器启动期通过 `extraSerializers` 把 HealthComponent 的 `ComponentSerializerEntry` 喂给 `Scene::LoadOptions`
- demo.scene.json 内手工塞一个挂 HealthComponent 的实体 → Load 后该实体在 World 里 + Inspector 显示 + 改值 + Save → 重启 → Load → 值保留

### 临时方案（v0.3 c2a 编辑器侧）

GAP 未落地前，v0.3 c2 拆为：
- **c2a**（GAP 落地前可做）：公共 API `OrangeEditor::RegisterComponentSchema<T>(...)` + HealthComponent demo schema → Inspector → Undo/Redo 三段链路验收；**不**进 demo.scene.json，仅靠 SeedDemoWorld / 启动 hook attach
- **c2b**（GAP 落地后另开 session）：HealthComponent 通过 `extraSerializers` 接入 Scene 序列化，完成 Save/Load round-trip 验收 + 进 demo.scene.json

### 状态

- **登记**：2026-05-14
- **处理**：引擎侧落地 2026-05-14（G1 + G2，独立 session）
  - G2：`include/orange/engine/scene/ComponentSerializerEntry.h` 新建（公共化 ComponentSerializerEntry / SaveContext / LoadContext / ComponentKind / EntityToPersistentId / PersistentIdToEntity）；`src/scene/ComponentSerializers.h` 缩短为 include 公共头 + 内部 helper 声明
  - G1：`SceneSerialization.h` 的 SaveOptions / LoadOptions 增加 `std::span<const ComponentSerializerEntry> extraSerializers{}`；`SceneSerialization.cpp` Save/Load 路由 extra 条目（冲突检测 + Pass 1 PureData + Pass 2 BackendDependent）
  - **编辑器侧消费**（2026-05-14）：`demo_game/HealthComponent.h/cpp` 新建（DemoGame::HealthComponent{hp, maxHp} + HealthHas/Write/Read + kHealthSchemaVersion + RegisterHealthComponentSchema + GetHealthSerializerEntry）；`EditorHost.extraSerializers` 字段落地；main 启动期注册 schema + push_back entry；所有 Scene::Save / Load 调用点（SceneOp::Open / Save / SaveAs + Play 快照 Save + Play Stop 快照 Load）均传入 `extraSerializers`；DemoWorld 新增 "Test Fighter" 实体挂 HealthComponent；完整 Save/Load round-trip 验收路径就绪
- **关联**：OrangeEditor v0.3 c2 已完整落地（c2a Inspector + c2b Save/Load）
- **归属**：待评审；候选挂到 `docs/roadmap.md` Phase 7+ 序列化扩展性深化 或独立小 task

---

## GAP-2026-05-15-camera-editor-vs-runtime-separation

- **发现方**：OrangeEditor v0.4 c5（Camera frustum gizmo）
- **发现日期**：2026-05-15
- **一句话定性**：引擎当前 `Render::Camera` component 同时承载"编辑器 viewport 相机"与"游戏运行时相机"两个角色，`ApplyEditorCameraToWorld` 每帧把 World 内**首个** Camera 组件的 view/projection 全量覆写为编辑器轨道相机的矩阵——这导致：编辑器内"选中游戏 Camera entity → 显示其 frustum"无法基于 component 实际数据展示（component 数据=编辑器相机视野，frustum 视觉上与 viewport 自身重合，无意义）

### 触发场景

OrangeEditor v0.4 c5 实现 Camera frustum gizmo：

- 选中 demo scene 内挂着 `Render::Camera` component 的实体
- 期望 viewport 内看到该相机的 frustum 线框（视野范围 + near/far plane 投影），帮助美术 / 关卡设计师判断游戏运行时相机会看到什么

当前阻塞：

- `tools/OrangeEditor/EditorCameraControl.cpp::ApplyEditorCameraToWorld` 拿 World 中首个 Camera component → 整体覆写为 `BuildEditorCamera(...)` 返回值
- DemoWorld 内 `world.AddComponent<Camera>(camera, cam)` 落了一个 Camera entity，但每帧第一句话就被覆写
- Plugin 读 `component.view` / `component.projection` 反推 frustum corners → 拿到的是编辑器 viewport 自己的视野，frustum 必然与 viewport 自身边框对齐，对用户毫无信息量

### 缺什么

引擎侧需要把"编辑器 viewport 相机"与"ECS 内游戏 Camera"概念分离。候选路径（待评审）：

1. **引擎侧 EditorCameraContext**：Pipeline 新增一个"editor override camera"输入字段（不挂 ECS），渲染时优先用 override 而非首个 ECS Camera。OrangeEditor 直接 push `BuildEditorCamera` 结果给 Pipeline，不再 mutate World。ECS Camera 保持游戏侧语义不被破坏
2. **Camera 标签分类**：Camera component 加 `enum class CameraRole { Game, EditorViewport }` 字段，Pipeline 默认渲染 `EditorViewport`，但忽略其在序列化 / Inspector 里的显示；编辑器内的 viewport 自己挂一个 hidden `EditorViewport` 角色 Camera，不影响 `Game` 角色
3. **Camera 拆 Desc + Runtime**：`CameraDesc { fov, aspect, near, far, mode }` 是数据 POD（可序列化、Inspector 可编辑、不被覆写），`Camera { view, projection }` 是运行时缓存（Pipeline 每帧从 transform + desc 推导，序列化忽略）。frustum gizmo 基于 desc 算 → 与渲染状态解耦

路径 1 最小侵入；路径 3 最干净（也顺手让 Camera 在 Inspector 里有真实可编辑字段——目前 view/projection 是 mat4，schema 不支持，Inspector 段是空的）。

### 期望验收

- demo scene 内 Camera entity 的 component 数据被编辑器读取时**反映游戏侧设置**（不是编辑器轨道相机）
- 选中该 entity → viewport 内 frustum 反映**该游戏相机**的视野（fov / aspect / near / far / 朝向）
- 移动 Camera entity transform → frustum 跟着动，视觉验证摆位
- Pipeline 渲染仍由编辑器 viewport 相机驱动（编辑器内看到的画面与游戏运行时画面**可不同**）

### 临时方案（v0.4 c5 编辑器侧）

GAP 未落地前，c5 frustum gizmo 走**fake hardcode 默认参数**路径：

- `CameraFrustumGizmoPlugin` 用 hardcode 默认 fov=45° / aspect=16:9 / near=0.1 / far=10 + entity Transform 推 view（lookAt(position, position + rot * -Z, rot * Y)）算 frustum 8 corners
- 视觉上能看到一个"假"frustum 在 entity 位置 + 朝向，给用户**摆位提示**；但 fov/near/far 数值是 hardcode，不反映 Camera component 真实数据
- 代码内 TODO 注释明示当前限制 + 链接本 GAP
- 待 GAP 落地（路径 1 / 2 / 3 任一）后，plugin 切到读真实数据

### 状态

- **登记**：2026-05-15
- **处理**：待评审；候选挂到 `docs/roadmap.md` Phase 7+ 或独立小 task
- **关联**：OrangeEditor v0.4 c5 已用 fake 默认参数路径落地，等 GAP 决议后回头修正

---

## GAP-2026-05-16-directional-light-transform-decoupled

- **发现方**：OrangeEditor v0.4 c4（DirectionalLight gizmo）验收
- **发现日期**：2026-05-16
- **一句话定性**：DirectionalLight component 的 `direction` 字段与 entity 的 `TransformComponent` 完全解耦——`Pipeline::ComputeLightViewProj` 只读 `light.direction` 不读任何 transform；但 `DirectionalLightGizmoPlugin` 把箭头**起点**画在 `pTC->position`，给用户"光源在这里"的视觉错觉。结果：编辑器拖 DirectionalLight entity → 黄色箭头跟着飘 → 用户预期阴影实时变化 → 实际阴影完全不变（component direction 没动），交互反馈与渲染结果断裂

### 触发场景

OrangeEditor v0.4 c4 落地了 IEditorGizmoPlugin 首批消费——`DirectionalLightGizmoPlugin` 给挂 `DirectionalLight` 的 entity 画黄色方向箭头。验收路径：

- 选中 demo scene 内的 DirectionalLight 实体（"Sun" / "Light"）
- viewport 拖 entity（Translate gizmo 改 `Transform.position`）/ Inspector 改 position
- 黄色箭头**起点**跟着 entity 移动，视觉上像"光源在飘"
- 但场景里物体的阴影**完全不变**（plane 上 cube 的 shadow 既不平移也不变形）

复现 100%。

### 根因

- `src/render/Pipeline.cpp:2352 ComputeLightViewProj` 只用 `light.direction`，shadow lightPos 从 `sceneCenter - lightDir * 2 * halfExtent` 推断，**完全不读 entity Transform**
- `tools/OrangeEditor/plugin/DirectionalLightGizmoPlugin.cpp:65` 箭头起点 `origin = pTC->position`；line 71 方向 `pDL->direction / dirLen`
- 二者各自独立：拖 entity Transform → gizmo 起点变 / Pipeline lightPos 不变；改 `light.direction` → gizmo 方向变 / Pipeline 同步变；但**没有**任何路径让 transform → direction 同步

### 缺什么

工业惯例（Unity / Unreal / Godot）：DirectionalLight 的 direction = entity Transform 的"local 朝下方向"经 rotation 旋转后的世界向量。component 上不单独存 direction，靠 transform 派生。候选路径（待评审）：

1. **Convention A（rotation-derived direction，最干净）**：DirectionalLight 删 `direction` 字段；Pipeline 改 `direction = transformRotation * (0, -1, 0)`；编辑器用户按 Rotate gizmo 转 entity 改方向。代价：序列化 SchemaVersion bump + migrator（旧 scene direction 字段读出后转 quat 写回 rotation）
2. **Convention B（双 source，过渡式）**：保留 `direction` 字段；Pipeline 优先看 transform-rotation（identity 时 fallback 到 direction）。Inspector 加只读"effective direction"显示推导后值。代价：两份真相易撞冲突，long-term 仍需走 Convention A
3. **Convention C（gizmo 内 hack）**：纯编辑器侧——DirectionalLight gizmo 拖动同时写 `transform.rotation` 和 `light.direction`，保持二者同步。代价：引擎内 ECS 系统 / 游戏脚本若改 transform 不会传播到 direction，"美术拖编辑器看到对的，运行时不一样"

路径 1 最干净（与 Camera GAP-2026-05-15 路径 3 同思路：废 component 内冗余字段，让 transform 唯一拥有几何状态）。

### 期望验收

- 移动 DirectionalLight entity 的 `Transform.rotation`（Inspector 或将来 Rotate gizmo）→ 阴影在 viewport 内**实时**变化
- 移动 entity `Transform.position` → 黄色箭头起点跟着移动是**预期**（视觉摆位提示），但阴影**不**变化（平行光 position 无意义；可考虑 plugin 改成画在 sceneCenter / camera-facing 固定屏幕位置以避免 misleading）
- DirectionalLight Inspector 不再有独立 `direction` 字段（或字段标只读 + 注明"由 Transform.rotation 推导"）
- 现有 demo scene 的 DirectionalLight 经 migrator 自动从旧 direction 字段升到 rotation；视觉无回归

### 临时方案（v0.4 编辑器侧）

GAP 未落地前，编辑器侧**不**主动 workaround（避免 Convention C 那种"拖动同时写两个字段"在引擎自己 tick 时撞冲突）。验收文档登记本 GAP 链接，告知用户"这是已知设计缺口、阴影不变是当前真实行为"。

`v0.4-acceptance-checklist.md` c4 ### bugs 段保留这条记录（已存在），不在编辑器侧硬改方向同步。

### 状态

- **登记**：2026-05-16
- **处理**：待评审；候选挂到 `docs/roadmap.md` Phase 7+ 或独立小 task；优先级建议高于 GAP-2026-05-15（用户每次拖光都撞，反馈级 P1）
- **关联**：OrangeEditor v0.4 c4 ### bugs 第 1 条；与 GAP-2026-05-15-camera-editor-vs-runtime-separation 同思路（component 几何字段 vs Transform 唯一真相）

---

## GAP-2026-05-16-builtin-asset-disk-serialization

- **发现方**：OrangeEditor v0.5 start-checklist（Asset 浏览器 + Material 子模式 milestone）
- **发现日期**：2026-05-16
- **一句话定性**：编辑器内置 mesh / material 仅在启动期通过 `InitializeEditorAssets` + `BuildNamedMaterialInstances` 在内存中注册，缺 .material / .mesh 磁盘落盘 + AssetRegistry 从盘加载命名 asset 的路径，导致 v0.5 Asset 浏览器无法浏览真实磁盘 asset、Material 子模式调参无法持久化
- **状态**：待评审 + 待落地

### 触发场景

v0.5 milestone 描述"资源浏览器 + Material 子模式"假设 `assets/` 下有 mesh / texture / material 文件可浏览，Material 调参可保存到 .material 文件。但实际现状：

- `assets/` 目录仅含 `configs/` + `scenes/` 的 JSON 文件
- demo scene 的 `Renderable.mesh = "editor/cube"` / `materialInstanceId = "builtin/toon"` 全是**字符串 ID**，指向编辑器启动期 `InitializeEditorAssets` + `BuildNamedMaterialInstances` 在内存中注册的命名 asset
- 仓库无 .material 文件；`Resources/Models/` 下的 .obj 是 GEA 旧架构历史归档，未接入新 AssetRegistry
- 用户在 v0.5 范围选择上选了"方案 B：完整磁盘模型"——demo scene 改引用磁盘路径 + Material 调参可持久化

当前内置 asset 注册位置：

- `tools/OrangeEditor/DemoWorld.cpp::InitializeEditorAssets` —— mesh 程序化构造（cube / plane / sphere 等顶点 + index 直接构造 MeshAsset）
- `tools/OrangeEditor/DemoWorld.cpp::BuildNamedMaterialInstances` —— material 实例直接构造（pickShader + uniform 默认值 + texture handle）
- 注册 ID："editor/cube" / "editor/plane" / "builtin/toon" / "builtin/dissolve" / "builtin/emissive" 等

### 缺什么（按依赖拆）

#### G1 · 内置 mesh 落盘 + .mesh / .obj 加载链路

- 决定**内置 mesh 落盘格式**：
  - 选项 a：复用 Phase 2 Task 02 的 .obj loader 把程序化 cube / plane / sphere 烘焙成 `assets/meshes/cube.obj` 等真实 .obj 文件（生成期一次，提交仓库）
  - 选项 b：新增 `.mesh` 二进制格式 + serializer（顶点 + index 直接 binary dump）
  - 推荐选项 a（loader 已有；.obj 是 source asset，符合 D2 "只浏览 source asset" 决策）
- 写一个一次性 `scripts/bake_builtin_meshes.py` 或 `tests/bake_builtin_meshes.cpp` 把 `DemoWorld.cpp::Build*Mesh` 的逻辑跑一遍输出 .obj 文件到 `assets/meshes/`
- `InitializeEditorAssets` 改为从盘 `Load<MeshAsset>("assets/meshes/cube.obj")` 而非程序化构造；同时保留程序化构造作为 .obj 文件缺失时的 fallback（开发期友好）
- demo scene 的 `Renderable.mesh` 字段值从 `"editor/cube"` 迁移到 `"assets/meshes/cube.obj"`；Scene SchemaVersion bump + migrator 把旧 ID 重写成路径

#### G2 · 内置 MaterialInstance 落盘 + .material 加载链路

- Phase 3 Task 01 已有 `MaterialInstance::Read / Write` JSON 序列化（按 CLAUDE.md "Phase 3 Material" 段叙述），但**项目内未实际使用** —— 没有任何 .material 文件 + 没有 `MaterialLoader` 注册到 AssetRegistry
- 写 `MaterialLoader` 实现 `IAssetLoader<MaterialInstance>` 接口，调用既有 Read 函数从 JSON 反序列化
- 一次性 `scripts/bake_builtin_materials.py` 把 `BuildNamedMaterialInstances` 内每条 MaterialInstance 用 Write 函数 dump 成 `assets/materials/builtin/toon.material`、`assets/materials/builtin/dissolve.material` 等
- `BuildNamedMaterialInstances` 改为从盘 `Load<MaterialInstance>("assets/materials/builtin/toon.material")`；保留程序化构造作为 .material 缺失时的 fallback
- demo scene 的 `Renderable.materialInstanceId` 字段从 `"builtin/toon"` 迁移到 `"assets/materials/builtin/toon.material"`

#### G3 · AssetRegistry 启动期预加载 + path → handle 反查

- v0.5 Asset 浏览器要把"磁盘文件 .material" 显示成卡片并支持 DnD 写入 Renderable.materialInstance —— DnD payload 必须能携带 "磁盘路径 → AssetHandle" 的映射，要求 AssetRegistry 暴露 `LookupByPath(path)` 接口
- 启动期 / 浏览器扫盘期 / 用户首次拖一个新 asset 入 Inspector 时，三种触发都要能 lazy-load + dedup（避免同一文件多次加载产生多个 handle）
- 现状 `AssetRegistry::Load<T>(path)` 已经做 dedup（"同 path 复用同一 handle"），但**没有公开 LookupByPath** 接口让 Asset 浏览器查询"这个文件路径是否已加载 / 对应哪个 handle"

#### G4 · Scene SchemaVersion bump + migrator

- demo / save_load_demo / thirty_seconds_demo 三个 scene 文件目前用 `"editor/cube"` / `"builtin/toon"` 风格 ID；G1 / G2 落地后迁移到磁盘路径
- 按 CLAUDE.md "Serialization and reflection" 节："不改已发版本，新版本 + migrator"——SchemaVersion 从 N bump 到 N+1，migrator 把字符串 ID 重写为路径
- migrator 在 Scene::Load 内自动跑，旧 .scene.json 文件不必手改

### 期望验收

落地后能跑通：

1. `git ls-files` 看到 `assets/meshes/cube.obj` / `assets/meshes/plane.obj` 等 + `assets/materials/builtin/toon.material` 等磁盘文件
2. demo.scene.json 内 `Renderable.mesh` / `materialInstanceId` 是磁盘相对路径而非字符串 ID
3. 启动 OrangeEditor → 自动加载 demo scene → 视觉与现状像素级一致（migrator 自动迁移生效；如未迁移走 fallback 路径仍能 render）
4. `AssetRegistry::LookupByPath("assets/materials/builtin/toon.material")` 返回有效 handle
5. OrangeEditor v0.5 Asset 浏览器在此 GAP 落地**之后**消费这些能力（Asset 浏览器扫盘 + Material 调参 → Save → 关闭重启保留）

### 不在本 GAP 范围

- **UUID + .meta** 资产数据库（参 `vendor/Orange-Wiki/wiki/concepts/editor/asset-database.md` 远期建议）—— 本 GAP 走"路径哈希 / 路径字符串作为 ID"的近期方案
- **ACP（Asset Conditioning Pipeline）中间格式**（runtime 编译产物）—— 仍按 editor-roadmap L11 推到 v1.x + 主 roadmap Phase 9
- **AssetWatcher / FileSystemWatcher 自动重加载** —— 远期，v0.5 接受"改盘 + 编辑器重启"刷新

### 落地节奏

按 engine-known-gaps 处理纪律：本 GAP 由独立 session 受理。建议 session 序列：

1. **GAP 落地 session**（本 GAP）：G1 + G2 + G3 + G4 一次性落（共享 .obj / .material loader 注册 + Scene migrator 逻辑；拆 4 个 session 会反复改 AssetRegistry / DemoWorld 同一区域，碎片化）
2. **v0.5 推进 session**：消费 G1–G4 已落地的能力实现 Asset 浏览器 + Material 子模式 + Inspector AssetRef 字段

---

## 处理记录

（空）
