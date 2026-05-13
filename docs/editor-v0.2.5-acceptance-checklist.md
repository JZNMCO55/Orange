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

`SeedDemoWorld` 或 `assets/editor/demo.scene.json` 内 13 个实体：

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

- [ ] Editor 启动无 crash；自动加载 `assets/editor/demo.scene.json` 或回退 `SeedDemoWorld`
- [ ] 选中任意实体 → Inspector 显示该实体所有挂着的 component header
- [ ] 任改一个标量字段 → `Ctrl+Z` 回滚 → `Ctrl+Y` 重做，三步值一致
- [ ] Save 当前 scene → 关闭 → 重启 editor → 加载，所有字段保持
- [ ] Play → Pause → Stop 状态机走通；Stop 后世界状态回到 Play 前
- [ ] 右键任意可移除 component header → Remove Component → component 消失；后续 +Add Component 菜单出现该项 → 选中后 component 重新挂上（值是默认）

---

## Commit 5：Name / Transform / Hierarchy schema 迁移

引入 `PropertyType::EntityRef`；`PropertyType::Quat` case 从 placeholder 改 Euler-cache 真实实现；
`DrawComponentSchemaSection` Remove 后统一 invalidate `transformEulerCacheEntity`。

### 验收点

- [ ] **Name section**
  - [ ] 显示一个无 label 的 InputText 占满整行
  - [ ] 连续输入多字符 → `Ctrl+Z` 一次回到原名（Merge coalesce）
  - [ ] Entity Tree 与 Inspector 两个 InputText 同时观察同 entity 时不串味（rename buffer 隔离）

- [ ] **Transform section**
  - [ ] Position / Rotation (°) / Scale 三行 DragFloat3
  - [ ] Rotation Euler 拖动平滑，gimbal lock 附近无数字跳变
  - [ ] 编辑 Rotation 后 Undo → Inspector 显示值回到编辑前
  - [ ] 切换不同实体 → Rotation 缓存按新实体的 quat 重算
  - [ ] 右键 Transform header → Remove Component → Undo 不 crash
  - [ ] Remove Transform → 再 Add Transform → Rotation 字段显示默认 (0,0,0)，无 cache 残留

- [ ] **Hierarchy section**
  - [ ] 4 行 EntityRef 显示：`Parent: #<id>` / `Parent: (none)` 等
  - [ ] 不出现 Remove Component 右键菜单（无 Removable）
  - [ ] +Add Component 菜单不包含 Hierarchy（无 Addable）
  - [ ] 视觉降级：v0.1 期 `(edit by drag-drop in Entity Tree)` 提示已删除——确认可接受

### bugs
（无）

---

## Commit 6：ParticleEmitter schema 迁移 + 3 个 schema 扩展

引入 `FieldNested<ParentPtr, ChildPtr>`（双 NTTP 嵌套）/ `FieldCustom<T>(getFn, setFn)`（任意
mapping）/ `PropertyAttributes::groupSeparator`（`ImGui::SeparatorText`）。

### 验收点

- [ ] **Particle Emitter section**（在 demo scene 火焰 / 萤火 emitter 实体上）
  - [ ] 顶层字段：`Emitting` checkbox / `Emission Rate (/s)` DragFloat
  - [ ] 7 个 SeparatorText 分组顺序：Lifetime → Spawn Offset → Initial Velocity → Forces → Color curve → Size curve → Pool
  - [ ] 14 字段全显示且顺序与 v0.1 一致

- [ ] **嵌套字段（FieldNested）行为**
  - [ ] 编辑 `Emission Rate` → Undo 回滚（验证 desc.X 嵌套写入正确）
  - [ ] 编辑 `Gravity` (Vec2) → 拖动连续生效
  - [ ] 编辑 `Max Particles` → DragScalar U32 控件正常工作（v0.1 期是 DragInt → cast，schema 路径直接 UInt）

