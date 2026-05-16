# OrangeEditor v0.4.5 UI DPI 自适应 + widget 比例化 milestone 验收清单

- 基准日期：2026-05-16
- 适用范围：OrangeEditor v0.4.5 各 commit 的 UI 视觉验收
- 设计意图：与 v0.4 同款；按 acceptance-checklist 精炼原则（顶部建前置环境节，
  commit 验收点仅写最小差异），目标单文件 ≤ 300 行

## 前置环境

走任一 commit 验收点前的通用准备 —— 各 commit 节内**不重复**这些步骤：

1. 拉取并构建 OrangeEditor：`cmake --build build --config Debug --target OrangeEditor`
2. 启动 `build/bin/Debug/OrangeEditor.exe`，确认无 crash + 自动加载 `assets/scenes/demo.scene.json`
   - 失败回退 SeedDemoWorld 也算 OK（条件触发，不算回归）
3. 任意选中一个挂多 component 的实体（推荐 demo scene 内 `MainCharacter` /
   `Cube_Particles` —— 它们覆盖 Transform / Hierarchy / Renderable / Animator /
   ParticleEmitter 等多种 component）

## 双机 DPI 配置 acceptance

v0.4.5 的核心验收靠"在两台机器上看 UI" —— 单机无法验证。准备两台机器：

| 机器 | 物理分辨率 | 系统缩放 | 逻辑分辨率 |
|------|-----------|---------|----------|
| A（开发机） | 3840 × 2160 | 175% | ~2194 × 1234 |
| B（窄屏机） | 2520 × 1680 | 150% | 1680 × 1120 |

两台机器都跑同一份 OrangeEditor.exe，加载同一 demo scene；验收要求两台都通过。

## 回归节奏

v0.4.5 整 milestone 完工后一次性大节点回归。两台机器分别跑下文 c1 / c2+c3 / c4
验收点 + 小节点回归路径 + v0.4 关键 picking / gizmo 回归（同 v0.4 节点 A 大回归
合并跑）。

通过的清单条目划掉（`[ ]` → `[✅]`）；失败登记到对应 commit 节内 `### bugs` 段。

## 测试盲点提醒（v0.4.5 上下文）

按大节点回归容易漏的几类：

1. **窄屏 / 高 DPI 双向验证**：单机 4K × 175% 上 widget 看起来都 OK 不代表问题
   解决。本 milestone 撞墙就是因为 v0.4 期只在开发机验证，1680×1120 × 150% 撞了
   字段截断。两台机器都过才算通过
2. **font atlas 重建**：DPI scale 仅在编辑器启动期读 `glfwGetWindowContentScale`
   一次。运行时把窗口拖到另一显示器（不同 scale）→ font 不会自动重建（已知限制）。
   验收**不必**测拖屏；记入"已知限制"即可
3. **ImGui Table NoSavedSettings**：BeginPropertyTable 用 `NoSavedSettings` flag，
   ImGui 不会把列宽存到 imgui.ini —— 切实体 / 切场景列宽自动重新计算。验收：
   关闭 editor → 重启 → Inspector 列宽与上次一致（按 schema 算）
4. **plugin 路径**：AnimatorMiniPreviewPlugin 的 ParseEnd 在 Table 外画 ——
   验收时点选 Animator 实体确认 Mini-Preview 节区在 schema properties 段下方
   独立显示，不与 properties Table 错位

---

## 小节点回归路径（每个 commit 在两台机器各跑一遍）

> 与 v0.4 同款入门级路径；本 milestone 改的全是视觉布局，回归侧重 "功能没坏"。

- [ ] 启动编辑器无 crash；自动加载 demo scene（或回退 SeedDemoWorld）
- [ ] 选中任意实体 → Inspector 显示该实体所有挂着的 component header（不漏不重）
- [ ] 任改一个标量字段 → `Ctrl+Z` 回滚 → `Ctrl+Y` 重做，三步值一致
- [ ] 编辑后 → 顶部 File 菜单的 Save 项**亮起可点**（不是灰显）
- [ ] Save → 关闭 editor → 重启 → 自动加载，所有字段保持
- [ ] Play → Pause → Stop 状态机走通；Play / Pause / Stop 按钮按当前 state 灰
  显或可点（一组按钮顺手目测：Edit → 仅 Play 可点；Play → 仅 Pause / Stop 可点；
  Paused → Play / Stop 可点）
