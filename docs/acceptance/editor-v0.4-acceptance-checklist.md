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
验收点全数通过，再走 v0.4 自身验收。`docs/acceptance/editor-v0.3-acceptance-checklist.md` 的所有 `[ ]`
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

- [x] Editor 启动无 crash；自动加载 `assets/scenes/demo.scene.json`
  - 并未默认加载demo.scene.json,加载的是seedworld
- [✅] 选中任意实体 → Inspector 显示该实体所有挂着的 component header
- [✅] 任改一个标量字段 → `Ctrl+Z` 回滚 → `Ctrl+Y` 重做，三步值一致
- [✅] 编辑任何字段后 → File 菜单的 Save 项**亮起可点**（不是灰显）
- [✅] Save 当前 scene → 关闭 editor → 重启 → 自动加载，所有字段保持
- [✅] Play → Stop 状态机走通；Stop 后所有带几何的实体保留原材质
- [✅] 右键任意可移除 component header → Remove Component → component 消失；后续 + Add
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

- [✅] **viewport 内点击实体即选中**
  - [ ] 启动编辑器、自动加载 demo scene
  - [✅] 在 Scene 面板内对任意可见几何体（Ground / Tower / Box / Pillar / Static Circle 等）
    点击鼠标**左键**
  - [✅] Inspector 立即切换到该实体（Name 段显示该实体名 / Transform 段显示其位姿）
  - [✅] Entity Tree 内对应节点高亮（与 v0.1 既有 selection 视觉一致）

- [✅] **viewport 空白区域点击清除选中**
  - [✅] 选中任意实体 → 在 Scene 面板 viewport 空白处（如远处天空 / 镜头外区域）点左键
  - [✅] Inspector 立即清空（无任何 component header）
  - [✅] Entity Tree 内之前高亮节点回到非高亮态

- [✅] **drag vs click 区分（4px 阈值）**
  - [✅] 选中实体 A → 在 viewport 内按住左键**拖动超过几个像素**再松开
  - [✅] **不**触发 picking（A 仍保持选中，Inspector 不切换）
  - [✅] 选中实体 A → 在 viewport 内按住左键**几乎不动**（< 4px）就松开
  - [✅] 触发 picking（命中谁就选谁）

- [✅] **多次点击不串味**
  - [✅] 连续点击 5~10 个不同实体，每次 Inspector 立即换到新实体
  - [✅] 选 A 编辑 Transform.rotation 多次 → 点 B → 再点回 A → A 的 Euler 字段显示与编辑后
    一致（**不**显示 0/默认值——cache 切实体时正确清理）

- [✅] **Entity Tree → viewport 单向（已有路径未回退）**
  - [✅] 在 Entity Tree 内点选某实体 → Inspector 切换（v0.1 既有行为，c1 不应破坏）
  - [✅] viewport 端**当前无 outline 视觉**（outline / 选中高亮在后续 commit；c1 仅 Inspector
    + Entity Tree 端可观察选中态）

- [✅] **picking 命中精度**
  - [✅] 选 demo scene 内一个**有旋转**的实体（如 Tower 若有非零 rotation）→ 点击其几何视觉中心
    应命中
  - [✅] 选两个相互遮挡的实体 → 点击近处那个应命中近处（取最近击中）
  - [✅] 点击粒子 emitter / DirectionalLight 实体（无几何或仅图标）→ 当前**不能**命中（c1 仅
    AABB 命中带 Renderable 的实体；图标 picking 在 c4 IEditorGizmoPlugin 落地）

### bugs
（无新增；节点 A 大回归补跑后若仍无新增，本段保持）
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

- [✅] **gizmo 显示 + 选中实体时可见**
  - [✅] 启动编辑器、自动加载 demo scene
  - [✅] 在 viewport 内点击任意带几何的实体（Ground / Tower / Box / Pillar 等）
  - [✅] 选中后在该实体位置看到 3 条彩色 axis line + 箭头三角：**红轴沿 +X / 绿轴沿
    +Y / 蓝轴沿 +Z**
  - [✅] gizmo 屏幕长度看上去约 80~100 像素（远近实体大致一致——不会"远处变成一个点 /
    近处充满整个屏幕"）

- [✅] **未选中时 gizmo 不显示**
  - [✅] 点 viewport 空白处清除选中 → 整个 viewport 内**不**出现任何 axis line / 箭头
  - [✅] 重新点选实体 → gizmo 立即出现在新实体位置

- [✅] **hover 高亮**
  - [✅] 鼠标移到某条 axis line 上（不点击）→ 该 axis 颜色变亮 + 线条加粗
  - [✅] 鼠标移开 → axis 立即回到原色 / 原粗细
  - [✅ ] 三条 axis 同时 hover-test 不串味（只有最近的那条高亮）

- [✅] **基础拖动 + 视觉同步**
  - [✅] 选中实体 → 按住红色 X 轴 → 沿屏幕水平方向拖动鼠标
  - [✅] 实体在 viewport 内沿 +X / -X 方向移动；同时 Inspector 内 Transform.position.x
    数值实时变化
  - [✅] 同样验证绿色 Y 轴（实体沿 ±Y 移动）+ 蓝色 Z 轴（沿 ±Z 移动）

- [ ] **一次 drag = 一次 Undo（CommandStack BeginGroup 首批消费）**
  - [✅] 记下实体起始 position（例如 `(0, 0, 0)`）
  - [✅] 按住红 X 轴拖动到大约 `(3, 0, 0)`（中间途经多个像素 → 内部 push 多帧命令，靠
    intra-group coalesce 合并）
  - [✅] 松开 LMB → 实体停在 `(3, 0, 0)` 附近
  - [✅] 按 `Ctrl+Z` **一次** → 实体**回到** `(0, 0, 0)`（不是中间某个值——验证整次拖动是
    单条 Undo 条目）
  - [✅] 按 `Ctrl+Y` 一次 → 实体回到拖动结束的位置

