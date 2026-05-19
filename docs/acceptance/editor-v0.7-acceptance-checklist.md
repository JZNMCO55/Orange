# OrangeEditor v0.7 milestone 验收清单

- 基准日期：2026-05-19
- 适用范围：OrangeEditor v0.7 已落地 sub-commits（c0 + c1）
- 设计意图：v0.7 是多 sub-commit milestone（c0~c4），本清单滚动更新；c2/c3/c4 落地后追加对应段，v0.7 整体 ✅ 时定版

## 前置环境

1. `git pull && cmake --build build --config Debug --target OrangeEditor`
2. 启动 `build/bin/Debug/OrangeEditor.exe`；首次启动自动 seed 或 File → Open `assets/scenes/pbr_showcase.scene.json`

## c0 · IEditorAssetInspectorPlugin 抽象（消除 L16）

### 1. Material 子模式接管路径

- [ ] 底部 Assets tab 点 `assets/materials/pbr.material`（或任一 `.material` 文件）→ Inspector 区域立即切到 Material 编辑视图：顶部 `Material: <path>` + Template Combo + PBR 五通道调参（pbr 模板时）+ Save 按钮
- [ ] Template Combo 含已注册模板项（至少 `pbr`、`default`）；切换某模板后下次帧 Combo 仍保留选择不被盘上值覆盖
- [ ] PBR Base Color / Metallic / Roughness / AO 滑块拖动 → 选中 entity 球体 viewport 视觉立即变化（live MaterialInstance 即时生效）
- [ ] Save 按钮：dirty 时可点 → 弹出 "已保存到 .material 文件" 提示

### 2. 实体 Inspector 路径不受影响

- [ ] 取消选中资源（点 Assets tab 空白 / 切别的非 `.material` 文件）+ Hierarchy 选任一实体 → Inspector 切回实体 Inspector：显示 `Entity #N` + 各 component 段 + Add Component 按钮
- [ ] 实体 Inspector 内任一字段编辑（如 Transform position drag）+ Ctrl+Z / Ctrl+Y 正常 work

## c1 · Inspector Animator backend 切换

### 3. Animator Backend Combo

- [ ] Hierarchy 选挂有 Animator 的实体（demo 内 "Test Fighter"）→ Inspector → AnimatorComponent 段底部 Mini-Preview 横幅上方出现 **Backend Combo**
- [ ] Combo 至少含 `procedural`（demo 注册的内置 backend）；当前选中项 = 该实体 animator 的 BackendName
- [ ] 切换 Combo 到另一 backend（游戏侧若额外注册 `skeletal_dragonbones` 等）→ AnimatorComponent.animator 即时重建为新 backend；Mini-Preview "Backend:" 行随之更新
- [ ] Ctrl+Z 撤销切换 → backend 恢复到上一种；Ctrl+Y 重做切换
- [ ] Play 模式（Inspector 顶部 ▶）进入后 Combo 自动禁用；Stop 退回 Edit 模式 Combo 恢复可用

### 4. Plugin 正交并存（c0 + c1 联合）

- [ ] 同一帧 Inspector 同时显示：实体 component schema 段（含 read-only `backend` 字段）+ AnimatorMiniPreviewPlugin 装饰段（Combo + Status + Backend 三行）—— 证明 IEditorInspectorPlugin（装饰）与 IEditorAssetInspectorPlugin（接管）正交并存
- [ ] 点 `.material` 文件 → Inspector 切到 Material 子模式 → 再点 entity → 回到实体 Inspector + Animator Combo 仍正常显示

## 已知不验收（与 c0 + c1 范围正交）

- `assetInspectorPlugins` / `inspectorPlugins` 注册表实现细节、unique_ptr 生命周期、CanHandle 命中优先级 —— 纯架构改动无视觉表现
- v0.7 c2/c3/c4 deliverables（AnimationStateMachine 图编辑 / DragonBones 浏览 / Procedural channel 面板）—— 本期 scope 外，v0.7 整体仍 ❌
- **切换 backend 后旧 animator 内部状态丢失**（elapsed clock / 当前 clip）—— v0.7 c1 刻意接受的有损语义；Undo 只恢复 backend 类型不恢复内部状态
- v0.5 c5 已存在的 fallback / 退化路径（pbr 模板未挂 live instance 时显示提示）—— 行为不变，无需重复验证
