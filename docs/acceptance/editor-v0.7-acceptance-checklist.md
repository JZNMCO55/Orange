# OrangeEditor v0.7 milestone 验收清单

- 基准日期：2026-05-19
- 适用范围：OrangeEditor v0.7（c0 plugin 抽象 + c1 backend 切换 + c2 状态机图 + c3 DragonBones 浏览 + c4 Procedural channel）—— milestone-level 定版
- 完工标记：v0.7 整体 ✅；已知限制 L7 全部消除

## 前置环境

1. `git pull && cmake --build build --config Debug --target OrangeEditor`
2. 启动 `build/bin/Debug/OrangeEditor.exe`；首次启动自动 seed 或 File → Open `assets/scenes/pbr_showcase.scene.json`

## c0 · IEditorAssetInspectorPlugin 抽象（消除 L16）

### 1. Material 子模式接管路径

- [ ] 底部 Assets tab 点 `assets/materials/pbr.material`（或任一 `.material` 文件）→ Inspector 切到 Material 编辑视图：`Material: <path>` + Template Combo + PBR 五通道调参 + Save 按钮
- [ ] PBR Base Color / Metallic / Roughness / AO 滑块拖动 → 选中 entity 球体 viewport 视觉立即变化
- [ ] Save 按钮：dirty 时可点 → 弹出 "已保存到 .material 文件" 提示

### 2. 实体 Inspector 路径不受影响

- [ ] 取消选中资源 + Hierarchy 选任一实体 → Inspector 切回实体 Inspector + 各 component 段正常显示
- [ ] 实体 Inspector 字段编辑 + Ctrl+Z / Ctrl+Y 正常 work

## c1 · Inspector Animator backend 切换

### 3. Animator Backend Combo

- [ ] Hierarchy 选挂有 Animator 的实体 → AnimatorComponent 段 Mini-Preview 上方出现 **Backend Combo**（含 `procedural` 等已注册 backend）
- [ ] 切换 Combo → animator 即时重建；Ctrl+Z / Ctrl+Y 撤销 / 重做切换
- [ ] Play 模式 Combo 禁用；Stop 后恢复可用

## c2 · AnimationStateMachine 图编辑器

### 4. .anim_fsm 接管 + 节点图

- [ ] 底部 Assets tab 点 `assets/animations/demo.anim_fsm` → Inspector 切到 Anim FSM 子模式：顶部 path + Save + dirty 指示 + 统计行（3 states / 5 transitions / initial idle）
- [ ] Canvas 内可见 3 矩形节点（idle / walk / jump）按 demo layout 摆开；初始 idle 节点左上角金色三角（initial state 标识）
- [ ] 5 条 transition 边：直线 + 中点箭头朝 to 节点；含 1 条带 self-loop（如有）画圆环不带箭头

### 5. 节点交互 + Save + Undo/Redo

- [ ] 单击节点选中（border 加粗 + 上方 Selected 行显示 name）；拖动节点改位置
- [ ] 右键空白 = Add State popup（输入 name → Add 创建新节点）
- [ ] 右键节点 popup：Rename / Delete / Set as Initial / Cancel + "Transitions from this state" Edit/Delete 列表 + "Create transition to" Selectable 列表
- [ ] Ctrl+Z / Ctrl+Y 拖动 / Add / Delete / Rename / Set Initial / 边增删全可撤销
- [ ] Save 按钮 dirty 时启用 → 写盘 + 清 dirty；重启后状态保留

### 6. transition condition 编辑

- [ ] "Parameters" 折叠段 + Add Parameter popup（type combo: Bool / Int / Float / Trigger）；列表带 Delete 按钮
- [ ] 节点 popup Transitions list 点 Edit 选中某 transition → 顶部 "Selected transition: from → to" 折叠段出现
- [ ] Selected transition 段内 + Add Condition / Delete Condition：每条 row = paramName combo + op combo（If/IfNot/>/</==/!=/>=/<=）+ threshold input（按 paramName 对应 type 渲染 Checkbox / InputInt / InputFloat）
- [ ] 编辑后 Save 写盘；reload demo.anim_fsm 验证 round-trip（包含 parameters 段 + transitions[].conditions[]）

## c3 · DragonBones 资源浏览

### 7. _ske 资源接管 + metadata 显示

- [ ] Assets tab 选中 `_ske.json` / `_ske.dbbin` 文件 → Inspector 接管：顶部 dragonBones name + armatures 数 + 每个 armature TreeNode（bones 表 + animations 列表带 Preview 按钮 toast）
- [ ] 选回实体 / 其它 asset → 子模式切走，原路径无回归

> 注：当前 assets/ 内无 DragonBones 资源；c3 落地的是"plugin 链路 + metadata 浏览代码就绪"，用户放入实际 `_ske.json` 即可立即生效。

## c4 · Procedural Animator channel 浏览

### 8. AnimatorComponent procedural backend 时显示 channel list

- [ ] Hierarchy 选挂 procedural backend 的 Animator entity → Inspector AnimatorComponent 段 Mini-Preview 末尾出现 "Channels (N):" + 每条 BulletText（demo 默认含 `dissolve_t` 一条）
- [ ] 切到非 procedural backend（如 skeletal_dragonbones）→ channel 段自动隐藏

## patch · Intel Iris Xe sky pass driver bug workaround（commit `0890006`）

### 9. Intel Iris Xe 测试机启动回归

- [ ] Intel Iris Xe 测试机启动 OrangeEditor 不再崩在 `vkCmdBeginRendering` 内 `igvk64.dll` 0x3B0 deref；进入 ImGui dock 主循环
- [ ] NV / AMD 桌面 GPU 行为无回归

## 已知不验收

- 切 .anim_fsm 文件时 Clear 全局 cmdStack（影响 entity 等其它 panel 的 Undo）—— 与切 Scene 同款纪律
- 边走直线 + 箭头不走 Bezier；端点不 clip 到节点矩形边缘 —— polish 留 v1.x
- 无拖出 pin / flying line 创建 transition（popup Selectable 替代功能等价）
- canvas 无 pan / zoom（Lumix 同款简化）
- DragonBones 单 clip 实时预览渲染（独立 preview viewport）—— c3 scope 外，留 v1.x
- Procedural channel **fn 配置**（不仅是 name 浏览）—— std::function 不可序列化，需 channel 表达式 DSL；与 Material UBO 通路一起在 v1.x 落
- 切换 backend 后旧 animator 内部状态丢失（c1 刻意接受的有损语义）
