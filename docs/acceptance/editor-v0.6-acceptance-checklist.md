# OrangeEditor v0.6 多 chunk / per-layer + dirty 状态 milestone 验收清单

- 基准日期：2026-05-17
- 适用范围：OrangeEditor v0.6 核心功能验收
- 设计意图：只列用户能在编辑器内点击 / 拖拽 / 看效果的**核心**功能；已知简化 / 内部机制不列

## 前置环境

1. `git pull && cmake --build build --config Debug --target OrangeEditor`
2. 任意 cwd 启动 `build/bin/Debug/OrangeEditor.exe`（双击 / IDE F5 / 命令行均可）
3. 启动后窗口直接 maximize，demo scene 自动加载

## 核心功能

### 1. Dirty 状态 + 关闭确认 + Save 入口（c1 / c2 / c3）

- [ ] 改动任意 Inspector 字段后窗口 title 出现 `*` 后缀（"demo.scene.json * — OrangeEditor"）
- [ ] dirty 时 menu bar 右侧 **Save** 按钮 accent 蓝高亮；点击 → title `*` 消失
- [ ] dirty 时按 Esc / 点窗口 × / File>New / File>Open / File>Open Split 任一 → 弹"未保存改动"确认 popup（Save / Discard / Cancel 三选一）

### 2. Layer 编辑 UI（c4 / c5）

左侧 **Layers** 面板（与 Entity Tree dock 同 tab）。

- [ ] Layers 面板列出 `default` layer；`[X]` 删除按钮灰禁（hover 提示）
- [ ] 点 **+ Add Layer** → popup 输入 id `foreground` / displayName `Foreground` → Create → 列表新增条目
- [ ] Entity Tree 每行末尾右对齐显示当前 layer chip（灰色文本，hover 出 tooltip）
- [ ] 节点右键 → **Move to layer > Foreground** → 行末 chip 变 `foreground`；当前归属项标 `(current)` disabled
- [ ] Layers 面板取消 **Foreground** visibility checkbox → viewport 中该 layer 实体立即消失（Render 过滤生效）
- [ ] Play Mode 下取消 visibility → 该 layer 上的 dynamic body 物理冻结（不再下落 / 不再产生 contact）
- [ ] `Ctrl+Z` 回退 visibility 切换 / Move to layer / Add Layer 操作

### 3. Per-layer 多文件落盘（c6）

- [ ] File > **Save Split As...** → 选 `XXX.scene.manifest.json` 路径 → 同目录生成 manifest + `<layer.id>.scene.json` 各一份
- [ ] 关闭编辑器后重启 → File > **Open Split...** 选刚保存的 manifest → World + Layer 列表完整还原（含 displayName / visible / source）
- [ ] 单文件 File>Save 路径不受影响：File>Save Scene As → 仍写单文件 `.scene.json`，layer manifest 元数据不持久化但每 entity 的 `LayerComponent.layerId` 跟随

### 4. Asset 浏览器右键 "Pick to Inspector field"（L17）

- [ ] 选中带 Renderable 的 entity（如 `Dynamic Box`）→ Asset 浏览器右键 `cube.mesh` → 菜单 **Pick to Renderable.mesh** 可点 → mesh 切换 + `Ctrl+Z` 回滚
- [ ] 同款流程右键 `toon.material` → **Pick to Renderable.material** 可点 → 材质切换 + Material 子模式不接管（互斥选择由 RMB 路径绕过）
- [ ] 未选中 entity / 选中 entity 无 Renderable 时菜单显示 `(no entity selected)` / `(...has no Renderable)` 提示

## 已知简化范围（不验收）

- **多文件 / 单文件路径不互通**：从 manifest 打开后点 File>Save 仍走单文件 Save 覆盖 manifest 文件本身，不再重写 per-layer 文件。继续 split 必须再走 Save Split As
- **partition.visible / displayName 在单文件 Save 不持久化**：单文件重启后所有 layer 默认 visible（实体的 layerId 通过 LayerComponent 落盘恢复）；要持久化 visible 必须走 Save Split As
- **删 layer 不可 Undo**：Layers 面板的 `[X]` 直接 mutate + 清 cmdStack（与 DestroySubtree 同款约定），归属于被删 layer 的 entity 自动改归 default
- **Hierarchy 跨 layer parent-child 关系在 split 模式下丢失**（per-layer .scene.json idMap 从 0 重排）——与引擎侧 GAP-2026-05-17 C2 落地记录的"per-layer 独立编辑预期约束"对齐
- Layers 面板不支持重命名 displayName / 调整 layer 顺序 / 显示 per-layer entity count（→ v0.7+）
- 多 scene tab（roadmap v0.6 deliverable）未做——单 World 单 scene 路径下用户体感无差，按 editor-roadmap "条件触发：单关卡可承受可推迟" 暂留

## 大节点回归

- [ ] **跨功能链路**：v0.5 Asset 浏览器 DnD / Material 子模式 / Inspector 字段编辑 / v0.4 gizmo 拖动 / v0.2 Undo Redo / v0.1.5 demo scene 加载——全部跑通无回退
- [ ] **Play Mode round-trip**：Play → 物理 tick 正常 → Stop → World 还原到 Play 前 + partition 不变
