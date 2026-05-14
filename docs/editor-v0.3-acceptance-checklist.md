# OrangeEditor v0.3 游戏侧 Schema 注册落地 milestone 验收清单

- 基准日期：2026-05-14
- 适用范围：OrangeEditor v0.3 "游戏侧 Schema 注册落地" milestone 各 commit 的 UI / 行为验收
- 设计意图：与 v0.2.5 同款——把每个 commit 的验收点沉淀进本文档，按节点统一跑回归

## 回归节奏

**v0.3 整 milestone 完工后一次性大节点回归**。v0.3 仅 3 个 commit 加 2 个 GAP 消费 commit，回归
窗口小、bug 累积成本可接受。

回归后处理（同 v0.2.5）：
- 通过的清单条目划掉（`[ ]` → `[✅]`）
- 失败的条目登记到对应 commit 节内 `### bugs` 段，包含「现象 / 复现步骤 / 候选根因」
- 修复后 push 新 commit（commit message 引用本文档 bug 编号），不重写历史

## 大节点回归的测试盲点提醒

按大节点回归容易漏的几类路径（v0.3 上下文）——回归时请刻意覆盖：

### 1. v0.2.5 遗留 bug 在 v0.3 内的回归状态

v0.2.5 完工时悬挂的 BUG-1 / BUG-2 在 v0.3 期间通过独立 commit 闭环。**v0.3 验收必须确认这些
bug 不复现**：

- **BUG-1（File > Save 始终置灰）**：v0.2.5 收尾 commit 已修复（`scene.dirty` 钩子接通命令栈
  变更回调）。v0.3 任何 commit 引入的字段编辑都应正确触发 dirty → Save 菜单亮起
- **BUG-2（Play 后掉落物棋盘格）**：经 `GAP-2026-05-14-renderable-material-instance-round-trip`
  彻底修复（`MaterialInstance` 通过 `namedMaterialInstances` 表正确参与 Save/Load round-trip）；
  v0.3 任何 Play → Stop 路径都应保留材质

### 2. 游戏侧 component 通过 schema 注册的完整链路

v0.3 的核心 deliverable 是"无需改 editor 一行"让游戏侧 component 完整参与编辑器。`HealthComponent`
是首个验证用例，验收时要把"schema → Inspector → Undo/Redo → Save/Load → Play snapshot"五段链路
都跑一遍——任一段断裂都意味着 v0.3 deliverable 未达成。

### 3. Plugin dispatch 不破坏其它 schema 段渲染

v0.3 c3 引入 `SchemaInspector` 内的 plugin 调度路径——遍历 `inspectorPlugins` 找 `CanHandle ==
true` 的 plugin 接管。**未注册 plugin 的 component 段（Name / Transform / Hierarchy /
DirectionalLight / Renderable / RigidBody / Collider / ParticleEmitter）渲染必须与 v0.2.5 完工
时完全一致**——任何视觉变化都是 regression。

### 4. Save/Load 路径覆盖完整性

`extraSerializers` 字段在 5 个 Save/Load 调用点都要透传：
- File > Save（保存当前场景）
- File > Save As...（保存到新文件）
- File > Open Scene...（打开外部场景文件）
- Play 进入时的快照保存（编辑器内部，用户感知是 Play 按钮）
- Stop 时的快照恢复（编辑器内部，用户感知是 Stop 按钮）

漏掉任何一条 → 该路径上 HealthComponent 会丢失。v0.3 c2 已在 5 处全部接通，回归时按"Save / Save As / Open / Play→Stop"四条路径各跑一遍验。

### 5. Demo scene 实体覆盖完整性

v0.3 c1 修补了 v0.2.5 BUG-3 / BUG-4：

- **BUG-3 修复**：demo scene 新增 "Slime Doll" 实体挂 procedural Animator
- **BUG-4 修复**：demo scene 新增 "Static Circle (demo)" / "Static Polygon (demo)" / "Static
  EdgeChain (demo)" 三个实体，分别挂 Circle / Polygon / EdgeChain shape Collider

v0.2.5 阶段无法在编辑器内观察的 Animator section / 三种非 Circle Collider shape 段，v0.3 内
全部具备前置条件，验收时务必把这两段补跑——一并验证 v0.2.5 c9 / c8 的渲染正确性。