- [ ] 顶部 main menu 右侧 state label 在 Edit / Play / Paused 三态显示 `[Edit]`
  / `[Play]` / `[Paused]`，切换时**整组**按钮右边距稳定（不左右晃）

---

## Commit 1：Inspector schema 改 ImGui::Table 两列布局

`EditorWidgets`：新增 `BeginPropertyTable / PropertyLabel / EndPropertyTable`
帮手；DragVec3Colored 改造不再自带 label。`SchemaInspector` DrawComponentSchema
Section 包 Table 在 properties 循环外，按 groupSeparator 段化每段独立 Table；
DrawProperty 所有 case 改 "##v" 隐藏 label。tooltip 从控件 hover 迁到 label
hover。

### 验收点（两台机器各跑一遍）

- [ ] **窄屏字段名永不截断**（机器 B 1680×1120 × 150% 上是核心验收点）
  - [ ] 选中 demo scene 内任意带 Particle Emitter / RigidBody 等"长 label
    名" component 的实体
  - [ ] Inspector 内字段名**完整可见**（无 `Sta...` / `Par...` / `Spr...` 等
    省略符）
  - [ ] 列宽随窗口拖动自动变化：把 dock 内 Inspector 列拖窄到极限，控件占
    满右列宽度收缩到 ~50px 仍可拖；label 列宽保持不变（仍显示完整文本）

- [ ] **label / 控件中线对齐**
  - [ ] Inspector 内每行：左列 label 文本与右列控件（DragFloat / Checkbox /
    Combo / Button 等）的视觉中线水平对齐，不上下错位

- [ ] **tooltip 走 label hover**
  - [ ] 鼠标停在 Inspector 字段左列 label 上（不进右列控件） → tooltip 文本
    自动弹出
  - [ ] 鼠标进入控件本身 → tooltip **不**弹出（tooltip 仅 label hover 触发）
  - [ ] tooltip 文本内容与 schema 注册的 `.Tooltip(...)` 一致

- [ ] **Vec3 三色按钮 + DragFloat 占满右列**
  - [ ] 选中 Transform 实体，看 Position / Scale / Rotation 三行
  - [ ] 每行从左到右：[X 红 button][drag][Y 绿 button][drag][Z 蓝 button][drag]
  - [ ] 三个 drag 列宽均匀 = (列剩余宽 - 3 button - 间距) / 3
  - [ ] 列拖窄 / 拖宽 → drag 列宽自动跟着变；button 始终正方形

- [ ] **GroupSeparator 分段**
  - [ ] 选中 ParticleEmitterComponent 实体 → 看 `Lifetime` / `Spawn Offset` /
    `Initial Velocity` / `Forces` / `Color curve` / `Size curve` / `Pool` 等
    分段标题
  - [ ] 每段标题（SeparatorText）**占满**整段宽度（不被左右两列分割）
  - [ ] 标题与下一段第一行控件之间无错位

- [ ] **GroupSeparator 后的 visibleIf 仍生效**
  - [ ] 选中 ColliderComponent 实体 → 切 shape 从 Circle → Box → Polygon
  - [ ] 每种 shape 对应分段仅在该 shape 下显示（其余隐藏）；Polygon /
    EdgeChain 段只显示 SeparatorText 占位无字段

- [ ] **Enum / String-readOnly / EntityRef 显示**
  - [ ] RigidBody.bodyType（Enum）—— 左列 label "Body Type"，右列下拉显示
    当前枚举名（Static / Kinematic / Dynamic）
  - [ ] Animator.backend（String readOnly）—— 左列 label "Backend"，右列灰显
    后端名（如 `skeletal_dragonbones` / `procedural` / `(no backend)`）
  - [ ] Hierarchy.parent / firstChild 等（EntityRef）—— 左列 label，右列显
    `#<id>` 或 `(none)` 灰显

- [ ] **Quat Euler 缓存仍工作**
  - [ ] 选中 Transform 实体 → 拖 Rotation 三轴 DragFloat
  - [ ] 拖动期间数字**不抖**（不每帧从 quat 重推 Euler 抖回）
  - [ ] 切到另一实体 → 切回 → Rotation 显示当前 quat 的 Euler（重新缓存）
  - [ ] Undo Rotation 改动 → 缓存重置，Rotation 显示回滚后值