- [ ] **自定义字段（FieldCustom）行为**
  - [ ] `Color Start RGB` 走 ColorEdit3（vec3 + isColor）
  - [ ] `Color Start Alpha` 走 DragFloat 无上限——拖到 2.0 / 5.0 触发粒子明显发光（bloom 拾取）
  - [ ] Color End RGB / Alpha 同上
  - [ ] 编辑 Color Start RGB 后 Undo → RGB 回到编辑前，Alpha 不被影响
  - [ ] 编辑 Color Start Alpha 后 Undo → Alpha 回到编辑前，RGB 不被影响（验证 4 个虚拟字段独立 coalesce）

- [ ] **视觉分组（GroupSeparator）**
  - [ ] 7 个 SeparatorText 视觉与 v0.1 一致（同款文字、位置）

- [ ] **Add / Remove**
  - [ ] 右键 Particle Emitter header → Remove Component → 粒子停止
  - [ ] Undo → component 恢复 ⚠ 实际上 Remove 走 schema.remove + cmdStack.Clear，Undo 无效——这是 v0.1 同款行为，确认仍是 expected
  - [ ] +Add Component 菜单包含 Particle Emitter（如 v0.1 行为一致）

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
  - [ ] `Visible` checkbox 可勾 / 取消，几何相应隐 / 显
  - [ ] `Casts Shadow` checkbox 可勾 / 取消；hover 显示 tooltip 文本与 v0.1 一致
  - [ ] **视觉降级**：v0.1 期两行 `Mesh handle: <id>` / `MaterialInstance: <ptr>` 调试信息**不再显示**——确认可接受

- [ ] **Add / Remove**
  - [ ] 右键 Renderable header → Remove Component → 几何消失
  - [ ] `+Add Component` → `Renderable` 菜单项**仍可用**（仍走 InspectorPanel 内 hardcode 路径，预绑 cubeMesh + defaultMaterial）；从菜单加挂的 Renderable 立即显示白色立方体
  - [ ] 验证 schema 的 `.Removable()` 与 hardcode `+Add` 路径**互不冲突**：Remove → Add → Remove → Add 多次循环正常

- [ ] **跨 commit 状态**（验证 c5 / c6 未受影响）
  - [ ] 切换实体看 Inspector：Name / Transform / Hierarchy / DirectionalLight / Renderable / RigidBody / Collider / ParticleEmitter / Animator header 顺序未变

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

- [ ] **Collider section 通用字段**（任意持 Collider 的实体，可用 `samples/06_physics_platformer` 或自建）
  - [ ] `Density` DragFloat（无 clamp）
  - [ ] `Friction` DragFloat（clamp 0..1）
  - [ ] `Restitution` DragFloat（clamp 0..1）
  - [ ] `Is Sensor` checkbox
  - [ ] 上述 4 字段在**任何 shape 下**都显示且可编辑

- [ ] **Circle shape 段**
  - [ ] 实体 shape = CircleDesc 时：显示 `SeparatorText "Shape: Circle"` + `Radius` + `Center`
  - [ ] 不显示 Box / Polygon / EdgeChain 任一段
  - [ ] 编辑 Radius / Center → 拖动连续生效 + Undo 回滚

- [ ] **Box shape 段**
  - [ ] 实体 shape = BoxDesc 时：显示 `SeparatorText "Shape: Box"` + `Half Extents` + `Center`
  - [ ] 不显示 Circle / Polygon / EdgeChain 任一段
  - [ ] 编辑 Half Extents / Center → 拖动连续生效 + Undo 回滚

- [ ] **Polygon / EdgeChain 段（零字段 header-only）**
  - [ ] Polygon shape：仅显示一行 `SeparatorText "Shape: Polygon (vertex editing — later)"`，**无任何可编辑控件**
  - [ ] EdgeChain shape：仅显示一行 `SeparatorText "Shape: EdgeChain (vertex editing — later)"`，**无任何可编辑控件**
  - [ ] **视觉降级**：v0.1 期 `Shape: Polygon (8 verts)` 的动态 vertex count、`Shape: EdgeChain (..., loop=yes)` 的 loop 状态**不再显示**——确认可接受

