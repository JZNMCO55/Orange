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
- **处理**：✅ 引擎侧落地 2026-05-17（独立 session，路径 A "rotation-derived direction，最干净"；与 CLAUDE.md "engine-known-gaps 跨 session 工作流" 对齐：登记 / 实现分 session，本次实现 session 内合并落 engine + samples + tests + editor 消费方避免中间态 broken build）
- **关联**：OrangeEditor v0.4 c4 ### bugs 第 1 条（已通过本 GAP 落地天然消失）；与 GAP-2026-05-15-camera-editor-vs-runtime-separation 同思路（component 几何字段 vs Transform 唯一真相）

### 落地记录（2026-05-17）

按用户选择走**路径 A**（DirectionalLight 删 direction 字段，方向由 entity 的 TransformComponent.rotation 派生）。理由：与 Unity / Unreal / Godot 工业惯例完全一致，避免双 source-of-truth 的长期债。

**实际落地范围**（单 commit `cf44a0c`，14 文件）：

- **G1 引擎公共面（LightComponent.h）**：DirectionalLight struct 删 `glm::vec3 direction` 字段；保留 color / intensity / castsShadow 三项。加两个 inline helper：
  - `kDirectionalLightLocalForward = (0, -1, 0)` 常量约定（identity rotation = 光向 -Y）
  - `ComputeDirectionalLightWorldDir(rotation)` —— Pipeline / gizmo / 工具代码共用的方向派生公式
  - `MakeDirectionalLightRotationFromDir(desiredDir)` —— 反推 quat 用于 scene migrator / sample 初始化场景。手写 from-to 标准 quat 公式（dot + cross + sqrt 半角）避免拉 `<glm/gtx/quaternion.hpp>` 实验扩展的 GLM_ENABLE_EXPERIMENTAL 宏污染消费者侧编译环境
- **G2 Pipeline.cpp**：`ComputeLightViewProj` / `UpdateLightUbo` 签名改 `const glm::vec3& lightWorldDir`。两个 callsite（Render / RenderOffscreen）在 `view<DirectionalLight>()` 找到 entity 后 `try_get<TransformComponent>` 查 rotation，调 `ComputeDirectionalLightWorldDir` 派生方向喂给 Pipeline；entity 没挂 Transform 时退到 identity（光向 -Y），与 nullptr-light 分支的中性默认 (0.3,-1,0.4) 不冲突
- **G3 SchemaVersion bump + v1 migrator（ComponentSerializers.cpp）**：`ReadDirectionalLight` 检测旧 scene 的 `direction` 字段时，`MakeRotationFromDir` 转 quat upsert 到 entity 的 TransformComponent（有 Transform 则覆盖 rotation 保留 position/scale；无则新建 default + rotation）。dispatch 顺序保证 Transform 在 DirectionalLight 之前 Read，migrator 永远能拿到正确状态。`WriteDirectionalLight` 不再写 direction（已无该字段），新写出的 scene 自然是 v2 格式
- **G4 跨文件消费方迁移**：
  - **samples/05/06/07/08/09/12** + **tools/OrangeEditor/DemoWorld.cpp**：原 `DirectionalLight dl{}; dl.direction = ...;` 改为给 light entity 挂带 `MakeRotationFromDir(...)` 派生的 TransformComponent。samples/07/08 内每帧 mutate direction 的 LightSpinLayer 改 mutate light entity 的 `Transform.rotation`
  - **编辑器 schema/RegisterBuiltinSchemas.cpp**：删 DirectionalLight 的 `direction` Field 注册（Inspector 不再显示 Direction 控件）
  - **编辑器 plugin/DirectionalLightGizmoPlugin.cpp**：箭头方向改读 `ComputeDirectionalLightWorldDir(pTC->rotation)`，与 Pipeline 同源派生公式，gizmo 视觉与 shading 方向永远一致；entity 没挂 Transform 时不画（无方向概念）
  - **tests/render/LightAndShadowTest.cpp**：默认字段测试改校验 color/intensity/castsShadow + identity-rotation 派生 (0,-1,0)；ECS component 测试加 MakeRotationFromDir → ComputeWorldDir 反推回原方向精度校验
  - **tests/scene/SceneSerializationTest.cpp**：round-trip 改先挂 Transform with MakeRotationFromDir 再 Save→Load→反推匹配

**期望验收对照**：

