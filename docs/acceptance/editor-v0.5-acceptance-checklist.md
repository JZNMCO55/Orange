# OrangeEditor v0.5 Asset 浏览器 + Material 子模式 milestone 验收清单

- 基准日期：2026-05-16（验收 patch：2026-05-17）
- 适用范围：OrangeEditor v0.5 核心功能验收
- 设计意图：只列用户能在编辑器内点击 / 拖拽 / 看效果的**核心**功能；已知简化 / 内部机制不列

## 前置环境

1. `git pull && cmake --build build --config Debug --target OrangeEditor`
2. 任意 cwd 启动 `build/bin/Debug/OrangeEditor.exe`（双击 / IDE F5 / 命令行均可——启动期 `ChdirToRepoRoot` 自动定位仓库根，v0.5 patch 加入）
3. 启动后窗口直接 maximize，demo scene 自动加载

## 核心功能

### 1. Asset 浏览器

底部 **Assets** tab 浏览 `assets/` 目录树 + 文件列表。

- [x] 左侧目录树显示 4 个顶层目录：`configs / materials / meshes / scenes`
- [x] 点 `meshes` 看到 `[M] cube.mesh / plane.mesh`
- [x] 点 `materials/builtin` 看到 7 个 `[Mat] *.material` 文件
- [x] 点 `scenes` 看到 `[S] demo.scene.json`

### 2. Inspector 字段交互（拖 / 点选 / 清除）

选中 demo scene 内任意 Renderable 实体（如 `Dynamic Box`），Inspector 显示 **Mesh** + **Material** 两个字段。

- [x] 字段显示当前资源短名（`cube.mesh` / `toon.material`），hover 出全路径 tooltip
- [x] 从 Assets tab **拖** `plane.mesh` 到 Mesh 字段：几何切换为平面，`Ctrl+Z` 回滚
- [x] Assets tab 点选 `cube.mesh` → Inspector Mesh 字段 **Pick** 按钮亮起 → 点击切到 cube（`.mesh` 不触发子模式，Pick 路径可用）
- [x] 点 Mesh 字段右侧 **×**：几何消失，`Ctrl+Z` 还原

### 3. Material 子模式

Asset 浏览器选中 `.material` 文件时 Inspector 切到 Material 编辑视图。

- [x] 点选 `assets/materials/builtin/toon.material` → Inspector 显示 path + Template Combo
- [x] Combo 切到 `dissolve` → Save 按钮可点 → 点击 → 弹"已保存" popup
- [-] 关闭编辑器 → 重启 → 之前用 toon 的物体改用 dissolve 渲染（template 切换持久化）—— 默认加载 SeedDemoWorld fallback，暂不测试
- [x] 切回点选某实体（viewport 或 hierarchy）→ Inspector 切回实体 Inspector（B3 互斥选择）

## 已知简化范围（不验收）

- Material 子模式当前**只能切 Template**，不能调 uniform 参数（颜色 / 轮廓宽度等）；Save 也仅写 templateName。完整 uniform 编辑 + 持久化由后续 GAP-2026-05-16-material-system-enumerate-and-instance-overrides 落地后补
- Material 子模式 Save 后必须**重启编辑器**才看到 template 切换效果
- Asset 浏览器不支持新建 / 删除 / 重命名（v0.6 加）
- Asset 浏览器无缩略图，按扩展名前缀 icon `[M]/[Mat]/[S]/[?]` 标识类型
- **Pick 按钮对 `.material` 字段不可用**（B3 修引入的互斥选择副作用：点 `.material` 切到 Material 子模式 → 实体 Inspector 不画 → Pick 按钮不显示）。`.material` 用 DnD（路径 2.2）替代；登记 v0.6 改进：Asset 浏览器右键菜单加 "Pick to Inspector field"

## 大节点回归

- [x] **跨功能链路**：Asset 浏览器拖资源 → 实体 Material 切换 → 选 .material 文件编辑 Template → Save 一遍跑通
- [x] **场景保存 / 加载**：File 菜单 Save / Open / New 不破，保存 → 重启 → 视觉一致
- [x] **v0.4 功能不退化**：viewport 点选 / W/E/R gizmo 切换 / DnD reparent 全部仍工作

## v0.5 验收 patch（B1 / B2 / B3）

2026-05-16 首次验收发现 3 个阻塞 bug，2026-05-17 patch 全清：