- [✅] **拖动与轨道相机 LMB 互不冲突**
  - [✅] 不选中实体时按住 LMB 拖动 → 相机轨道旋转（v0.1 行为）
  - [✅] 选中实体后**不在 gizmo handle 上**按住 LMB 拖动 → 相机轨道旋转
  - [✅] 选中实体**在 gizmo handle 上**按住 LMB 拖动 → gizmo 拖动（不旋转相机）
  - [✅] 拖完 gizmo 松手 → **不**触发 picking（选中实体不变；典型撞坑：松手时既拖完
    gizmo 又触发 picking 把选中改成 handle 下方的物体）

- [✅] **drag 中途切实体 / 删除实体的安全终止**
  - [✅] 选实体 A → 开始拖 X 轴（按住 LMB）→ 在 Entity Tree 中点选另一个实体 B
  - [✅] 当前选中切到 B；A 的拖动**停止**（A 不再继续被拖）
  - [✅] 仍按住 LMB → 不进入 B 的拖动（gizmo 期待 fresh click）
  - [✅] 松开 LMB → 无任何 crash / 错误日志
  - [✅] cmdStack 状态完整：再编辑 B 的任意字段 → Undo / Redo 正常

- [✅] **Play Mode 期间 gizmo 禁用**
  - [✅] 选中带几何的实体 → 看到 gizmo
  - [✅] 顶部 ▶ Play 按钮点一下 → 进入 Play Mode → gizmo **消失**（不绘制）
  - [✅] Play 期间 viewport 内拖动鼠标 → 不修改任何实体 position（gizmo 不响应输入）
  - [✅] ■ Stop 按钮点一下 → 回到 Edit Mode → gizmo **重新出现**在该实体位置
  - [✅] Stop 后立即拖 gizmo → 与 Play 之前行为一致（cmdStack 未被破坏）

- [✅] **相机后方实体不绘制**
  - [✅] 选中实体 → 旋转相机让该实体走出视锥（在 viewport 看不到）
  - [✅] gizmo 不在 viewport 边缘 / 角落出现"飘"的 axis line
  - [✅] 旋转相机让实体重新进入视锥 → gizmo 立即恢复显示

- [✅] **Inspector / gizmo 双路径写 position 不串味**
  - [✅] 选中实体 → 在 Inspector 拖动 Transform.position.x DragFloat 改到 `2`
  - [✅] 在 viewport 拖 X 轴再加 `3` → 实体在 `(5, 0, 0)` 附近
  - [✅] `Ctrl+Z` 一次 → 回到 `(2, 0, 0)`（仅回滚 gizmo 那次拖动）
  - [✅] 再 `Ctrl+Z` 一次 → 回到 `(0, 0, 0)`（回滚 Inspector 那次编辑）
  - [✅] 验证两条路径都走命令栈、互不串味、按操作时序逐步回滚

### bugs
（无新增；节点 A 大回归补跑后若仍无新增，本段保持）

---

## Commit 3：Rotate + Scale gizmo + W/E/R 模式切换

`tools/OrangeEditor/EditorRotateGizmo.{h,cpp}` + `EditorScaleGizmo.{h,cpp}` 引入，配合
`EditorGizmoState` 增 `Mode` enum（Translate / Rotate / Scale）+ rotate / scale 拖动起点字
段（`dragStartEntityRot` / `dragStartRotateRef` / `dragStartEntityScale` /
`dragStartScaleRefSigned` / `dragStartMouseScreen`）。`ScenePanel` 按 `host.gizmo.mode`
分派对应 gizmo；W/E/R 键盘快捷键切换模式（不依赖 viewport hover，但要求 ImGui 无文本输入
active 以免拦截字母键；mid-drag 不切换以保持原子性）。

同 PR 把 c2 期 `EditorTranslateGizmo.cpp` anon ns 里的 4 个 helper（`ProjectWorldToScreen` /
`ScreenToWorldRay` / `PointSegmentDistance2D` / `ClosestPointOnAxisToRay`）+ 新增的
`RayPlaneIntersect` 提取到 `EditorGizmoMath.{h,cpp}`（内部 helper，命名空间
`OrangeEditor::Internal::GizmoMath`，仅 editor target 内消费），三个 gizmo cpp 共享。避免
c3+ 多 gizmo 跨文件重复维护"Vulkan NDC y-down"等坐标系约定。

CommandStack 联动：
- Rotate：`BeginGroup("Rotate Drag")` + per-frame `SetFieldValueCommand<glm::quat>(entity,
  "Transform.rotation", oldVal=dragStartEntityRot, newVal=newRot)`；apply lambda 同步
  invalidate `selection.transformEulerCacheEntity`，让 Inspector 下一帧 Quat case 重算
  Euler 显示（与 SchemaInspector.cpp Quat case invalidate 路径对偶）
- Scale：`BeginGroup("Scale Drag")` + per-frame `SetFieldValueCommand<glm::vec3>(entity,
  "Transform.scale", oldVal=dragStartEntityScale, newVal=newScale)`；单轴拖按沿轴 signed
  距离 ratio 乘单轴；中心 `Axis::Center` 走 uniform 屏幕 (dx+dy)/100+1 乘全 3 轴；factor
  clamp [0.01, 100] 防退化

参考实现：
- `vendor/LumixEngine/src/editor/gizmo.cpp` Rotate 段（polyline 圆环 + signed angle
  atan2 路径 + axisAngle quat 累乘）+ Scale 段（屏幕距离启发 + 沿轴 signed 比例）
- 圆环切线方向参数化（`MakePlaneBasis` 取与 axis 正交的 u/v 单位向量，cos/sin 描点 48
  段）

### 验收点