- [ ] **字段顺序变更（视觉降级）**
  - [ ] v0.1 期顺序：shape 段 → 通用字段；c8 改为：通用字段 → shape 段
  - [ ] 顺序变更原因：schema 是线性顺序，Polygon / EdgeChain 的零字段段若放在中间会让通用字段被挤；颠倒让 shape 段总在 component 段末尾
  - [ ] 确认新顺序视觉上可接受

- [ ] **shape 类型切换控件**
  - [ ] Inspector 内**无** shape 类型切换 Combo / 按钮 / 任何入口（与 v0.1 deliberately not implemented 行为一致）
  - [ ] 切换 shape 需要走代码 / 场景文件，不是 Inspector 操作

- [ ] **跨 shape Coalesce**
  - [ ] 编辑 Circle 实体的 `circle.radius` → 选 Box 实体编辑 `box.halfExtents` → 回到 Circle 实体编辑 `circle.radius` → Undo 一次只回滚最后一次 Circle 编辑（fieldKey `Collider.circle.radius` ≠ `Collider.box.halfExtents`，coalesce 按 entity+fieldKey 双键正确隔离）

- [ ] **Add / Remove**
  - [ ] 右键 Collider header → Remove Component → component 消失；cmdStack 被 Clear（破坏性操作）
  - [ ] `+ Add Component` → Collider 菜单项**仍可用**（走 InspectorPanel 内 hardcode 路径，添加默认 `ColliderComponent{}`，shape 默认是 CircleDesc）
  - [ ] 添加后 Inspector 立即显示新挂的 Collider 段，Circle shape 段可见

- [ ] **跨 commit 状态**（验证 c5 / c6 / c7 未受影响）
  - [ ] 切换实体看 Inspector：Name / Transform / Hierarchy / DirectionalLight / Renderable / RigidBody / Collider / ParticleEmitter / Animator header 顺序未变（**c8 把 Collider 位置由"自定义函数调用"改为"schema 段"，但视觉位置不变**）
  - [ ] visibleIf 不影响其他 component 的字段渲染（其他 schema 注册不挂 visibleIf，DrawProperty 内 nullptr 默认走"总显示"路径）
  - [ ] ParticleEmitter c6 引入的 GroupSeparator 路径不受 visibleIf 引入影响（GroupSeparator 与 visibleIf 在 SchemaInspector 内的顺序：先 visibleIf → 再 SeparatorText → 再 get/set 检查 → 再 switch 控件）

- [ ] **schema 基础设施新增**
  - [ ] `PropertyAttributes::visibleIf` 字段 + `PropertyAttributes::VisibleIfFn` typedef 存在
  - [ ] `ComponentSchemaBuilder::VisibleIf(pred)` 链式 API 可用
  - [ ] `ComponentSchemaBuilder::Group(separator, pred)` 链式 API 可用，注册的 PD 仅渲染 SeparatorText 后立即返回
  - [ ] 编译期 Lint / clang-tidy（如有）无新警告

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

- [ ] **Animator section**（在挂了 Animator 的实体上，可用 `samples/05_dragonbones_demo` 或类似）
  - [ ] Header 文本 = `Animator`
  - [ ] 展开后**仅显示一行** `Backend: <name>`
    - [ ] `<name>` 为 backend 字符串：DragonBones 后端 → `skeletal_dragonbones`；Procedural 后端 → `procedural`；游戏自注册后端 → 该后端 `BackendName()` 返回值
    - [ ] 不显示 v0.1 期的指针地址（`Animator (runtime) : 0x...`）
    - [ ] 不显示 v0.1 期的占位文本（`(animator backend editing — later task)`）
  - [ ] **空 animator unique_ptr** 防御路径：如果某 entity 的 `AnimatorComponent.animator == nullptr`（理论上不应进入此状态，v0.1 hardcode 进了就显示 `nullptr`），schema 显示 `Backend: (no backend)` 而非 crash

