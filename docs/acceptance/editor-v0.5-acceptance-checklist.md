# OrangeEditor v0.5 Asset 浏览器 + Material 子模式 milestone 验收清单

- 基准日期：2026-05-16
- 适用范围：OrangeEditor v0.5 5 个 commit + 前置 GAP-2026-05-16 落地（3 commit）
- 设计意图：与 v0.2.5 / v0.3 / v0.4 同款，把每个 commit 的验收点沉淀进本文档

## 前置环境

各 commit 验收点前的通用准备 —— 不在每节重复：

1. `git pull && cmake --build build --config Debug --target OrangeEditor`
2. 启动 `build/bin/Debug/OrangeEditor.exe`（**从项目根目录跑**，让 cwd 是仓库根，相对路径 `assets/...` 解析正确）
3. 启动后窗口直接 maximize（v0.4.5 后 hotfix 行为）+ 五个面板齐全：Entity Tree (左) / Scene (中) / Inspector (右) / Assets+Console+Animation (底 tab)
4. 自动加载 demo scene `assets/scenes/demo.scene.json`（17 entities）

## 回归节奏

v0.5 整 milestone 完工后一次性大节点回归。GAP 3 个 commit + v0.5 5 个 commit
按时序在两台机器各跑一遍（开发机 4K × 175% + 窄屏机 1680×1120 × 150% 同 v0.4.5）。

通过的清单条目划掉（`[ ]` → `[✅]`）；失败登记到对应 commit 节内 `### bugs` 段。

## 测试盲点提醒（v0.5 上下文）

- **磁盘文件状态**：GAP-2026-05-16 落地后 `assets/meshes/*.mesh` + `assets/materials/builtin/*.material` 是 lazy-bake 产物。**首次跑** OrangeEditor 自动生成 + 用户手动 git add 入仓；之后跑直接从盘 Load。若验收时这些文件意外缺失（git clean / 误删），重跑 OrangeEditor 一次会自动 lazy bake 出来
- **cwd 必须项目根**：所有 asset 相对路径按 cwd 解析。如果用户从 `build/bin/Debug` 下双击 `.exe` 启动，cwd 就是 `build/bin/Debug` → demo scene / .mesh / .material 全找不到 → 回退 SeedDemoWorld。验收要求**命令行从项目根跑**
- **DnD payload 类型校验未做**：c4 接受任何 "ORANGE_ASSET" payload，不校验 AssetKind 匹配（拖 .scene 文件到 mesh 字段也会尝试写）。命中 Renderable.mesh 时 Load<MeshAsset> 失败 → mesh handle 空 → 几何消失但不崩。属于已知限制
- **Material 子模式 Save 后运行时不刷新**：c5 简化版 Save 只动盘上文件，运行时 MaterialInstance 不重建（避免悬挂 Renderable.materialInstance 字段指针）。用户必须重启 editor 看到 template 切换效果。属于 c5 已知简化范围

---

## 小节点回归路径（每个 commit 在两台机器各跑一遍）

- [ ] 启动编辑器无 crash + 直接 maximize + 自动加载 demo scene（17 entities）
- [ ] 五个 dock 区域齐全：Entity Tree / Scene / Inspector / 底部 tab (Assets / Console / Animation)
- [ ] 选中任意实体 → Inspector schema-driven 显示组件（沿用 v0.2.5+）
- [ ] hover Inspector 控件**仅一个高亮** + tooltip 在对应 label（v0.4.5 c1 / 98bc156 修复）
- [ ] 缩窗 / 拉宽 → ImGui 内容跟随充满 window 无空白（v0.4.5 后 f86a312 修复）

---

## GAP-2026-05-16 G1+G4：mesh 落盘（commit `222bd3f`）

* MeshLoader v1 → v2 加 UV 段 + Save 静态方法
* `assets/meshes/cube.mesh` (641B) + `plane.mesh` (121B) 入仓
* demo scene 内 Renderable.mesh 字段从 `"editor/cube"` 迁移到 `"assets/meshes/cube.mesh"`

### 验收点

- [ ] `git ls-files | grep meshes/` 看到两个 .mesh 文件
- [ ] `assets/scenes/demo.scene.json` 内 `"mesh"` 字段值全是磁盘路径
- [ ] 启动编辑器，**stderr 无** `mesh load failed` warn
- [ ] Scene 视口的 cube / plane mesh 视觉与 GAP 落地前**像素级一致**（UV 不丢失 → textured 材质正常）

