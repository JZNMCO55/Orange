# OrangeEditor v0.2.5 整骨 milestone 验收清单

- 基准日期：2026-05-13
- 适用范围：OrangeEditor v0.2.5 "架构整骨" milestone 各 commit 的 UI / 行为验收
- 设计意图：v0.2.5 内多 commit 高频迁移 schema，让用户在每 commit 后手工验收会打断节奏；改为
  把每个 commit 的验收点沉淀进本文档，按**小节点**统一跑回归——既保住开发节奏，也控制 bug
  累积窗口

## 回归节奏建议

不要等 v0.2.5 整 milestone 完才回归——窗口过大、bug 定位成本高。推荐分 3 个小节点：

| 节点 | 时机 | 范围 |
|------|------|------|
| 节点 A | 全部内置组件 schema 化完成（c3–c8 累计） | 跑"小节点回归路径"+ c3–c8 各 commit 验收点 |
| 节点 B | Add Component 菜单 + 插件接口落地完成 | 同 A + 新 commit 验收点 + 关注 Add 菜单 schema 驱动正确性 |
| 节点 C | CommandStack 改造完成（v0.2.5 完工） | 全 milestone 回归 + 关闭 milestone 验收 |

每个小节点跑完后：
- 通过的清单条目划掉（在本文档内 `[ ]` → `[x]`）
- 失败的条目登记到对应 commit 节内 `### bugs` 段，包含「现象 / 复现步骤 / 候选根因」
- 修复后 push 新 commit（commit message 引用本文档 bug 编号），不重写历史

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