- [✅] **W/E/R 模式切换基础**
  - [✅] 选中带 Transform 的实体（demo scene 任意实体均可）
  - [✅] 默认看到 Translate gizmo（3 个箭头）—— 编辑器启动后默认 mode
  - [✅] 按 `W` 键 → 仍是 Translate gizmo（无变化）
  - [✅] 按 `E` 键 → gizmo **变成 3 个圆环**（X 红 / Y 绿 / Z 蓝，绕实体位置呈球面）
  - [✅] 按 `R` 键 → gizmo **变成 3 个箭头 + tip 小方块 + 中心白色小方块**（scale 视觉）
  - [✅] 再按 `W` 键 → 回到 Translate 箭头
  - [✅] 三种模式视觉清晰可区分：translate 三角箭头 / rotate 圆环 / scale 立方体 tip

- [✅] **模式切换不打断拖动**
  - [✅] 选实体 → 按 `W` → 按住红 X 箭头开始拖动（不松开）
  - [✅] 拖动中按 `E` 或 `R` 键 → gizmo **保持 Translate 模式**（不切换到 Rotate / Scale）
  - [✅] 松开 LMB 后再按 `E` → 切换到 Rotate gizmo
  - [✅] 同理：rotate 拖动中按 W/R 也不切换；scale 拖动中按 W/E 也不切换

- [✅] **模式切换不被 Inspector 文本框拦截**
  - [✅] 在 Inspector 的 Name 字段 InputText 内点击 → 光标进入文本框
  - [✅] 按 `W` / `E` / `R` 键 → 字母进入 Name 字段（**不**切换 gizmo 模式）
  - [✅] 点击 viewport 空白处（脱出 InputText focus）
  - [✅] 再按 `W` / `E` / `R` → 正常切换 gizmo 模式

- [✅] **Rotate gizmo 拖动 + Undo/Redo**
  - [✅] 选实体 → 按 `E` 切到 Rotate → Inspector 记下 Transform.rotation 初始 Euler 值
  - [✅] 按住红色 X 圆环 → 拖动鼠标绕圆周 → 实体绕 X 轴旋转；同时 Inspector 的 Rotation
    字段数值实时变化
  - [✅] 松开 LMB → 实体停在新角度
  - [✅] `Ctrl+Z` 一次 → 实体**回到拖动前的角度**（不是中间某帧）+ Inspector Rotation
    回到初始值
  - [✅] `Ctrl+Y` 一次 → 回到拖动结束的角度
  - [✅] 同样验证绿色 Y 圆环 + 蓝色 Z 圆环

- [✅] **Rotate Inspector / gizmo 双路径不串味**
  - [✅] 选实体 → Inspector 拖 Rotation Y 改到 `45°`
  - [✅] 按 `E` 切 Rotate → 拖 X 圆环再加 `30°`
  - [✅] `Ctrl+Z` 一次 → 仅回滚 gizmo 那次（X 旋转回到 0°；Y 仍是 45°）
  - [✅] 再 `Ctrl+Z` 一次 → Y 也回滚到 0°（Inspector 那次）
  - [✅] **关键**：gizmo 拖动期间 Inspector 的 Rotation 字段应**实时**显示新 Euler 值
    （不是停在旧值——验证 `transformEulerCacheEntity` invalidate 正确）

- [✅] **Scale 单轴拖动**
  - [✅] 选实体 → 按 `R` 切 Scale → Inspector 记下 Transform.scale 初始 `(1, 1, 1)`
  - [✅] 按住红 X 箭头 → 沿 X 方向**远离原点**拖动 → 实体在 X 方向**变长**；Inspector
    scale.x 数值实时增大
  - [✅] 反向拖动（朝原点 → 沿轴负方向）→ 实体在 X 方向**变短**；scale.x 数值缩小
  - [✅] 极端拖动到很小（不越过 0）→ scale.x clamp 到 ≥ 0.01（不应变 0 / 负数）
  - [✅] 松开 LMB → `Ctrl+Z` 一次回到 `(1, 1, 1)`
  - [✅] 同理验证 Y 轴（scale.y 变化，X/Z 不变）+ Z 轴

- [✅] **Scale uniform 中心 handle**
  - [✅] 选实体 → 按 `R` → 中心**白色小立方体**位于 gizmo 原点（与 3 轴起点重合）
  - [✅] hover 中心立方体 → 立方体颜色变亮（黄色高亮）
  - [✅] 按住中心 → 鼠标**向右下角**拖动 → 实体**整体放大**；Inspector scale.x/y/z **同步**
    变大（uniform）
  - [✅] 反向（向左上角）拖动 → 实体整体缩小；scale 三轴同步变小
  - [✅] `Ctrl+Z` 一次回到 `(1, 1, 1)` —— 三轴一起回滚

- [✅] **Scale 各模式 hit-test 优先级**
  - [✅] 中心立方体在原点附近 → 鼠标放在原点中心 → hover 高亮的是**中心**（不是某条轴）
  - [✅] 鼠标稍微离开原点（往 X 方向移） → hover 高亮的是 **X 轴**（不是中心）
  - [✅] 切换不串味（不出现"中心和 X 同时高亮"）

- [✅] **三模式跨模式切换 + cmdStack 干净**
  - [✅] 拖 Translate X 移到某位置 → 按 `E` 切 Rotate → 拖 Y 圆环 → 按 `R` 切 Scale →
    拖中心放大
  - [✅] `Ctrl+Z` 三次 → 逐步回滚 scale → rotate → translate，每步对应一次拖动
  - [✅] 每次 `Ctrl+Z` 期间 gizmo **保持当前模式**（不会被 Undo 反向切回旧模式——gizmo
    mode 不入命令栈，是 UI 状态）

- [✅] **Play Mode 期间所有 3 模式 gizmo 都禁用**
  - [✅] 选实体 → 按 W → 看到 Translate gizmo
  - [✅] ▶ Play → gizmo **消失**（同 c2 行为）
  - [✅] Play 期间按 E / R → mode 字段可能更新（无危害）但 gizmo 仍不绘制不响应
  - [✅] ■ Stop → gizmo 重新出现，按当前 mode 显示对应类型
  - [✅] 拖动正常工作，cmdStack 状态保持

