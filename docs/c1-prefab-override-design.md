# C1 · Prefab override / 嵌套 / 变体 —— 设计提案

- 基准日期：2026-06-03
- 定位：`docs/maturity-roadmap.md` **Phase C · C1**（内容规模化第一块）。本文是**设计提案 / 实施前地基**，非决策——开工时据此开 **C1.0 ADR** 裁定存储模型。
- 方法：扎在真实 prefab 代码（PrefabAsset / PrefabInstanceComponent / PrefabInstantiation）盘点现状 → 指出"全 bake 模型无 override 概念"的断裂 → 给存储模型选项 + headless 安全先行 slice + 待裁定问题 + 实施顺序。
- 关联：[[project_prefab_engine_mvp_landed]]（prefab MVP，ADR-015）、[[project_a2_entityguid_key_migration_landed]]（A2.1 主键 + A2.2 实例↔模板 guid 锚定，**本提案的直接地基**）、[[reference_orangeengine_models_gitignored_local_demo]]。

---

## 1. 现状盘点（grounded in code）

### 1.1 prefab 模板 = SaveSubtreeToString blob（形态 B）
`include/orange/engine/asset/PrefabAsset.h`：`PrefabAsset = { prefabName, templateBlob }`，`templateBlob` 是 `Scene::SaveSubtreeToString` 产出的 scene/world JSON 原文（字节保真）。`prefab/asset` 1.0 schema。**不二次解析**——实例化时直接喂 `LoadFromString`。

### 1.2 实例 = 全 bake 的完整实体（**无 delta、无 override 概念**）
`src/scene/PrefabInstantiation.cpp::InstantiatePrefab`：`LoadFromString(templateBlob)` → 克隆出**完整实体**（每个 component 都是模板的拷贝）→ `ReassignEntityGuids`（换新实例 guid）→ 挂 `PrefabInstanceComponent`。

→ **实例实体存进 scene 时是各自独立的完整数据**（`SaveImpl` 照常逐 component 写出全部字段）。实例与模板的唯一联系是标签：
- `PrefabInstanceComponent { sourcePrefabPath, instanceId, isInstanceRoot, templateEntityGuid }`（`templateEntityGuid` 是 A2.2 刚加的逐实体模板锚）。

### 1.3 断裂：全 bake 模型撑不起 override
全 bake 下：
- **改模板不传播**：模板加个 component / 改个默认值，已有实例**毫无反应**（它们是 bake 死的独立拷贝）。成熟引擎（Unity prefab / Godot scene inheritance）的核心价值"改模板批量更新实例"在此模型下不存在。
- **无 override 概念**：既然实例是全量数据，就没有"实例相对模板改了什么"的记录——蓝条标记（哪些字段被 override）、revert（丢弃 override 回模板值）、apply（把实例改动推回模板）全部无从谈起。
- **A2.2 已铺好锚**：`templateEntityGuid` + `FindEntityByGuid`（S2）让"实例 entity ↔ 模板 entity"可双向匹配——**override 的前提（知道每个实例实体对应模板哪个实体）已就位**，缺的是"存 delta + load 时重建"。

---

## 2. 存储模型选项

### 选项 A · 全 bake + 派生 override 视图（轻量 MVP）
保留现有"实例全 bake 存 scene"，**不改存储**；override 是**派生计算**——load 后把实例实体与模板实体（经 templateEntityGuid 匹配）逐 component diff，得到"被 override 的字段集"供蓝条显示 + revert（把某字段写回模板值）。
- ➕ 零存储格式改动、零向后兼容风险、增量；蓝条 / revert / apply 都能在 diff 之上做。
- ➖ **改模板不自动传播**（实例仍是 bake；只有未被 override 的字段可在"刷新实例"动作里手动从模板重拉）。即"propagation 靠显式 refresh，非 load 时自动"。
- ➖ diff 是运行时计算（需载模板 + 逐 component 比较）——非热路径可接受。

### 选项 B · template + delta（Unity 式，存储重构）
实例**不再全 bake**；scene 只存"prefab 引用 + override delta 列表（property path → value）"。load 时 = `LoadFromString(template)` + 逐条 apply delta 重建实例。
- ➕ 改模板**自动传播**（未 override 字段每次 load 从模板取最新）；override 是一等存储；最贴 Unity 心智。
- ➖ **存储格式大改**：实例序列化从"全 component"变"prefab 引用 + delta"，需新 schema + 迁移现有 bake 实例 + 改 SaveImpl 对实例实体的特判（不写全量、写 delta）。回滚困难、向后兼容复杂。
- ➖ delta property-path 寻址 + apply 顺序 + 新增/删除 entity（模板加子实体）的处理都更复杂。

