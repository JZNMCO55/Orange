# A2 · Stable EntityGUID 消费闭环 —— 设计提案

- 基准日期：2026-06-02
- 定位：`docs/maturity-roadmap.md` **Phase A 地基 · A2**（A1 Transform 层级传播之后的第二块地基）。本文是**设计提案 / 实施前的地基**，非决策——真正开工时据此开 **A2.0 ADR**（roadmap 标 "ADR-013 续"）。
- 方法：扎在真实序列化代码（file:line）后盘点现状 → 指出"顺序 persistent-id ≠ 稳定身份"的具体断裂面 → 给迁移设计 + 向后兼容 + **headless 安全先行 slice** + 待裁定开放问题 + 实施顺序。
- 关联：[[project_entityguid_core_landed]]（EntityGuid core，ADR-013）、`GAP-2026-05-30-prefab-asset-and-entity-guid`、`docs/b2.6-animator-clip-authoring-spec.md`（同款"spec 先行 + headless slice 先落"节奏）。

---

## 1. 现状盘点（grounded in code）

### 1.1 实体持久身份 = 顺序 int（0..N-1），**非** guid

`src/scene/SceneSerialization.cpp` `SaveImpl`（~140）：

- 收集所有 live entity，按 EnTT entity **index 升序**排序（剥 version bits），分配 `0..N-1` 的 `EntityToPersistentId idMap`（~193）。
- 每个 entity 写 `base/"id"` = 这个顺序 int（~228）。
- 跨实体引用（见下）全部用这个顺序 int 当 key，Load 时经 `PersistentIdToEntity` 表反查回新 World 的 Entity。

排序的目的（代码注释 ~149）是**字节稳定**：EnTT view 是 LIFO 迭代，不排序会让 Save→Load→Save 往返翻转 entity 数组、制造无业务变动的污染 diff。排序后 Source 与 Loaded World 输出相同字节序列。

### 1.2 Hierarchy 父子引用 = 顺序 persistent-id

`src/scene/ComponentSerializers.cpp` `WriteHierarchy`（~162）/ `ReadHierarchy`（~181）：

- `parent` / `firstChild` / `nextSibling` / `prevSibling` 四个 Entity 字段，写时 `PersistentIdOf(h->parent, ctx.entityToId)`（~173）转成顺序 int、读时 `EntityForPersistentId(...)`（~232）反查。
- `kInvalidPersistentId = -1`（~69）表示空链接。

→ **父子拓扑完全锚在"这一次 Save 的顺序编号"上**。

### 1.3 GuidComponent 已可序列化，但**只是普通可选 component**，不参与互引用

`ComponentSerializers.cpp` `WriteGuid`（~293）/ `ReadGuid`（~306）：guid 以字符串 `value`（`Core::Guid::ToString`）落盘，Load 原样读回。scene schema **1.10→1.11** 加的（`SceneSerialization.cpp` ~76）。

但：没有任何 `parent`/prefab 链接用 guid 当 key——guid 是"挂在实体身上的一个稳定标签"，**还不是序列化的引用主键**。

### 1.4 guid 是**零散**分配的，Save 不自动补全

`Scene::EnsureEntityGuids(World&)`（`EntityGuid.h`）是惰性补全入口，但调用点只有：
- `PrefabInstantiation.cpp:72` 实例化后 `ReassignEntityGuids`；
- `EntityTreePanel.cpp:722/792` duplicate/clone 后 `SeparateClonedIdentities`；
- 测试。

`SaveImpl` 签名是 `const World&`——**无法**在 Save 内 mutate 补 guid。结果：**只有经过 prefab / clone 的实体才有 GuidComponent**，从零搭的场景实体可能整场无 guid。→ guid 当前**不是普遍存在的稳定身份**，离"消费闭环"还差"普遍分配 + 当主键"两步。

### 1.5 Prefab 链接已用"路径 + instanceId"（字符串/数值稳定 key，非顺序 int）

`PrefabInstanceComponent`：`sourcePrefabPath`（字符串路径，跨会话稳定）+ `instanceId`（哪一次实例化的分组 key）。这是**已经做对**的一类稳定引用（不依赖顺序 int）——A2 要把"实例内 entity ↔ 模板内 entity"的逐实体锚定也升级到 guid（见 A2.2）。

---

## 2. 问题：顺序 persistent-id 在哪些场景断裂

顺序 int 在**单文件一次往返**内自洽（Save 写的 id 和 Load 读的 id 同一套）。它断裂在所有"身份需要跨这次 Save 存活"的场景：