- [✅] **picking gate 仍正确（c2 行为不回退）**
  - [✅] 三模式之一拖完 gizmo 松手 → **不**触发 picking（选中实体不变）
  - [✅] viewport 空白处单击（任何模式下）→ 触发 picking（清除选中或选中下方物体）

### bugs
1. 在进行Rotation后，拉伸Scale的line，并不会沿着指定方向拉伸

---

## Commit 4：Light + ParticleEmitter gizmo via IEditorGizmoPlugin（v0.2.5 c12 抽象首批真实消费）

新建 `plugin/GizmoContext.h` 定义 plugin Draw 钩子入参（viewProj / imageOrigin / imageSize /
ImDrawList\*；HitTest 相关字段留 v0.4+ 真撞到需求再加，对偶 v0.2.5 c12 头注释"fields 增加不破坏现
有 plugin 编译"纪律）。新建两个 concrete plugin：

- `plugin/DirectionalLightGizmoPlugin.{h,cpp}`：选中实体挂 `DirectionalLight` 时，从 entity world
  位置沿 `direction` 单位方向画**黄色 3D 箭头**（屏幕长度 ~100 px 自适应）。CanHandle 按
  `schema.typeName == "DirectionalLight"` 匹配；HitTest 走基类默认 false（纯装饰 overlay）
- `plugin/ParticleEmitterGizmoPlugin.{h,cpp}`：选中实体挂 `ParticleEmitter` 时，画两个东西：
  - **青色 spawn box** —— entity world XY 平面内 `desc.spawnOffsetMin` / `spawnOffsetMax`
    围成的矩形（4 角投影到屏幕，连 4 段边线 + 极淡 fill）。Box 退化（min==max）跳过绘制
  - **浅青色 velocity 箭头** —— 沿 `avg(initialVelocityMin, initialVelocityMax)` 方向，屏幕
    长度 ~80 px 自适应。avg velocity = 0（length < 1e-4）时跳过箭头绘制

`panels/ScenePanel.cpp` 加 plugin dispatch：Edit Mode 且选中实体有效 + plugin 注册表非空时，
遍历每个 plugin × 每个匹配 (CanHandle && schema.has) 的 schema 对调 `Draw(host, entity, schema,
component, ctx)`。所有返回 true 的 plugin **全部**绘制（不互斥；多 plugin 可同时叠加 overlay）。
Plugin Draw 是纯装饰，**不**参与 gizmoActive 判定 → picking 不被 gate（与 c2/c3 内置 Transform
gizmo 路径正交）。

`main.cpp` 启动期 `editorHost.gizmoPlugins.push_back(...)` 注册两个 plugin。

设计参考：
- `vendor/godot/editor/plugins/node_3d_editor_gizmos.h` `EditorNode3DGizmoPlugin` 多档接口
  （OrangeEditor 走更扁平的 Draw / HitTest 直接调度，不学 Godot 的"gizmo 实例挂在 Node 上"）
- `vendor/LumixEngine/src/editor/gizmo.cpp` 没有显式 plugin 抽象（所有 gizmo 写死在 SceneView，
  不可扩展，OrangeEditor 不学）

### 验收点

- [✅] **DirectionalLight gizmo 显示**
  - [✅] 启动编辑器、自动加载 demo scene
  - [✅] 在 Entity Tree 找到 demo scene 内的 DirectionalLight 实体（名字含 "Light" 或类似），
    点选
  - [✅] viewport 内**该实体位置出现一根黄色箭头**，从实体位置沿 light direction
    （demo scene 默认大致斜下方）延伸
  - [✅] 切换 W/E/R gizmo 模式 → 黄色箭头**始终显示**（与内置 Transform gizmo 并存，不被
    隐藏）
  - [✅] 同时观察：内置 Transform gizmo（红/绿/蓝箭头 or 圆环 or 立方体）也正常显示在同一
    实体上，两套 overlay **不互相覆盖、不闪烁**

- [✅] **DirectionalLight 方向调整后箭头实时更新**
  - [✅] 选中 DirectionalLight 实体
  - [✅] 在 Inspector 内拖 Directional Light 段的 `Direction` Vec3 字段任一分量（如 X 改
    到 `1.0` / `-1.0`）
  - [✅] viewport 内**黄色箭头方向实时跟着变**（不需要切实体刷新）
  - [✅] Undo 一次 → 箭头方向回到编辑前
  - [✅] 把 direction 改成 `(0, 0, 0)` → 箭头**消失**（退化方向防御）
  - [✅] 改回非零向量 → 箭头**重新出现**

- [✅] **ParticleEmitter gizmo 显示**
  - [✅] demo scene 内的两个 ParticleEmitter 实体（火焰 + 萤火，名字含 "Fire" / "Sparkle"
    或类似），分别点选
  - [✅] 选中后 viewport 内**实体位置出现青色矩形线框**（spawn box，可能很小但要能看见）
  - [✅] 同时出现**浅青色箭头**（initial velocity 平均方向；火焰大致朝上 / 萤火也大致朝上）
  - [✅] spawn box 内**极淡的青色填充**提示是个区域（不是空线框）

- [✅] **ParticleEmitter spawn box / velocity 编辑实时更新**
  - [ ] 选中火焰 Emitter → Inspector 拖 `Spawn Offset Max` Vec2 → spawn box 在 viewport
    内**变大 / 变小 / 变形**实时跟随
  - [✅] 拖 `Initial Velocity Min/Max` Vec2 → 箭头方向实时跟随
  - [✅] 把 `Spawn Offset Min == Spawn Offset Max`（同值，最简法 → 把 Max 拖到与 Min 同值）
    → spawn box 退化消失（box 退化防御），velocity 箭头仍显示
  - [✅] 把 initialVelocity Min == Max == (0,0) → 箭头消失，spawn box 仍显示

