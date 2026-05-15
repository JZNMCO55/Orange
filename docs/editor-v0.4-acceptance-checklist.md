# OrangeEditor v0.4 Gizmo & 特殊对象可视化 milestone 验收清单

- 基准日期：2026-05-15
- 适用范围：OrangeEditor v0.4 "Gizmo & 特殊对象可视化" milestone 各 commit 的 UI / 行为验收
- 设计意图：与 v0.2.5 / v0.3 同款——把每个 commit 的验收点沉淀进本文档，按节点统一跑回归

## 回归节奏

**v0.4 整 milestone 完工后一次性大节点回归**，并与 v0.3 节点 A 大回归合并执行（用户决定的合并节奏，
基准 2026-05-15）。代价：v0.3 + v0.4 任一 commit 内引入的 bug 都要在合并节点内 bisect 定位，
准备好接受 `1e5ab57..HEAD` 区间扩大。

回归后处理（同 v0.2.5 / v0.3）：
- 通过的清单条目划掉（`[ ]` → `[✅]`）
- 失败的条目登记到对应 commit 节内 `### bugs` 段，包含「现象 / 复现步骤 / 候选根因」
- 修复后 push 新 commit（commit message 引用本文档 bug 编号），不重写历史

## 大节点回归的测试盲点提醒

按大节点回归容易漏的几类路径（v0.4 上下文）——回归时请刻意覆盖：

### 1. v0.3 遗留 commit 的回归

v0.3 节点 A 大回归推迟到 v0.4 完工后合并执行——v0.4 验收必须**先**确认 v0.3 三个 commit
（c1 demo scene 补实体 / c2 HealthComponent 端到端 / c3 AnimatorMiniPreviewPlugin）的所有
验收点全数通过，再走 v0.4 自身验收。`docs/editor-v0.3-acceptance-checklist.md` 的所有 `[ ]`
项目同步在合并节点跑。

### 2. picking + gizmo 与 CommandStack BeginGroup / EndGroup 的联动

v0.4 c2 起 gizmo 拖动调度 v0.2.5 c13 落地的 `CommandStack::BeginGroup / EndGroup` API ——
v0.2.5 期间这套 API 没有任何 caller 真实消费，本 milestone 是首批消费者。回归时刻意覆盖：

- **一次完整 drag = 一条 Undo 撤销**：拖 gizmo 从 A → 中间途经 B、C → 松开到 D。`Ctrl+Z` 一次
  应回到 A，**不是**回到 C 再回到 B 再回到 A
- **drag 中途切实体 / 取消（Esc）**：是否清理 pending group？v0.2.5 c13 对此未指定 caller 契约，
  v0.4 内首次具体化
- **drag + Inspector DragFloat 混合**：拖 gizmo（group 内多条 SetField）→ 接着 Inspector 拖 DragFloat
  → Undo 是否按操作时序逐步回滚

### 3. picking 与 Inspector / Entity Tree 双向同步

v0.4 c1 已落 viewport → selection 单向；后续 commit 不应破坏：

- viewport 点选 → Entity Tree 高亮 + Inspector 切换
- Entity Tree 点选 → Inspector 切换（v0.1 已有；v0.4 内不应回退）
- viewport 空白点选 → 清除选中 → Inspector 空 / Entity Tree 无高亮

### 4. gizmo overlay 不污染 viewport 渲染

gizmo 走 viewport overlay sub-pass（不入 RenderGraph 主路径），回归时确认：

- gizmo 不影响 demo scene 内既有视觉（HDR / Bloom / Tonemap / 阴影 / 体积光 / 粒子）
- 隐藏 gizmo（v0.4 工具栏开关，c5 落）后 viewport 视觉与 v0.3 完工时**像素级一致**

### 5. Play Mode 期间 gizmo 与 picking 行为

`InspectorPanel.cpp` 在 Play Mode 期间走 `BeginDisabled`；gizmo / picking 应类似处理：