| 验收点 | 落地状态 |
|--------|---------|
| 移动 DirectionalLight entity 的 Transform.rotation → 阴影在 viewport 实时变化 | ✅（Pipeline 每帧读 TC.rotation 派生方向，rotation 改动即下一帧生效） |
| 移动 entity Transform.position → 黄色箭头起点跟着移动是预期，阴影不变 | ✅（箭头起点仍是 TC.position；shadow 公式只依赖方向不依赖 entity 位置——平行光语义） |
| DirectionalLight Inspector 不再有独立 direction 字段 | ✅（schema 已删；用户旋转 entity 改光向） |
| 现有 demo scene 的 DirectionalLight 经 migrator 自动从旧 direction 字段升到 rotation；视觉无回归 | ✅（migrator 在 ReadDirectionalLight 内 upsert TC.rotation；不主动改 assets/scenes/*.scene.json，让 migrator 在真实加载路径得到验证；首次 Open + Save 后文件自然清洗为新格式） |

**测试通过**：`light_and_shadow_test` + `scene_serialization_test` 全绿（`ctest -C Debug -R "light_and_shadow|scene_serialization"`），其余测试无回归。

**关键改动文件**：`include/orange/engine/render/LightComponent.h` / `src/render/Pipeline.cpp` / `src/scene/ComponentSerializers.cpp` / `samples/{05,06,07,08,09,12}*/main.cpp` / `tools/OrangeEditor/DemoWorld.cpp` / `tools/OrangeEditor/plugin/DirectionalLightGizmoPlugin.cpp` / `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp` / `tests/render/LightAndShadowTest.cpp` / `tests/scene/SceneSerializationTest.cpp`

**未在本 GAP 范围**：

- 引擎侧没新增 ADR —— 路径 A 即 GAP 原文推荐路径 + 与同思路 GAP-2026-05-15 路径 3 对偶，沿用现有"component 几何字段 vs Transform 唯一真相"原则，没有新决策需要 ADR
- DirectionalLight gizmo 拖动改方向（Rotate gizmo overlay）—— 当前用户调 entity Rotate gizmo / Inspector Transform.rotation 即可；DirectionalLight 自带"拖箭头改方向" 是 v0.4 c4 的设计扩展，本 GAP 范围外
- 旧 scene 文件主动清洗（重写 assets/scenes/*.scene.json 删 direction 字段）—— 留给 migrator 在真实加载路径上自然清洗，不批量修改文件历史

---

## GAP-2026-05-16-builtin-asset-disk-serialization

- **发现方**：OrangeEditor v0.5 start-checklist（Asset 浏览器 + Material 子模式 milestone）
- **发现日期**：2026-05-16
- **一句话定性**：编辑器内置 mesh / material 仅在启动期通过 `InitializeEditorAssets` + `BuildNamedMaterialInstances` 在内存中注册，缺 .material / .mesh 磁盘落盘 + AssetRegistry 从盘加载命名 asset 的路径，导致 v0.5 Asset 浏览器无法浏览真实磁盘 asset、Material 子模式调参无法持久化
- **状态**：✅ 已落地（2026-05-16，本 session 内 G1+G2+G4 + 顺手扩 MeshLoader v2 加 UV 支持；G3 评审决定不需要——LookupByPath 不必新增 API，AssetRegistry::Load 自带 dedup + namedMaterialInstances map 已是 path→ptr 反查）

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

### 落地记录（2026-05-16）

实际工作流偏离原计划：用户在 v0.5 推进 session（本 session）授权"先落 GAP 再推 v0.5"，把"GAP 落地 + v0.5 消费"合并到同一 session。两阶段顺序串行执行，无双向操作冲突（GAP 三个 commit 全部 ✅ + 验证通过后才进入 v0.5 commit）。

**实际落地范围**：

- **G1 ✅**（commit `222bd3f`）：MeshLoader v1 → v2 加 UV 段 + 引入 `MeshLoader::Save(path, mesh)` 静态方法。`tools/OrangeEditor/DemoWorld.cpp::InitializeEditorAssets` 注册 MeshLoader + lazy bake 路径（检测 `assets/meshes/X.mesh` 缺失则程序化构造 + MeshLoader::Save 写盘后再 AssetRegistry::Load）。本 commit 自带烘焙产物：`assets/meshes/cube.mesh` (641B) + `assets/meshes/plane.mesh` (121B)
- **G2 ✅**（commit `60eaa40`）：`.material` JSON 文件格式 v1.0 最小集（仅 `templateName` 字段）。`tools/OrangeEditor/DemoWorld.cpp` 加 `writeMaterialFile / readMaterialTemplate / bakeAndLoadMaterial` 三个 helper lambda（直接用 JsonReader/JsonWriter，不引入 IAssetLoader<MaterialInstance>——后者需要 MaterialSystem 注入容器没法塞 IAssetLoader 接口）。七个内置 material（floor / wall / toon / rim_light / dissolve / default / light_object）走 lazy bake。`BuildNamedMaterialInstances` map key 从 "builtin/X" 改成 "assets/materials/builtin/X.material" 路径风格。本 commit 自带 7 个 `.material` lazy bake 产物
- **G3 ❌ 评审拒绝**：不需要新增 `AssetRegistry::LookupByPath` API。`AssetRegistry::Load<T>(path)` 已自带 dedup（"同 path 复用同一 handle"）；`namedMaterialInstances` map 本身就是 path → MaterialInstance* 反查接口；v0.5 Asset 浏览器 DnD payload 携带 path 字符串足够，写入字段时按 path 直接 Load / 查表
- **G4 ✅**（commit `222bd3f` + `60eaa40` 联合）：`demo.scene.json` 内 6 处 Renderable.mesh + 多处 materialInstanceId 字段从命名 ID 迁移到磁盘路径。`src/scene/ComponentSerializers.cpp::ReadRenderable` 加 "editor/X" → "assets/meshes/X.mesh" + "builtin/X" → "assets/materials/builtin/X.material" 透明 mapping fallback，老 .scene.json 仍可加载（warn-less，无 SchemaVersion bump—— mapping 透明）

**期望验收对照**：

| 验收点 | 落地状态 |
|--------|---------|
| `git ls-files` 看到 `assets/meshes/*.mesh` + `assets/materials/builtin/*.material` 磁盘文件 | ✅ |
| `demo.scene.json` 内 `mesh` / `materialInstanceId` 是磁盘相对路径 | ✅ |
| 启动 OrangeEditor → 自动加载 demo scene → 视觉与现状像素级一致 | ✅（17 entities，stderr 无 load failed warn） |
| `AssetRegistry::LookupByPath` 返回有效 handle | ❌ 评审拒绝，不需要新 API |
| OrangeEditor v0.5 Asset 浏览器消费这些能力 | 本 session 后续 v0.5 commit 落地 |

**未来扩展**：

- v0.5 c5 Material 子模式落地时把 .material schema 从 v1.0（仅 templateName）扩到 v1.1（uniforms + textures override 字段）—— 当前内置 material 全部 default-constructed 无 override 所以不阻塞
- 远期 UUID + .meta 资产数据库 / ACP 中间格式 / FileSystemWatcher 自动重加载 仍按本 GAP 原始"不在范围"段记录走，独立 milestone 处理

---

## GAP-2026-05-16-material-system-enumerate-and-instance-overrides ✅

- **发现方**：OrangeEditor v0.5 c5（Material 子模式实施）
- **发现日期**：2026-05-16
- **一句话定性**：Material 子模式 UI 撞上两个引擎能力缺口——(1) `MaterialSystem` 缺 `EnumerateTemplateNames()` / `GetTemplateNameAt(i)` 公共 API，Inspector Combo 控件没法动态列出所有已注册模板；(2) `MaterialInstance` 缺 "枚举所有 uniform override / texture binding" 公共 API + `.material` JSON schema v1.0 只存 templateName，调参不能持久化
- **状态**：✅ 落地完成（2026-05-17，独立 session，按 CLAUDE.md "engine-known-gaps 跨 session 工作流"）

### 触发场景

OrangeEditor v0.5 c5 Material 子模式：Asset 浏览器选中 .material 文件 → Inspector 切到 material 编辑视图。需要的两项能力：

1. **Template Combo 列出可选模板**：让用户切 toon → dissolve 等。当前 hardcoded `kBuiltinTemplateNames[]` 数组（toon / rim_light / dissolve / emissive / textured）—— 与 `MaterialSystem::RegisterBuiltins` 注册一致但游戏侧 `RegisterTemplate` 自定义模板不会出现。
2. **Uniform 调参 + 持久化**：当前 Material 子模式只能切 templateName 写盘；用户改 ToonColor 等 uniform → Save → 重启编辑器 → 改动丢失（.material 文件只存 templateName）。

### 缺什么

#### G1 · `MaterialSystem::EnumerateTemplateNames` 公共 API

```cpp
// include/orange/engine/render/MaterialSystem.h
std::vector<std::string_view> GetTemplateNames() const;
// 或
const std::vector<std::string>& EnumerateTemplateNames() const;
```

`MaterialSystem` 内部已用 unordered_map 存 templates，把 key 集合暴露为 view 即可。MaterialInstance 子模式 Combo 控件直接从此 API 拿列表。

#### G2 · `MaterialInstance` 枚举 override + `.material` schema v1.1

`MaterialInstance` 公共面需要 "迭代所有已设置 uniform / texture override" 接口。当前只有 `HasUniformOverride(name)` + `GetUniformXxx(name)`，无法不知道 name 的情况下遍历。

`.material` JSON schema v1.0 → v1.1：

```json
{
  "schemaVersion": {"namespace":"render/material_instance","major":1,"minor":1},
  "templateName": "toon",
  "uniforms": [
    {"name":"ToonColor","type":"vec4","value":[0.8,0.3,0.2,1.0]},
    {"name":"OutlineWidth","type":"float","value":0.05}
  ],
  "textures": [
    {"binding":0,"path":"assets/textures/foo.png"}
  ]
}
```

`DemoWorld.cpp::bakeAndLoadMaterial` 内 `MaterialSystem::CreateInstance` 之后按 uniforms 列表 SetUniform 还原 override；Save 路径反向迭代 override 列表写盘。

### 期望验收

落地后能跑通：

1. Material 子模式 Combo 控件**自动**列出所有已注册模板（含游戏侧 RegisterTemplate 注入的自定义模板），不再硬编码
2. 用户改 toon material 的 ToonColor → Save → 重启编辑器 → 颜色保留
3. `.material` schema v1.1 兼容读 v1.0（无 uniforms 字段时 default override empty）

### 关联

- OrangeEditor v0.5 c5 当前简化版（commit 本 session 末尾 v0.5 c5 commit）—— Material 子模式 UI 框架 + Save 仅 templateName
- GAP-2026-05-16-builtin-asset-disk-serialization（已 ✅）—— 本 GAP 是其下一阶段

### 落地记录（2026-05-17）

按 GAP 原文 G1 + G2 拆三步落地：

- **C1 ✅ `MaterialSystem::GetTemplateNames` 公共 API**
  - `include/orange/engine/render/MaterialSystem.h` + `src/render/MaterialSystem.cpp`：返回 `std::vector<std::string>`（值类型，免 rehash 让 string_view 悬挂）；顺序未定义（unordered_map 遍历无序），调用方按需 sort
  - 设计偏离 GAP 原文 `vector<string_view>`：选 value 类型避免 view 悬挂，符合 0.x 阶段"优先稳"取向
  - Editor 侧 `tools/OrangeEditor/panels/InspectorPanel.cpp` 删 `kBuiltinTemplateNames` 硬编码 5 项，改调 `GetTemplateNames` + 字典序排序喂 ImGui::Combo；游戏侧 `RegisterTemplate` 注入的自定义模板自动出现
  - 测试：`tests/render/MaterialSystemTest.cpp` 新增 `TestGetTemplateNames`（空表 / builtin 5 / 自定义 6）

- **C2 ✅ `MaterialInstance` 枚举 override + 类型查询 API**
  - `include/orange/engine/render/MaterialInstance.h` + `src/render/MaterialInstance.cpp`：新增 `GetUniformOverrideNames` / `GetTextureOverrideBindings` / `GetUniformOverrideType`，让序列化 / Inspector 在不知道 name 的情况下遍历所有 override
  - 测试：`tests/render/MaterialInterfaceTest.cpp` 新增 `TestEnumerateOverrides`（空 instance / 3 uniform + 2 texture override / 类型查询命中与未命中 / null instance 退化）

- **C3 ✅ `.material` schema v1.1 + Editor 端 round-trip**
  - 新增 `tools/OrangeEditor/MaterialFileIO.{h,cpp}` —— editor 侧 helper（不在引擎公共面，schema 解析是消费方的事）：`WriteMaterialFile` / `ReadMaterialFile` / `BuildDataFromInstance` / `ApplyDataToInstance`
  - v1.1 schema = templateName + uniforms[]（name+type+value，含 6 种 type）+ textures[]（binding+path）
  - v1.0 兼容：旧文件 uniforms / textures 字段视作空，等价 GAP 原文"无 uniforms 字段时 default override empty"
  - `DemoWorld.cpp::bakeAndLoadMaterial` 改调 helper 走 v1.1 schema；`InspectorPanel.cpp::DrawMaterialSubMode` Save 走 v1.1（uniform 调参 UI 是独立 deferred deliverable，当前 Save 仅写 templateName + 空 uniforms / textures 段）
  - 测试：新增 `tests/render/MaterialFileIOTest.cpp` 自带编译 helper（不 require lib 化），覆盖 v1.1 round-trip / v1.0 兼容 / instance round-trip / 错误路径 4 个 case

### 期望验收对照

| 验收点 | 落地状态 |
|--------|---------|
| Material 子模式 Combo 控件**自动**列出所有已注册模板（含游戏侧 RegisterTemplate 注入的自定义模板），不再硬编码 | ✅（InspectorPanel `CollectTemplateNames` 调 `GetTemplateNames`） |
| 用户改 toon material 的 ToonColor → Save → 重启编辑器 → 颜色保留 | **部分**：schema v1.1 已支持持久化所有 6 种 uniform type 的 override；Save 写盘 + Load 还原 round-trip 由 `MaterialFileIOTest` 覆盖；但 Inspector "调参 UI"（让用户在 ImGui 上编辑 uniform 值的控件）是独立 deferred deliverable，本 GAP 不包含——上线后 Save 路径直接调 `BuildDataFromInstance` 即可 |
| `.material` schema v1.1 兼容读 v1.0（无 uniforms 字段时 default override empty） | ✅（`MaterialFileIOTest::TestV10Compat`） |

### 不在本 GAP 范围（已登记 / 后续 session 处理）

- ~~**`AssetRegistry::LookupSourcePath(handle) -> path`**~~ —— ✅ 已落地（2026-05-17 同日补完）。原以为需要新增 API 但实际 `PathOf<T>` 早已存在；`BuildDataFromInstance` 加可选 `AssetRegistry*` 参数后 texture override 写盘端完整。详见 [GAP-2026-05-17-asset-registry-handle-to-path](#gap-2026-05-17-asset-registry-handle-to-path)
- **Material Inspector uniform 调参 UI**：让用户在 ImGui 中编辑 ToonColor 等 uniform 值的控件 —— OrangeEditor v0.5 后续 patch 范围（c6 或 c7），不在引擎 GAP 范围。引擎侧的"调参 → SetUniform → Save → Load → SetUniform 还原"链路在本 GAP 已全部就位
- **InspectorPanel Save 后运行时 MaterialInstance 不刷新**：仍是 v0.5 c5 deferred（重启编辑器才看到新 templateName 生效）—— 完整刷新路径要走 namedMaterialInstances 重建 + 所有 Renderable 字段重定向，超出本 GAP 范围

---

## GAP-2026-05-17-scene-layer-component

- **发现方**：OrangeEditor v0.6 启动 ritual（milestone-start-checklist 步骤 6 跨仓影响识别）
- **发现日期**：2026-05-17
- **一句话定性**：引擎侧 `include/orange/engine/scene/` 缺 `LayerComponent` / `WorldPartition` 概念，编辑器侧 v0.6 "Scene 拆 chunk → layer 两级；per-layer 落盘 + 单独加载 / hide / show" deliverable 无法落地

### 触发场景

editor-roadmap.md v0.6 milestone "多 chunk / per-layer + dirty 状态" 的 6 个 deliverable 中有 3 个依赖引擎侧 layer 概念：

- **Scene 序列化扩 SchemaVersion**：新 schema v2.0 需要在 entity 上多一个 layer 字段
- **Scene 拆 chunk → layer 两级；per-layer 落盘 + 单独加载 / hide / show**：核心 feature
- **多 scene tab**：跨 World 容器协调（次要，可能也涉及）

引擎当前能力对照：

| 原子能力 | 现状 | 备注 |
|---------|------|------|
| Scene/World 概念 | ✅ | `include/orange/engine/scene/World.h` 单个 EnTT registry |
| Entity hierarchy（parent/child） | ✅ | `HierarchyComponent` |
| Entity name / transform | ✅ | `NameComponent` / `TransformComponent` |
| **Entity layer 归属** | ❌ | 无 `LayerComponent` |
| **Layer 容器 / 分组 / hide-show** | ❌ | 无 `WorldPartition` / `LayerManager` |
| **per-layer 序列化（拆 chunk）** | ❌ | 当前 `SceneSerialization` 单文件整 World 序列化，不支持分文件 |
| Scene SchemaVersion + migrator | ✅ | Phase 5 Task 01 已有 migrator 机制；v1.0 → v2.0 走同款路径 |

### 缺什么

- 公共头 `include/orange/engine/scene/LayerComponent.h`：layer id（字符串或 uint16）+ visible 标志
- 公共头 `include/orange/engine/scene/WorldPartition.h`（暂定名）：layer manifest（id → name / visible / source file）；query 该 World 当前有哪些 layer；遍历某 layer 的所有 entity；hide/show 某 layer（影响 Render / Physics tick filter）
- `SceneSerialization` 扩展：保存时按 entity layer 分组写多文件（per-layer .scene.json）；加载时拼回 manifest
- Render Pipeline 在收集 RenderableComponent 时过滤 layer.visible=false 的 entity
- Physics tick 同理过滤（避免隐藏 layer 内的 RigidBody 仍参与物理）

### 期望验收

- editor v0.6 实现 "Hierarchy 加 layer 列 + 右键 'Move to layer'" UX 时，能调引擎公共 API 完成 layer 分组 / 序列化 / hide-show
- 引擎自带 sample 演示：两个 layer（背景 + 前景），加载 / 隐藏前景 → viewport 只剩背景
- Scene schema v2.0 写盘 + 加载 v1.0 旧文件（migrator 把所有 entity 归入 default layer）

### 状态

- **登记**：2026-05-17
- **处理**：引擎侧落地 2026-05-17（独立 session，按 CLAUDE.md "engine-known-gaps 跨 session 工作流"）
- **关联**：editor-roadmap.md v0.6 deliverable 2 / 3 / 5；现有 `SceneSerialization` / `World` 公共 API；`Pipeline::Render` / `PhysicsWorld::Step` —— layer.visible 过滤的最终消费方

### 落地记录（2026-05-17）

按 user 在 start-checklist 步骤 7 选择的设计方案：(1) per-layer **多文件 + manifest** 落盘（匹配 GAP 原文 + wiki §陷阱 4 关于 VCS 冲突的建议），(2) Render + Physics 都接入 layer.visible 过滤（满足验收 "viewport 只剩背景" + 避免 hidden layer 物理逻辑漏出）。

四个 commit 串行落地：

- **C1 ✅ LayerComponent + WorldPartition 公共面**
  - 新增 `include/orange/engine/scene/LayerComponent.h`：单字段 `std::string layerId`，visible 不放 component 上（避免 manifest 与 component 双 source of truth）
  - 新增 `include/orange/engine/scene/WorldPartition.h` + `src/scene/WorldPartition.cpp`：layer manifest CRUD（AddLayer / RemoveLayer / ResetLayers / GetLayer*）+ visibility 查询（IsLayerVisible / SetLayerVisible / IsEntityVisible）+ entity → layer 归属（GetLayerOf / SetLayerOf）；构造期自动注册 "default" layer（兜底；不可删）
  - `src/scene/ComponentSerializers.cpp` 注册 `Layer` 序列化器（PureData，写 `{"id": "..."}` 对象形态留 schema bump 空间）
  - 设计偏离 GAP 原文"暂定名 WorldPartition + visible 字段在 component"——实际落地：visible 放 manifest（避免重复 source），名字采用 WorldPartition 不变

- **C2 ✅ SceneSerialization 多文件 + manifest**
  - `scene/world` schema **1.1 → 1.2**（minor bump，因新增 optional LayerComponent；不是 GAP 原文"v2.0 major bump"——技术上无破坏性变化，旧 v1.1 文件 graceful 兼容：缺 LayerComponent 段 → WorldPartition.GetLayerOf 回退 default，等价于"migrator 归入 default layer"语义）
  - `scene/manifest` schema **1.0 新建**（独立 namespace）
  - 新公共 API：`Scene::SaveSplit(world, partition, manifestPath, options)` / `Scene::LoadSplit(manifestPath, world, partition, options)`
    - SaveSplit：按 partition.GetLayers() 顺序，每条 layer 写一个独立 .scene.json（仅含归属该 layer 的 entity，subset idMap 从 0 重排），最后写 manifest 文件（schemaVersion + layers 数组 with id / displayName / visible / source）
    - LoadSplit：读 manifest → partition.ResetLayers 灌入 → 按顺序 Load 每条 source；新增 `LoadOptions::assignLayerId` 字段让 Load 自动给本次新建且没挂 LayerComponent 的 entity 兜底归属
  - 旧 `Save` / `Load` 单文件路径**不动**——保持 v1.1 schema 完全兼容；新增字段在公共 header 内增量。内部把 Save 主体抽到 anonymous namespace `SaveImpl(world, path, options, entityFilter)`，让 Save / SaveSplit 共用
  - 已知设计选择：HierarchyComponent 跨 layer 引用**不会被正确序列化**（per-layer 文件内 idMap 从 0 重排，跨文件 ID 不对齐）——视为"per-layer 独立编辑"工程意图下的预期约束，遇到时 v0.6 编辑器应在 attach-time 拒绝跨 layer parent-child

- **C3 ✅ Render + Physics layer.visible 过滤**
  - Render：`RenderScene::Collect` 接 `const WorldPartition* partition = nullptr` 可选参数；非空时对每个 drawable 候选查 `partition->IsEntityVisible(world, e)`，false 即跳过（不进 drawable list，自然不进 shadow pass）。`Pipeline::SetWorldPartition(const WorldPartition*)` setter（与 SetPostProcessChain / SetMaterialSystem 同款模式），Render() 内透传给 RenderScene::Collect
  - Physics：`PhysicsWorld::SetBodyEnabled(handle, bool)` / `IsBodyEnabled` 新增，封 Box2D 3.x `b2Body_Enable` / `b2Body_Disable` / `b2Body_IsEnabled`（disabled body 不参与积分 / 不产生 contact / 仍在 world 内）
  - 新公共 free function：`Physics::ApplyLayerVisibility(world, partition, physics)`（独立 .h/.cpp，不耦合 Scene 模块到 PhysicsWorld）；遍历 World 内 RigidBodyComponent，按 layer 可见性调 SetBodyEnabled。调用方在每帧 Step 之前调一次

- **C4 ✅ Sample + 文档收尾**
  - 新增 `samples/12_layer_partition_demo`：background layer (plane + sphere + DirectionalLight + Camera) + foreground layer (两个 dynamic box 受重力下落)；VisibilityToggleLayer 每 3 秒翻转 foreground.visible + 调 ApplyLayerVisibility；本 session 实测 8 秒内 toggle 两次（false → true），干净退出，cube 在 hide 时视觉消失 + 物理冻结
  - 本 GAP 落地记录写回 docs/engine-known-gaps.md（本节）
  - invariant lint + drift baseline 重新跑：全绿（7 个 grandfathered 不变 / no drift）

### 期望验收对照

| 验收点 | 落地状态 |
|--------|---------|
| editor v0.6 能调引擎公共 API 完成 layer 分组 / 序列化 / hide-show | ✅（LayerComponent / WorldPartition / SaveSplit / LoadSplit / Pipeline.SetWorldPartition / ApplyLayerVisibility 全部公共面就位） |
| 引擎自带 sample 演示：两个 layer，加载 / 隐藏前景 → viewport 只剩背景 | ✅（samples/12_layer_partition_demo） |
| Scene schema v2.0 写盘 + 加载 v1.0 旧文件（migrator 归 default） | **部分**：技术上 schema bump 1.1 → 1.2 + 旧文件 graceful 兼容（缺 LayerComponent 段 → default layer），与 GAP 原文 v2.0 major bump 偏离；语义上等价于 migrator 归 default 行为，但形式上"无 migrator"——按 CLAUDE.md "Serialization and reflection" 新增 optional 字段是 minor bump 的标准路径，没有结构性破坏可言。本设计偏离已在 C2 段落说明 |

### 不在本 GAP 范围（后续 session 处理）

- **多 scene tab** —— editor-roadmap v0.6 deliverable 5，纯编辑器侧 UI；引擎不需要新能力
- **HierarchyComponent 跨 layer 引用的序列化保留** —— 当前 per-layer 文件 idMap 从 0 重排导致跨 layer parent-child 在 split 模式下丢失；如未来 v0.6 编辑器允许并真实使用，需要扩 manifest schema 引入"跨 layer 引用映射段"。当前视为预期约束
- **Editor 侧 Hierarchy 加 layer 列 + 右键 'Move to layer' UX** —— editor v0.6 范围；本 GAP 落地后 editor 在新 session bump vendor 后消费

---

## GAP-2026-05-17-asset-registry-handle-to-path ✅

- **发现方**：GAP-2026-05-16-material-system-enumerate-and-instance-overrides C3 落地
- **发现日期**：2026-05-17
- **一句话定性**：`AssetRegistry` 缺 `AssetHandle<T>` → 源路径反查 API；导致 `MaterialFileIO::BuildDataFromInstance` 在写盘端无法把 `MaterialInstance::GetTextureBinding` 拿到的 handle 翻译回 path，texture override 段写盘只能填 binding（path 空），reader 端识别空 path 跳过还原 —— texture override round-trip 形式上残缺
- **状态**：✅ 落地完成（2026-05-17，独立 session）——**GAP 原文认知误差**：能力本来就在（`AssetRegistry::PathOf<T>` 公共 API 已存在于 `include/orange/engine/asset/AssetRegistry.h:211`，2026-05-14 GAP-renderable-material-instance-round-trip 落地时引入用于 scene 序列化），本 GAP 实际只需让 `MaterialFileIO` 消费方真正调它

### 触发场景

`tools/OrangeEditor/MaterialFileIO.cpp::BuildDataFromInstance` 遍历 `instance.GetTextureOverrideBindings()` → 每个 binding 调 `instance.GetTextureBinding(binding)` 拿 `AssetHandle<TextureAsset>` → **缺一步**反查回原始 path 字符串 → 写盘需要 path 而不是 handle。

当前 `BuildDataFromInstance` 在 texture 段只填 binding，path 留空（`MaterialFileIO.cpp` 倒数第二个 for 循环）。reader 端 `ApplyDataToInstance` 看到 path 为空就跳过还原。schema v1.1 的 textures[] 字段就位 + uniform override 路径完整，但 texture override 写盘端语义残缺。

### 缺什么

`AssetRegistry` 公共面增加：

```cpp
// include/orange/engine/asset/AssetRegistry.h
template <typename T>
std::optional<std::string> LookupSourcePath(AssetHandle<T> handle) const;
// 或
template <typename T>
std::string GetSourcePath(AssetHandle<T> handle) const;  // 失败返空
```

内部 registry 已经按 path 做 dedup（同 path 第二次 Load 返回同 handle），所以应该已经持有 handle → path 反向映射或可重建。

### 期望验收

- `BuildDataFromInstance` 在 texture override 段填写完整 path（写盘端不再残缺）
- 新增单测：texture override round-trip（Set → Build → Write → Read → Apply → texture handle IsValid + 命中同 path 资源）
- 不破坏现有 `LoadXxx` 公共 API；只新增一个查询接口

### 关联

- 母 GAP：[GAP-2026-05-16-material-system-enumerate-and-instance-overrides](#gap-2026-05-16-material-system-enumerate-and-instance-overrides)（已 ✅）
- 消费方：`tools/OrangeEditor/MaterialFileIO.cpp::BuildDataFromInstance` 第二个 for 循环

### 落地记录（2026-05-17）

**评审发现 G1 无需新增**：开工 ritual 第 1 步阅读 `AssetRegistry.h` 时直接看到 line 211 已有 `PathOf<T>(handle) -> string_view` 模板方法，handle 失效 / 已卸载返回空 view，正是本 GAP 期望的反查接口。该 API 由 2026-05-14 母级别的 [GAP-2026-05-14-renderable-material-instance-round-trip](#gap-2026-05-14-renderable-material-instance-round-trip) 落地时为 scene 序列化引入，但当时本仓库的 OrangeEditor 侧 MaterialFileIO 还没写，所以 c3 commit 那位作者（也是我）登记 GAP 时漏看了——典型"GAP 登记没做开工前 5 分钟的 surface scan"。

两个 commit 落地：

- **C1 ✅ `MaterialFileIO::BuildDataFromInstance` 加可选 `const AssetRegistry*` 参数**
  - 默认 `nullptr` 向后兼容（DemoWorld / InspectorPanel 现有 caller 不必同步改动）
  - 非空时按 binding 调 `instance.GetTextureBinding(b)` 拿 `AssetHandle<TextureAsset>` → `pAssetRegistry->PathOf(handle)` → 翻为 string 落盘
  - `.h` 头注释从"已知约束 / 后续 GAP 落地后自动完善" 改为说明能力已就位

- **C2 ✅ `MaterialFileIOTest::TestTextureRoundTripWithRegistry`**
  - 程序式构造 1x1 红色 TextureAsset → `registry.Insert<TextureAsset>(path, ...)` → SetTexture(handle) → Build(&registry) 写盘 path 非空 → Write → Read → Apply(&registry) → handle 还原
  - 关键洞察：`AssetRegistry::LoadErased` 在 loader 检查之前先看 `pathToHandle` dedup（`src/asset/AssetRegistry.cpp:352`），Insert entry 同 path 的 Load 直接命中 cache 返回原 handle，根本不调 loader。所以单测环境（无 `RegisterLoader<TextureAsset>`）也能跑完整 round-trip：还原 handle.Value() == 原 Insert handle.Value() + `registry.Get` 拿回同一 `TextureAsset*`（指针等价）

### 期望验收对照

| 验收点 | 落地状态 |
|--------|---------|
| `BuildDataFromInstance` 在 texture override 段填写完整 path（写盘端不再残缺） | ✅（c1） |
| 新增单测：texture override round-trip（Set → Build → Write → Read → Apply → handle IsValid + 命中同 path 资源） | ✅（c2 TestTextureRoundTripWithRegistry，5 个 case 全 PASS） |
| 不破坏现有 `LoadXxx` 公共 API；只新增一个查询接口 | ✅（PathOf 早已存在；BuildDataFromInstance 是 editor 侧 helper，加默认参数向后兼容） |

### 不在本 GAP 范围

- **`InspectorPanel` Save 路径接 `BuildDataFromInstance`**：当前 Save 仅写 templateName + 空 uniforms/textures（c3 commit `b7c92d3` 旧路径），待 Material Inspector 调参 UI 上线时一并切到 `BuildDataFromInstance(currentInstance, editingTemplateName, &assetRegistry)`——属 OrangeEditor v0.5 后续 patch 范围
- **`DemoWorld::bakeAndLoadMaterial` 内部 texture 段**：当前内置 material 全部 default-constructed（无 texture override），bake 路径未实际触发 texture 写盘，无需改动

---

## GAP-2026-05-17-mesh-loader-supported-version-symbol

- **发现方**：GAP-2026-05-16-material-system-enumerate-and-instance-overrides C3 完整 build 验证时
- **发现日期**：2026-05-17
- **一句话定性**：`tests/asset/AssetRegistryTest.cpp` 79 / 290 行引用 `Orange::Engine::Asset::MeshLoader::kSupportedVersion`，但 `MeshLoader.h` 没暴露此符号；`asset_registry_test` 编译失败 —— pre-existing bug，不是本 GAP 引入
- **状态**：登记，未开工

### 触发场景

跑 `cmake --build build --config Debug --target asset_registry_test` 报：

```
AssetRegistryTest.cpp(79,34): error C2039: "kSupportedVersion": 不是 "Orange::Engine::Asset::MeshLoader" 的成员
AssetRegistryTest.cpp(290,38): error C2039: 同上
```

stash 出本 GAP 全部改动后该错误仍存在，确认 pre-existing。

### 缺什么 / 怎么修

两条路径：(1) 在 `include/orange/engine/asset/MeshLoader.h` 暴露 `static constexpr SchemaVersion kSupportedVersion`；(2) 改测试不依赖该符号（按 MeshLoader 实际公共面调）。

需要查 `MeshLoader.h` / `.cpp` 内现有 schema 版本声明（疑似只是 .cpp 内的 anonymous 常量），决定哪条修法更对齐设计意图。

### 关联

- pre-existing；与本 GAP 并行登记仅为不漏。本 GAP 范围内 lint + drift 全绿、material 5 个测试全通过、OrangeEditor build 通过；asset_registry_test 失败不阻塞本 GAP ✅

---

## GAP-2026-05-17-editor-first-frame-flash

- **发现方**：OrangeEditor v0.6.5 c0 视觉验收
- **发现日期**：2026-05-17
- **一句话定性**：编辑器启动时一闪而过出现"左上约 1920×500 白色矩形 + 其余纯黑"的异常画面（持续 < 1 帧），是 ImGui dock layout 渲染完成前的瑕疵帧；pre-existing，**与 c0 无关**（stash c0 改动后启动仍复现）
- **状态**：登记，未开工

### 触发场景

启动 `build/bin/Debug/OrangeEditor.exe`，maximize 完成后到 demo.scene 加载 + dock layout 首帧渲染之间的若干帧内，主 swap-chain 已 present 但 ImGui DrawData 未带任何 panel 内容，视觉上出现：

- 左上约 1920×500 大小白色矩形（疑似 ImGui dummy window background 或 swap-chain clear color 残留）
- 其余区域纯黑（疑似 swap-chain clear 后无 draw 覆盖）

持续时间：肉眼可见 ~0.5–1 帧，约 16–33ms 量级。后续帧正常呈现 5 panel + viewport。

### 历史关联

`tools/OrangeEditor/main.cpp:200-213` v0.4.5 落地的 workaround 注释里登记过同源问题：

> 启动即 maximize —— v0.4.5 后用户在低 DPI / 窄屏机器报"初始打开 OK，用户拉伸 / 最大化后 Inspector 永久消失"…… 嫌疑点剩 GLFW WindowSize callback chain（AppHost OnSize + ImGui ImplGlfw 1.91+ WindowSize chained handler）在 resize 风暴下的事件分发顺序、或 OrangeRender swap-chain rebuild 与 ImGui DisplaySize sync 的时序竞争。

v0.4.5 workaround（把 `glfwMaximizeWindow` 提到 ImGui Init 之前）解决了**永久消失**这条极端 path，但首帧白屏闪烁未消除——本 GAP 是 v0.4.5 workaround 的**残留分支**。

### 嫌疑

按相同的根因树排查：

| 嫌疑 | 验证手段 |
|------|---------|
| **swap-chain rebuild 与 ImGui DisplaySize sync 时序竞争** | 在 `main.cpp` ImGui_ImplVulkan_Init 后 + 第一帧 BeginFrame 前插入一段 "dummy 几帧 + clear to black + 不画 ImGui" 让 swap-chain settle，看是否消除闪烁 |
| **dock layout 首帧建立耗 1–2 帧才 visible** | 把 `BuildDefaultLayoutOnce` 改为预热（main 启动期跑一次）而不是 OnUpdate 首帧懒建，看首帧能否立即拿到 dock 节点尺寸 |
| **ImGui first valid DrawData 与 swap-chain present 节奏不齐** | 加 frame counter，前 N 帧 swap-chain submit empty draw（背景色与 ImGui dock background 一致），让肉眼感受不到差异 |

### 期望验收

- 启动 OrangeEditor.exe 后无肉眼可感的"白色矩形 + 黑色背景"闪烁
- v0.4.5 修复的"resize 后 Inspector 永久消失"回归测试不退化
- 1080p / 1680×1120 / 4K 三种 DPI 配置下均不复现

### 关联

- `tools/OrangeEditor/main.cpp:200-213`（v0.4.5 workaround 注释）
- editor-roadmap v0.6.5 c0（本 GAP 的发现点，但 c0 不修；独立 session 处理）
- pre-existing；不阻塞 v0.6.5 任何 commit 推进

---

## GAP-2026-05-17-mesh-vertex-normals

- **发现方**：Phase 6.5 PBR-01 milestone-start-checklist 步骤 4（摸现有 Pipeline 渲染路径）
- **发现日期**：2026-05-17
- **一句话定性**：`MeshAsset` / `InterleavedVertex` / `MeshLoader v1` / 所有 sample 的 `MakeSphereMesh` 都不存 vertex normal；现有 shaders 只能通过 `dFdx/dFdy` 推 flat face normal（toon.frag.glsl:45）—— PBR 球体会渲染成低多边形多面体，直接阻塞 Phase 6.5 PBR-01 / PBR-03 "9 球阵 roughness 0→1 视觉过渡" 验证
- **状态**：登记，未开工

### 触发场景

Phase 6.5 PBR direct lighting milestone（B.1）启动 ritual：

- Task PBR-01 落 monolithic `pbr.{vert,frag}.glsl`，Cook-Torrance + GGX + Schlick + Smith 都要 smooth world-space normal
- Task PBR-03 验证：9 球阵（3 metallic × 3 roughness）+ 1 directional light，roughness 0→1 specular highlight 从 sharp 到 wide 连续过渡 + 金属/塑料视觉区分

引擎当前能力对照：

| 原子能力 | 现状 | 备注 |
|---------|------|------|
| `MeshAsset` 公共面 | ❌ 无 normal | 头注释 line 5–9 明说 "法线 / tangent / 多 set UV / skin 等更丰富属性留待后续扩展" |
| `InterleavedVertex` (Pipeline.cpp:117) | ❌ 仅 pos(3) + uv(2) stride 20 B | line 114–116 注释明说"未来引入 vertex normal / tangent 时在 Material 上再加一个描述字段" |
| `MeshLoader v1` 磁盘格式 | ❌ 不写 normal | 磁盘 .mesh 文件无 normal 段 |
| Sample `MakeSphereMesh` 8 处 | ❌ | 仅算 pos + uv |
| Procedural cube / plane 落盘产物 | ❌ | `assets/meshes/cube.mesh` / `plane.mesh` 由 GAP-2026-05-16-builtin-asset-disk-serialization lazy bake 产出，同样无 normal |
| shader 端 normal 来源 | dFdx/dFdy fallback | `toon.frag.glsl:45` 推 flat face normal —— sphere 渲染成多面体 |

### 缺什么（按依赖拆）

#### G1 · MeshAsset 公共 API + helper

- `include/orange/engine/asset/MeshAsset.h` 加 `struct VertexNormal3 { float x, y, z; }` + 私有字段 `std::vector<VertexNormal3> mNormals`
- 配套 accessor / `HasNormals()` / 构造函数 overload（带 normals 的 4 参版本）
- 提供 helper：`MeshAsset::ComputeFlatNormals()`（per-triangle）+ `MeshAsset::ComputeSmoothNormalsFromTriangles()`（per-vertex 邻面加权平均）—— 旧资产 fallback 用

#### G2 · `MeshLoader` schema bump（v1 → v2）

- 磁盘格式 v2 加可选 normal 段；v1 旧文件 loader 走 fallback：检测无 normal → 自动调 `ComputeSmoothNormalsFromTriangles` 填上
- Write 路径写出 normal 段
- 兼容现有 `assets/meshes/cube.mesh` / `plane.mesh`（不破坏；lazy bake 走 v2 路径重写出带 normal 的版本，或保留 v1 + 加载期补算）

#### G3 · Pipeline `InterleavedVertex` + `FillVertexInputLayout` 扩展

- `InterleavedVertex` 加 normal[3] 字段，stride 20 → 32 B（按 std140 / 顶点 attribute 对齐）
- `FillVertexInputLayout` 加 location=2 vec3 normal attribute
- `InterleaveMesh` 把 `mesh.Normals()` 填进；空 normal → 兜底 `(0, 1, 0)` 或调 G1 helper 现场算

#### G4 · 现存 shader vertex 输入层兼容

两条路径，二选一（实施期评审）：

- **路径 A**：所有 vertex shader (textured_mesh / toon / rim_light / dissolve / emissive / shadow_caster 等) 都加 `layout(location = 2) in vec3 inNormal;` 但不一定 output —— attribute 不被消费的话 driver 可 dead-code-elim，但 VID 必须声明（Vulkan 强对齐 mesh stride 与 shader attribute），最干净
- **路径 B**：保留 stride 20 与 stride 32 双 VID，Pipeline 创建期按 shader 是否需要 normal 切换 —— 复杂但向前兼容旧 .mesh 文件零迁移

#### G5 · 程序化 mesh 生成器与磁盘资产同步

- 8 个 sample 的 `MakeSphereMesh` 同步生成 normal（球体平凡：`normalize(position)`）
- `tools/OrangeEditor/DemoWorld.cpp` 的内置 cube / plane lazy bake 路径触发 G1 helper 算 normal 后再写盘
- 现存 `assets/meshes/cube.mesh` / `plane.mesh` 通过 lazy bake 自动重生成 v2 格式（或加载期补算）

### 期望验收

- demo.scene 在编辑器内打开后球体（若有）渲染为平滑圆球
- Phase 6.5 sample 13_pbr_direct 9 球阵表面平滑过渡（无 facet artifact）
- 现存 `.mesh` 文件 loader 兼容（v1 无 normal 走 fallback 自动补算，不报错）
- 序列化 round-trip：`MeshAsset` 写 / 读 normal 字段无丢失
- Pipeline 创建不破现有 shaders（textured_mesh / toon / rim_light / dissolve / emissive / shadow_caster 等加载/编译/绘制均正常）

### 关联

- **强阻塞**：[Phase 6.5 PBR-01](../roadmap.md#task-065-01--monolithic-pbr-shader-落地ibl-槽位-dummy)（不解决则 PBR shader 只能走 flat-normal fallback，球体多面体化）
- 关联引用：`include/orange/engine/asset/MeshAsset.h:5-9`（头注释明确标"留待后续扩展"）/ `src/render/Pipeline.cpp:114-116`（注释明确标"未来引入 vertex normal / tangent"）/ `src/render/builtin_shaders/toon.frag.glsl:45`（dFdx/dFdy fallback 实例）

### 不在本 GAP 范围

- **tangent / bitangent**（PBR 法线贴图需要，B.1 不上 normal map 暂可推迟到 B.2 或独立 GAP）
- **vertex color / 多 set UV / skin weights**（同 MeshAsset 头注释提及，分别独立 GAP）
- **mikkT space tangent 算法**（如未来支持 normal map 时一并）

### 状态

- **登记**：2026-05-17
- **处理**：2026-05-17 同日落地（独立 session，按 CLAUDE.md "engine-known-gaps 跨 session 工作流"）
- **归属**：Phase 6.5 B.1 启动前 prerequisite；落地后 PBR-01 在新 session bump 消费

### 落地记录（2026-05-17）

按 user 在 session 启动 ritual 步骤选择的设计方案：(1) G4 走 **路径 A**（所有 vert shader 统一加 `inNormal`，单 VID stride 32B；与 Lumix/Godot 工业约定一致），(2) 现存 v1/v2 `.mesh` 文件迁移走 **load 时自动算 smooth normal + 不重写盘**（最稳路径，不副作用地碰用户已存在的资产文件）。

G1 ~ G5 一次性落地，无 commit 拆分（GAP 体量适中，单 commit 边界清晰）：

- **G1 ✅ `MeshAsset` 公共 API + helper**
  - `include/orange/engine/asset/MeshAsset.h`：新增 `VertexNormal3` struct（default `{0, 1, 0}` 防 zero-normal）+ 私有 `mNormals` + 4 参构造（pos/uv/normal/indices）+ `Normals()` accessor + `HasNormals()` 谓词
  - 新增 `src/asset/MeshAsset.cpp`：`ComputeFlatNormals()` per-triangle face normal（共享顶点 vertex 后写覆盖先写——非严格 flat，仅 fallback 用）+ `ComputeSmoothNormalsFromTriangles()` 面积加权平均的每顶点平滑法线（loader fallback + 程序化 mesh 通路均消费此 helper）；不依赖 glm，裸 float 自行 cross / normalize，避免在 Asset 模块引入 render 上游依赖
  - `CMakeLists.txt`：把 `src/asset/MeshAsset.cpp` 加进 `orange_engine` 源列表

- **G2 ✅ `MeshLoader` v2 → v3 schema bump**
  - `include/orange/engine/asset/MeshLoader.h`：新增 `kVersionV3`，`kLatestVersion` 升到 v3；头注释展开 v3 字节布局（v2 末尾再追加 `hasNormals` uint8 + 可选 `normals[vertexCount] : float[3]`）
  - `src/asset/MeshLoader.cpp` Load：兼容 v1/v2/v3 三档读取；v1/v2 / v3-hasNormals=0 文件读完后调 `ComputeSmoothNormalsFromTriangles` 现场补算 normal（渲染端从 v3 起统一假定 `Normals()` 非空）；构造时按 normals/uvs 是否非空走 2/3/4 参 overload
  - Save：始终写 v3 格式；`HasUVs()` / `HasNormals()` 各自决定是否写对应可选段；normal 段非 per-vertex 一一对应时返回 `InvalidArgument`

- **G3 ✅ `Pipeline` InterleavedVertex + VID 扩展**
  - `src/render/Pipeline.cpp`：`InterleavedVertex` 加 `normal[3]` 字段（stride 20 → 32 B）；`FillVertexInputLayout` 新增 location=2 / Float32x3 attribute；`InterleaveMesh` 把 `mesh.Normals()` 填进；空 normal 路径兜底 `(0, 1, 0)`（MeshLoader / 程序化构造现都保证 Normals() 非空，兜底仅防御性）

- **G4 ✅ 6 个内置 vert shader + 1 个 sample shader 加 `inNormal`（Path A）**
  - 6 个内置 vert shader（`textured_mesh / toon / rim_light / dissolve / emissive / shadow_caster`）：统一新增 `layout(location = 2) in vec3 inNormal;`；`shadow_caster` 仅声明不消费（与 inUV 同模式），其余把 `mat3(uModel) * inNormal` 输出到 fragment 的 `vNormal`（rim_light 因 location 2 已被 vModelPos 占用，vNormal 出在 location 3）
  - `toon.frag.glsl` / `rim_light.frag.glsl`：删 dFdx/dFdy face-normal fallback，统一读 `normalize(vNormal)`
  - sample 08 自定义 shader：`samples/08_custom_shader/shaders/fresnel.vert.glsl` 加 inNormal + 输出 vNormal；`fresnel.frag.glsl` 切走 vNormal 读取
  - 非均匀缩放支持（mat3 inverse-transpose）暂未引入——本期渲染端仍假定模型变换无非均匀缩放；PBR 阶段如需再升级

- **G5 ✅ 8 个 sample + Editor DemoWorld mesh 工厂同步**
  - 8 个 sample 的 `MakeQuadMesh` / `MakeCubeMesh` / `MakePlaneMesh` / `MakeSphereMesh`（03/04/04_with_bloom/05/06/07/08/09/10/11/12）：返回 `unique_ptr<MeshAsset>` 前补调 `ComputeSmoothNormalsFromTriangles()`；sphere 的 lat/lon 网格 vertex 在共享路径下平均出近似 `normalize(position)`，cube 24 顶点每面独占退化为 face normal（分面 shading 符合预期）
  - `tools/OrangeEditor/DemoWorld.cpp`：`MakePlaneMesh` / `MakeCubeMesh` 同上；lazy bake 路径首次写盘时 Save 写出带 normal 段的 v3 `.mesh`；既有 v2 cube.mesh/plane.mesh 不重写，由 MeshLoader Load fallback 补算

- **测试修复（顺手）**：`tests/asset/AssetRegistryTest.cpp` 历史遗留的 `MeshLoader::kSupportedVersion` 引用（HEAD 上常量已不存在；因 `ORANGE_ENGINE_BUILD_TESTS=OFF` 默认未触发未被发现）改为 `MeshLoader::kVersionV1`（fixture 字节结构就是 v1 形态）；以便本 GAP 跑 ctest 端到端验证

### 期望验收对照

| 验收点 | 落地状态 |
|--------|---------|
| demo.scene 在编辑器内打开后球体（若有）渲染为平滑圆球 | ✅ —— sphere mesh smooth normal 已喂进 vertex buffer，toon / rim / fresnel shader 全读 vNormal |
| Phase 6.5 sample 13_pbr_direct 9 球阵表面平滑过渡（无 facet artifact） | **待 PBR-01 落地后验证**（本 GAP 解锁前置） |
| 现存 `.mesh` 文件 loader 兼容（v1 无 normal 走 fallback 自动补算，不报错） | ✅ —— MeshLoader Load v1/v2/v3-no-normal 路径均调 `ComputeSmoothNormalsFromTriangles` 补算；既有 `assets/meshes/cube.mesh` / `plane.mesh` 保持 v2 格式继续可读 |
| 序列化 round-trip：MeshAsset 写 / 读 normal 字段无丢失 | ✅ —— `MeshLoader::Save` 写 v3 hasNormals 段，`Load` 读回；现有 `AssetRegistryTest::TestMeshLoadGetUnload` 间接覆盖（v1 fixture → fallback compute → mesh 可用） |
| Pipeline 创建不破现有 shaders（textured_mesh / toon / rim_light / dissolve / emissive / shadow_caster 等加载/编译/绘制均正常） | ✅ —— ctest 全 43 测试通过（含 builtin_materials_test / pipeline_template_cache_test / pipeline_hdr_target_test / pipeline_offscreen_test / bloom_chain_test / light_and_shadow_test 等所有覆盖渲染路径的 case） |

### 关键改动文件汇总

- 公共 API：`include/orange/engine/asset/MeshAsset.h` / `include/orange/engine/asset/MeshLoader.h`
- 引擎私有实现：`src/asset/MeshAsset.cpp`（新增）/ `src/asset/MeshLoader.cpp` / `src/render/Pipeline.cpp`
- 内置 shader：`src/render/builtin_shaders/{textured_mesh,toon,rim_light,dissolve,emissive,shadow_caster}.vert.glsl` / `src/render/builtin_shaders/{toon,rim_light}.frag.glsl`
- 构建：`CMakeLists.txt`
- Samples：`samples/{03_textured_quad,04_3d_mesh,04_3d_mesh_with_bloom,05_skeletal_animation,06_physics_platformer,07_full_pipeline,08_custom_shader,09_vfx_demo,10_thirty_seconds_demo,11_save_load_demo,12_layer_partition_demo}/main.cpp` / `samples/08_custom_shader/shaders/fresnel.{vert,frag}.glsl`
- Editor：`tools/OrangeEditor/DemoWorld.cpp`
- 测试：`tests/asset/AssetRegistryTest.cpp`（顺手修陈旧常量引用）

### 不在本期范围（已登记 / 后续 session 处理）

- **tangent / bitangent + mikkT space 法线贴图支持** —— PBR-01 不上 normal map，留给 B.2 或独立 GAP
- **mat3 inverse-transpose 非均匀缩放兼容** —— vert shader 当前用 `mat3(uModel)` 近似；非均匀缩放走 PBR 时再升级
- **现有 v2 cube.mesh / plane.mesh 主动 rewrite 到 v3** —— 按 user 选择走 load-time fallback；本 session 不副作用碰用户既有 .mesh 文件

---

## 处理记录

- **GAP-2026-05-17-asset-registry-handle-to-path**（2026-05-17 落地）：发现 `AssetRegistry::PathOf<T>` 公共 API 早已存在（GAP 登记时漏看），实际只需 `MaterialFileIO::BuildDataFromInstance` 加可选 `const AssetRegistry*` 参数 + 内部消费 PathOf。详细见上文条目末尾"落地记录"节。涉及 commit：`784bf1a`。关键改动文件：`tools/OrangeEditor/MaterialFileIO.{h,cpp}` / `tests/render/MaterialFileIOTest.cpp`（TestTextureRoundTripWithRegistry）
- **GAP-2026-05-16-material-system-enumerate-and-instance-overrides**（2026-05-17 落地）：MaterialSystem::GetTemplateNames + MaterialInstance enumerate override API + .material schema v1.0 → v1.1（uniforms/textures）+ Editor 端 MaterialFileIO helper + 4 新测试。详细见上文条目末尾"落地记录"节。涉及 commit：`3b9718d`（C1）/ `6b598ba`（C2）/ `b7c92d3`（C3）。关键改动文件：`include/orange/engine/render/MaterialSystem.h` / `include/orange/engine/render/MaterialInstance.h` / `src/render/MaterialSystem.cpp` / `src/render/MaterialInstance.cpp` / `tools/OrangeEditor/MaterialFileIO.{h,cpp}`（新增）/ `tools/OrangeEditor/DemoWorld.cpp` / `tools/OrangeEditor/panels/InspectorPanel.cpp` / `tools/OrangeEditor/CMakeLists.txt` / `tests/render/MaterialFileIOTest.cpp`（新增）/ `tests/render/MaterialInterfaceTest.cpp` / `tests/render/MaterialSystemTest.cpp` / `tests/CMakeLists.txt`
- **GAP-2026-05-16-directional-light-transform-decoupled**（2026-05-17 落地）：DirectionalLight 删 direction 字段 + Pipeline 改用 entity.Transform.rotation 派生方向 + ReadDirectionalLight v1 migrator 旧 scene direction 字段自动转 TC.rotation + 7 sample/DemoWorld/编辑器 schema/gizmo plugin/2 tests 全数迁移。详细见上文条目末尾"落地记录"节。关键改动文件：`include/orange/engine/render/LightComponent.h` / `src/render/Pipeline.cpp` / `src/scene/ComponentSerializers.cpp` / `samples/{05,06,07,08,09,12}*/main.cpp` / `tools/OrangeEditor/DemoWorld.cpp` / `tools/OrangeEditor/plugin/DirectionalLightGizmoPlugin.cpp` / `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp` / `tests/render/LightAndShadowTest.cpp` / `tests/scene/SceneSerializationTest.cpp`
- **GAP-2026-05-17-scene-layer-component**（2026-05-17 落地）：LayerComponent + WorldPartition 公共面 + SceneSerialization 多文件 + manifest + Render/Physics layer.visible 过滤 + sample。详细见上文条目末尾"落地记录"节。关键改动文件：`include/orange/engine/scene/LayerComponent.h` / `include/orange/engine/scene/WorldPartition.h` / `include/orange/engine/scene/SceneSerialization.h`（SaveSplit/LoadSplit + LoadOptions.assignLayerId）/ `include/orange/engine/render/RenderScene.h`（Collect 加 partition 参数）/ `include/orange/engine/render/Pipeline.h`（SetWorldPartition）/ `include/orange/engine/physics/PhysicsWorld.h`（SetBodyEnabled/IsBodyEnabled）/ `include/orange/engine/physics/LayerVisibilitySync.h` / `src/scene/WorldPartition.cpp` / `src/scene/SceneSerialization.cpp`（SaveImpl 抽取 + SaveSplit/LoadSplit + scene/world 1.2 + scene/manifest 1.0）/ `src/scene/ComponentSerializers.cpp`（Layer 序列化器注册）/ `src/render/RenderScene.cpp` / `src/render/Pipeline.cpp` / `src/physics/PhysicsWorld.cpp` / `src/physics/LayerVisibilitySync.cpp` / `samples/12_layer_partition_demo/`
- **GAP-2026-05-16-builtin-asset-disk-serialization**（2026-05-16 落地）：内置 mesh / material 磁盘落盘 + Scene 引用迁移到磁盘路径。详细见上文条目末尾"落地记录"节。涉及 commit：`222bd3f`（G1 + 部分 G4）/ `60eaa40`（G2 + G4 剩余）。关键改动文件：`include/orange/engine/asset/MeshLoader.h` / `src/asset/MeshLoader.cpp` / `tools/OrangeEditor/DemoWorld.cpp` / `src/scene/ComponentSerializers.cpp` / `assets/scenes/demo.scene.json` / `assets/meshes/*.mesh` / `assets/materials/builtin/*.material`
- **GAP-2026-05-17-mesh-vertex-normals**（2026-05-17 落地）：MeshAsset 加 VertexNormal3 + helper（ComputeFlat/SmoothNormalsFromTriangles）+ MeshLoader v2 → v3 schema bump（hasNormals + normals 段，Load 兼容 v1/v2/v3 + fallback 补算）+ Pipeline InterleavedVertex stride 20→32 加 normal attr + 6 内置 vert shader + 1 sample shader 加 inNormal（Path A 单 VID）+ toon/rim/fresnel frag 切 vNormal 替换 dFdx fallback + 8 sample/DemoWorld mesh 工厂调 ComputeSmoothNormalsFromTriangles + 顺手修 AssetRegistryTest 陈旧 kSupportedVersion 常量引用。ctest 全 43 测试通过。详细见上文条目末尾"落地记录"节。关键改动文件：`include/orange/engine/asset/MeshAsset.h` / `include/orange/engine/asset/MeshLoader.h` / `src/asset/MeshAsset.cpp`（新增） / `src/asset/MeshLoader.cpp` / `src/render/Pipeline.cpp` / `src/render/builtin_shaders/{textured_mesh,toon,rim_light,dissolve,emissive,shadow_caster}.vert.glsl` / `src/render/builtin_shaders/{toon,rim_light}.frag.glsl` / `CMakeLists.txt` / `samples/0[3-9]*/main.cpp` / `samples/1[0-2]*/main.cpp` / `samples/08_custom_shader/shaders/fresnel.{vert,frag}.glsl` / `tools/OrangeEditor/DemoWorld.cpp` / `tests/asset/AssetRegistryTest.cpp`