- [ ] **AnimatorMiniPreviewPlugin（v0.3 c3 plugin）回归**
  - [ ] 选中带 Animator 的实体 → Inspector "Animator" component header 内：
    第一段 schema properties（Backend 字段在 PropertyTable 内）+ 第二段
    Mini-Preview（plugin ParseEnd 输出，在 Table 外，Spacing + Separator +
    "Status: Running/Finished" + "Backend: xxx"）

### bugs

（验收期登记；当前为空）

---

## Commit 2+3：toolbar 按钮 / Combo 列宽改 CalcTextSize 派生

`EditorRenderLayer` 主菜单栏 Play / Pause / Stop 按钮 + state label +
`ScenePanel` viewport toolbar 的 View Mode / Shading Combo 一齐改 CalcTextSize
+ FramePadding 派生。

### 验收点（两台机器各跑一遍，重点 B 机器）

- [ ] **顶部 Play / Pause / Stop 按钮不被字撑爆**
  - [ ] B 机器（1680×1120 × 150%）上 main menu 右侧 Play / Pause / Stop 按钮
    文本完整可见（"Play" / "Pause" / "Stop" 不被截断）
  - [ ] 按钮间距均匀；与 state label 之间留出一个 ItemSpacing 间隙
  - [ ] state label `[Edit]` / `[Play]` / `[Paused]` 三态切换时按钮组**整体**右
    边距稳定，不左右晃

- [ ] **viewport toolbar Combo 不被字撑爆**
  - [ ] B 机器上 Scene 面板顶部 toolbar 的 View Mode Combo 显示 "Persp"
    （disabled）全文不截断；Shading Combo 显示 "Shaded" 全文不截断
  - [ ] disabled 状态下 hover tooltip 仍弹出（说明该功能未实现 + 依赖项）
  - [ ] Gizmos checkbox 可点击 toggle；toolbar 整体水平占用合理（不挤压
    viewport image）

- [ ] **A 机器（开发机）回归**
  - [ ] A 机器（4K × 175%）上同样路径全跑一遍，确认 c2/c3 改动没把 A 机器
    的视觉变糟（之前 v0.4 期硬编码值 36 / 70 / 80 在 A 机器恰好合适）

### bugs

（验收期登记；当前为空）

---

## Commit 4：invariant lint editor-widget-pixel-literal

`scripts/check_invariants.py` 新增规则：tools/OrangeEditor 内禁止 widget 层字
面量像素列宽。本 commit 无 UI 改动，验收走脚本路径（不算编辑器 acceptance，
但本节登记完整工作流）。

### 验收点

- [ ] **lint 干净**：`python scripts/check_invariants.py` 退出码 0；输出
  `All invariants OK. (7 grandfathered by baseline)`
- [ ] **drift 干净**：`python scripts/check_claude_md_drift.py` 退出码 0
- [ ] **规则真实生效（sanity check）**：
  - 临时往 tools/OrangeEditor 下任一 cpp 加 `ImGui::SetNextItemWidth(80.0f);`
  - 跑 lint → 报 1 个 `editor-widget-pixel-literal` 新违规
  - 删除该行 → 再跑 lint → 回到 baseline 干净

### bugs

（验收期登记；当前为空）

---

## 已知限制（开发期容忍）

- **font atlas 重建**：拖窗口到另一 DPI 显示器 → font / style 不会自动重新缩放
  （仅启动期读 `glfwGetWindowContentScale` 一次）。**不**作为本 milestone 验收
  范围；后续 v0.8 EditorSettings 接入主题切换时再考虑动态 font reload
- **同段 schema label 等宽**：BeginPropertyTable 内每个 component 段独立算
  maxLabelW，相邻 component 段列宽可能不一致 —— 视觉上 CollapsingHeader 已断
  开分隔，可接受
- **Plugin 路径在 Table 外**：IEditorInspectorPlugin 的 ParseEnd 在 Table
  EndPropertyTable 之后调用（plugin 可自己再 Begin/End 新 Table 或走单列布局）。
  当前仅 AnimatorMiniPreviewPlugin 一个真实 case，视觉无错位