- **B1 · Asset 树缺目录**：根因 `OrangeEditor.exe` 启动 cwd 不在仓库根 → `Scene::Load("assets/scenes/...")` 失败走 SeedDemoWorld fallback → DemoWorld lazy-bake 写出 `build/bin/Debug/assets/{meshes,materials}` 副本污染。修：`main.cpp` 启动期加 `ChdirToRepoRoot()`（用 `GetModuleFileNameW` 拿 .exe 路径 walk up，找含 `assets/scenes/demo.scene.json` 的目录 `fs::current_path()` 切过去）。同时 Asset 浏览器去掉 root `assets` TreeNode，平铺顶层子目录 + `SetNextItemOpen(true, ImGuiCond_Once)` 防 imgui.ini 残留折叠状态。
- **B2 · Material Combo 切换失效**：根因 `DrawMaterialSubMode` 每帧从盘重读 `templateName` + 用局部 `newTemplateIdx` → 用户切 Combo 后下一帧立即被盘上原值覆盖（外观就是"Combo 切不动"）。修：把 `editingMaterialPath` + `editingTemplateName` 缓存搬到 `EditorAssetContext` 跨帧持久，仅切到另一 `.material` 时刷新。
- **B3 · Inspector 模式互切死锁**：根因 `IsMaterialAssetSelected(selectedAssetPath)` 优先级在前 + 实体选中入口不清 `selectedAssetPath` → 点 `.material` 后 selectedAssetPath 永远卡住 → Inspector 永远卡在 Material 子模式。修：4 处实体选中入口（ScenePanel viewport pick / EntityTreePanel 单击 / 右键菜单 / 新建实体）都清 `selectedAssetPath`；`.material` 点选反向清 `selectedEntity`（互斥选择，匹配 Cocos/Unity 惯例）。副作用：Pick 按钮对 `.material` 失效（见"已知简化"）。

## Wiki 设计意图对照（retro）

按 `docs/milestone-end-checklist.md` 第 3 步走参考引擎 / wiki retro 对比，对照 `vendor/Orange-Wiki/wiki/` 核心页（asset-database / plugin-architecture / property-reflection / property-grid-imgui / orange-editor-architecture / game-world-editor）：

### 对齐（5/7 deliverable）

- **c1 `PropertyType::AssetRef` + `AssetKind`**：等价 Lumix `Attributes::resource_type`（`concepts/editor/property-reflection.md` § Orange 实现建议）
- **c2 三 tab 容器**：参 `concepts/editor/orange-editor-architecture.md` § D5 Cocos 底部布局
- **c3 路径字符串 + 树/文件列表双视图**：参 `concepts/editor/asset-database.md` § Orange 实现建议"近期单人开发用路径哈希（Lumix 方案）"；简化范围（无缩略图 / 无搜索 score / 无 Favorites）落在 wiki 显式标记的"远期升级"段
- **c4 DnD + Command 写回**：参 `techniques/editor/property-grid-imgui.md` Lumix `visit(Property<Path>&)`；用 Selectable 代 InputText 是 asset id 字符串路径下的合理变形
- **命令系统纪律**：c4 push `SetFieldValueCommand<std::string>` 严格遵守 wiki "ImGui 修改走 executeCommand" 原则

### 临时偏离（2/7，已登记 `docs/editor-roadmap.md` L15 / L16）

- **L15 schema 注册依赖 static globals**：违背 `concepts/editor/plugin-architecture.md` Lumix `StudioApp::addPlugin(plugin)` 依赖注入路径；消除点 v0.8（让 `FieldAssetRef` 等接受 `EditorHost&` / 子 context）与 wiki 推荐方向一致
- **L16 Inspector 顶部 if 分发**：正中 `concepts/editor/property-reflection.md` 明确反对的"在 Visitor 内写 ComponentType 判断分支"模式；消除点 v0.7 c0 抽 `IEditorAssetInspectorPlugin`（参 Godot `EditorInspectorPlugin::can_handle()`）与 wiki 推荐方向一致

### 结论

两条临时偏离均在 retro 期自发识别（不是事后被 wiki 打脸），消除路径与 wiki 已记录的正确演进方向一致。简化范围全部落在 wiki "Orange 实现建议" 远期段或新登记 GAP 内（`GAP-2026-05-16-material-system-enumerate-and-instance-overrides`），无偷工减料。

验收 patch retro：3 个 bug 全部在 user 手动验收期暴露——印证 [[feedback_milestone_acceptance_checklist_required]] "milestone ✅ 必随附 acceptance-checklist 文档" + 用户亲自走查 GUI 操作的纪律价值（automated invariant lint / drift 检测 + acceptance-checklist 文档静态完整 都无法捕捉这类"cwd 不对 fallback 掩盖 / UI 状态机死锁 / 跨帧缓存覆盖"运行时 bug）。