- [✅] **未选中实体 plugin 不画 gizmo**
  - [✅] 点击 viewport 空白处清除选中 → 黄色 light 箭头 / 青色 spawn box **都消失**
  - [✅] 选不挂 DirectionalLight / ParticleEmitter 的实体（如 Ground / Tower / Test Fighter）
    → 不画 light 箭头 / spawn box（只画内置 Transform gizmo）

- [✅] **Play Mode 期间 plugin gizmo 全部禁用**
  - [✅] 选中 DirectionalLight 或 ParticleEmitter 实体 → 看到对应 overlay
  - [✅] ▶ Play 按钮 → 内置 Transform gizmo + plugin gizmo **全部消失**
  - [✅] Play 期间不出现"plugin overlay 跑出来挡视野"
  - [✅] ■ Stop → overlay 全部回来

- [✅] **plugin Draw 不入命令栈（纯装饰）**
  - [✅] 选中 DirectionalLight 实体 → cmdStack 状态记下（File>Save 是否亮）
  - [✅] 切换实体 / 拖 viewport 边角 / 旋转编辑器相机 → 不触发任何命令栈变化（File>Save
    亮 / 灰状态不变；不出现"莫名其妙生了一条命令"现象）
  - [✅] 同理 ParticleEmitter：选中后只 viewport 滚轮 / 旋转视图，cmdStack 不动

- [✅] **picking gate 不被 plugin 打断**
  - [✅] 选中 DirectionalLight → viewport 在 light 箭头**外侧**单击空白处 → 触发 picking
    （清空选中），plugin overlay 消失
  - [✅] 选中 ParticleEmitter → viewport 在 spawn box **内部**单击（落在 emitter 实体 AABB
    之外的空白区域）→ 触发 picking（清空 / 选下方物体）—— plugin overlay 不接管 LMB

### bugs
1. **DirectionalLight 在移动的过程中，物体阴影不会实时发生变化**
   - **现象**：拖动 DirectionalLight entity（Translate gizmo / Inspector 改 position）→ 黄色方向箭头起点跟着移动，但场景物体阴影完全不变
   - **复现**：选 demo scene 内 DirectionalLight 实体 → 拖 X/Y/Z 任一方向 → 观察 plane 上 cube 的 shadow → 阴影不动
   - **根因**：DirectionalLight component 的 `direction` 字段与 entity Transform 完全解耦——`Pipeline::ComputeLightViewProj` 只读 `light.direction` 不读 transform，但 gizmo 把箭头起点画在 entity position
   - **登记**：`docs/engine-known-gaps.md` GAP-2026-05-16-directional-light-transform-decoupled
   - **处置**：本 session 不修；待 GAP 评审 + 落地（候选 Convention A：废 direction 字段、改 transform-derived，与 GAP-2026-05-15 同思路）；编辑器侧**不**主动 workaround，避免与引擎 tick 撞冲突

---

## Commit 5：Camera frustum gizmo + Viewport 工具栏（v0.4 收尾）

editor-roadmap.md v0.4 最后一项 deliverable，落两个东西：

**1. Camera frustum gizmo（via IEditorGizmoPlugin）**

- 注册 Camera schema（typeName="Camera"，空字段，不 Addable / 不 Removable）—— 让
  plugin dispatch 路径走通；Inspector 内显示空 "Camera" header（不暴露 view/projection
  矩阵字段，schema 体系无 Mat4 PropertyType + 矩阵被编辑器每帧覆写，编辑无意义）
- 新建 `plugin/CameraFrustumGizmoPlugin.{h,cpp}`：选中带 `Render::Camera` 的实体时，
  画 frustum 12 段线框（near rect 4 + far rect 4 + connecting 4），青色（与 Particle
  Emitter spawn box 同色系）；near plane 更亮 + 略粗，区分远近
- 引擎缺口登记：`docs/engine-known-gaps.md` 新增 `GAP-2026-05-15-camera-editor-vs-
  runtime-separation`，详述当前 `ApplyEditorCameraToWorld` 每帧覆写 Camera component
  的限制 + 三种候选修复路径
- 临时方案：plugin 用 **hardcode 默认 fov=45° / aspect=16:9 / near=0.1 / far=10**
  + entity Transform 推 view（`lookAt(pos, pos + rot*-Z, rot*+Y)`）算 frustum 8
  corners；代码内 TODO 注释明示等 GAP 落地后切真实数据

**2. Viewport 工具栏（ScenePanel 顶部，参 Cocos Creator 3.6.0 布局）**

- **Gizmos checkbox**（实现）—— `host.gizmo.visible` 总开关；写 false 时 c2/c3 内置
  Translate/Rotate/Scale + c4/c5 所有 plugin overlay 一起隐藏（不绘制、不响应输入），
  状态保持以让用户切回 true 后 UX 一致
- **View Mode**（disabled placeholder + tooltip）—— 依赖编辑器引入 2D Lock 相机
  模式，未实现
- **Shading**（disabled placeholder + tooltip）—— 依赖 OrangeRender wireframe pass，
  按 engine-known-gaps 工作流推到 OrangeRender incoming_feature 独立 session
- **Camera Mode**（disabled placeholder + tooltip）—— Design Resolution 锁定，依赖
  CameraDesc，链接 GAP-2026-05-15

`EditorGizmoState` 加 `bool visible = true` 字段；c2/c3/c4 早退守卫统一改为
`!host.gizmo.visible || playState != Edit` 双条件。

参考实现：
- `vendor/Cocos Creator 3.6.0` viewport toolbar 布局（截图，editor-roadmap.md D5 节）
- `vendor/LumixEngine/src/editor/scene_view.cpp` `gizmo_config` toggle 实现思路

### 验收点

- [✅] **Gizmos 总开关 checkbox 基础**
  - [✅] 启动编辑器、自动加载 demo scene
  - [✅] Scene 面板顶部出现一行工具栏：`[x] Gizmos | [Persp ▼] [Shaded ▼] [Camera Mode]`
  - [✅] hover "Gizmos" checkbox → tooltip 显示总开关说明文字
  - [✅] 默认勾选状态（gizmo 默认显示）