---

## 小节点回归路径（每次小节点都跑一遍）

> 沿用 v0.2.5 同款入门级路径，确认 v0.3 没破坏基础链路。

- [ ] Editor 启动无 crash；自动加载 `assets/scenes/demo.scene.json`
- [ ] 选中任意实体 → Inspector 显示该实体所有挂着的 component header
- [ ] 任改一个标量字段 → `Ctrl+Z` 回滚 → `Ctrl+Y` 重做，三步值一致
- [ ] 编辑任何字段后 → File 菜单的 Save 项**亮起可点**（不是灰显）
- [ ] Save 当前 scene → 关闭 editor → 重启 → 自动加载，所有字段保持
- [ ] Play → Stop 状态机走通；Stop 后**所有**带几何的实体保留原材质（**不出现**橘黑棋盘格）
- [ ] 右键任意可移除 component header → Remove Component → component 消失；后续 + Add
  Component 菜单出现该项 → 选中后 component 重新挂上（值是默认）

---

## Commit 1：demo scene 补 Animator + 非 Box Collider 实体（消化 v0.2.5 BUG-3 / BUG-4）

为 v0.2.5 BUG-3 / BUG-4 提供前置条件——之前 c9 Animator schema 与 c8 三种非 Circle Collider
shape 段因 demo scene 缺实体无法在编辑器内观察，本 commit 补齐 4 个实体。

### 验收点

- [ ] **Slime Doll 实体存在**（消化 BUG-3）
  - [ ] Entity Tree 内能看到名为 "Slime Doll" 的节点
  - [ ] 选中后 Inspector 显示 Animator section
  - [ ] Animator section 的 `Backend` 字段显示 `procedural`（灰色只读文本）
  - [ ] **不**显示 `Remove Animator` 右键菜单项（符合 c9 设计——Animator 不 Removable）
  - [ ] + Add Component 菜单内不包含 Animator 项（符合 c9 设计——不 Addable）

- [ ] **Static Circle (demo) 实体存在**（部分消化 BUG-4，验证已有 Circle case）
  - [ ] Entity Tree 内能看到名为 "Static Circle (demo)" 的节点
  - [ ] 选中后 Inspector 显示 Collider section
  - [ ] Collider section 顶部显示 `Shape: Circle` 分隔条
  - [ ] 显示 `Radius` 和 `Center` 字段
  - [ ] **不**显示 Box / Polygon / EdgeChain 任一 shape 段

- [ ] **Static Polygon (demo) 实体存在**（消化 BUG-4 Polygon case）
  - [ ] Entity Tree 内能看到名为 "Static Polygon (demo)" 的节点
  - [ ] 选中后 Inspector 显示 Collider section
  - [ ] Collider section 显示 `Shape: Polygon` 分隔条
  - [ ] **不**显示 Circle / Box / EdgeChain 任一 shape 段
  - [ ] 通用字段 `Density / Friction / Restitution / Is Sensor` 仍正常显示

- [ ] **Static EdgeChain (demo) 实体存在**（消化 BUG-4 EdgeChain case）
  - [ ] Entity Tree 内能看到名为 "Static EdgeChain (demo)" 的节点
  - [ ] 选中后 Inspector 显示 Collider section
  - [ ] Collider section 显示 `Shape: EdgeChain` 分隔条
  - [ ] **不**显示 Circle / Box / Polygon 任一 shape 段

- [ ] **跨 shape 切换实体不残留**
  - [ ] 在 Static Circle / Polygon / EdgeChain 三个实体之间快速来回切换
  - [ ] 每次切换 Inspector 内 shape 段立即换到正确的一种
  - [ ] 不出现"上一个实体的 shape 段残留"或"显示两段 shape"

### bugs
（待大节点回归后填）

---

## Commit 2：HealthComponent 端到端（游戏侧 component 通过 extraSerializers 完整 round-trip）

v0.3 milestone 核心 deliverable —— 演示一个"伪游戏侧" component（`HealthComponent { int hp;
int maxHp }`）通过 schema 注册 + extraSerializers 接进 Scene 序列化，完整走通"schema → Inspector
→ Undo/Redo → Save/Load → Play snapshot"五段链路，**无需改 editor 一行**。

