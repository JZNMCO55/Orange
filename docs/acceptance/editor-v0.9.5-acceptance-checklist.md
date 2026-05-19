# OrangeEditor v0.9.5 Schema dispatch 整骨 milestone 验收清单

- 基准日期：2026-05-19
- 适用范围：OrangeEditor v0.9.5 schema AssetRef accessor ctx-aware 改造（ADR-004）
- 设计意图：纯架构整骨，无视觉变化。只验证 v0.9 已通过的 AssetRef 编辑路径行为完全不变（回归不破）

## 前置环境

1. `git pull && cmake --build build --config Debug --target OrangeEditor`
2. 启动 `build/bin/Debug/OrangeEditor.exe`；首次启动自动 seed 或 Open `assets/scenes/pbr_showcase.scene.json`

## 核心功能（AssetRef 字段编辑回归）

### 1. Renderable.mesh 编辑

- [ ] 选场景内任一 Renderable entity（如左上球）→ Inspector → Renderable 段 → Mesh 字段显示当前 mesh 短名（如 `sphere.mesh`），hover 显示全路径 tooltip
- [ ] 拖底部 Assets tab 里 `cube.mesh` 到 Mesh 字段 → viewport 立即变 cube；Ctrl+Z 还原球体；Ctrl+Y 重新变 cube
- [ ] 点 Mesh 字段右侧 × 清除按钮 → 字段显示 `(none)`；Ctrl+Z 恢复

### 2. Renderable.materialInstance 编辑

- [ ] Inspector → Renderable 段 → Material 字段显示 `pbr.material` 或当前 material 短名
- [ ] Assets tab 选 `pbr_showcase/m0_r0.material` 后点 Material 字段右侧 Pick 按钮 → 字段更新 + viewport 球体外观变化；Ctrl+Z 还原

### 3. Environment.cubemap 编辑

- [ ] Hierarchy 选 Environment entity → Inspector → Environment 段 → Cubemap 字段显示当前 HDR 短名（如 `studio_small_09_2k.hdr`）
- [ ] 拖另一个 `.hdr` 到 Cubemap 字段 → Pipeline 自动 re-bake；viewport sky / IBL 更新；Ctrl+Z 还原原 HDR + 自动 re-bake

### 4. 非 AssetRef 字段未受影响

- [ ] DirectionalLight Color / Intensity / Casts Shadow 编辑正常 + Undo/Redo
- [ ] Transform position / rotation / scale 编辑正常 + Undo/Redo
- [ ] ParticleEmitter Color curve（拆出的 RGB + Alpha 字段）编辑正常

## 已知不验收（与 v0.9.5 范围正交）

- gpAssetContext / SetEditorAssetContextForSchema 下架是纯内部架构改动，无视觉表现 —— 不需要"用户验证 file-scope 单例消失"
- SchemaInspector AssetRef case 双路径 dispatch（c2 保留的旧 get/set 回退分支）对内置 schema 不再触发，仅作为外部 plugin 兼容防御 —— 不需要用户验证回退路径
- 外部 plugin 注册 FieldAssetRef 走旧签名的兼容性 —— 当前无外部 plugin 用例，留待真出现时验证