- [ ] **Gizmos 关闭后所有 overlay 消失**
  - [ ] 选中带 Transform 的实体 → 看到内置 Transform gizmo（W/E/R 三模式之一）
  - [ ] 选中 DirectionalLight → 看到黄色方向箭头
  - [ ] 选中 ParticleEmitter → 看到青色 spawn box + 箭头
  - [ ] 选中 Camera 实体 → 看到 frustum 线框
  - [ ] **取消勾选 Gizmos checkbox** → 上述**所有** overlay 在 viewport 内同时消失
  - [ ] 重新勾选 → 全部 overlay 立即恢复

- [ ] **Gizmos 关闭期间 picking 仍正常**
  - [ ] 取消 Gizmos 勾选 → 在 viewport 内点击不同几何体 → 选中实体正确切换
  - [ ] 验证 hover gizmo handle 阻拦 picking 的行为在 visible=false 时也不会误触发

- [ ] **disabled placeholder 项的 tooltip**
  - [ ] hover "Persp" Combo / "Shaded" Combo / "Camera Mode" SmallButton（即使 disabled）
  - [ ] 每个 placeholder 弹出**说明未实现 + 依赖什么 + 登记位置**的 tooltip 文字
  - [ ] 点击 disabled 项不响应（视觉灰色 + 无 hover 高亮）

- [ ] **Camera frustum 显示**
  - [ ] 在 Entity Tree 找到 demo scene 内挂 Camera 的实体（DemoWorld 内的 "camera"
    实体，typeName 可能显示为根节点附近的 "Camera" 名字）
  - [ ] 选中后 viewport 内**出现 12 段青色线框组成的 frustum**：
    - 4 段亮青色 near rect（更亮 + 略粗）
    - 4 段普通青色 far rect
    - 4 段连接 near/far rect 的边
  - [ ] frustum 朝向跟随 entity 旋转：Inspector 改 Transform.rotation → frustum
    在 viewport 内重新指向
  - [ ] frustum 位置跟随 entity.position：Inspector 改 position → frustum 移动到
    新位置

- [ ] **Camera 实体 Inspector 显示**
  - [ ] 选中 Camera 实体 → Inspector 内出现一个**空的 "Camera" component header**
    （可折叠 / 展开，没字段）
  - [ ] **不**显示 Remove Component 右键菜单项（Camera schema 未 .Removable()）
  - [ ] +Add Component 菜单**不**包含 Camera（schema 未 .Addable()）

- [ ] **Camera frustum 当前限制（已知 fake 行为）**
  - [ ] frustum 的 fov / aspect / near / far 是 hardcode 默认值（45° / 16:9 /
    0.1 / 10）—— 视觉上是一个固定形状的锥体，**不**反映 Camera component 真实
    view/projection 矩阵
  - [ ] 用户视角：能看到"哦这个 entity 是相机，朝向那边"，但**不**能看到"游戏相
    机的实际视野有多大" —— 这是 engine-known-gaps GAP-2026-05-15 限制
  - [ ] 编辑器轨道相机移动 / 缩放 viewport → frustum 不跟着改变形状（**仅**朝向
    跟 Camera entity transform 走，形状是 hardcode）

- [ ] **多 plugin overlay 同选多 component 实体并存**
  - [ ] 找一个**同时挂 DirectionalLight + Camera** 的实体（如有）→ 验证黄色光箭
    头 + 青色 frustum 同时显示，不互相覆盖
  - [ ] 同理：同时挂 ParticleEmitter + Camera 的（如有）→ spawn box + frustum 并存

- [ ] **Play Mode 与 visible 开关正交**
  - [ ] 勾选 Gizmos → 选实体 → 看到 overlay → ▶ Play → overlay 消失（Play Mode
    强制禁用，与 visible 状态无关）
  - [ ] Stop → overlay 恢复
  - [ ] **取消** Gizmos 勾选 → ▶ Play → overlay 仍消失（visible=false + Play 双重
    禁用）→ Stop → overlay 仍不显示（visible 还是 false）
  - [ ] 勾回 Gizmos → overlay 立即恢复

### bugs
（无新增；节点 A 大回归补跑后若仍无新增，本段保持）

---

## v0.4 milestone 完工准备

c5 落地后，v0.4 "Gizmo & 特殊对象可视化" milestone 5 个 commit 全数 ✅：

- c1 viewport picking
- c2 Translate gizmo + CommandStack BeginGroup 首批消费
- c3 Rotate + Scale gizmo + W/E/R 模式切换
- c4 Light + ParticleEmitter gizmo (IEditorGizmoPlugin 首批消费)
- c5 Camera frustum gizmo + Viewport 工具栏

整 milestone ✅ 标记需要：
1. **节点 A 全 milestone 回归**（与 v0.3 节点 A 合并执行——用户决定的合并节奏）
2. milestone-end-checklist 8 步走完（含 v0.4 retro 段填写）
3. `docs/editor-roadmap.md` v0.4 heading 落 ✅
4. 跑 `scripts/check_claude_md_drift.py` 确认 CLAUDE.md 同步

具体 commit 数和顺序在开工时按需调整；本占位仅为对齐 `editor-roadmap.md` v0.4 deliverables
列表的方向参考。

**TODO（按需补，editor-roadmap.md 标 "取决于第一款游戏是否真用到"）**：Region (trigger) /
Spline / Sound radius / Nav mesh gizmo —— 不进 v0.4 critical path，第一款游戏 fork 后真实
撞到再升格。

---

## v0.4 milestone 完工标记

c1 ~ cN 全数 ✅ 后，整 milestone ✅ 标记落 `docs/editor-roadmap.md` v0.4 heading；本
acceptance-checklist 已归档到 `docs/acceptance/`（2026-05-16 与 v0.2.5 / v0.3 同期归档）。

