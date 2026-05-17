---
id: ADR-002
title: OrangeEditor 视觉体系决策（EditorTheme token + Cocos 灰 + 橙 accent + Codicons + 4px 色带）
status: accepted
date: 2026-05-17
deciders: [v0.6.5 session]
related:
  - docs/editor-roadmap.md（§D5 / §D5.1 / §v0.6.5）
  - docs/acceptance/editor-v0.6.5-acceptance-checklist.md
  - vendor/LumixEngine/src/editor/settings.h
  - vendor/godot/editor/themes/editor_theme.h
  - vendor/godot/editor/themes/editor_theme_manager.h
---

## Context

OrangeEditor v0.1 ~ v0.6 各 milestone "功能优先" 落地，每条只保证"能用 + 不丑得离谱"，没有任何一条专门处理整体视觉一致性。结果在 v0.4 收尾时被用户当场指出 viewport 工具栏不够美观以致没法做完整功能验收（这是 v0.6.5 立项的直接触发点）。同时观察到：

- 编辑器整体走 ImGui `StyleColorsDark()` 默认深蓝黑（~#15151E），与多数工业 dark theme（VS Code / JetBrains / Blender / Maya / Unity / Unreal 均在 #2A2A2A ~ #313131 炭灰区间）拉开视觉感受
- `EditorRenderLayer.cpp` / `ToolbarPanel.cpp` 多处 hardcode `ImVec4(0.20f, 0.45f, 0.85f, 1.0f)` 形式的字面量 RGBA，"dirty Save 蓝高亮"等点状视觉散落在各 cpp，无中心化点
- 按钮 label 全是裸文字 / 短符号（`"X"` / `"+"` / `".."` / `"×##clear"`），无 icon 系统；非程序员用户辨识度差
- Inspector component header 只有文字 + 折叠箭头，缺乏 per-type 视觉识别度

v0.6.5 立项目标：用一个 milestone 把 c0 ~ c6 已落 UI 表面的视觉 token 全部中心化 + 引入 brand accent + icon font + per-component-type 识别色，并把规约用 invariant lint 固化下来防止后续 regression。

## Options Considered

本 ADR 覆盖四个独立维度的选择，各列候选 + 选定理由：

### D1 · 整体视觉方向

调研 `vendor/cocos-engine/` UI 布局（参 §D5）+ Cocos Creator 3.8.8 截图，对照 `vendor/godot/editor/themes/editor_theme_manager.h`：

| 方案 | 描述 | 工程代价 | 视觉收益 |
|------|------|---------|---------|
| A · Cocos-leaning | 深炭灰 + 单色 icon + 半透 selection + 圆角 0 + 紧凑字号 | 低 | 一致性高、polish 难度低；节点树 / Inspector 识别 component 类型差 |
| B · Godot-leaning | 整行实色 selection + 彩色 per-type icon + 圆角 2px + 较多 accent | 高（彩色 icon 要自己设计/选 ~30+ 张图，OrangeEditor 没积累） | 节点树辨识度最高 |
| **C · Cocos 布局 + Godot 识别度** | Cocos 工具感（间距 / 圆角 / 配色克制）+ 整行实色 selection 改 Cocos 半透 + Inspector component header 左 4px 色带（per-type 不同色，不依赖整套彩色 icon） | 中（色带是 EditorTheme 加 8 个色 token，比整套彩色 icon 便宜 90%） | 节点树仍单色（接受），Inspector 实际工作流主战场识别度高 |

### D2 · 主 accent 色基调

| 候选 | 描述 |
|------|------|
| Cocos 浅蓝 #3E8DCC | 跟 §D5 主基准一致、最保守 |
| Godot 蓝 #4A8BC8 | 略饱和、更"友好 IDE"感 |
| **OrangeEngine 橙 #FF8A3D** | 与项目名一致；但橙色作大色块易疲劳，必须配套半透 selection |

### D3 · selection 表现