- Play 期间 viewport 点击是否仍触发 picking？（设计选项：禁用 / 仅高亮不入命令栈 / 完全允许）
- Play 期间 gizmo 是否仍可拖？（推荐：禁用，Play snapshot 不接收编辑期 mutate）
- Stop 后 picking / gizmo 立即可用，cmdStack 状态保持

具体策略由 c2 决定并文档化于本清单。

---

## 小节点回归路径（每次小节点都跑一遍）

> 沿用 v0.2.5 / v0.3 同款入门级路径，确认 v0.4 没破坏基础链路。

- [ ] Editor 启动无 crash；自动加载 `assets/scenes/demo.scene.json`
- [ ] 选中任意实体 → Inspector 显示该实体所有挂着的 component header
- [ ] 任改一个标量字段 → `Ctrl+Z` 回滚 → `Ctrl+Y` 重做，三步值一致
- [ ] 编辑任何字段后 → File 菜单的 Save 项**亮起可点**（不是灰显）
- [ ] Save 当前 scene → 关闭 editor → 重启 → 自动加载，所有字段保持
- [ ] Play → Stop 状态机走通；Stop 后所有带几何的实体保留原材质
- [ ] 右键任意可移除 component header → Remove Component → component 消失；后续 + Add
  Component 菜单出现该项 → 选中后 component 重新挂上（值是默认）

---

## Commit 1：Viewport picking (ray-AABB hit-test) ✅

`EditorPicking.{h,cpp}`：editor-side 自包含 picking 工具——`BuildEditorCamera` 复用相机 + 投
影；`ComputeMeshLocalAABB` 扫描 `MeshAsset` 顶点；`TransformAABB` 走 8 角点变换（T*R*S，匹配
`src/render/RenderScene.cpp::ComposeWorldMatrix`，层级关系暂忽略，与引擎渲染端当前限制一致）；
`RayAABBIntersect` slab 算法 + epsilon 退化轴防御；`PickEntityAt` 遍历 (Transform, Renderable)
view 取最近击中实体 / `Entity::Invalid()`。

`panels/ScenePanel.cpp`：LMB-release on `ImGui::Image` 触发 picking——4px drag threshold 区分
click 与相机轨道拖动；screen → item-local → NDC（Vulkan y-down 与 ImGui y-down 对齐，无额外
翻转）；击中 → `selection.selectedEntity` / 未击中 → `Entity::Invalid`（清除选中，对齐 Cocos /
Unreal / Unity 惯例）；切实体时 invalidate `transformEulerCacheEntity`（与 Quat case 一致）。

Entity Tree 通过既有 selection 绑定自动高亮（无需双向布线）。

### 验收点

- [ ] **viewport 内点击实体即选中**
  - [ ] 启动编辑器、自动加载 demo scene
  - [ ] 在 Scene 面板内对任意可见几何体（Ground / Tower / Box / Pillar / Static Circle 等）
    点击鼠标**左键**
  - [ ] Inspector 立即切换到该实体（Name 段显示该实体名 / Transform 段显示其位姿）
  - [ ] Entity Tree 内对应节点高亮（与 v0.1 既有 selection 视觉一致）

- [ ] **viewport 空白区域点击清除选中**
  - [ ] 选中任意实体 → 在 Scene 面板 viewport 空白处（如远处天空 / 镜头外区域）点左键
  - [ ] Inspector 立即清空（无任何 component header）
  - [ ] Entity Tree 内之前高亮节点回到非高亮态

- [ ] **drag vs click 区分（4px 阈值）**
  - [ ] 选中实体 A → 在 viewport 内按住左键**拖动超过几个像素**再松开
  - [ ] **不**触发 picking（A 仍保持选中，Inspector 不切换）
  - [ ] 选中实体 A → 在 viewport 内按住左键**几乎不动**（< 4px）就松开
  - [ ] 触发 picking（命中谁就选谁）