| Commit | 主题 | 状态 |
|--------|------|------|
| c1 | Viewport picking (ray-AABB hit-test) | ✅ |
| c2 | Translate gizmo + CommandStack BeginGroup 首批消费 | ✅ |
| c3 | Rotate / Scale gizmo + W/E/R 切换 | ✅ |
| c4 | Light + ParticleEmitter gizmo (IEditorGizmoPlugin 首批消费) | ✅ |
| c5 | Camera frustum + Viewport 工具栏 | ✅ |
| **节点 A 全 milestone 回归** | **与 v0.3 节点 A 合并执行**（用户决定的合并节奏） | — |

---

## v0.4 retro · 参考引擎对比

按 `docs/milestone-end-checklist.md` 第 3 步要求，对本期引入的**新机制 / 新抽象**做事后追评，
确认设计选择在 milestone 完工事后看仍然合理。

### 决策点 1 · gizmo handle hit-test 走"2D 屏幕距离"而非"3D ray-cylinder"

**决策位置**：`tools/OrangeEditor/EditorTranslateGizmo.cpp` + `EditorRotateGizmo.cpp` +
`EditorScaleGizmo.cpp`（c2 / c3）。所有 axis handle / 圆环段 hit-test 走 2D 屏幕坐标点-线段
距离阈值（8 px），而非 world-space ray 与 cylinder / cone 几何求交。

**选项**：
- **A · 2D 屏幕距离阈值**：投影 axis 端点到屏幕，2D point-to-segment 距离 < kHitThresholdPx
  - 参 `vendor/LumixEngine/src/editor/gizmo.cpp` Translate / Rotate 段（同款 2D 距离思路）
- **B · 3D ray-cylinder 求交**：把 axis 当成有半径的世界空间圆柱，做 ray-cylinder hit-test
  - 参 Unity / Unreal source 不可读；Godot `editor_node_3d.cpp` 走 plane intersection
    + 距离阈值（接近 A 但 plane 单位是 world，阈值是 plane normal 投影距离）

**已选**：A（c2 落地时直接采用，c3 / c5 继承）

**事后追评**：仍合理。理由：
- 2D 屏幕距离阈值（像素）对 UI 直觉最对齐——用户感受到的是"鼠标离 handle 视觉中心多近"，
  而非"鼠标 ray 离 world axis 多近"；3D 距离阈值会让远处实体 handle 难选中、近处实体 handle
  误触发
- 实现简单（一行投影 + 一行点线段距离）；rotate 圆环也复用同款（48 段 polyline 每段独立 hit-test）
- 性能完全足够（c5 完工后 viewport 内 hit-test 路径每帧调 < 100 次 segment 距离检查，N/A）

**潜在问题（已知）**：c2 / c3 期没有"hover 时显示精确 hit-test 反馈"（如把高亮缩放到鼠标接近
端点而非整条 axis 平均）。这是后续 UX 优化点，不构成 v0.4 缺陷。

### 决策点 2 · 共享 helper 提取到 `EditorGizmoMath` 而非 anon ns 复制

**决策位置**：c3 重构期间。c2 期 `EditorTranslateGizmo.cpp` 的 anon ns 内有 4 个 helper
（ProjectWorldToScreen / ScreenToWorldRay / PointSegmentDistance2D / ClosestPointOnAxisToRay）；
c3 引入 Rotate / Scale 需要同款 + 新增 `RayPlaneIntersect`。

**选项**：
- **A · 提取到 `EditorGizmoMath.{h,cpp}` 内部 helper**（OrangeEditor::Internal::GizmoMath 命名空间）
- **B · c3 cpp 复制 c2 anon ns helper**（3 份独立维护）

**已选**：A（c3 落地时同步重构）

**事后追评**：仍合理。理由：
- 共享 helper 在 c4 (light/particle plugin) + c5 (camera frustum plugin) 均消费——4 个 cpp
  共享单一实现，避免"Vulkan NDC y-down 与 ImGui y-down 同向"等坐标系约定跨文件漂移
- 重构成本低（c2 测试已通过 → c3 重构是机械搬迁，外部 API 不变）
- 命名空间 `OrangeEditor::Internal::GizmoMath` 显式表达"编辑器内部 helper，不对外公开"，
  不污染 OrangeEngine 公共 API（与 EditorPicking.cpp 同款 editor-side 自包含工具纪律）

**对偶选择**：刻意**不**把 EditorPicking.cpp 的反投影合并进 GizmoMath——两者各自归属域清晰
（picking 是 entity 命中、gizmo 是 handle 命中），合并会让"为什么 gizmo helper 在 picking 路径
使用 picking 反投影"的反向依赖出现，违反 SRP。

### 决策点 3 · IEditorGizmoPlugin 调度模型走"扁平 Draw/HitTest"而非 Godot 多档接口

**决策位置**：`tools/OrangeEditor/plugin/IEditorGizmoPlugin.h`（v0.2.5 c12 声明，c4 首批消费
时确认实际落地路径）。

**选项**：
- **A · 扁平 Draw + HitTest 双钩子**（OrangeEditor 当前选择）：plugin 仅实现 Draw（纯虚）+
  HitTest（默认 false）。SceneView overlay 遍历 plugin × 匹配 schema 调 Draw，所有 true 的全画。
- **B · Godot 多档接口**（has_gizmo / create_gizmo / get_name / set_state / get_handle_value /
  commit_handle ...）—— plugin 自管 gizmo 实例挂在 Node 上，编辑器 SceneView 调度多档钩子
  - 参 `vendor/godot/editor/plugins/node_3d_editor_gizmos.h`
- **C · Lumix 写死无 plugin 抽象**——所有 gizmo 在 SceneView 内 hardcode
  - 参 `vendor/LumixEngine/src/editor/gizmo.cpp`

**已选**：A（v0.2.5 c12 头注释明确"OrangeEditor 不学 Godot 的 gizmo 实例挂在 Node 上 / 不学
Lumix 的不可扩展，走扁平 Draw/HitTest"）