- [ ] **ReadOnly 控件行为**
  - [ ] Backend 字段**不可编辑**：尝试点击 / 双击 / 拖拽都没有 InputText 出现
  - [ ] 文本视觉为 `Text "Backend:"` + `SameLine` + `TextDisabled "<value>"`（label 正常色 + value 灰色）
  - [ ] Inspector 整段被 `BeginDisabled(!canEdit)` 包裹时（Play / Paused 期）—— readOnly 字段同样灰显，但视觉与 Edit 期 readOnly 几乎一致；接受这一退化（不区分 "Inspector 全段 disable" vs "字段自身 readOnly"）

- [ ] **Add / Remove**
  - [ ] 右键 Animator header → **无 Remove Component 菜单项**（schema 未 `.Removable()`）
  - [ ] `+ Add Component` → **无 Animator 菜单项**（schema 未 `.Addable()`；与 v0.1 期 InspectorPanel +Add popup 显式跳过 Animator 行为一致）

- [ ] **跨 commit 状态**（验证 c5 ~ c8 未受影响）
  - [ ] 切换实体看 Inspector：Name / Transform / Hierarchy / DirectionalLight / Renderable / RigidBody / Collider / ParticleEmitter / Animator header 顺序未变（c9 把 Animator 由"自定义函数调用"改为"schema 段"，但视觉位置不变）
  - [ ] 其他 component 的 String 字段（如 Name）未受 readOnly 引入影响——未挂 readOnly 的字段仍走 InputText 编辑路径，无任何 regression
  - [ ] 命令栈在 Animator 段不产生任何条目（readOnly 不 Push 命令）；Undo / Redo 跳过 Animator 段

- [ ] **schema 基础设施新增**
  - [ ] `PropertyAttributes::readOnly` 字段存在
  - [ ] `ComponentSchemaBuilder::ReadOnly()` 链式 API 可用
  - [ ] SchemaInspector 早退检查放宽：`set==nullptr && !readOnly` 才早退（readOnly 字段允许 nullptr setter）
  - [ ] String 以外的 PropertyType 上设 readOnly 暂被忽略，走默认编辑路径（c9 仅 String case 实现，文档化为已知限制）

- [ ] **v0.2.5 整骨收尾里程碑**
  - [ ] `tools/OrangeEditor/EditorRenderLayer.h` 内**无任何** `DrawInspectorXxx` 成员声明
  - [ ] `tools/OrangeEditor/panels/InspectorPanel.cpp` 内**无任何** `DrawInspectorXxx` 函数体（仅留若干"已删除"注释指向对应 schema 注册位置作历史索引）
  - [ ] DrawInspectorPanel 函数体全部走 `schemaReg.Find<...>() → DrawComponentSchemaSection`

### bugs
（待大节点回归后填）

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
  - [ ] 在未挂任何 component 的新建实体上点 +Add Component → 菜单包含 6 项：
    Transform / Directional Light / Renderable / RigidBody / Collider / Particle Emitter
  - [ ] 菜单**不**包含：Name（无 Addable）、Hierarchy（无 Addable）、Animator（无 Addable）
  - [ ] 在已挂某 component 的实体上 +Add Component → 该 component 不再出现在菜单内
    （schema.has 守卫）

- [ ] **Transform 重新加挂**（c5 oversight 修正）
  - [ ] 在某实体上右键 Transform header → Remove Component → Transform 段消失
  - [ ] 点 +Add Component → 菜单出现 `Transform` 项
  - [ ] 选中后 Transform 默认值（position=0 / rotation=identity / scale=1）出现在 Inspector