依赖引擎侧两个 GAP 落地（独立 session 处理）：
- `GAP-2026-05-14-renderable-material-instance-round-trip`（顺手修复 v0.2.5 BUG-2）
- `GAP-2026-05-14-scene-serializer-extension`（`SaveOptions/LoadOptions.extraSerializers` 字段）

### 验收点

- [ ] **Test Fighter 实体存在 + Health section 显示**
  - [ ] Entity Tree 内能看到名为 "Test Fighter" 的节点
  - [ ] 选中后 Inspector 末尾出现 `Health` section（在所有内置 component 段之后，
    plugin 注册顺序决定的最末位）
  - [ ] Section 展开后显示两个字段：`HP` 和 `Max HP`，分别是数字输入控件
  - [ ] `HP` 初始值显示 `75`，`Max HP` 初始值显示 `100`

- [ ] **HP / Max HP 字段可编辑 + Undo/Redo**
  - [ ] 把 `HP` 从 75 改成 80（直接输入数字或拖动）
  - [ ] 按 `Ctrl+Z`（Undo）→ HP 回到 75
  - [ ] 按 `Ctrl+Y`（Redo）→ HP 回到 80
  - [ ] 把 `Max HP` 从 100 改成 200 → Undo 回到 100 → Redo 回到 200
  - [ ] **HP 与 Max HP 互不串味**：交错编辑两个字段后 Undo 多次，能按编辑顺序逐步回滚

- [ ] **Save → 重启 → Load round-trip**
  - [ ] 把 HP 改成 88
  - [ ] 顶部 File 菜单的 `Save` 项**亮起**（不灰显）
  - [ ] 点 File > Save
  - [ ] 关闭编辑器（直接关窗口或菜单 File > Exit）
  - [ ] 重新启动编辑器
  - [ ] 选中 Test Fighter → Inspector 内 HP 仍是 88（**没有**回到默认 75 或 100）

- [ ] **Play → Stop snapshot round-trip**（"播放快照"路径，与 Save 路径不同）
  - [ ] 把 HP 改成 42
  - [ ] 顶部 ▶ Play 按钮点一下（编辑器进入 Play 模式）
  - [ ] ■ Stop 按钮点一下（回到 Edit 模式）
  - [ ] 选中 Test Fighter → Inspector 内 HP 仍是 42

- [ ] **Remove Component + Add Component（Health 也是 Removable / Addable）**
  - [ ] 右键 `Health` section 的 header → 上下文菜单出现 `Remove Component`
  - [ ] 点 `Remove Component` → Health section 消失
  - [ ] 点 + Add Component → 菜单内出现 `Health` 项
  - [ ] 选 `Health` → Health section 重新挂上，HP / Max HP 都是默认值（100 / 100）

- [ ] **跨实体不串味**
  - [ ] 选 Test Fighter 编辑 HP → 切到任何**没挂** Health 的实体（如 Ground）→ 该实体 Inspector
    **不**显示 Health section
  - [ ] 切回 Test Fighter → HP 仍是刚才改的值

### bugs
（待大节点回归后填）

---

## Commit 3：AnimatorMiniPreviewPlugin + SchemaInspector plugin 调度集成

v0.3 milestone 第二个核心 deliverable —— 第一个真实 `IEditorInspectorPlugin` case，验证 v0.2.5
c11 仅声明的 plugin 抽象在 v0.3 工业可用。在 Animator section 段末追加 mini-preview 横幅，纯
装饰式（只读，不入命令栈）。

同时落地 `SchemaInspector::DrawComponentSchemaSection` 内的 plugin 调度路径——遍历
`inspectorPlugins`，第一个 `CanHandle == true` 的 plugin 接管段渲染（`ParseBegin` 返 true 跳过
默认字段渲染；`ParseEnd` 总在段末追加 UI）。

### 验收点