| 候选 | 描述 |
|------|------|
| a · 暗橙 + 实色 | 主 accent 降饱和到 #D17B3F，整行实色填充 |
| **b · 饱和橙 + 半透 selection** | 主 accent 保留 #FF8A3D，selection 用 30% alpha overlay；橙仅用在 focus outline / active tab 下沿 / Save dirty / Play / Stop 等"小面积高对比"场景 |

橙色处于警告色波段，人眼对其本能更敏感（消防 / 施工 / 警示牌同因），大面积饱和橙整行长时间盯易疲劳；半透叠加既保留 brand 又避刺眼。

### D4 · icon font

| 候选 | 描述 |
|------|------|
| **Codicons**（VS Code 同款） | MIT，~500 图标，IDE 工具感、密度高；★★★★★ 与方向 C Cocos 工具感匹配度最高 |
| Lucide | ISC，~1500 图标，现代清爽 |
| Font Awesome 6 Free | CC BY 4.0，~2000 图标，老牌但偏 web 装饰 |

否决"直接拷贝 Cocos / Godot 编辑器图标"：Cocos Creator 是专有 EULA（非 MIT，与 cocos-engine 运行时 MIT 不同），其 icon 资产不能重分发；Godot icon MIT 兼容但混用 Godot 彩色 icon + Cocos 布局会破坏 v0.6.5 "视觉统一" 目的。

### D5 · EditorTheme 架构

| 候选 | 描述 |
|------|------|
| Lumix `settings.h` 1648 行 | Variable + Category + Workspace/User 双层 storage + GUI 调整 + 持久化 |
| Godot `editor_theme_manager.{h,cpp}` ~1000 行 | 类 Theme 资源 + 多 palette + 多主题切换 |
| **OrangeEditor 轻量路径** | const + namespace 分组（Color / Spacing / Rounding / Font / Icon / ComponentTypeBand 6 个 sub-namespace）+ getter 风格返回 `const&`，零注册系统 / 零用户可配 / 零多主题 |

Lumix Settings 是 v0.8 EditorSettings 整骨范围（消除 L13）；Godot ThemeManager 过重，与 Cocos 工具感方向不符。OrangeEditor v0.6.5 走最轻路径——getter 命名习惯让 v0.8 切 Settings 时改 getter 实现即可，调用方零改动。

## Decision

5 个维度全部按上述各节加粗候选拍板：

- **D1**：方向 C（Cocos 布局 + Godot selection 明确性 + Inspector 4px 色带补丁）
- **D2**：OrangeEngine 橙 #FF8A3D 作主 accent
- **D3**：半透 selection（橙 30% alpha 整行叠加）+ 橙仅小面积高对比
- **D4**：Codicons（MIT，~500 图标，VS Code 同款）
- **D5**：EditorTheme 轻量 getter 风格（6 sub-namespace），不引入注册系统 / 用户可配 / 多主题

具体 RGB 采色见 `tools/OrangeEditor/theme/EditorTheme.cpp` 各 token 实现，token 命名 + 注释含"决策由来 + 用途"自包含。

## Consequences

### 正面

- 所有视觉决策中心化在 `EditorTheme.h` + `EditorTheme.cpp`；后续 milestone 的 UI 表面（v0.7 Animation 状态机图 / v0.8 Settings 面板 / v0.9 Profiler）直接消费 token，不再重复决策
- §D5.1 红线 → invariant lint（`editor-literal-rgba` + `editor-bare-text-button`），机器化防御 regression
- v0.8 EditorSettings 整骨（消除 L13）有清晰升级路径：getter 实现从 `return kXxx;` 改成 `return Settings::Get<ImVec4>("...")`，调用方零改动

### 负面 / 待还的债