1. **Re-save 后 id 漂移**：场景增删实体后再 Save，顺序 int 整体重排——任何外部记下"我引用 id=5"的东西（书签 / 外部映射 / 另一文件）全部错位。当前没有外部引用，所以没暴露；但 prefab override / PIE / 跨文件引用一旦引入立刻撞上。
2. **跨文件 / 跨 prefab 锚定**：prefab 实例要记"我这个 entity 对应模板里的哪个 entity"，模板和实例是两套独立的顺序 int 空间——无法用顺序 int 跨空间锚定，必须用模板内稳定 guid。
3. **跨会话引用**：保存"玩家上次选中了哪个 entity""任务系统盯着哪个 entity"需要跨进程重启稳定，顺序 int 不保证（取决于该次 Save 的实体集合）。
4. **re-import override**：DCC 资产重导后，要把用户在引擎里对某 sub-entity 的改动重新贴回新导入的对应 entity——需要稳定身份匹配，顺序 int 在重导后必变。

成熟引擎对策：Unity `GlobalObjectId` / fileID、Godot scene-unique `%Name` + 稳定 node path、Unreal `FGuid`、Lumix entity GUID——都把**稳定 GUID（或等价稳定 key）当持久引用主键**，顺序 index 只做运行时句柄。A2 = 把 OrangeEngine 拉齐到这条工业线。

---

## 3. A2 里程碑（roadmap）

| 里程碑 | 内容 | 跨仓 | headless 可测 |
|---|---|---|---|
| **A2.1** | scene 序列化以 EntityGuid 为持久身份（而非顺序 remap key）：Save 前普遍补 guid + 互引用（parent/sibling）用 guid 锚定 | 否 | 是 |
| **A2.2** | prefab 实例链接用 EntityGuid 锚定"实例 entity ↔ 模板 entity"（替/补 instanceId 的逐实体粒度） | 否 | 是 |
| **A2.3** | PIE world-clone 用 EntityGuid 保稳定引用（进 Play snapshot/Stop restore 跨 clone 身份不变） | 否 | 是 |

A2.1 是地基；A2.2 依赖 A2.1；A2.3（PIE）还依赖 B1，但 EntityGuid 稳定性这一半可先于 PIE 落。

---

## 4. 设计选项与推荐

### 选项 A：顺序 int 直接换成 guid 字符串当主键（一刀切）

`id` 字段从顺序 int 改成 guid 字符串；parent/sibling 全存 guid 字符串。

- ➕ 干净、单一主键。
- ➖ **破坏字节稳定的现有手段**（§1.1 的顺序排序前提）——写盘顺序仍可按 entity index 排序，但主键变长字符串、且历史文件全需迁移。
- ➖ 大爆炸：一次改全部互引用 + 全部现有 .scene/.prefab 文件，回滚困难。**不推荐一次性做**。

### 选项 B（推荐）：guid 当主键 + 顺序 int 降级为"文件内本地序号" + 双键过渡

分两层解耦：

1. **稳定身份主键 = guid**：Save 前 `EnsureEntityGuids` 普遍补全；每个 entity 记 `guid`。跨实体引用（parent/sibling、prefab 锚、未来跨文件引用）**全部用 guid**。
2. **文件内本地序号 = 顺序 int（保留）**：`id` 字段仍写顺序 int，**仅作"本文件内可读的紧凑局部编号"**（diff 友好、人读友好），**不再承担跨文件/跨会话身份**。Load 时本地序号和 guid 都建表，互引用解析**优先 guid、回退本地序号**（读旧文件）。

- ➕ **向后兼容**：旧文件无 guid → 互引用回退顺序 int（现状路径），Load 后 `EnsureEntityGuids` 补 guid，再 Save 即升级到 guid 主键。无需一次性迁移所有文件。
- ➕ **字节稳定保留**：顺序排序前提不动（§1.1），guid 是 entity 上确定性的额外字段。
- ➕ **增量可测**：每步（普遍补 guid → parent 双写 guid → 解析优先 guid → 移除 int 依赖）都能 headless round-trip 验证。
- ➖ 过渡期 parent 引用双键（guid + int）增加少量文件体积——可接受，且最终可在某个 major schema bump 去掉 int 回退。

**推荐选项 B**：与本仓既有"schema 加版本 + migrator、出厂即冻结"纪律（CLAUDE.md "Serialization and reflection"）一致，零破坏性。

### Schema 演进

scene schema **1.12 → 1.13**（minor，加 guid-based 引用，向后兼容读 1.12 的纯 int 引用）：
- HierarchyComponent 的 parent/firstChild/nextSibling/prevSibling **增写 `*Guid` 字符串字段**（与现有 int 字段并存）；读时优先 `*Guid`，缺则回退 int（旧文件）。
- 已 shipped 的 1.12 语义不改（int 字段保留原义）——符合"shipped schema 不改字段语义，加新版本/字段"。

---

## 5. Headless 安全先行 slice（可在大改 / GUI 前落地）

照 B2.6「改点 1 先行」先例（headless 可测的那部分先做先验），A2 也有几块**低风险、纯引擎、headless 可测**的先行件，能在不碰互引用主键迁移的前提下先落：