- [ ] **多次点击不串味**
  - [ ] 连续点击 5~10 个不同实体，每次 Inspector 立即换到新实体
  - [ ] 选 A 编辑 Transform.rotation 多次 → 点 B → 再点回 A → A 的 Euler 字段显示与编辑后
    一致（**不**显示 0/默认值——cache 切实体时正确清理）

- [ ] **Entity Tree → viewport 单向（已有路径未回退）**
  - [ ] 在 Entity Tree 内点选某实体 → Inspector 切换（v0.1 既有行为，c1 不应破坏）
  - [ ] viewport 端**当前无 outline 视觉**（outline / 选中高亮在后续 commit；c1 仅 Inspector
    + Entity Tree 端可观察选中态）

- [ ] **picking 命中精度**
  - [ ] 选 demo scene 内一个**有旋转**的实体（如 Tower 若有非零 rotation）→ 点击其几何视觉中心
    应命中
  - [ ] 选两个相互遮挡的实体 → 点击近处那个应命中近处（取最近击中）
  - [ ] 点击粒子 emitter / DirectionalLight 实体（无几何或仅图标）→ 当前**不能**命中（c1 仅
    AABB 命中带 Renderable 的实体；图标 picking 在 c4 IEditorGizmoPlugin 落地）

### bugs
（待大节点回归后填）

---

## Commit 2：Translate Gizmo + CommandStack BeginGroup / EndGroup 首批消费

viewport overlay 显示 3 轴 world-space translate gizmo（X 红 / Y 绿 / Z 蓝），鼠标拖某轴 →
实体 `TransformComponent.position` 沿该轴更新；一次完整 drag = `BeginGroup("Translate Drag",
MergeMode::Ends)` + 多条 `SetFieldValueCommand<Vec3>`（fieldKey `"Transform.position"` 与
SchemaInspector 同步）+ `EndGroup`，`Ctrl+Z` 一次回到拖动前。本 commit 同时是 v0.2.5 c13 落地
的 `BeginGroup / EndGroup` API 的**首批真实 caller**。

实现要点：
- `tools/OrangeEditor/context/EditorGizmoState.h`：第 5 个 sub-context，承载 hoveredAxis /
  draggingAxis / dragStart* 跨帧状态（按 v0.2.5 c1 拆 context 同款架构纪律新增）
- `tools/OrangeEditor/EditorTranslateGizmo.{h,cpp}`：gizmo 绘制 + 2D 屏幕空间 hit-test +
  drag 数学（mouse ray 与 axis line 最近点）+ ImDrawList overlay 绘制 + 输入路由
- `panels/ScenePanel.cpp`：`DrawAndHandleTranslateGizmo` 调用插入在 `ImGui::Image` 之后 /
  picking 触发之前；返回值作 picking gate（gizmo 接管 LMB 时跳过 picking）
- Play Mode 期间 gizmo 既不绘制也不响应输入；拖动中撞 playState 切换 / entity 失效 /
  Transform 被 Remove → 自动 EndGroup + 重置 state

参考实现：`vendor/LumixEngine/src/editor/gizmo.cpp` Translate 段（2D 屏幕距离阈值 / closest
point on axis 数学）；handle 屏幕长度自适应（投影 (entity+1*X) 与 entity 屏幕距离换算 world
units per ~90 px）。

**deferred 到 c3+**：rotate gizmo / scale gizmo / W/E/R 模式切换 / world vs local 切换 UI /
多选 gizmo 中心点 / handle 屏幕长度像素级精确（当前 ~90 px 启发式，足够 c2 验证）。

### 验收点

- [ ] **gizmo 显示 + 选中实体时可见**
  - [ ] 启动编辑器、自动加载 demo scene
  - [ ] 在 viewport 内点击任意带几何的实体（Ground / Tower / Box / Pillar 等）
  - [ ] 选中后在该实体位置看到 3 条彩色 axis line + 箭头三角：**红轴沿 +X / 绿轴沿
    +Y / 蓝轴沿 +Z**
  - [ ] gizmo 屏幕长度看上去约 80~100 像素（远近实体大致一致——不会"远处变成一个点 /
    近处充满整个屏幕"）

