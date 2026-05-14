# OrangeEditor v0.2.5 整骨 milestone 验收清单

- 基准日期：2026-05-13
- 适用范围：OrangeEditor v0.2.5 "架构整骨" milestone 各 commit 的 UI / 行为验收
- 设计意图：v0.2.5 内多 commit 高频迁移 schema，让用户在每 commit 后手工验收会打断节奏；改为
  把每个 commit 的验收点沉淀进本文档，按**小节点**统一跑回归——既保住开发节奏，也控制 bug
  累积窗口

## 回归节奏

**v0.2.5 整 milestone 完工后一次性大节点回归**。用户选择此节奏（开发节奏优先），代价是
bug 定位成本——一旦回归测试失败需要 `git bisect` 在 v0.2.5 全段 commit 内定位，准备好接
受这个成本。

回归后处理：
- 通过的清单条目划掉（在本文档内 `[ ]` → `[x]`）
- 失败的条目登记到对应 commit 节内 `### bugs` 段，包含「现象 / 复现步骤 / 候选根因」
- 修复后 push 新 commit（commit message 引用本文档 bug 编号），不重写历史

## 大节点回归的测试盲点提醒

按大节点回归容易漏的几类路径（v0.2.5 上下文）——回归时请刻意覆盖：

### 1. 多 component / 跨 commit 状态泄漏

每个 schema commit 单独看都没问题，组合起来才会暴露：

- **CommandStack 跨 commit 累积**：连续编辑 Transform → Name → ParticleEmitter → DirectionalLight → Undo / Redo 多次。验证命令栈在不同 commit 引入的 schema 字段之间正确切换 fieldKey、不串味
- **Euler cache 在多触发路径下的 invalidate**：编辑 Transform.rotation → 选别的实体 → 回来 → 改 Transform → Remove Transform → Undo → Redo。每条路径都要不残留旧 Euler 值
- **rename buffer 与 schema String 互不串味**：Entity Tree 内联重命名（仍走 `RenameCommand`）与 Inspector Name section schema 路径（走 `SetFieldValueCommand<std::string>`）写同一个 NameComponent。先 Tree 改 → 再 Inspector 改 → Undo 几次，验证两条路径 Merge 不冲突

### 2. Coalesce 边界

`SetFieldValueCommand` 的 Merge 走 entity + fieldKey 双键匹配。schema 通用路径靠 builder
拼接 `"<typeName>.<propName>"` 形成 fieldKey。容易漏测：

- **交错编辑**：编辑字段 A → 编辑字段 B → 回到字段 A → Undo 一次只回滚最后一次 A 编辑（而不是把两次 A 合成一条）。如果 fieldKey 拼接有 bug 会触发
- **不同 entity 同字段名**：选 entity X 编辑 Transform.position → 选 entity Y 编辑 Transform.position。Undo 应只回滚 Y，不影响 X

### 3. Save → 重启 → 加载 round-trip

Scene 序列化走引擎 `Read` / `Write`，不经 schema。但 schema 注册的字段集与序列化字段集
**必须一致**——schema 漏一个字段，Inspector 看不到，但 Save / Load 仍保留它（用户感知
"改不动的字段"）。回归路径：

- Save 当前 demo scene → 关闭 editor → 重启 → 加载 → 全字段值与保存前一致
- 特别关注 ParticleEmitter（14 字段，覆盖最广）、RigidBody（含 enum）、Transform（含 quat→Euler 缓存）

### 4. Play / Pause / Stop 期间 schema Inspector 的 disable

`InspectorPanel.cpp` 顶层有 `ImGui::BeginDisabled(!canEdit)` 包裹整段，按
`mHost.scene.playState == PlayState::Edit` 启用。验证：

- Play 期间所有 schema 字段灰显且不能编辑
- Pause 期间同上
- Stop 后立即可编辑，且 cmdStack 状态保持（不被 play snapshot restore 清空）

### 5. demo scene 完整覆盖

`SeedDemoWorld` 或 `assets/scenes/demo.scene.json` 内 13 个实体：

- 5 种内置材质（textured / toon×3 / rim_light / dissolve / emissive）→ Renderable 段（c7 迁后）
- DirectionalLight 含 `castsShadow` → c3 段
- 2 个 ParticleEmitterComponent（火焰 + 萤火）→ c6 段
- 2.5D EditorCamera 默认值

每个实体都点一遍，所有 schema 段都展开看一眼。

### 6. Add Component 菜单（c10 之后）

`+ Add Component` 当前是 hardcode `if/else` 列表。c10 改 schema 注册表枚举驱动后，验证：