- **S1 · Save 前普遍补 guid（可选 SaveOption）**：给 `SaveOptions` 加 `ensureGuids`（默认开），在 Save 的**调用方**（非 const SaveImpl 内部）先 `EnsureEntityGuids(world)` 再 Save——把"guid 零散"变"guid 普遍"。纯 additive，旧消费者不传则行为不变。round-trip 测：补 guid → Save → Load → 每 entity 有 guid 且与 Save 前一致。
- **S2 · World guid→entity 查找 ✅ 已落地（2026-06-02，commit `b2cad7b`）**：`Scene::FindEntityByGuid(const World&, const Core::Guid&)`（`EntityGuid.h/.cpp`）——线性扫 `view<GuidComponent>` 返回首个 guid 匹配 entity，无匹配 / guid 非法(全 0) → `Entity::Invalid()`。**只读、决策中立**（不分配、不改序列化、与 A2.1 主键迁移正交），照本节"先落零风险"如期落。`scene_entity_guid_test::TestFindEntityByGuid`（命中/未命中/零 guid 判负/Reassign 后旧失效新命中/无 GuidComponent 不被零 guid 误匹配）。非 hot-path 线性足够，成热点再加缓存索引。**这是 A2.2/A2.3 的公共底座**。
- **S3 · guid round-trip 不变性测试加固**：锁住"Save→Load guid 逐字节稳定""ReassignEntityGuids 后旧 guid 不复现""EnsureEntityGuids 幂等"——部分已在 `EntityGuidTest`，补 scene 级 round-trip。

S1+S2+S3 都不动 parent 互引用主键（§4 的迁移留给 A2.1 正式开工 + ADR），但把"普遍分配 + 反查底座 + 不变性锁"三块地基先夯实，使 A2.1 的主键迁移只剩"parent 写/读切 guid"这一处集中改动。

---

## 6. 待裁定（A2.0 ADR open questions）

1. **顺序 int 是去是留**：选项 B 保留作本地序号；是否最终（某 major bump）彻底移除、只留 guid？建议留到 PIE/prefab override 真正消费 guid 后再评估，避免过早删除破坏 diff 友好性。
2. **guid 普遍分配的时机**：Save 前补（S1，惰性）vs CreateEntity 即时分配（World 即时）？现状刻意惰性（GuidComponent.h 注释："World 不知道 Scene 概念"）。建议维持惰性 + Save 前规整，除非 PIE 需要运行时即时稳定 guid。
3. **guid 字符串 vs 128-bit 二进制**：序列化目前 `Core::Guid::ToString`（字符串，可读）。体积/性能若成问题再评估二进制；0.x 阶段可读优先。
4. **prefab 锚定粒度**（A2.2）：实例 entity ↔ 模板 entity 的映射是"按模板内 guid"还是"按模板内顺序"？倾向模板内 guid（模板自身 Save 时已普遍补 guid）。
5. **跨文件引用的承载组件**：未来"实体引用另一文件实体"是新 `EntityRefComponent{fileGuid, entityGuid}` 还是字段级？留到真实需求（任务系统 / 关卡引用）拉动。

---

## 7. 实施顺序（建议）

1. **A2.0 ADR**：据本文裁定 §6，定选项 B + schema 1.13 迁移策略 + 顺序 int 去留。
2. **先行 slice（headless，可与 ADR 同 session 或紧随）**：S1（Save 前普遍补 guid 的 SaveOption）→ S2（FindEntityByGuid 索引）→ S3（不变性测试加固）。每步独立 commit + ctest。
3. **A2.1 主键迁移**：HierarchyComponent parent/sibling 增写 `*Guid` + 读优先 guid 回退 int；scene schema 1.13；round-trip 测（新写 guid、旧读 int、混合）。
4. **A2.2 prefab 锚定**：实例 ↔ 模板用 guid（消费 S2）。
5. **A2.3 PIE clone 稳定性**：随 B1 PIE 落地，进 Play 的 world-clone 保 guid 稳定（消费 S1/S2）。

每步 headless round-trip 可测；不涉 GUI、不跨仓。dogfood 仅在 prefab override / PIE 真消费时验"跨会话/跨 Play 引用不丢"。

---

## 8. 小结

A2 的本质：把**顺序 int（仅单次 Save 内自洽）**让位给 **guid（跨 Save/跨文件/跨会话稳定）**当持久引用主键，但用**双键过渡（选项 B）**保住字节稳定与向后兼容、零破坏。当前 guid 已能序列化、已有惰性分配 + clone 换新工具，**缺的是"普遍分配（S1）+ 反查底座（S2）+ 互引用切 guid（A2.1）"**。S1/S2/S3 是可立即落的 headless 先行件，A2.1 的主键迁移是集中、可测的单点改动。这块地基做扎实，prefab override（C1）、PIE world-clone（B1.3/A2.3）、re-import override 才有稳定身份可依。