- [ ] **未选中时 gizmo 不显示**
  - [ ] 点 viewport 空白处清除选中 → 整个 viewport 内**不**出现任何 axis line / 箭头
  - [ ] 重新点选实体 → gizmo 立即出现在新实体位置

- [ ] **hover 高亮**
  - [ ] 鼠标移到某条 axis line 上（不点击）→ 该 axis 颜色变亮 + 线条加粗
  - [ ] 鼠标移开 → axis 立即回到原色 / 原粗细
  - [ ] 三条 axis 同时 hover-test 不串味（只有最近的那条高亮）

- [ ] **基础拖动 + 视觉同步**
  - [ ] 选中实体 → 按住红色 X 轴 → 沿屏幕水平方向拖动鼠标
  - [ ] 实体在 viewport 内沿 +X / -X 方向移动；同时 Inspector 内 Transform.position.x
    数值实时变化
  - [ ] 同样验证绿色 Y 轴（实体沿 ±Y 移动）+ 蓝色 Z 轴（沿 ±Z 移动）

- [ ] **一次 drag = 一次 Undo（CommandStack BeginGroup 首批消费）**
  - [ ] 记下实体起始 position（例如 `(0, 0, 0)`）
  - [ ] 按住红 X 轴拖动到大约 `(3, 0, 0)`（中间途经多个像素 → 内部 push 多帧命令，靠
    intra-group coalesce 合并）
  - [ ] 松开 LMB → 实体停在 `(3, 0, 0)` 附近
  - [ ] 按 `Ctrl+Z` **一次** → 实体**回到** `(0, 0, 0)`（不是中间某个值——验证整次拖动是
    单条 Undo 条目）
  - [ ] 按 `Ctrl+Y` 一次 → 实体回到拖动结束的位置

- [ ] **拖动与轨道相机 LMB 互不冲突**
  - [ ] 不选中实体时按住 LMB 拖动 → 相机轨道旋转（v0.1 行为）
  - [ ] 选中实体后**不在 gizmo handle 上**按住 LMB 拖动 → 相机轨道旋转
  - [ ] 选中实体**在 gizmo handle 上**按住 LMB 拖动 → gizmo 拖动（不旋转相机）
  - [ ] 拖完 gizmo 松手 → **不**触发 picking（选中实体不变；典型撞坑：松手时既拖完
    gizmo 又触发 picking 把选中改成 handle 下方的物体）

- [ ] **drag 中途切实体 / 删除实体的安全终止**
  - [ ] 选实体 A → 开始拖 X 轴（按住 LMB）→ 在 Entity Tree 中点选另一个实体 B
  - [ ] 当前选中切到 B；A 的拖动**停止**（A 不再继续被拖）
  - [ ] 仍按住 LMB → 不进入 B 的拖动（gizmo 期待 fresh click）
  - [ ] 松开 LMB → 无任何 crash / 错误日志
  - [ ] cmdStack 状态完整：再编辑 B 的任意字段 → Undo / Redo 正常

- [ ] **Play Mode 期间 gizmo 禁用**
  - [ ] 选中带几何的实体 → 看到 gizmo
  - [ ] 顶部 ▶ Play 按钮点一下 → 进入 Play Mode → gizmo **消失**（不绘制）
  - [ ] Play 期间 viewport 内拖动鼠标 → 不修改任何实体 position（gizmo 不响应输入）
  - [ ] ■ Stop 按钮点一下 → 回到 Edit Mode → gizmo **重新出现**在该实体位置
  - [ ] Stop 后立即拖 gizmo → 与 Play 之前行为一致（cmdStack 未被破坏）

- [ ] **相机后方实体不绘制**
  - [ ] 选中实体 → 旋转相机让该实体走出视锥（在 viewport 看不到）
  - [ ] gizmo 不在 viewport 边缘 / 角落出现"飘"的 axis line
  - [ ] 旋转相机让实体重新进入视锥 → gizmo 立即恢复显示