- 菜单出现的项 = schema 注册了 `.Addable()` 的 component（不多不少）
- 添加后 Inspector 立即显示新 component 的 schema 段（无需切换实体刷新）

### 7. git bisect 准备

大节点回归发现 bug 后，准备 `git bisect` 区间 `94bd463..HEAD`（c3 起到 milestone 完工
commit），每个步骤用对应的 commit 验收点作为 reproducer。回归前先**确认 c3 状态可
build / 运行**（即 bisect 区间端点干净），避免 bisect 中途撞到无关 broken commit。

---

## 小节点回归路径（每次小节点都跑一遍）

- [✅] Editor 启动无 crash；自动加载 `assets/scenes/demo.scene.json` 或回退 `SeedDemoWorld`
- [✅] 选中任意实体 → Inspector 显示该实体所有挂着的 component header (component header如果指的是component的名称的话Renderable之类的，那这条可以过)
- [✅] 任改一个标量字段 → `Ctrl+Z` 回滚 → `Ctrl+Y` 重做，三步值一致
- [-] Save 当前 scene → 关闭 → 重启 editor → 加载，所有字段保持 (###bug, Save 选项始终置灰)
- [-] Play → Pause → Stop 状态机走通；Stop 后世界状态回到 Play 前 (###bug, play 后掉落物的由原来无贴图变为有棋盘状贴图)
- [✅] 右键任意可移除 component header → Remove Component → component 消失；后续 +Add Component 菜单出现该项 → 选中后 component 重新挂上（值是默认）

---

## 已发现的 milestone-level bug（汇总）

小节点回归阶段已经在小节点路径上暴露、但属于"跨 commit / 编辑器整体"层级的 bug——
单独提到这里登记便于 milestone 完工前集中修。每条修复后回到对应小节点路径项把 `[-]`
改回 `[ ]` 重新走、通过后划 `[✅]`。

### BUG-1 File > Save 菜单项始终置灰

- **现象**：菜单栏 `File > Save` / `Save As` 项不论编辑器在什么状态下都灰显，无法保存
  当前 scene；用户因而无法走"Save → 关闭 → 重启 → 加载"小节点路径
- **影响范围**：阻塞小节点路径的 Save round-trip 验收，也阻塞 c14 "场景切换不崩" 的
  New Scene / Open Scene 手动覆盖
- **候选根因**（待确认）：Save 菜单项的 `enabled` 判定可能挂在某个尚未实现 / 始终
  返回 false 的脏标记上；也可能是 ImGui 菜单代码里硬写了 `enabled=false`
- **复现步骤**：启动 editor → 加载 demo scene → 改任意字段 → 打开 File 菜单 → Save 灰显
- **修复后回归**：勾掉小节点路径第 4 条 + c14 回归补"切场景不崩"路径

### BUG-2 Play 后场景几何贴图被污染为棋盘格

- **现象**：编辑器进入 Play Mode → Pause/Stop 回到 Edit Mode 后，原本"无贴图"
  （默认材质 / 纯色 fallback）的掉落物 / 几何体显示为**棋盘状贴图**；属于 Stop 后世界
  状态没有完整 restore 到 Play 前的快照
- **影响范围**：阻塞小节点路径的 Play/Pause/Stop 状态机验收，也是 Play snapshot/restore
  路径污染 Renderable.materialInstance 的征兆
- **候选根因**（待确认）：Play 进入时的 world snapshot 没有完整复制 Renderable 的
  `materialInstance` 字段（裸指针）；Stop restore 时被 fallback 路径替换为"missing
  texture 棋盘格" debug 材质
- **复现步骤**：启动 editor → 加载 demo scene → 观察某个无贴图实体（如默认 Renderable）
  → Play → Stop → 同一实体显示棋盘格
- **修复后回归**：勾掉小节点路径第 5 条 + c14 回归补"Play/Pause/Stop 路径行为不变"

### BUG-3 / BUG-4：demo scene 实体覆盖缺口（验收前置条件）

不是 bug，是**验收前置条件缺失**——按"剔除手改 scene 才能观测的项"原则，这些缺口
让对应功能段无法在编辑器里观察，登记到这里以便 v0.3 demo 整改时一并补齐。

- **BUG-3 demo scene 无 Animator 实体**：当前 `assets/scenes/demo.scene.json` /
  `SeedDemoWorld` 内 13 个实体均未挂 `AnimatorComponent`，c9 Animator section /
  ReadOnly 控件行为无法在 editor 内直接验
- **BUG-4 demo scene 无 Box / Polygon / EdgeChain shape 的 Collider 实体**：当前 demo
  内 Collider 实体仅 CircleDesc，c8 三个非 Circle shape 段、跨 shape coalesce 无法在
  editor 内直接验
- **修复方式**：v0.3 milestone 开工 ritual 时把"demo scene 覆盖完整性"作为前置任务
  补齐——加 1 个 Animator 实体（procedural 后端最小载体即可）+ 3 个 Collider 实体
  分别用 Box / Polygon / EdgeChain shape；不在 v0.2.5 内做（避免顺手 hack）

---

## Commit 5：Name / Transform / Hierarchy schema 迁移

引入 `PropertyType::EntityRef`；`PropertyType::Quat` case 从 placeholder 改 Euler-cache 真实实现；
`DrawComponentSchemaSection` Remove 后统一 invalidate `transformEulerCacheEntity`。

### 验收点

- [✅] **Name section**
  - [✅] 显示一个无 label 的 InputText 占满整行
  - [✅] 连续输入多字符 → `Ctrl+Z` 一次回到原名（Merge coalesce）
  - [✅] Entity Tree 与 Inspector 两个 InputText 同时观察同 entity 时不串味（rename buffer 隔离）

- [✅] **Transform section**
  - [✅] Position / Rotation (°) / Scale 三行 DragFloat3
  - [✅] Rotation Euler 拖动平滑，gimbal lock 附近无数字跳变(gimbal lock 这个指的是？)
  - [✅] 编辑 Rotation 后 Undo → Inspector 显示值回到编辑前
  - [✅] 切换不同实体 → Rotation 缓存按新实体的 quat 重算
  - [✅] 右键 Transform header → Remove Component → Undo 不 crash
  - [✅] Remove Transform → 再 Add Transform → Rotation 字段显示默认 (0,0,0)，无 cache 残留

- [✅] **Hierarchy section**
  - [✅] 4 行 EntityRef 显示：`Parent: #<id>` / `Parent: (none)` 等
  - [✅] 不出现 Remove Component 右键菜单（无 Removable）
  - [✅] +Add Component 菜单不包含 Hierarchy（无 Addable）
  - [✅] 视觉降级：v0.1 期 `(edit by drag-drop in Entity Tree)` 提示已删除——确认可接受

### bugs
（无）

---

## Commit 6：ParticleEmitter schema 迁移 + 3 个 schema 扩展

引入 `FieldNested<ParentPtr, ChildPtr>`（双 NTTP 嵌套）/ `FieldCustom<T>(getFn, setFn)`（任意
mapping）/ `PropertyAttributes::groupSeparator`（`ImGui::SeparatorText`）。

### 验收点

- [✅] **Particle Emitter section**（在 demo scene 火焰 / 萤火 emitter 实体上）
  - [✅] 顶层字段：`Emitting` checkbox / `Emission Rate (/s)` DragFloat
  - [✅] 7 个 SeparatorText 分组顺序：Lifetime → Spawn Offset → Initial Velocity → Forces → Color curve → Size curve → Pool
  - [✅] 14 字段全显示且顺序与 v0.1 一致
  
- [ ] **嵌套字段（FieldNested）行为**
  - [✅] 编辑 `Emission Rate` → Undo 回滚（验证 desc.X 嵌套写入正确）
  - [✅] 编辑 `Gravity` (Vec2) → 拖动连续生效
  - [✅] 编辑 `Max Particles` → DragScalar U32 控件正常工作（v0.1 期是 DragInt → cast，schema 路径直接 UInt）

- [ ] **自定义字段（FieldCustom）行为**
  - [✅] `Color Start RGB` 走 ColorEdit3（vec3 + isColor）
  - [✅] `Color Start Alpha` 走 DragFloat 无上限——拖到 2.0 / 5.0 触发粒子明显发光（bloom 拾取）
  - [✅] Color End RGB / Alpha 同上
  - [✅] 编辑 Color Start RGB 后 Undo → RGB 回到编辑前，Alpha 不被影响
  - [✅] 编辑 Color Start Alpha 后 Undo → Alpha 回到编辑前，RGB 不被影响（验证 4 个虚拟字段独立 coalesce）

- [ ] **视觉分组（GroupSeparator）**
  - [✅] 7 个 SeparatorText 视觉与 v0.1 一致（同款文字、位置）

- [ ] **Add / Remove**
  - [✅] 右键 Particle Emitter header → Remove Component → 粒子停止
  - [✅] Undo → component 恢复 ⚠ 实际上 Remove 走 schema.remove + cmdStack.Clear，Undo 无效——这是 v0.1 同款行为，确认仍是 expected
  - [✅] +Add Component 菜单包含 Particle Emitter（如 v0.1 行为一致）

### bugs
（无）

---

## Commit 7：Renderable schema 迁移

把 `DrawInspectorRenderable`（约 60 行 hardcode）迁到 schema 路径。`mesh`
（`AssetHandle<MeshAsset>`）/ `materialInstance`（`MaterialInstance*` 裸指针）两
字段不注册到 schema（同 RigidBody.handle 路径，等 ReadOnly attribute 或
`PropertyType::AssetHandle` 引入后还原）；`visible` / `castsShadow` 走通用
Bool 路径。本 commit 不引入新机制。

### 验收点

- [ ] **Renderable section**（demo scene 任意带几何的实体）
  - [✅] `Visible` checkbox 可勾 / 取消，几何相应隐 / 显
  - [✅] `Casts Shadow` checkbox 可勾 / 取消；hover 显示 tooltip 文本与 v0.1 一致
  - [✅] **视觉降级**：v0.1 期两行 `Mesh handle: <id>` / `MaterialInstance: <ptr>` 调试信息**不再显示**——确认可接受

- [ ] **Add / Remove**
  - [✅] 右键 Renderable header → Remove Component → 几何消失
  - [✅] `+Add Component` → `Renderable` 菜单项**仍可用**（仍走 InspectorPanel 内 hardcode 路径，预绑 cubeMesh + defaultMaterial）；从菜单加挂的 Renderable 立即显示白色立方体
  - [✅] 验证 schema 的 `.Removable()` 与 hardcode `+Add` 路径**互不冲突**：Remove → Add → Remove → Add 多次循环正常

- [ ] **跨 commit 状态**（验证 c5 / c6 未受影响）
  - [✅] 切换实体看 Inspector：Name / Transform / Hierarchy / DirectionalLight / Renderable / RigidBody / Collider / ParticleEmitter / Animator header 顺序未变

### bugs
（待大节点回归后填）

---

## Commit 8：Collider schema 迁移

把 `DrawInspectorCollider`（~135 行 hardcode）迁到 schema 路径。引入两个新的 schema
基础设施：`PropertyAttributes::visibleIf` + `Builder::VisibleIf(pred)` 链式 API，以及
`Builder::Group(separator, pred)` 入口（注册纯 GroupSeparator-only header 段）。`shape`
（`std::variant<CircleDesc / BoxDesc / PolygonDesc / EdgeChainDesc>`）的 4 个 alternative
通过 visibleIf 互斥显示——Circle / Box 各 2 字段、Polygon / EdgeChain 仅占位 header。

### 验收点

- [✅] **Collider section 通用字段**（任意持 Collider 的实体，可用 `samples/06_physics_platformer` 或自建）
  - [✅] `Density` DragFloat（无 clamp）
  - [✅] `Friction` DragFloat（clamp 0..1）
  - [✅] `Restitution` DragFloat（clamp 0..1）
  - [✅] `Is Sensor` checkbox
  - [✅] 上述 4 字段在**任何 shape 下**都显示且可编辑

- [ ] **Circle shape 段**
  - [✅] 实体 shape = CircleDesc 时：显示 `SeparatorText "Shape: Circle"` + `Radius` + `Center`
  - [✅] 不显示 Box / Polygon / EdgeChain 任一段
  - [✅] 编辑 Radius / Center → 拖动连续生效 + Undo 回滚

> **Box / Polygon / EdgeChain shape 段验收不在本清单**：切换 shape 需手改 scene
> 文件 / `SeedDemoWorld` 代码，不是 Inspector 操作（参见下方 "shape 类型切换控件"
> 段确认 Inspector 无切换入口）。这三个 shape 的 schema 段渲染正确性、跨 shape
> coalesce 行为留待 v0.3 资产 / scene 编辑能力上线后，在该 milestone 的"Collider
> shape 编辑"功能点段内统一验。

- [✅] **字段顺序变更（视觉降级）**
  - [✅] v0.1 期顺序：shape 段 → 通用字段；c8 改为：通用字段 → shape 段
  - [✅] 确认新顺序视觉上可接受

- [ ] **shape 类型切换控件**
  - [✅] Inspector 内**无** shape 类型切换 Combo / 按钮 / 任何入口（与 v0.1 deliberately not implemented 行为一致）
  - [✅] 切换 shape 需要走代码 / 场景文件，不是 Inspector 操作

- [ ] **Add / Remove**
  - [ ] 右键 Collider header → Remove Component → component 消失；cmdStack 被 Clear（破坏性操作）
  - [ ] `+ Add Component` → Collider 菜单项**仍可用**（走 InspectorPanel 内 hardcode 路径，添加默认 `ColliderComponent{}`，shape 默认是 CircleDesc）
  - [ ] 添加后 Inspector 立即显示新挂的 Collider 段，Circle shape 段可见

- [ ] **跨 commit 状态**（验证 c5 / c6 / c7 未受影响）
  - [ ] 切换实体看 Inspector：Name / Transform / Hierarchy / DirectionalLight / Renderable / RigidBody / Collider / ParticleEmitter / Animator header 顺序未变（**c8 把 Collider 位置由"自定义函数调用"改为"schema 段"，但视觉位置不变**）

### bugs
（待大节点回归后填）

---

## Commit 9：Animator schema 迁移

把 `DrawInspectorAnimator`（~15 行 hardcode）迁到 schema 路径。同步引入
**`PropertyAttributes::readOnly` + `Builder::ReadOnly()`** —— 让 String 字段
走"label: value"形式的 TextDisabled 展示路径（无 InputText、无命令推送）。
AnimatorComponent 仅注册 1 个 String 字段（IAnimator::BackendName() 字符串），
不 Addable / 不 Removable（IAnimator 抽象类，无默认构造路径；v0.1 同款）。

**收尾里程碑**：c9 完成后所有 9 个内置 component 全数 schema 化，
`EditorRenderLayer` 内 `DrawInspectorXxx` 路径整体清除。

### 验收点

> **Animator section / ReadOnly 控件行为段不在本清单**：当前 demo scene 内无任何
> 挂 `AnimatorComponent` 的实体（参见下方 bugs 段登记），要观察 Animator 段渲染需
> 借 `samples/05_dragonbones_demo` 或手编 demo scene 加挂 Animator 实体——属编辑器
> 外操作。Animator section 渲染、ReadOnly TextDisabled 视觉、空 animator unique_ptr
> 防御等留待 demo 补 Animator 实体后（v0.3 候选）的"Animator 字段"功能点段内统一验。

- [ ] **Add / Remove**（与 demo 是否有 Animator 实体无关，可在任意实体上验）
  - [ ] 任选一实体（无论是否挂 Animator）右键其 component header → 上下文菜单不出现"Remove Animator"项（schema 未 `.Removable()`）
  - [ ] 任选一实体点 `+ Add Component` → 菜单内**无** Animator 项（schema 未 `.Addable()`；与 v0.1 期 InspectorPanel +Add popup 显式跳过 Animator 行为一致）

- [ ] **跨 commit 状态**（验证 c5 ~ c8 未受影响，与 demo 是否有 Animator 实体无关）
  - [ ] 切换实体看 Inspector：Name / Transform / Hierarchy / DirectionalLight / Renderable / RigidBody / Collider / ParticleEmitter header 顺序未变
  - [ ] 其他 component 的 String 字段（如 Name）未受 readOnly 引入影响——未挂 readOnly 的字段仍走 InputText 编辑路径，无任何 regression

### bugs
（无；"demo scene 无 Animator 实体"是验收前置条件缺失而非 c9 bug，已挪到 milestone bug 段 BUG-3）

---

## Commit 10：Add Component 菜单改 schema 注册表驱动

把 `InspectorPanel.cpp` 内 ~50 行 hardcode +Add Component if/else 列表替换为 schema
注册表迭代；同时把上半段 9 个 `schemaReg.Find<...>() → DrawComponentSchemaSection`
显式分派替换为 `Orange::Editor::Schema::DrawEntityViaSchemas(mHost, e)` 单调用。

同步引入 schema 基础设施扩展：
- `ComponentSchema::AddFn` 签名改 `void (*)(EditorHost&, Entity)`（原 `void (*)(World&, Entity)`），
  让 add 路径能访问 editor-side 资源（典型：Renderable 预绑 cubeMesh + defaultMaterial）
- `Builder::AddableWith(customFn)` 新增——caller 提供 capture-less lambda 注入自定义 add 路径

修正 v0.2.5 c5 / c7 的两处 Addable 缺失：
- Transform schema 加 `.Addable()`（v0.1 期 +Add 菜单实际包含 Transform，c5 注册时误标）
- Renderable schema 加 `.AddableWith(...)` 还原 v0.1 期挂 Renderable 时预绑 cube + defaultMaterial 行为

**收尾里程碑**：InspectorPanel.cpp 内不再 mention 任何具体内置 component 类型；EditorRenderLayer
也不再持有 `ComponentHeader` helper（schema/SchemaInspector.cpp 内 `ComponentHeaderLocal` 承担同等
角色）。游戏侧自定义 component 通过 ComponentSchemaRegistry 同款注册即可自动出现在 Inspector +
+Add 菜单——v0.3 game-side schema 注册落地后**无需改 editor 一行**。

### 验收点

- [ ] **+Add Component 菜单内容（按 schema registry 枚举驱动）**
  - [✅] 在未挂任何 component 的新建实体上点 +Add Component → 菜单包含 6 项：
    Transform / Directional Light / Renderable / RigidBody / Collider / Particle Emitter
  - [✅] 菜单**不**包含：Name（无 Addable）、Hierarchy（无 Addable）、Animator（无 Addable）
  - [✅] 在已挂某 component 的实体上 +Add Component → 该 component 不再出现在菜单内
    （schema.has 守卫）

- [ ] **Transform 重新加挂**（c5 oversight 修正）
  - [✅] 在某实体上右键 Transform header → Remove Component → Transform 段消失
  - [✅] 点 +Add Component → 菜单出现 `Transform` 项
  - [✅] 选中后 Transform 默认值（position=0 / rotation=identity / scale=1）出现在 Inspector

- [ ] **Renderable 预绑还原**（c7 deferred 兑现）
  - [✅] 在某未挂 Renderable 的实体上 +Add Component → 选 `Renderable`
  - [✅] 立即在 Scene viewport 看到一个白色立方体（cubeMesh + textured material），
    **不是**隐形空 Renderable
  - [✅] Inspector 内 Renderable 段显示 `Visible=true` / `Casts Shadow=true`
  - [✅] Remove → +Add 重复多次，每次都正确预绑

- [ ] **AddableWith 抽象 + Addable 默认路径**
  - [✅] DirectionalLight / RigidBody / Collider / ParticleEmitter / Transform 走默认 Addable，
    挂上后 component 字段 = 各自默认构造值
  - [✅] Renderable 走 AddableWith，挂上后 mesh / materialInstance 不是默认空值

- [ ] **AddFn 签名变更副作用**
  - [✅] 现有 cmdStack.Clear() 行为保留：+Add 后 Undo 不能回滚（破坏性操作历史清零）
  - [✅] 切换 entity / Play Mode / Save / Load 等路径未受 AddFn 签名变更影响

- [ ] **Inspector 上半段统一为 DrawEntityViaSchemas**
  - [ ] 9 个 schema 段渲染顺序与 c9 完工时一致：Name → Transform → Hierarchy →
    DirectionalLight → Renderable → RigidBody → Collider → ParticleEmitter → Animator
  - [✅] 任一 component 在 entity 未挂时不画空段（schema.has 守卫）
  - [✅] 切实体后 Inspector 立即刷新到新 entity 的 component 集合

### bugs
（待大节点回归后填）

---

## Commit 11：`IEditorInspectorPlugin` 接口声明 + `EditorHost` plugin registry

**本期范围**：仅声明抽象基类 + EditorHost 注册表字段；**不**实现真实 plugin、
**不**集成进 SchemaInspector。v0.3 起出第一个真实 plugin case（候选：Animator
mini-preview / Material 缩略图）+ SchemaInspector 调度路径。

设计参考 Godot `EditorInspectorPlugin`（多档钩子）/ Lumix `PropertyGrid::IPlugin`
（单 onGUI 钩子），落地 begin/end 两档最小集——property-level 拦截
（parse_property）等真实 case 浮出再加。

> 说明：本 commit 仅声明接口 + 加空注册表字段，编辑器上**无任何可观察的视觉变化**。
> 接口存在性 / 签名 / 前向声明纪律 / `inspectorPlugins` 字段未被 push_back 等代码层
> 验收由 `scripts/check_invariants.py` + code review 把关，不作为编辑器验收项。

### 验收点

- [ ] **回归式行为不变**
  - [✅] 编辑器启动 + 自动加载 demo scene + 选实体 + Inspector 各 component 段渲染完整
  - [✅] 行为与 c10 完成态完全一致（无任何视觉变化，因 plugin 注册表为空且 SchemaInspector
    本 commit 不查询 plugin）

### bugs
（待大节点回归后填）

---

## Commit 12：`IEditorGizmoPlugin` 接口声明

**本期范围**：仅声明抽象基类 + EditorHost 注册表字段；**不**实现任何 gizmo plugin、
**不**集成进 viewport overlay、**不**定义 `GizmoContext` 字段（v0.4 决定）。与 c11
完全对偶。

设计要点：
- **Plugin 仅承载 component-specific overlay**（DirectionalLight 方向箭头 / ParticleEmitter
  spawn box / Camera frustum / 游戏侧自定义 gizmo）。内置 Transform translate / rotate /
  scale gizmo **不**走 plugin 路径（v0.4 走专门的 TransformGizmo 子系统）
- **`GizmoContext` 前向声明**：v0.4 定义具体字段（viewport 矩阵 / ImDrawList / picking
  ray / handle 命中槽），plugin 头 ABI 在 v0.4 加 context 字段时**无需变更**

> 说明：与 c11 对偶，本 commit 仅声明接口 + 加空注册表字段，编辑器上**无任何可观察
> 的视觉变化**。接口存在性 / 签名 / `GizmoContext` 仅前向声明 / `gizmoPlugins` 字段未被
> push_back 等代码层验收由 `scripts/check_invariants.py` + code review 把关，不作为
> 编辑器验收项。

### 验收点

- [ ] **回归式行为不变**
  - [✅] 编辑器启动 + 自动加载 demo scene + 选实体 + Inspector 渲染 + viewport overlay
  - [✅] 行为与 c11 完成态完全一致（无任何视觉变化，因 gizmoPlugins 注册表为空且 viewport
    本 commit 不查询 gizmo plugin）

### bugs
（待大节点回归后填）

---

## Commit 13：CommandStack `BeginGroup` / `EndGroup` + `MergeMode` 三档

引入命令组（atomic grouped commands）+ 三档合并策略。

设计参考 Lumix `WorldEditor::beginCommandGroup()`（group 内多条命令一次 undo
原子撤销）+ Godot `UndoRedo` 的三档 `MergeMode`（DISABLE / ENDS / ALL）。
本期是基础设施落地——v0.4 Gizmo 拖动时调度的"一次 drag = group 内多条
SetField" 用例，本 commit 提供 API，v0.4 实际消费。

**保后兼容**：Push 在未 BeginGroup 时行为零变化；现有 Inspector DragFloat
coalesce 路径不受影响。

> 说明：v0.2.5 内**没有任何 caller 实际调用 BeginGroup / EndGroup**（gizmo 拖动用例在
> v0.4 落地）。因此 group / MergeMode 三档的行为只能通过单元测试或调试器步进观察，
> **不属于编辑器可操作项**。代码层验收（枚举 / API 签名 / 组内 push / EndGroup 入栈 /
> 三档 merge 语义 / group undo redo 原子性 / Clear 清理 pending）由单元测试 +
> code review 把关。本节只保留"现有路径不被破坏"的回归验收。

### 验收点

- [ ] **保后兼容（默认 push 路径不变）**
  - [✅] Inspector 任何 DragFloat 连续拖动仍 coalesce 成单条栈条目（一次 Undo 回滚全程拖动）
  - [] 切换实体 / Play / Pause / Stop / Save / Load 路径行为与 c12 baseline 一致
  - [✅] Undo / Redo 一次回滚一步，与 c12 一致

### bugs
无法保存

---

## Commit 14：CommandStack 解 World\* 强耦合

所有命令（SetFieldValueCommand / CreateEntityCommand / RenameCommand / LambdaCommand
reparent）一律改 capture / 存 `EditorHost*`（弱引用），不再直接 capture `World*`。
Execute / Undo 内通过 `host->scene.pWorld.get()` 间接解 World——切场景时 host.scene
.pWorld 换新指针 / 置空，命令走 nullptr 防御分支 no-op，不再因 dangling World*
崩溃。

**保留 Clear() 调用点**（scene swap / 破坏性操作仍调 Clear），c14 改进仅是把"漏
Clear 必崩"降级为"漏 Clear 安全 no-op"——纯防呆改进。

> 说明：本 commit 是纯重构 + 防呆（命令存 host 弱引用而非 world 裸指针），编辑器上**无新
> 视觉行为**。各 command 类签名 / `ResolveWorld` helper / `MakeFieldApply` host-based 改造 /
> EntityTreePanel 调用点迁移等代码层验收由 code review 把关，不作为编辑器验收项。
>
> "场景切换不崩"理论上是编辑器可操作项，但 v0.2.5 编辑器**当前没有 New Scene / Open Scene
> 菜单入口**（Save 都是置灰状态，见 milestone bug 段），所以该路径暂时无法手动覆盖；
> 留待 v0.3 资产 / scene 管理 UI 落地后补 mini-回归。

### 验收点

- [ ] **回归式行为不变**
  - [✅] 编辑器启动 + Demo scene 加载 + 选实体 + Inspector 各字段编辑（Name / Transform /
    DirectionalLight / Renderable / RigidBody / Collider / ParticleEmitter / Animator）
  - [✅] Entity Tree 增删 / Rename / reparent（拖拽）路径正常
  - [✅] Undo / Redo 多次往返，状态值与 c12 baseline 一致
  - [✅] Play → Pause → Stop 状态机走通（注意 Stop 后 Renderable 贴图被污染的已知 bug 单独登记）

### bugs
（待大节点回归后填）

---

## v0.2.5 milestone 完工标记

c14 落地后，v0.2.5 "架构整骨" milestone 13 个 commit 全数 ✅（c3~c14；c1/c2 是 EditorState
→ EditorHost 拆分前置）：

| Commit | 主题 | 状态 |
|--------|------|------|
| c3 | DirectionalLight schema | ✅ |
| c4 | RigidBody schema | ✅ |
| c5 | Name / Transform / Hierarchy schema | ✅ |
| c6 | ParticleEmitter schema + FieldNested / FieldCustom / GroupSeparator | ✅ |
| c7 | Renderable schema | ✅ |
| c8 | Collider schema + visibleIf + Group(header-only) | ✅ |
| c9 | Animator schema + ReadOnly | ✅ |
| **节点 A 小回归** | 推迟到里程碑统一执行 | — |
| c10 | Add Component schema 化 + DrawEntityViaSchemas 收尾 | ✅ |
| c11 | IEditorInspectorPlugin 接口 + EditorHost 注册表 | ✅ |
| c12 | IEditorGizmoPlugin 接口 + EditorHost 注册表 | ✅ |
| **节点 B 小回归** | 推迟到里程碑统一执行 | — |
| c13 | CommandStack BeginGroup / EndGroup + MergeMode 三档 | ✅ |
| c14 | CommandStack 解 World\* 强耦合 | ✅ |
| **节点 C 全 milestone 回归** | 待统一执行（用户决定的回归节奏） | — |

整 milestone ✅ 标记由节点 C 回归通过后落到 `docs/editor-roadmap.md` 的 v0.2.5
heading 上；本 acceptance-checklist 同时归档到 `docs/qa-records/` 或保留原位。

---

## 后续 commit（待追加）

每个新 commit 落地时在本文档**追加**一节，结构同上：

```markdown
## Commit N：<commit 标题>

<commit 摘要 1-2 句>

### 验收点

- [ ] ...

### bugs
（待小节点回归后填）
```

### 待开工 commit 占位

- ~~Commit 7：Renderable schema 迁移~~ ✅
- ~~Commit 8：Collider schema 迁移（含 `std::variant<CircleDesc / BoxDesc / PolygonDesc / EdgeChainDesc>` 多形）~~ ✅
- ~~Commit 9：Animator schema 迁移（含 `unique_ptr<IAnimator>` 抽象，仅展示 backend type）~~ ✅
- **节点 A 小回归**（推迟到里程碑统一回归——用户决定）
- ~~Commit 10：Add Component 菜单改 schema 注册表枚举驱动（不再 hardcode if/else 列表）~~ ✅
- ~~Commit 11：`IEditorInspectorPlugin` 接口声明 + `EditorHost` plugin registry（仅声明，无真实 plugin）~~ ✅
- ~~Commit 12：`IEditorGizmoPlugin` 接口声明（仅签名，v0.4 消费）~~ ✅
- **节点 B 小回归**（推迟到里程碑统一回归——用户决定）
- ~~Commit 13：CommandStack `BeginGroup` / `EndGroup` + `MergeMode` 三档~~ ✅
- ~~Commit 14：CommandStack 解 World\* 强耦合（命令存 entity id，scene swap 不再 Clear 整栈）~~ ✅
- **节点 C 全 milestone 回归 + v0.2.5 ✅**（待统一执行）

具体 commit 数和顺序在开工时按需调整；本占位仅为对齐 `editor-roadmap.md` v0.2.5
deliverables 列表的方向参考。

---

## 文档生命周期

- v0.2.5 milestone ✅ 后：本文档可归档（迁到 `docs/qa-records/` 或保留在原位作为
  v0.x 期 milestone 验收范本）；不删
- 未来 milestone（v0.3 / v0.4 / ...）若沿用此模式：复制本文档为
  `editor-v<X.Y>-acceptance-checklist.md`，重新填内容；不在本文档继续追加跨
  milestone 内容（避免文档膨胀 + 上下文混淆）
