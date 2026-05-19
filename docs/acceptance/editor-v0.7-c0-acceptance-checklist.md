# OrangeEditor v0.7 c0 Inspector asset plugin 整骨验收清单

- 基准日期：2026-05-19
- 适用范围：OrangeEditor v0.7 c0 IEditorAssetInspectorPlugin 抽象 + Material 子模式 plugin 迁出（消除已知限制 L16）
- 设计意图：纯架构整骨，无视觉变化。只验证 v0.5 c5 落地的 Material 子模式编辑路径行为完全不变（回归不破），以及实体 Inspector 路径不受影响

## 前置环境

1. `git pull && cmake --build build --config Debug --target OrangeEditor`
2. 启动 `build/bin/Debug/OrangeEditor.exe`；首次启动自动 seed 或 File → Open `assets/scenes/pbr_showcase.scene.json`

## 核心功能（v0.5 c5 Material 编辑路径回归）

### 1. Material 子模式接管路径

- [ ] 底部 Assets tab 点 `assets/materials/pbr.material`（或任一 `.material` 文件）→ Inspector 区域立即切到 Material 编辑视图：顶部 `Material: <path>` + Template Combo + PBR 五通道调参（pbr 模板时）+ Save 按钮
- [ ] Template Combo 含已注册模板项（至少 `pbr`、`default`）；切换某模板后下次帧 Combo 仍保留选择不被盘上值覆盖
- [ ] PBR Base Color / Metallic / Roughness / AO 滑块拖动 → 选中 entity 球体 viewport 视觉立即变化（live MaterialInstance 即时生效）
- [ ] Save 按钮：dirty 时可点 → 弹出 "已保存到 .material 文件" 提示

### 2. 实体 Inspector 路径不受影响

- [ ] 取消选中资源（点 Assets tab 空白 / 切别的非 `.material` 文件）+ Hierarchy 选任一实体 → Inspector 切回实体 Inspector：显示 `Entity #N` + 各 component 段 + Add Component 按钮
- [ ] 实体 Inspector 内任一字段编辑（如 Transform position drag）+ Ctrl+Z / Ctrl+Y 正常 work

### 3. Plugin 注册路径未破其他 plugin

- [ ] 选挂有 Animator 的实体（如 demo 内 "Test Fighter"）→ AnimatorComponent 段底部仍显示 v0.3 落地的 Mini-Preview 横幅（"Status: Finished" / "Backend: ..."）—— 证明 IEditorInspectorPlugin（component 装饰）与新 IEditorAssetInspectorPlugin（资源接管）正交并存

## 已知不验收（与 c0 范围正交）

- `assetInspectorPlugins` 注册表实现细节、unique_ptr 生命周期、CanHandle 命中优先级 —— 纯架构改动无视觉表现
- v0.7 c1+ deliverables（Animator backend 切换 / AnimationStateMachine 图编辑 / DragonBones 浏览 / Procedural channel 面板）—— 本期 scope 外，v0.7 整体未 ✅
- v0.5 c5 已存在的 fallback / 退化路径（pbr 模板未挂 live instance 时显示提示）—— 行为不变，无需重复验证