- [ ] **Slime Doll 上的 mini-preview 显示**
  - [ ] 选中 "Slime Doll" 实体
  - [ ] Inspector 滚到 Animator section
  - [ ] Animator section 内除了原有 `Backend: procedural` 只读字段外，下方出现一条分隔线
  - [ ] 分隔线下方显示灰色文字：`Mini-Preview (IEditorInspectorPlugin demo)`
  - [ ] 下一行显示带颜色的状态文本：`Status: Running` 或 `Status: Finished`，**绿色**或**灰色**
  - [ ] 再下一行显示灰色文字：`Backend: procedural`（与上方 Backend 字段值一致）

- [ ] **只挂 Animator 的实体才出现 mini-preview**
  - [ ] 选中其它没挂 Animator 的实体（如 Ground / Backdrop / Tower / Test Fighter）→ Inspector
    **不**出现 Animator section，也**不**出现 Mini-Preview 横幅

- [ ] **mini-preview 纯只读，不入命令栈**
  - [ ] 看 Mini-Preview 横幅 → 没有任何可点击 / 可拖动 / 可输入的控件
  - [ ] 选别的实体后再切回 Slime Doll → mini-preview 内容显示仍正常（无残留 / 闪烁 / 错位）

- [ ] **其它 schema 段渲染未受影响（regression）**
  - [ ] 选中任意带 Renderable 的实体（如 Ground）→ Renderable section 字段仍正常显示
  - [ ] 选中 Test Fighter → Health section 仍正常显示（Health plugin 未注册——HealthComponent
    走默认 schema 渲染路径，无 mini-preview）
  - [ ] 选中带 ParticleEmitter 的实体（Fire Emitter / Sparkle Emitter）→ 14 字段 / 7 个
    SeparatorText 分组顺序与 v0.2.5 完工时完全一致
  - [ ] 9 个内置 component header 顺序未变：Name → Transform → Hierarchy → DirectionalLight
    → Renderable → RigidBody → Collider → ParticleEmitter → Animator（之后是 Health，仅
    Test Fighter 有）

### bugs
（待大节点回归后填）

---

## v0.2.5 遗留 bug 在 v0.3 内的回归状态确认

v0.2.5 完工时 milestone-bug 段悬挂 4 个 bug，v0.3 期内的处理状态：

### BUG-1（File > Save 始终置灰）—— 已修复

- **修复 commit**：v0.2.5 收尾期间一并修（`ed86a57` "OrangeEditor: 修复 File>Save 始终置灰
  (BUG-1) + v0.2.5 验收清单整改"）
- **修复方式**：`cmdStack.SetOnChanged` 钩子在 main.cpp 启动期接到 `scene.dirty = true`——
  任何 Push / Undo / Redo / EndGroup 后 dirty 立刻置 true，Save 菜单 enabled 条件 `dirty` 亮起
- **v0.3 回归确认**：本清单"小节点回归路径"第 4 / 5 条覆盖

### BUG-2（Play 后掉落物棋盘格）—— 已修复

- **修复路径**：通过 `GAP-2026-05-14-renderable-material-instance-round-trip` 在引擎 + 编辑器
  两侧分别落地（commit `5f6a031` 引擎侧 + `98865b7` 编辑器侧）
- **修复方式**：
  - 引擎：`SaveOptions/LoadOptions.namedMaterialInstances` 字段；`WriteRenderable` 把
    `materialInstance*` 反查为 id 字符串写入 JSON；`ReadRenderable` 按 id 查表恢复
  - 编辑器：`BuildNamedMaterialInstances(EditorAssetContext&)` 提供内置材质表；所有 Save/Load
    调用点透传；`ReattachMaterialInstances` by-name hardcode 兜底**已彻底删除**
- **v0.3 回归确认**：本清单"小节点回归路径"第 6 条覆盖（特别注意 Dynamic Box / 任意带几何的
  实体——v0.2.5 期间 hardcode 兜底外的实体撞棋盘格的现象现已根除）

### BUG-3（demo scene 无 Animator 实体）—— 已消化

- **消化路径**：v0.3 c1 整改 demo scene，新增 Slime Doll 实体（挂 procedural Animator）
- **v0.3 回归确认**：本清单 Commit 1 "Slime Doll 实体存在" 验收段覆盖

### BUG-4（demo scene 无 Box / Polygon / EdgeChain shape 的 Collider 实体）—— 部分消化

