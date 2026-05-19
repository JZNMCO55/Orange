# OrangeEditor v0.8.5 编辑器视觉基线 milestone 验收清单

- 基准日期：2026-05-19
- 适用范围：OrangeEditor v0.8.5 sky / grid / tonemap / PBR showcase 核心视觉验收
- 设计意图：只列用户能在编辑器内点击 / 看效果的**核心**视觉变化；已登记的归属债 / shader 内部数学 / fallback 路径不列

## 前置环境

1. `git pull && cmake --build build --config Debug --target OrangeEditor`
2. 启动 `build/bin/Debug/OrangeEditor.exe`；首次启动自动 seed + Save `assets/scenes/pbr_showcase.scene.json`（24 entity）

## 核心功能

### 1. sky / grid pass + ScenePanel toolbar 控件

- [ ] ScenePanel toolbar 含 **Grid** + **Sky** checkbox；默认两者都勾选
- [ ] 取消 **Grid** → viewport 平面棋盘格线立即消失；勾回立即恢复
- [ ] 取消 **Sky** → 背景切到中性灰 (~#1F1F22)；勾回 procedural 3 色 horizon（地平线偏暖 / 天顶偏蓝）+ 太阳 disc
- [ ] grid 与场景几何遮挡正确：camera 推近球体后 grid 被球体前面挡住、被球体后面穿过

### 2. PBR showcase 场景

- [ ] File → Open Scene 选 `pbr_showcase.scene.json`，进入 24 entity 场景：左侧 3×3 暖橙球阵 + 右侧 3×3 furnace 纯白球阵
- [ ] 9 个暖橙球：横轴 metallic 0 / 0.5 / 1，纵轴 roughness 0 / 0.5 / 1；金属球（右列）有清晰反射，粗糙球（上行）色调柔和
- [ ] 9 个纯白球（furnace 测试）：整体亮度均匀，金属球不发黑塑料、粗糙球不偏暗
- [ ] Hierarchy 含 Camera + DirectionalLight (Sun) + Environment 三个 root entity；点 Environment 在 Inspector 看到 cubemap 字段

### 3. Default material 切 PBR + tonemap

- [ ] 选任意 entity → Inspector → +Add Component → Renderable；默认材质显示为 `pbr.material`（非 `default.material` 棋盘格）
- [ ] 暖橙 r0 球（左上）emissive 区域不再硬染色边、edge 平滑过渡（ACES tonemap 生效证据）

## 已知不验收（与 v0.8.5 范围正交）

- grid pass + dummy IBL ambient 默认值 (0.25 灰) + clear color 当前塞 engine `Pipeline` 公共面，登记于 engine-known-gaps `GAP-2026-05-19-editor-aux-passes-in-engine-pipeline`，迁回 editor 端走独立 session
- PBR push constant 160 B 在 `maxPushConstantsSize < 160B` 设备日志告警 + 不阻塞启动（实际未阻塞当前桌面 GPU），登记于 `GAP-2026-05-19-pbr-push-constant-exceeds-spec-min`
- 自动 re-bake `Pipeline::BakeIblFromWorld` 走同步阻塞 `WaitIdle`（~几百 ms）；async upload 留 Phase 9+ async render graph

## v0.8.5 retro · 单 commit 偏差点

ca0f112 单 commit 35 文件 / +3076 行 把 "6 块视觉联动 + 4 个 bug fix + PBR showcase 资产 + 18 个 material 文件 + 941 行 scene.json" 塞一个 commit，违背 v0.8 期 c1/c2/c3 切片纪律。

**校准下次 commit-plan**：含 ≥3 块视觉联动 + 大量配套资产的 milestone 必须预先拆 c1（pass infra）/ c2（资产）/ c3（showcase scene）/ c4（bug fix 汇总），不要等 review 阶段才发现回滚 / bisect 牵连。