---

## GAP-2026-05-16 G2+G4 剩余：material 落盘（commit `60eaa40`）

* `.material` JSON 文件格式 v1.0（仅 templateName）+ DemoWorld lazy bake
* `assets/materials/builtin/*.material` × 7 文件入仓
* `BuildNamedMaterialInstances` map key 改路径风格
* demo scene 内 materialInstanceId 字段迁移到磁盘路径
* ReadRenderable 加 "builtin/X" → "assets/materials/builtin/X.material" 兜底 mapping

### 验收点

- [ ] `git ls-files | grep materials/builtin/` 看到 7 个 .material 文件
- [ ] `cat assets/materials/builtin/toon.material` 内容是 `{"schemaVersion":...,"templateName":"toon"}`
- [ ] demo scene 内 `"materialInstanceId"` 字段值全是磁盘路径
- [ ] Scene 视口 toon / dissolve / emissive 等材质视觉与 GAP 落地前**像素级一致**

---

## GAP 处理记录归档（commit `a6f8562`）

- [ ] `docs/engine-known-gaps.md` 内 GAP-2026-05-16-builtin-asset-disk-serialization 末尾有"落地记录"节
- [ ] 末尾"处理记录"段含本 GAP 一行总结

---

## v0.5 c1：PropertyType::AssetRef + Renderable 字段 schema 化（commit `b4380e6`）

* schema 系统新增 AssetRef + AssetKind
* RenderableComponent.mesh / materialInstance 字段改用 FieldAssetRef 注册
* Inspector 当前显示 path 短名 + hover tooltip 全路径

### 验收点

- [ ] 选中 demo scene 内任意 Renderable 实体（Dynamic Box / Ground / Tower 等）
- [ ] Inspector 内 Renderable 段有 **Mesh** + **Material** 两个字段
- [ ] Mesh 字段右侧显示 `cube.mesh` 或 `plane.mesh`（短名）
- [ ] Material 字段右侧显示 `toon.material` / `dissolve.material` 等短名
- [ ] hover 字段 value 区域 → tooltip 弹完整路径
- [ ] 字段空时显示 `(none)` 灰底

---

## v0.5 c2：底部 tab 容器（commit `e6d344b`）

* Animation panel placeholder 加入底部 tab 容器
* Assets + Console + Animation 三 tab 并列

### 验收点

- [ ] 底部 dock 区域显示 3 个 tab：Assets / Console / Animation
- [ ] 点击 tab 在三者间切换
- [ ] Animation tab 显示 deferred 占位文本
- [ ] 与 Cocos Creator 3.6.0 底部三 tab 布局对齐

---

## v0.5 c3：Asset 浏览器面板（commit `5de39f5`）

* 顶部路径面包屑 + ".." 上一级
* 左 30% 目录树（递归扫 assets/）+ 右 70% 文件列表
* 类型 icon prefix `[M]/[Mat]/[T]/[S]/[J]/[?]`
* 文件 hover tooltip 全路径
* DnD source payload "ORANGE_ASSET"

### 验收点

- [ ] 切到底部 Assets tab
- [ ] 左侧目录树显示 `assets / configs / meshes / materials / materials/builtin / scenes` 6 个节点
- [ ] 点 `meshes` 目录 → 右侧文件列表显示 `[M] cube.mesh` + `[M] plane.mesh`
- [ ] 点 `materials/builtin` 目录 → 显示 7 个 `[Mat] *.material` 文件
- [ ] 点 `scenes` 目录 → 显示 `[S] *.scene.json`（按 .scene.json 后缀识别）
- [ ] 点 `configs` 目录 → 显示 `[J] *.actions.json` 等 .json 文件
- [ ] 顶部 ".." 按钮：从子目录退到父目录；root `assets` 时 ".." 灰显
- [ ] 点选文件 → 高亮 + 顶部状态行更新
- [ ] hover 文件名 → tooltip 显示全路径

---

## v0.5 c4：Inspector AssetRef DnD + Pick（commit `854f473`）

* AssetRef case 三段控件：[短名 Selectable] [× 清除] [Pick 浏览器选中]
* DnD target 接受 "ORANGE_ASSET" payload + Push SetFieldValueCommand
* × 清除按钮：path 置空 + 命令
* Pick 按钮：浏览器选中 path 写入 + 命令；disabled 在浏览器空或同路径时