- [ ] **Renderable 预绑还原**（c7 deferred 兑现）
  - [ ] 在某未挂 Renderable 的实体上 +Add Component → 选 `Renderable`
  - [ ] 立即在 Scene viewport 看到一个白色立方体（cubeMesh + textured material），
    **不是**隐形空 Renderable
  - [ ] Inspector 内 Renderable 段显示 `Visible=true` / `Casts Shadow=true`
  - [ ] Remove → +Add 重复多次，每次都正确预绑

- [ ] **AddableWith 抽象 + Addable 默认路径**
  - [ ] DirectionalLight / RigidBody / Collider / ParticleEmitter / Transform 走默认 Addable，
    挂上后 component 字段 = 各自默认构造值
  - [ ] Renderable 走 AddableWith，挂上后 mesh / materialInstance 不是默认空值

- [ ] **AddFn 签名变更副作用**
  - [ ] 现有 cmdStack.Clear() 行为保留：+Add 后 Undo 不能回滚（破坏性操作历史清零）
  - [ ] 切换 entity / Play Mode / Save / Load 等路径未受 AddFn 签名变更影响

- [ ] **Inspector 上半段统一为 DrawEntityViaSchemas**
  - [ ] 9 个 schema 段渲染顺序与 c9 完工时一致：Name → Transform → Hierarchy →
    DirectionalLight → Renderable → RigidBody → Collider → ParticleEmitter → Animator
  - [ ] 任一 component 在 entity 未挂时不画空段（schema.has 守卫）
  - [ ] 切实体后 Inspector 立即刷新到新 entity 的 component 集合

- [ ] **代码层验收（架构纪律）**
  - [ ] `tools/OrangeEditor/panels/InspectorPanel.cpp` 内 grep `Orange::Engine::Render::` /
    `Orange::Engine::Physics::` / `Orange::Engine::Animation::` 等 component 类型 ——
    **零命中**（除注释里的历史索引）
  - [ ] `tools/OrangeEditor/EditorRenderLayer.h` 内 grep `DrawInspector` 或 `ComponentHeader`
    —— **零命中**（除注释里的历史索引）
  - [ ] `python scripts/check_invariants.py` 通过；baseline grandfathered 计数应继续下降
    （或保持，不应上升）

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

### 验收点

- [ ] **接口文件存在**
  - [ ] `tools/OrangeEditor/plugin/IEditorInspectorPlugin.h` 存在
  - [ ] header guard 命名规范：`ORANGE_EDITOR_PLUGIN_I_EDITOR_INSPECTOR_PLUGIN_H`
  - [ ] 命名空间：`Orange::Editor::Plugin`

- [ ] **接口签名**
  - [ ] `virtual ~IEditorInspectorPlugin() = default`
  - [ ] copy / move 显式 delete（plugin 实例由 unique_ptr 持有，禁拷贝禁移动避免 slicing）
  - [ ] `virtual bool CanHandle(const ComponentSchema&) const = 0` —— 纯虚
  - [ ] `virtual bool ParseBegin(EditorHost&, Entity, const ComponentSchema&, void*)` —— 默认返回 false
  - [ ] `virtual void ParseEnd  (EditorHost&, Entity, const ComponentSchema&, void*)` —— 默认 no-op

- [ ] **前向声明纪律**
  - [ ] IEditorInspectorPlugin.h 内 `struct EditorHost;` + `namespace Orange::Editor::Schema { struct ComponentSchema; }` 前向声明
  - [ ] **不**从 plugin 头反向 `#include "../EditorHost.h"`（避免循环）

- [ ] **EditorHost 注册表字段**
  - [ ] `EditorHost.h` 顶部新增 `#include "plugin/IEditorInspectorPlugin.h"` + `<memory>` + `<vector>`
  - [ ] EditorHost 内新增字段 `std::vector<std::unique_ptr<Orange::Editor::Plugin::IEditorInspectorPlugin>> inspectorPlugins;`
  - [ ] 字段初始化：默认为空 vector（无 plugin 注册路径）