- **消化路径**：v0.3 c1 整改 demo scene，新增 Static Circle / Static Polygon / Static EdgeChain
  三个实体
- **未消化部分**：Box shape 仍无独立 demo 实体（geometry 几何实体内部按 BoxDesc 用，但用户视角
  下没有一个名为 "Static Box" 之类的实体专门用于观察 Box shape 段）；登记为 v0.4 demo
  整改候选
- **v0.3 回归确认**：本清单 Commit 1 三个 "Static X (demo)" 验收段覆盖

---

## v0.3 milestone 完工标记

3 个 commit 全数 ✅：

| Commit | 主题 | 状态 |
|--------|------|------|
| c1 | demo scene 补 Animator + 非 Box Collider 实体（消化 BUG-3 / BUG-4） | ✅ |
| c2 | HealthComponent 端到端（含 extraSerializers 5 处接入 + Test Fighter） | ✅ |
| c3 | AnimatorMiniPreviewPlugin + SchemaInspector plugin 调度 | ✅ |
| **节点 A 全 milestone 回归** | 待统一执行（本文档所有"[ ]" 项目） | — |

整 milestone ✅ 标记已落 `docs/editor-roadmap.md` v0.3 heading（commit `30e3346`）；本
acceptance-checklist 同时归档：节点 A 回归通过后可迁到 `docs/qa-records/` 或保留原位
（v0.2.5 同款生命周期）。

---

## v0.3 retro · 参考引擎对比

按 `docs/milestone-end-checklist.md` 第 3 步要求，对本期引入的**新机制 / 新抽象**做事后追评，
确认设计选择在 milestone 完工事后看仍然合理。

### 决策点 1 · IEditorInspectorPlugin 调度模型选 Godot begin/end 双钩子

**决策位置**：`tools/OrangeEditor/plugin/IEditorInspectorPlugin.h` + `tools/OrangeEditor/schema/SchemaInspector.cpp::DrawComponentSchemaSection`

**选项**：
- **A · Godot 风格 begin/end 双钩子**：`ParseBegin` 返 true 接管整段、`ParseEnd` 总在段末追加 UI。可同时支持"装饰式"和"接管式"两种 plugin
  - 参考：`vendor/godot/editor/editor_inspector.h` `EditorInspectorPlugin` 的 `parse_begin/parse_property/parse_category/parse_end` 多档钩子
- **B · Lumix 风格 single onGUI**：单一 `onGUI(ComponentType, EntityRef)` 钩子，plugin 接管整段；无段首/段尾分离
  - 参考：`vendor/LumixEngine/src/editor/property_grid.h` `PropertyGrid::IPlugin::onGUI`
- **C · Cocos TypeScript decorator**：装饰器模式注册 component → editor UI；与栈完全不同，不可比

**已选**：A 的最小子集（仅 begin/end，property-level 拦截 `parse_property` 推迟到真实 use case 浮出）

**事后追评**：仍合理。理由：
- 第一个真实 case（AnimatorMiniPreviewPlugin）就是装饰式（`ParseEnd` 追加 mini-preview，**不**接管），证明 begin/end 分离的价值——若选 B 就必须 plugin 自己重画整段 Backend 字段
- 但 Godot 的 4 档钩子（含 `parse_property` / `parse_category`）确实给了 future 扩展头，未来需要"plugin 拦截某具体字段而非整段"时直接加钩子即可，不破已注册 plugin。当前最小子集没有引入"plugin 与 schema 字段命名硬绑定"的接口面债
- v0.4 第二个真实 case（IEditorGizmoPlugin）已沿用同款 begin/end 风格——抽象在 v0.3 完工后仍能扩展，不需要重设计

### 决策点 2 · HealthComponent 走"双份字段表"（schema 一份 + Read/Write 一份）

**决策位置**：`tools/OrangeEditor/demo_game/HealthComponent.cpp`（Schema builder + HealthWrite/HealthRead 各列一份字段）

**选项**：
- **A · 双份手写**：schema 列 `hp / maxHp`，Read/Write 也各列 `hp / maxHp`，靠 review 保一致
- **B · Lumix `serialize_member` 风格**：从 reflection 表自动派生 Read/Write，单源真相
  - 参考：`vendor/LumixEngine/src/engine/reflection.h` `getCompiledType` / `serialize_member` 系列