**事后追评**：仍合理。c4 / c5 三个 concrete plugin（DirectionalLight / ParticleEmitter /
CameraFrustum）全部走 Draw 纯装饰路径，HitTest 默认 false——证明扁平接口已经能覆盖"plugin
是 component-specific overlay" 的核心用例。Godot 多档接口的 `get_handle_value` /
`commit_handle` 等钩子是为"plugin 接管 drag 修改 component"设计的，OrangeEditor 当前 plugin
都不接管 drag（drag 走内置 Transform gizmo），所以多档接口暂时是过度设计。

**未来扩展头**：v0.4+ 真撞到"plugin 想接管 drag 修改某 component 字段"需求时，可在 IEditor
GizmoPlugin 上加 `OnDragBegin / OnDragUpdate / OnDragEnd` 钩子，**不**破坏现有 plugin 编译
（fields 增加规则同 GizmoContext，与 c4 头注释承诺一致）。

### 决策点 4 · IEditorGizmoPlugin 首批 case 一次出 2 个（c4）而非 1 个

**决策位置**：c4。v0.3 c3 是"第一个真实 IEditorInspectorPlugin case"（AnimatorMini
Preview，1 个），v0.4 c4 同款"首批真实 IEditorGizmoPlugin case" 但出了 2 个
（DirectionalLight + ParticleEmitter）。

**选项**：
- **A · 一次出 2 个 plugin**（c4 当前）：验证多 plugin 并存 + 互不串味
- **B · 先出 1 个，c5 再加 1 个**：节奏更慢，更保险

**已选**：A

**事后追评**：仍合理。理由：
- 两个 plugin 同 commit 落地实际验证了"多 plugin 在 dispatch loop 内并存"路径（c4 ScenePanel
  dispatch loop 遍历每个 plugin × 每个匹配 schema），单 plugin case 验证不到
- 工程量可控（plugin 共享 GizmoMath helper，每个 plugin .cpp 约 150 行）
- c5 顺利加入第 3 个 plugin（CameraFrustumGizmo）无需改 dispatch 路径——抽象稳

### 决策点 5 · c5 Camera frustum 走"hardcode fake 参数 + GAP 登记"而非"跳过 frustum"

**决策位置**：c5。`ApplyEditorCameraToWorld` 每帧覆写 Camera component 让"读真实 view/
projection 算 frustum"无意义。

**选项**：
- **A · hardcode fake fov/aspect/near/far + entity transform 推 view + GAP 登记**（当前实现）
- **B · 跳过 c5 Camera frustum，只做 Viewport 工具栏，frustum 推到 GAP 落地后做**
- **C · 编辑器侧 snapshot Camera 原始数据让 frustum 看真值**（heavy，破坏"编辑器只消费引擎
  API"纪律）

**已选**：A

**事后追评**：仍合理。理由：
- A 让 c5 deliverable 完整（editor-roadmap.md v0.4 列出的 Camera frustum 是 deliverable，不是
  TODO 段的"按需补"项）—— editor-roadmap 优先级保证
- fake 参数是**老实的**临时方案：代码内 TODO 注释 + GAP-2026-05-15 链接 + acceptance-checklist
  内"已知限制"段明示——不构成隐藏债（与 v0.3 retro 决策点 2 双份字段表同款"老实承认偏离"
  原则）
- 走 IEditorGizmoPlugin dispatch 路径而非另起 hardcode 路径，保架构对称性—— c5 之后所有 per-
  component gizmo 都走 plugin（c2/c3 内置 Transform gizmo 是唯一例外，IEditorGizmoPlugin.h
  头注释已明确豁免）

**潜在问题**：B 选项的诚实度更高（不引入 fake）。但 v0.4 milestone 边界已临近，B 选项会留
"editor-roadmap.md v0.4 列出但未实现"的尾巴。权衡后 A 选项的工程平衡更好——前提是 GAP 落地
后真的回头切真值（用 GAP 条目 + 代码 TODO 双重提醒）。

### 综合事后追评

v0.4 五个决策点 retro 后均**未发现**事后追评失误。新机制（2D 屏幕 hit-test / 内部 helper 提
取 / 扁平 plugin 接口 / 首批 case 多 plugin / fake frustum + GAP）在 milestone 完工事后看，
与同栈参考引擎设计契合或合理偏离（fake frustum 偏离纯净路径是工程平衡主动选择，登记 GAP
保证后续回头）。

**对 v0.5 的启示**（基于本 retro）：
- Asset 浏览器 + Material 子模式 milestone 启动时，schema 体系若撞到"无 Mat4 PropertyType"
  问题（Material 也含矩阵字段如 UV scale/offset 在 mat3 / mat2 内）—— 复用 c5 同款"空字段
  schema + plugin 接管 UI"策略，或扩 PropertySchema 加 Mat3/Mat4 PropertyType
- v0.5 若引入新 plugin（如 MaterialThumbnailPlugin），沿用 c4 / c5 IEditorGizmoPlugin /
  IEditorInspectorPlugin 同款扁平接口，**不**重新评估抽象

---

## 文档生命周期

- v0.4 milestone 节点 A 回归通过后：本文档已归档到 `docs/acceptance/`；不删，作为 v0.x 期 milestone 验收范本
- v0.5 milestone 启动时：在 `docs/acceptance/` 下新建 `editor-v0.5-acceptance-checklist.md`，重新填内容；
  不在本文档继续追加跨 milestone 内容（避免文档膨胀 + 上下文混淆）。**精炼优先**：v0.3 / v0.4 验收时用户当场反馈"重复内容太多"——通用前置步骤（build / 启动编辑器 / 选实体 / Save / Load）抽到文档顶部"前置环境"节一次写清，每个 commit 验收点只写**与该 commit 相关的最小差异**；参 memory `feedback_milestone_acceptance_checklist_concise`