- [ ] **本期不做的（确认未做）**
  - [ ] SchemaInspector.cpp **不**包含 plugin 调度逻辑（搜 `IEditorInspectorPlugin` / `CanHandle` 应仅命中 header 注释）
  - [ ] 没有任何派生类实例化（搜 `: public IEditorInspectorPlugin` 应零命中除注释外）
  - [ ] EditorHost.inspectorPlugins 在 main / Demo / 其他构造路径上**不**被 push_back

- [ ] **下行影响验证**
  - [ ] `cmake --build build --config Debug --target OrangeEditor` 全绿
  - [ ] 编辑器启动 + demo scene 加载 + 选实体 + Inspector 渲染所有 component 段 —— 行为与 c10 完全一致（plugin 字段为空，SchemaInspector 当前也不查 plugin，无任何视觉变化）
  - [ ] `python scripts/check_invariants.py` 通过（无新违规）

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

### 验收点

- [ ] **接口文件存在**
  - [ ] `tools/OrangeEditor/plugin/IEditorGizmoPlugin.h` 存在
  - [ ] header guard 命名规范：`ORANGE_EDITOR_PLUGIN_I_EDITOR_GIZMO_PLUGIN_H`
  - [ ] 命名空间：`Orange::Editor::Plugin`（与 c11 同款）

- [ ] **接口签名**
  - [ ] `virtual ~IEditorGizmoPlugin() = default`
  - [ ] copy / move 显式 delete
  - [ ] `virtual bool CanHandle(const ComponentSchema&) const = 0` —— 纯虚
  - [ ] `virtual void Draw(EditorHost&, Entity, const ComponentSchema&, void*, const GizmoContext&) = 0` —— 纯虚
  - [ ] `virtual bool HitTest(EditorHost&, Entity, const ComponentSchema&, void*, const GizmoContext&)` —— 默认返回 false

- [ ] **GizmoContext 前向声明纪律**
  - [ ] IEditorGizmoPlugin.h 内 `namespace Orange::Editor::Plugin { struct GizmoContext; }` —— **仅前向声明**，**不**定义字段
  - [ ] 头注释明确文档化 v0.4 预期填入的 GizmoContext 字段（viewport matrix / ImDrawList / picking ray / handle 命中槽）

- [ ] **前向声明同 c11 纪律**
  - [ ] IEditorGizmoPlugin.h 内 `struct EditorHost;` + `namespace Orange::Editor::Schema { struct ComponentSchema; }` 前向声明
  - [ ] **不**从 plugin 头反向 include EditorHost.h（避免循环）

- [ ] **EditorHost 注册表字段**
  - [ ] `EditorHost.h` include `plugin/IEditorGizmoPlugin.h`
  - [ ] EditorHost 内新增 `std::vector<std::unique_ptr<Orange::Editor::Plugin::IEditorGizmoPlugin>> gizmoPlugins;`
  - [ ] 字段初始化：默认为空 vector

- [ ] **本期不做的（确认未做）**
  - [ ] SchemaInspector.cpp / SceneView 渲染路径**不**包含 gizmo plugin 调度逻辑
  - [ ] 没有任何派生类实例化（搜 `: public IEditorGizmoPlugin` 应零命中除注释外）
  - [ ] EditorHost.gizmoPlugins 在 main / Demo / 其他构造路径上**不**被 push_back
  - [ ] **没有** GizmoContext 的字段定义（仅前向声明）

- [ ] **下行影响验证**
  - [ ] `cmake --build build --config Debug --target OrangeEditor` 全绿
  - [ ] 编辑器启动 + demo scene 加载 + 选实体 + Inspector 渲染 —— 行为与 c11 完全一致（gizmoPlugins 字段为空，viewport 当前也不查 gizmo plugin，无任何视觉变化）
  - [ ] `python scripts/check_invariants.py` 通过（无新违规）

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

### 验收点