### 推荐：**A 起步（MVP），预留 B**
- C1.1（override MVP）走**选项 A**：零破坏地拿到蓝条 + revert + 显式 refresh-from-template，先让 override **可见可操作**。和本仓"先 headless 可测的轻量件、避免大爆炸"一致（A2 选项 B、B2 spec-first 同节奏）。
- C1.2+ 若真需要"改模板自动传播"再评估迁选项 B（届时 A2 主键 + templateEntityGuid 锚已是现成地基）。
- **关键洞察**：选项 A 的核心是「**实例↔模板逐 component diff**」——这块**决策中立、headless 可测**，无论最终走 A 还是 B 都用得上（B 也要 diff 来生成 delta）。故它是 C1 的"S2 式安全先行件"。

---

## 3. Headless 安全先行 slice（决策中立，可在 ADR / 存储决策前落）

- **CS1 · 实例↔模板 override diff（核心先行件）**：给定一个实例实体（带 `templateEntityGuid`）+ 已载入的模板 world，**diff 出该实例相对模板被改了哪些 component / 字段**。实现可复用序列化：把实例实体的 components 序列化成 JSON、把对应模板实体的 components 序列化成 JSON、结构化 diff → 返回 override 字段集（component 名 + field path + 实例值 + 模板值）。**只读、决策中立**（不改存储、不预判 A/B），照 A2 的 S2/FindEntityByGuid 先例先落先验。headless 测：造模板 + 实例化 + 改实例某字段 → diff 精确报出该字段（未改字段不报）。
- **CS2 · refresh-from-template（选项 A 的传播动作，可选先行）**：把实例某实体的"未被 override 字段"从模板重拉（覆盖回模板值），override 字段保留。消费 CS1 的 diff（override 字段集）。headless 测：改模板默认值 → refresh → 实例未 override 字段更新、override 字段不动。

CS1 是 C1 一切的地基（蓝条/revert/apply/refresh 全消费它）。

---

## 4. Schema 策略
- 选项 A **不改 scene 存储**（实例仍全 bake）——override 是派生视图，**无 schema bump**（CS1/CS2 纯运行时计算）。蓝条/revert 是编辑器 UI 态。
- 若 C1 要**持久化 override 标记**（重开 scene 仍显示蓝条而不必每次 diff）：可给 `PrefabInstanceComponent` 加可选 `overriddenPaths` 字段（additive minor bump）——但 MVP 可先每次 load 时 diff 算（无需持久化），按需再加。
- 选项 B 才需大 schema 重构（留 C1.2+）。

---

## 5. 待裁定（C1.0 ADR open questions）
1. **存储模型**：选项 A（全 bake + 派生 diff，无自动传播）起步 vs 直接选项 B（template+delta，自动传播但大重构）？建议 A 起步。
2. **propagation 语义**：选项 A 下"改模板更新实例"是**显式 refresh 动作**（用户点"刷新实例"）还是 load 时自动？建议显式（避免 load 时隐式覆盖用户数据的惊吓）。
3. **override 粒度**：component 级（整 component override）vs field 级（单字段）？建议 field 级（蓝条精确到字段，Unity 同款），CS1 diff 本就到 field。
4. **override 持久化**：每次 load diff 算（不存） vs 存 `overriddenPaths`（重开免算）？建议 MVP 不存、运行时 diff，按需再加。
5. **嵌套 prefab / 变体**：本期是否做？建议**不做**（C1 MVP 只做单层 override；嵌套 prefab / prefab 变体是 C1.2/C1.3 独立 milestone）。
6. **新增/删除实体的 override**（模板加子实体 / 实例删子实体）：MVP 是否处理？建议**先只做字段值 override**（同 entity 集合），结构性 override（加/删 entity）留后续。

---

## 6. 实施顺序（建议）
1. **C1.0 ADR**：据本文裁定 §5，定选项 A 起步 + diff 粒度 + 持久化策略。
2. **CS1（headless，决策中立，可与 ADR 同 session 或紧随）**：实例↔模板 override diff（复用序列化 + structured diff，消费 A2.2 templateEntityGuid + S2 FindEntityByGuid）。独立 commit + ctest。
3. **CS2（headless）**：refresh-from-template（消费 CS1）。
4. **C1.1 编辑器 UI（dogfood-gated）**：Inspector 蓝条标记 override 字段 + 右键 revert（单字段回模板值）/ apply（推回模板，改 PrefabAsset blob + 重存 .prefab.json）+ "刷新实例" 动作。**GUI 交互 headless 测不到，dogfood**。
5. **C1.2+（独立 milestone）**：嵌套 prefab / 变体 / 选项 B 自动传播（若拉动）。

每步：CS1/CS2 headless round-trip 可测；C1.1 UI 必 dogfood（蓝条/revert/apply 手感靠真机）。

---

## 7. 小结
C1 override 的本质：现状"实例全 bake、与模板仅标签关联"撑不起 override（改模板不传播、无 delta 记录）。**A2.2 已铺好实例↔模板逐实体 guid 锚**，缺的是"diff 出 override（CS1）+ 据此做蓝条/revert/refresh"。推荐**选项 A（全 bake + 派生 diff）MVP 起步**——零破坏、headless 可测核心（CS1）先落，UI 层 dogfood-gated 后续。选项 B（template+delta 自动传播）留真拉动时评估。CS1 是决策中立的安全先行件，是 C1 一切的地基。
