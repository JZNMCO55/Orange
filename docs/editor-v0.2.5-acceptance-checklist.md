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

- Commit 7：Renderable schema 迁移（含 mesh / materialInstance handle 字段——预计需 `PropertyType::AssetHandle` 或只读 placeholder）
- Commit 8：Collider schema 迁移（含 `std::variant<CircleDesc / BoxDesc / PolygonDesc / EdgeChainDesc>` 多形）
- Commit 9：Animator schema 迁移（含 `unique_ptr<IAnimator>` 抽象，仅展示 backend type）
- **节点 A 小回归**
- Commit 10：Add Component 菜单改 schema 注册表枚举驱动（不再 hardcode if/else 列表）
- Commit 11：`IEditorInspectorPlugin` 接口声明 + `EditorHost` plugin registry（仅声明，无真实 plugin）
- Commit 12：`IEditorGizmoPlugin` 接口声明（仅签名，v0.4 消费）
- **节点 B 小回归**
- Commit 13：CommandStack `BeginGroup` / `EndGroup` + `MergeMode` 三档
- Commit 14：CommandStack 解 World\* 强耦合（命令存 entity id，scene swap 不再 Clear 整栈）
- **节点 C 全 milestone 回归 + v0.2.5 ✅**

具体 commit 数和顺序在开工时按需调整；本占位仅为对齐 `editor-roadmap.md` v0.2.5
deliverables 列表的方向参考。

---

## 文档生命周期

- v0.2.5 milestone ✅ 后：本文档可归档（迁到 `docs/qa-records/` 或保留在原位作为
  v0.x 期 milestone 验收范本）；不删
- 未来 milestone（v0.3 / v0.4 / ...）若沿用此模式：复制本文档为
  `editor-v<X.Y>-acceptance-checklist.md`，重新填内容；不在本文档继续追加跨
  milestone 内容（避免文档膨胀 + 上下文混淆）
