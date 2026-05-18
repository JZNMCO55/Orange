# OrangeEditor v0.8 验收清单

**milestone**：v0.8 编辑器 Log + 输入扩展 + Settings + 编辑器伴随
**完工日期**：2026-05-19
**关联 commits**：`939365f` (c1 EditorSettings) → `289e939` (c2 Console) → `170b7be` (c3 多选) → `c4` Keybinding → `7a1104d` (c5 Schema context 整骨)

## 启动 / 退出

- [ ] 启动 `build/bin/Debug/OrangeEditor.exe` 无 segfault；启动后 demo.scene 17 个 entity 全部就位
- [ ] 关闭编辑器后仓库根目录出现 `editor_settings.json`；JSON 内含 `gizmo` + `keybindings` 两段
- [ ] 再次启动后 Settings / Keybindings 字段保留上次修改的值

## v0.8 c1 · EditorSettings 系统

- [ ] View → Settings 打开 Settings 浮动面板；含 "Gizmo" + "Keybindings" 两个 CollapsingHeader
- [ ] 拖 "Translate idle" / "Translate highlight" 改变 viewport 内 translate gizmo 线宽即时跟随
- [ ] 调 "Handle screen length (px)" 改变三个 gizmo 屏幕长度
- [ ] 改 "X idle" 颜色 → X 轴 gizmo idle 状态用新色；hover 仍走 highlight 色
- [ ] 点 "Reset to defaults" 恢复 v0.4 期 hardcode 默认值

## v0.8 c2 · Console 面板接 Core::Log

- [ ] Console 面板出现 level 下拉（Trace+/Debug+/Info+/Warn+/Error+/Critical）+ search 文本框 + Clear/Auto/Quit 按钮
- [ ] 启动期 Pipeline::BakeIblFromWorld 等 INFO 日志出现在 Console 列表里
- [ ] 切到 "Warn+" → 仅显示 WRN / ERR / CRT 条目；切回 "Trace+" 全显示
- [ ] 输入 "Pipeline" 到 search → 仅显示 message 含此子串的条目
- [ ] 点 "Clear" 清空列表；Auto-scroll 勾选时新日志自动滚到底

## v0.8 c3 · 多选实体 + Inspector 指示

- [ ] Entity Tree 内点选 entity A → A 高亮；Ctrl+点 entity B → A 仍 primary，B 加入 additional
- [ ] 此时 Inspector 顶部出现 "2 entities selected (showing primary)" 黄色 banner + "[Multi-edit not yet wired]" 提示
- [ ] regular click（无 Ctrl）entity C → 清空 additional，primary 切到 C；banner 消失

## v0.8 c4 · Keybinding 自定义编辑器

- [ ] Settings 面板 "Keybindings" 段显示 5 条 binding 行（Gizmo Translate / Rotate / Scale + Rename / Delete Entity）+ 当前键名
- [ ] 点 "Rebind" 按钮 → 显示 "press a key (Esc = cancel)"；按 Y → binding 改为 Y 并退出 rebind
- [ ] 在 viewport 选实体后按改后的 Y → gizmo 切到对应 mode；原默认键不再触发
- [ ] 点 "Reset keybindings to defaults" 恢复 W/E/R/F2/Delete

## v0.8 c5 · Schema context injection 整骨

无可见 UI 改动——这是内部架构整骨。验收方式：
- [ ] grep 仓库确认 `gpAssetRegistry` / `gpNamedMaterialInstances` / `SetAssetRegistryForSchema` / `SetNamedMaterialInstancesForSchema` 全部消失（被 `gpAssetContext` / `SetEditorAssetContextForSchema` 替代）
- [ ] grep 确认 `editor_settings.json` 单文件持久化 gizmo settings + keybindings 两段；不是两个独立文件

## 大节点回归

- [ ] 历史 v0.5 Material 子模式：点 .material 文件 → Inspector 切换；改 templateName → Save → 重启保留
- [ ] 历史 v0.6 layer / split scene 路径：Save Split As / Open Split 仍能正常 round-trip
- [ ] 历史 v0.6.5 主题：Light/Dark theme 切换不破坏 Console / Settings 面板配色
- [ ] invariant lint + drift：全绿
