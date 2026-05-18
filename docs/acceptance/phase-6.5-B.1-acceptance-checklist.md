# Phase 6.5 B.1 · PBR direct lighting 验收清单

- 基准日期：2026-05-18
- 适用范围：Phase 6.5 B.1（Task 06.5-01 / 02 / 03）—— PBR direct lighting 落地，IBL 槽位 dummy 退化
- 设计意图：只列用户能在 sample / 编辑器内点击 / 拖拽 / 看效果的**核心**功能；已知简化 / 内部机制不列；B.2 IBL 相关项另开 checklist

## 前置环境

1. `cmake --build build --config Debug` 一次性把 OrangeEditor + sample 13_pbr_direct 都编出来
2. 任意 cwd 启动 `build/bin/Debug/13_pbr_direct.exe`（独立 sample）/ `build/bin/Debug/OrangeEditor.exe`（编辑器）

## 核心功能

### 1. sample 13_pbr_direct 9 球阵

跑 `build/bin/Debug/13_pbr_direct.exe`：

- [ ] 看到 **3×3 暖橙球阵**（baseColor 偏橙黄色）
- [ ] **行方向（自下而上）金属度递增**：底行球面有明显白色高光 + 橙色漫反射；顶行球面几乎无漫反射、高光本身带橙色着色（金属能量守恒：`kD = (1-F)(1-metallic)`）
- [ ] **列方向（自左向右）粗糙度递增**：左列球高光是清晰小点；中列球高光中等模糊；右列球高光几乎平涂消失
- [ ] **关闭窗口**正常退出，无残留进程 / 无控制台 stderr 异常

### 2. 编辑器 Inspector PBR 调参

启动编辑器，在 Hierarchy 任选一个带 `Renderable + Material(pbr)` 的实体（demo scene 内 Dynamic Box / Floor / Wall 之一切到 PBR 即可）：

- [ ] Inspector **Material** 段显示 `pbr` template + 5 个字段：BaseColor / Metallic / Roughness / Normal / AO
- [ ] **BaseColor 改红**：物体表面变红；metallic=1 时高光带红色（不再白色）
- [ ] **Metallic 拖 0 → 1**：表面从"塑料漫反射"过渡到"金属高反射"，diffuse 区域同时变暗
- [ ] **Roughness 拖 0 → 1**：高光从"清晰小点"过渡到"模糊大片"再到"几乎消失"

### 3. Material 资源持久化

- [ ] Inspector 改完 BaseColor / Metallic / Roughness 后点 **Save**：弹"已保存"popup
- [ ] 关闭编辑器 → 重启 → 同实体的 `.material` 文件加载回来后 Inspector 字段值与 Save 时一致

## 大节点回归

- [ ] **viewport 渲染不破**：编辑器 Scene 面板里现存所有 demo 实体（Floor / Wall / Dynamic Box 等）渲染正常，无黑屏 / 棋盘色残留 / Pipeline Initialize 失败
- [ ] **既有 sample 不退化**：跑 `12_layer_partition_demo.exe` 两图层切换正常；`07_full_pipeline.exe` 全场景渲染 + 物理 + 动画正常
- [ ] **lint baseline 全绿**：`python scripts/check_invariants.py` + `python scripts/check_claude_md_drift.py` 各自退出码 0

## 已知简化范围（不验收）

- **IBL 三通道**（BRDF LUT / irradiance / prefiltered specular）此期间绑 dummy 1×1 黑纹理，阴影区域偏暗是预期 —— 真实 IBL 接入留 B.2
- **Normal / AO 贴图**当前只是 schema 字段 + Inspector AssetRef 控件，shader 端不消费（B.1 dummy 槽位）—— 贴图绑定完整路径留 per-instance descriptor set 基础设施
- **Material schema v1.1 → v1.2** 暂未演进（uniforms 列表手写默认值即可），Disney diffuse / clear-coat 等扩展 BRDF 全部 out-of-scope