- [ ] **Inspector / gizmo 双路径写 position 不串味**
  - [ ] 选中实体 → 在 Inspector 拖动 Transform.position.x DragFloat 改到 `2`
  - [ ] 在 viewport 拖 X 轴再加 `3` → 实体在 `(5, 0, 0)` 附近
  - [ ] `Ctrl+Z` 一次 → 回到 `(2, 0, 0)`（仅回滚 gizmo 那次拖动）
  - [ ] 再 `Ctrl+Z` 一次 → 回到 `(0, 0, 0)`（回滚 Inspector 那次编辑）
  - [ ] 验证两条路径都走命令栈、互不串味、按操作时序逐步回滚

### bugs
（待大节点回归后填）

---

## Commit 3+ 占位

后续 commit 按 editor-roadmap.md v0.4 deliverables 顺序推进，每个 commit 落地时在本文档**追加**
一节，结构同上：

- **c3**：Rotate gizmo + Scale gizmo + W/E/R 模式切换
- **c4**：Light gizmo（DirectionalLight 方向箭头）+ ParticleEmitter gizmo（spawn box +
  velocity 向量）—— 走 v0.2.5 c12 落地的 `IEditorGizmoPlugin` 抽象（首批真实消费）
- **c5**：Camera frustum（选中带 Camera 组件实体显示线框）+ Viewport 工具栏（视图模式 /
  Shaded / Wireframe / Camera mode / Gizmo on-off 总开关；参 Cocos Creator 截图布局）

具体 commit 数和顺序在开工时按需调整；本占位仅为对齐 `editor-roadmap.md` v0.4 deliverables
列表的方向参考。

**TODO（按需补，editor-roadmap.md 标 "取决于第一款游戏是否真用到"）**：Region (trigger) /
Spline / Sound radius / Nav mesh gizmo —— 不进 v0.4 critical path，第一款游戏 fork 后真实
撞到再升格。

---

## v0.4 milestone 完工标记

c1 ~ cN 全数 ✅ 后，整 milestone ✅ 标记落 `docs/editor-roadmap.md` v0.4 heading；本
acceptance-checklist 同时归档（v0.2.5 / v0.3 同款生命周期）。

| Commit | 主题 | 状态 |
|--------|------|------|
| c1 | Viewport picking (ray-AABB hit-test) | ✅ |
| c2 | Translate gizmo + CommandStack BeginGroup 首批消费 | — |
| c3 | Rotate / Scale gizmo + W/E/R 切换 | — |
| c4 | Light + ParticleEmitter gizmo (IEditorGizmoPlugin 首批消费) | — |
| c5 | Camera frustum + Viewport 工具栏 | — |
| **节点 A 全 milestone 回归** | **与 v0.3 节点 A 合并执行**（用户决定的合并节奏） | — |

---

## v0.4 retro · 参考引擎对比

按 `docs/milestone-end-checklist.md` 第 3 步要求，对本期引入的**新机制 / 新抽象**做事后追评。
v0.4 milestone 完工时填本节，结构同 v0.3 retro 段。

候选决策点（c2+ 落地后回填）：
- gizmo handle hit-test 算法（screen-space 距离 vs world-space ray-cylinder/cone）
- gizmo 拖动 plane 选择策略（轴正交 plane vs 视线垂直 plane）
- world vs local space 切换是否本期落（vs 推迟到 v1.x）
- IEditorGizmoPlugin 第一个真实 case 选型（c4 落地时记录 AskUserQuestion 三选项）

---

## 文档生命周期

- v0.4 milestone 节点 A 回归通过后：本文档可归档（迁到 `docs/qa-records/` 或保留在原位作为
  v0.x 期 milestone 验收范本）；不删
- v0.5 milestone 启动时：复制本文档为 `editor-v0.5-acceptance-checklist.md`，重新填内容；
  不在本文档继续追加跨 milestone 内容（避免文档膨胀 + 上下文混淆）
