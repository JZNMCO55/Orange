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

GAP 未落地前，编辑器侧 v0.2.5 范围内的临时修复候选见 OrangeEditor 验收清单 BUG-2 段（`docs/editor-v0.2.5-acceptance-checklist.md`）：

- **Y'** hardcode 表加 Dynamic Box 一行（最小妥协）
- **Y''''** EditorRenderLayer 加 `mPlayMaterialSnapshot` 通用 by-name snapshot/restore（约 20 行，不新增 hardcode 名字）—— 推荐

下个 milestone 评审决定走 Y' / Y'''' 还是直接等本 GAP 落地走 N

### 状态

- **登记**：2026-05-14
- **处理**：未启动；预估 1 个独立 OrangeEngine session 体量（G1 + G2 schema bump + Read/Write + 单元测试）
- **关联**：OrangeEditor v0.2.5 BUG-2（已诊断，未修复）；editor-roadmap.md v0.3 资产 / scene 编辑能力
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
- **处理**：未启动；预估 1 个独立 OrangeEngine session 体量（G1 + G2 + 单元测试）
- **关联**：OrangeEditor v0.3 c2 拆分为 c2a / c2b（详见上方临时方案）
- **归属**：待评审；候选挂到 `docs/roadmap.md` Phase 7+ 序列化扩展性深化 或独立小 task

---

## 处理记录

（空）