### 验收点

- [ ] 选中 Dynamic Box（或任意 Renderable 实体）
- [ ] **DnD 路径**：底部 Assets tab → meshes 目录 → 拖 plane.mesh 到 Inspector Mesh 字段 → 字段值变 `plane.mesh` + viewport box 几何变 plane + Ctrl+Z 回滚成 cube
- [ ] **Pick 路径**：底部 Assets tab → 点选 toon.material → 选中 Renderable 实体 → Inspector Material 字段右侧 Pick 按钮**可点**（不灰）→ 点击 → 字段值变 `toon.material` + viewport 材质切 toon
- [ ] **× 清除路径**：Inspector Mesh 字段右侧点 × → 字段值变 `(none)` + viewport 几何消失 + Ctrl+Z 还原
- [ ] **Pick 灰显**：Asset 浏览器无选中 / 选中 path 与字段当前值相同 → Pick 按钮灰显
- [ ] **DnD type 不校验**（已知限制）：拖 .scene 文件到 mesh 字段 → 写入但 Load 失败 → 几何消失不崩

---

## v0.5 c5：Material 子模式简化版（commit `0a1a389`）

* Inspector 检测 selectedAssetPath 是 .material → 切到子模式
* 显示 path + Template Combo（5 个内置模板硬编码）
* Save 按钮写回 templateName（uniform 持久化 deferred）
* 完整版 uniform UI / 持久化登记 GAP-2026-05-16-material-system-enumerate-and-instance-overrides

### 验收点

- [ ] 底部 Assets tab → materials/builtin 目录 → 点选 `toon.material`
- [ ] **Inspector 切换**：从实体段切到 "Material:" 视图，显示当前 path + Template Combo
- [ ] Template Combo 下拉显示 5 个内置模板：toon / rim_light / dissolve / emissive / textured，当前选中 toon
- [ ] Combo 切到 dissolve → Save 按钮变可点 → 点击 → 弹 popup "已保存" + 关闭
- [ ] 关闭编辑器 → 检查 `assets/materials/builtin/toon.material` 文件 templateName 字段已改为 dissolve
- [ ] 重启编辑器 → toon material 实例改用 dissolve template 渲染（运行时 swap 效果）
- [ ] **切回实体 Inspector**：Asset 浏览器点选别的非 .material 文件（如 cube.mesh）或重新点选某实体 → Inspector 切回实体段
- [ ] **Save disabled**：templateName 与盘上原值相同 → Save 按钮灰 + 旁边 "(no changes to save)" 提示

### bugs

（验收期登记；当前为空）

---

## 已知简化范围（c5 deferred）

- **MaterialSystem::EnumerateTemplateNames 公共 API**：c5 Combo 硬编码 5 模板名，游戏侧 RegisterTemplate 自定义模板看不到。后续 GAP-2026-05-16-material-system-enumerate-and-instance-overrides 落地后改自动取
- **uniform / texture override 编辑 UI**：c5 当前没画 uniform 调参控件；当前内置 material 全 default 无 override。后续 GAP 同款落地后扩 .material schema v1.1 + 完整 UI
- **Save 后 runtime swap**：c5 Save 仅写盘，运行时 MaterialInstance 不重建；用户需重启编辑器看到 template 切换效果

---

## 整 milestone 大节点回归

- [ ] **跨 commit 联动**：c3 拖 → c4 接 → c5 编辑 + Save 完整链路一遍跑
- [ ] **DnD 与命令栈**：从 cube 拖到 plane → Ctrl+Z 回 cube → Ctrl+Y 再到 plane（三步值正确）
- [ ] **Asset 浏览器 + Inspector 并存**：用户在 Asset 浏览器选 .material 后再选实体——Inspector 应该走 selectedAssetPath 优先级（c5 设计）还是实体？验证 c5 当前优先 selectedAssetPath
- [ ] **demo scene Save / Open / New**：v0.5 改动后 File 菜单 Save / Open / New 路径不破——保存当前 scene、关闭再开、新建空 scene 三路径全跑
- [ ] **v0.4 picking + gizmo 回归**：v0.5 不应破坏 v0.4 c1~c5 已落功能（viewport 点选实体 / W/E/R 切 gizmo / DnD reparent / Play→Stop 状态机）