- **C · Godot `_get_property_list` 反射**：运行时反射拿字段表
  - 参考：`vendor/godot/core/object/object.h` PROPERTY_HINT 系列

**已选**：A，且这是项目级 invariant 决定的（CLAUDE.md "Serialization and reflection" 节明确禁 `entt::meta` / RTTR / cereal-with-reflection / clang AST codegen）

**事后追评**：仍合理。理由：
- HealthComponent 仅 2 字段，双份维护成本极低
- 即便 component 字段数膨胀到 ParticleEmitter 那样 14 字段，CLAUDE.md 内禁令是项目级 invariant，不为单个 case 破例
- Lumix `serialize_member` 是宏 + 模板（非 codegen），看起来与 invariant 不冲突——但它隐含"reflection 数据是序列化数据"假设，会让某些 schema-only 元数据（如 ImGui display label / DragSpeed / Range）也跟着进序列化输出。OrangeEditor 通过双份显式分离这两类元数据：schema = UI 渲染契约，Read/Write = 持久化契约，**两者职责不耦合**，这是设计优势不是债

**潜在问题**：双份手写如果 schema 加字段忘加 Read/Write（或反之），现象是"Inspector 能编辑但保存丢失"或"保存的字段 Inspector 不显示"。当前没有 lint 检测——v0.4+ 字段数膨胀时如果撞坑可考虑加一个 schema/serializer 一致性 lint，但本期不预先撒网

### 决策点 3 · AnimatorMiniPreviewPlugin 选作首个真实 plugin case

**决策位置**：3 候选间通过 `AskUserQuestion` 选定（commit 30e3346）

**选项**：
- **A · Animator mini-preview**：纯只读，IAnimator API（BackendName + IsFinished）已具备，工程量最小
- **B · Light 色温色环 + viewport 同步**：需要 Kelvin → RGB 经验公式 + SetField 命令栈联动；中等工程量
- **C · Material 缩略图**：需要离屏 RT 渲染缩略图；最大工程量，与 v0.5 Asset 浏览器有重叠工作

**已选**：A（用户在 AskUserQuestion 选定）

**事后追评**：仍合理。理由：
- 选 A 让 v0.3 c3 单 commit 完成"plugin dispatch 集成 + 第一个真实 plugin 落地"两件事；若选 B/C，dispatch 集成与 plugin 实现可能要拆 2 个 commit
- "纯装饰式"plugin（ParseEnd 追加 UI、不接管、不入命令栈）正好对应 IEditorInspectorPlugin 头注释"装饰式扩展"主用例——验证抽象的 minimal valid path
- B（Light 色温）+ C（Material 缩略图）作为第二 / 第三 plugin case 留给 v0.4 / v0.5 同期落，与对应 milestone 的内容关联性更强（B 与 v0.4 Light gizmo 同类、C 与 v0.5 Asset 浏览器同类）

### 综合事后追评

v0.3 三个决策点 retro 后均**未发现**事后追评失误。新机制（plugin dispatch / extraSerializers / 双份字段表 / mini-preview 选型）在 milestone 完工事后看与同栈参考引擎设计契合或合理偏离（双份字段表偏离 Lumix 是项目级 invariant 主动选择）。

**对 v0.4 的启示**（基于本 retro）：
- IEditorGizmoPlugin 沿用 begin/end 双钩子风格，不需要在 v0.4 重新评估抽象
- 游戏侧自定义 gizmo（与 HealthComponent 同款"游戏侧扩展"方向）将是 v0.4 末或 v0.5 的候选验证 case；HealthComponent 已建立"双份手写"模式可复用

---

## 文档生命周期

- v0.3 milestone 节点 A 回归通过后：本文档可归档（迁到 `docs/qa-records/` 或保留在原位作为
  v0.x 期 milestone 验收范本）；不删
- v0.4 milestone 启动时：复制本文档为 `editor-v0.4-acceptance-checklist.md`，重新填内容；
  不在本文档继续追加跨 milestone 内容（避免文档膨胀 + 上下文混淆）