- [ ] **MergeMode 枚举**
  - [ ] `enum class MergeMode : std::uint8_t { Disable=0, Ends=1, All=2 }`
  - [ ] 默认值在 `BeginGroup` 签名内为 `Ends`

- [ ] **BeginGroup / EndGroup / InGroup API**
  - [ ] `void BeginGroup(const char* name, MergeMode mode = MergeMode::Ends)`
  - [ ] `void EndGroup()`
  - [ ] `bool InGroup() const`
  - [ ] `BeginGroup` 内 name 参数文档为静态生命周期；CommandStack 不复制

- [ ] **组内 Push 行为**（手工单元测试 / 调试器步进）
  - [ ] BeginGroup 后调 `cmd->Execute()` 立刻生效（live preview）
  - [ ] Push 不直接进 `mStack`，进 `mPendingGroup`
  - [ ] 组内 intra-group coalesce 工作：同 GetType 连续 Push 在 pending 末尾合并

- [ ] **EndGroup 入栈**
  - [ ] 空组（BeginGroup 后无 Push）EndGroup 不污染栈
  - [ ] 非空组 EndGroup → 单条 CommandGroup 入栈 + `mIndex` ++
  - [ ] CommandGroup 的 `GetType()` 返回 BeginGroup 传入的 name

- [ ] **MergeMode::Disable 行为**
  - [ ] 第一次 `BeginGroup("X", Disable) ... EndGroup()` → 栈条目 +1
  - [ ] 第二次 `BeginGroup("X", Disable) ... EndGroup()` → 栈条目再 +1（不与栈顶合并）

- [ ] **MergeMode::Ends 行为**（默认）
  - [ ] 第一次 `BeginGroup("X", Ends) ... EndGroup()` → 栈条目 +1
  - [ ] 第二次 `BeginGroup("X", Ends) ... EndGroup()` → 栈条目仍是 1（与栈顶 "X" 合并）
  - [ ] 中间插入一个 `BeginGroup("Y", Ends) ... EndGroup()` 后再来 `BeginGroup("X", Ends)`：
    栈顶不是 "X"（是 "Y"），所以新 "X" 不与之前的 "X" 合并，栈条目 +1（总 3 条）

- [ ] **MergeMode::All 行为**
  - [ ] 同 Ends 的"连续两条 X 合并"
  - [ ] 区别：中间插入 "Y" 后再来 `BeginGroup("X", All)` → 跨过 "Y" 与最早 "X" 合并，栈条目仍 2（X+Y）
  - [ ] 头注释明确"All 跨条目合并隐含时序重排，调用方负责保证字段不冲突"

- [ ] **Group Undo / Redo 原子性**
  - [ ] Group 内 3 个 sub-command → Undo 一次回到 BeginGroup 之前状态（3 个字段同时回滚）
  - [ ] Redo 一次重新应用 3 个 sub-command（同样原子）

- [ ] **Clear() 清理 pending**
  - [ ] BeginGroup 后调 Clear() → mStack 清空 + mPendingGroup 清空 + mInGroup = false
  - [ ] Clear 后 InGroup() == false；后续 Push 走默认（非组）路径

- [ ] **保后兼容**
  - [ ] 未调 BeginGroup 的 Push 行为与 c12 完全一致
  - [ ] Inspector DragFloat 连续拖动仍 coalesce 成单条栈条目（fieldKey 匹配 + SetFieldValueCommand::Merge 返回 true）

- [ ] **下行影响验证**
  - [ ] `cmake --build build --config Debug --target OrangeEditor` 全绿
  - [ ] 编辑器启动 + Inspector 编辑 + Undo / Redo 路径行为不变（c12 baseline）
  - [ ] `python scripts/check_invariants.py` 通过

### bugs
（待大节点回归后填）

---

## Commit 14：CommandStack 解 World\* 强耦合

