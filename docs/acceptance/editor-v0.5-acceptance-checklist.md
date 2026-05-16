# OrangeEditor v0.5 Asset 浏览器 + Material 子模式 milestone 验收清单

- 基准日期：2026-05-16
- 适用范围：OrangeEditor v0.5 核心功能验收
- 设计意图：只列用户能在编辑器内点击 / 拖拽 / 看效果的**核心**功能；已知简化 / 内部机制不列

## 前置环境

1. `git pull && cmake --build build --config Debug --target OrangeEditor`
2. **从项目根目录**跑 `build/bin/Debug/OrangeEditor.exe`（cwd 必须是仓库根）
3. 启动后窗口直接 maximize，demo scene 自动加载

## 核心功能

### 1. Asset 浏览器

底部 **Assets** tab 浏览 `assets/` 目录树 + 文件列表。

- [ ] 左侧目录树显示 `meshes / materials/builtin / scenes / configs` 等节点
- [ ] 点 `meshes` 看到 `[M] cube.mesh / plane.mesh`
- [ ] 点 `materials/builtin` 看到 7 个 `[Mat] *.material` 文件
- [ ] 点 `scenes` 看到 `[S] demo.scene.json`

### 2. Inspector 字段交互（拖 / 点选 / 清除）

选中 demo scene 内任意 Renderable 实体（如 `Dynamic Box`），Inspector 显示 **Mesh** + **Material** 两个字段。

- [ ] 字段显示当前资源短名（`cube.mesh` / `toon.material`），hover 出全路径 tooltip
- [ ] 从 Assets tab **拖** `plane.mesh` 到 Mesh 字段：几何切换为平面，`Ctrl+Z` 回滚
- [ ] Assets tab 点选 `toon.material` → Inspector Material 字段右侧 **Pick** 按钮可点 → 点击后材质切到 toon
- [ ] 点 Mesh 字段右侧 **×**：几何消失，`Ctrl+Z` 还原

### 3. Material 子模式

Asset 浏览器选中 `.material` 文件时 Inspector 切到 Material 编辑视图。

- [ ] 点选 `assets/materials/builtin/toon.material` → Inspector 显示 path + Template Combo
- [ ] Combo 切到 `dissolve` → Save 按钮可点 → 点击 → 弹"已保存" popup
- [ ] 关闭编辑器 → 重启 → 之前用 toon 的物体改用 dissolve 渲染（template 切换持久化）
- [ ] 切回点选某实体（或选非 .material 文件）→ Inspector 切回实体 Inspector

## 已知简化范围（不验收）

- Material 子模式当前**只能切 Template**，不能调 uniform 参数（颜色 / 轮廓宽度等）；Save 也仅写 templateName。完整 uniform 编辑 + 持久化由后续 GAP-2026-05-16-material-system-enumerate-and-instance-overrides 落地后补
- Material 子模式 Save 后必须**重启编辑器**才看到 template 切换效果
- Asset 浏览器不支持新建 / 删除 / 重命名（v0.6 加）
- Asset 浏览器无缩略图，按扩展名前缀 icon `[M]/[Mat]/[S]/[?]` 标识类型

## 大节点回归

- [ ] **跨功能链路**：Asset 浏览器拖资源 → 实体 Material 切换 → 选 .material 文件编辑 Template → Save 一遍跑通
- [ ] **场景保存 / 加载**：File 菜单 Save / Open / New 不破，保存 → 重启 → 视觉一致
- [ ] **v0.4 功能不退化**：viewport 点选 / W/E/R gizmo 切换 / DnD reparent 全部仍工作

### bugs

（验收期登记；当前为空）
