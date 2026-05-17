# OrangeEditor v0.6.5 视觉统一与主题打磨 milestone 验收清单

- 基准日期：2026-05-17
- 适用范围：OrangeEditor v0.6.5 核心视觉验收
- 设计意图：只列用户能在编辑器内点击 / 看效果的**核心**视觉变化；token 实装细节 / 已知 fallback / 前期沿用回归不列；按"功能区"组织不按 commit 切片

## 前置环境

1. `git pull && cmake --build build --config Debug --target OrangeEditor`
2. 双击 / IDE F5 启动 `build/bin/Debug/OrangeEditor.exe`，maximize 后 demo scene 自动加载

## 核心功能

### 1. 整体配色与控件三态（c2 Cocos 炭灰主题）

- [ ] 编辑器整体走 Cocos 风格**炭灰**色调（背景 ~#242424 ~ #2E2E2E）；与 c0 之前的"ImGui 深蓝黑"明显不同
- [ ] 主字色浅灰偏白（非纯白），长时间盯不疲劳
- [ ] Inspector 字段输入框比 panel 背景更深，呈"凹陷"视觉
- [ ] 所有控件圆角 0px（Cocos 方正风格）
- [ ] panel / dock / popup 三层亮度有明显但不刺眼分层

### 2. 独立 toolbar + 居中 Play 控件（c0 / c3 / c4 / c6）

- [ ] menu bar（行 1）退回纯 File / Edit / View / Help + scene path indicator
- [ ] 独立 toolbar（行 2，紧贴 menu bar 下方）：Save 靠左 / Play+Pause+Stop **居中** / [State] 靠右
- [ ] 所有按钮使用 Codicons icon（非裸文字），hover 显示 tooltip
- [ ] Save 按钮：dirty 时 **橙色实色**高亮（非 dirty 时灰）
- [ ] Play 按钮 idle icon **绿色**（success）；Stop 按钮 idle icon **红色**（error）；Pause 默认灰
- [ ] 进入 Play 模式后 Play 变 disabled、Pause/Stop 变 enabled，icon 颜色清晰区分可用 / 不可用
- [ ] 拉伸窗口 → Play 控件始终居中，Save 靠左、[State] 靠右

### 3. accent 橙 + 半透 selection（c4）

- [ ] 选中 Hierarchy 实体 → 该行 **半透橙叠加**（不是整行实色橙、不是灰）
- [ ] Inspector component header hover / 展开 → 半透橙渐变（非饱和填充）
- [ ] DragFloat / 输入框 focus 时框内**轻染橙**
- [ ] 底部 tab（Assets / Console / Animation / Layers）切换：active tab **顶部 indicator 线** + 背景 半透橙
- [ ] CheckMark / Slider 圆点用橙主色（小面积可饱和）

### 4. Inspector component 类型识别色带（c5）

- [ ] 选中带多个 component 的实体（如 Slime Doll / Sparkle Emitter）→ Inspector 内每个 component header **左侧 4px 色带**
- [ ] 色带颜色按 component 类型区分（**绿** = Transform / **蓝** = Renderable / **黄** = DirectionalLight / **红** = RigidBody / **紫** = Collider / **青** = ParticleEmitter / **粉** = Animator / **灰** = Name；其他 component 浅灰 fallback）
- [ ] 折叠 / 展开 component → 色带跟着 header 高度变化

### 5. 节点 A 大回归（P2 覆盖 v0.4 c5 段 8 项遗留）

- [ ] viewport 工具栏（Gizmos / Persp / Shaded / Camera Mode）视觉与主题一致；Camera Mode 与 Persp / Shaded **等高**
- [ ] Gizmos checkbox 切换 → 所有 overlay（Transform / Light / ParticleEmitter / Camera frustum）同步显示 / 隐藏
- [ ] disabled placeholder 项 hover 仍弹 tooltip 说明
- [ ] Camera 实体选中 → viewport 内 frustum 线框显示；Inspector 显示空 Camera component header（不可 Remove / Add）
- [ ] Play Mode 期间所有 overlay 强制隐藏；Stop 后按 Gizmos 状态恢复

## 已知不验收（与 v0.6.5 范围正交）

- ImFontConfig.GlyphOffset.y 经验偏移导致 " + Add Component" 类 mixed 按钮里 + icon 比文字 baseline 略低 ~3px @ 18px font —— c4 commit message 明确登记的可接受副作用
- `assets/scenes/demo.scene.json` 等 GAP-2026-05-17 系列 round-trip 副产物未独立处理
- 启动期一闪而过的"白色矩形 + 黑色背景"画面 —— pre-existing，登记于 engine-known-gaps `GAP-2026-05-17-editor-first-frame-flash`