所有命令（SetFieldValueCommand / CreateEntityCommand / RenameCommand / LambdaCommand
reparent）一律改 capture / 存 `EditorHost*`（弱引用），不再直接 capture `World*`。
Execute / Undo 内通过 `host->scene.pWorld.get()` 间接解 World——切场景时 host.scene
.pWorld 换新指针 / 置空，命令走 nullptr 防御分支 no-op，不再因 dangling World*
崩溃。

**保留 Clear() 调用点**（scene swap / 破坏性操作仍调 Clear），c14 改进仅是把"漏
Clear 必崩"降级为"漏 Clear 安全 no-op"——纯防呆改进。

### 验收点

- [ ] **EntityCommands 改 host-based**
  - [ ] `CreateEntityCommand` 构造签名：`(EditorHost& host, CreatorFn creator)`
  - [ ] `RenameCommand` 构造签名：`(EditorHost& host, Entity, std::string, std::string)`
  - [ ] 字段：`EditorHost* mpHost`（替代原 `Orange::Engine::World* mpWorld`）
  - [ ] Execute / Undo 均通过本地 `ResolveWorld(mpHost)` helper 解 World + nullptr 防御
  - [ ] CreateEntityCommand::Execute 在 world == nullptr 时不调 mCreatorFn，留 mCreated = Invalid

- [ ] **SchemaInspector MakeFieldApply 改 host-based**
  - [ ] 模板函数签名：`MakeFieldApply(EditorHost* pHost, Entity, const ComponentSchema*, SetFn)`
  - [ ] lambda 内通过 `pHost->scene.pWorld.get()` 解 World + nullptr 防御 + nullptr 时 return
  - [ ] 所有 8 个 PropertyType case（Float / Int / UInt / Bool / Vec2 / Vec3 / Vec4 / Enum / String）的 `MakeFieldApply<T>(pWorld, ...)` 调用改为 `MakeFieldApply<T>(&host, ...)`
  - [ ] Quat case 的自定义 apply lambda 改 capture `pHost` + `pSchema` + `setFn` + `entity`（不再单独 capture `pWorld` / `pHostInner`）

- [ ] **EntityTreePanel 调用点迁移**
  - [ ] `CreateEntityCommand` 调用：`std::make_unique<CreateEntityCommand>(mHost, ...)`（去掉 `*mHost.scene.pWorld`）
  - [ ] `RenameCommand` 调用：`std::make_unique<RenameCommand>(mHost, entity, oldName, newName)`
  - [ ] reparent LambdaCommand：lambda capture `pH = &mHost`（不再 `pW = mHost.scene.pWorld.get()`），内部 `pH->scene.pWorld.get()` 解 + nullptr 防御

- [ ] **行为防御**（手工 / 调试器步进）
  - [ ] **场景切换不崩**：编辑器 New Scene / Open Scene → 切换瞬间 host.scene.pWorld 置新指针；之前命令栈上残留命令（虽然 caller 仍按规约调 `Clear()`，但即便漏调）也不再因 dangling World 崩
  - [ ] **正常 Edit Mode 行为不变**：c12 baseline 的所有 Inspector / Tree / Play / Undo / Redo 路径在 c14 下行为一致
  - [ ] CommandStack::Clear() 调用点不动（新 / 加载 / 删 entity / 删 component 等仍调）

- [ ] **保后兼容**
  - [ ] ICommand 接口签名不变（Execute() / Undo() 仍无参数）
  - [ ] CommandStack 接口不变（除 c13 加的 group 三方法外）
  - [ ] LambdaCommand 接口不变（lambda 内容由调用方决定，caller 负责 capture host 而非 world）

- [ ] **下行影响验证**
  - [ ] `cmake --build build --config Debug --target OrangeEditor` 全绿
  - [ ] 编辑器启动 + Demo scene 加载 + 选实体 + Inspector 各字段编辑 + Undo / Redo + Play Mode 路径无 regression
  - [ ] `python scripts/check_invariants.py` 通过

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