- `ImFontConfig.GlyphOffset.y = floor(fontPx * 0.15f)` 是 ImGui icon font 集成的标准 hack；副作用：`+ Add Component` / `+ Add Layer` 等 icon+文字混排按钮里 icon 比文字 baseline 略低 ~3 px @ 18px font。toolbar icon-only 按钮是主战场，该副作用可接受；后续如要"完美居中"需切方案 B 手工渲染按钮（InvisibleButton + DrawList AddText）
- 启动期首帧"白色矩形 + 黑色背景"画面 pre-existing，登记于 engine-known-gaps `GAP-2026-05-17-editor-first-frame-flash`，与 c0 三轮 fix 撞 ImGui DisplaySize sync 时序根因同源
- ComponentTypeBand 8 色对游戏侧自定义 component 走 `GetDefault()` 浅灰 fallback；后续若游戏侧需要 per-type 自定义色带色，扩 ComponentTypeBand 注册 API（v0.8 EditorSettings 整骨范围）

### 强制 invariant（已沉淀进 `scripts/check_invariants.py` 与 `editor-roadmap.md` §D5.1 红线）

1. **禁字面量 ImVec4 RGBA**：tools/OrangeEditor/ 内禁止 `ImVec4(0.\d+f, ...)` 形式字面量 RGBA；EditorTheme.{cpp,h} 是 token 定义层白名单豁免。lint 规则 `editor-literal-rgba`
2. **禁裸短符号文字按钮**：禁止 `ImGui::Button("X")` / `"+"` / `"▼"` 等单字符 / 短符号 label；多字 dialog 按钮（Save / Cancel / OK）允许。lint 规则 `editor-bare-text-button`
3. **禁 selection 整行实色填充**：必须用半透叠加；违反者视为今天讨论清楚的决策被推翻，需重新立项讨论
4. **禁直接拷贝 Cocos Creator / Godot 编辑器 PNG 图标资产进 OrangeEditor 仓库**：Cocos 是专有 EULA（非 MIT）；icon font 走开源 Codicons 路径

## Notes

工程教训（c4 三轮 icon 居中 fix 过程沉淀）：调字体 metrics 几何精度（GlyphMaxAdvanceX / GlyphOffset 同时调）撞 ImGui 内部 layout 算法不会赢——auto-size 按钮（`ImVec2(0, 0)`）+ 仅纵向 GlyphOffset 是 ImGui icon font 集成的稳态。横向居中靠 ImGui 默认 layout，纵向用经验偏移 `floor(fontPx * 0.15f)` 处理 Codicons 无 descender 导致的视觉偏上。

参考引擎 retro（milestone-end-checklist 第 3 步）：

| 维度 | Lumix | Godot | OrangeEditor v0.6.5 选择 | 事后追评 |
|------|-------|-------|--------------------------|---------|
| Theme 架构 | Settings 完整注册系统（用户可配 + Workspace/User 双层 + GUI + 持久化）| EditorThemeManager 重量 manager + 多 palette | 轻量 const + namespace + getter | 合理——v0.8 Settings 整骨有升级路径，v0.6.5 不越界 |
| accent 用量 | 极少（grayscale 工具感）| 多（蓝 selection 整行实色 + 顶部 tab）| 中（橙 brand + 半透 selection，小面积 brand 露出）| 合理——brand 一致性需要 + 大面积半透避刺眼 |
| icon 系统 | 内嵌字符串（"▶" 等）| 彩色 per-type SVG（每类节点独立色） | Codicons 单色 + Inspector 4px 色带补识别度 | 合理——色带替代彩色 icon 节省 ~30 张图的工程代价 |
| selection | grayscale 半透 | 整行实色蓝 | 半透橙叠加 | 合理——避橙色大色块刺眼，与 brand 一致性正交 |

Wiki 缺页登记（c7 §D5.1 末段已写）：4 个候选 page 待另开 wiki session ingest：

- `concepts/editor-ui/dark-theme-base-color.md`
- `concepts/editor-ui/accent-color-budget.md`
- `techniques/editor-ui/icon-font-integration.md`
- `comparisons/editor-ui/cocos-vs-godot-style.md`

wiki ingest 完成后，本 ADR + §D5.1 应改为引用 wiki 页相对路径，删除复述的通用知识。
