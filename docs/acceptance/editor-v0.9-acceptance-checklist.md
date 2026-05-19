# OrangeEditor v0.9 Profiler / Debug Draw 集成 milestone 验收清单

- 基准日期：2026-05-19
- 适用范围：OrangeEditor v0.9 Debug Draw + Profiler + Memory 三件套验收
- 设计意图：只列用户能在编辑器内点 / 看效果的**核心**功能；架构整骨 / 内部宏 / API 设计选型不进清单

## 前置环境

1. `git pull && cmake --build build --config Debug --target OrangeEditor`
2. 启动 `build/bin/Debug/OrangeEditor.exe`；demo.scene 17 个 entity 就位

## 核心功能

### 1. Debug Draw（c1 + c2）

- [ ] ScenePanel toolbar 含 **Debug Draw** checkbox（在 Grid / Sky 右侧）；默认未勾选
- [ ] 勾选 → viewport 立即出现原点 3 轴坐标（X 红 / Y 绿 / Z 蓝，长度 1.5）
- [ ] 选中 Hierarchy 任一 entity → viewport 内该 entity 位置出现黄色 wireframe sphere（半径 0.5，16 段）
- [ ] Ctrl+点其它 entity（v0.8 多选）→ 多个 sphere 同时显示，主选 + additional 都画
- [ ] 取消勾选 → 坐标轴 + sphere 立即消失；勾回 → 立即恢复（zero-cost off 路径）
- [ ] debug 几何不被场景几何遮挡：相机推近球体后，坐标轴仍在球体前面可见（always-on-top）

### 2. Profiler 面板 Performance tab（c3 + c4 + c5）

- [ ] View → Profiler 打开 Profiler 浮动面板；含 **Performance** + **Memory** 两个 tab，默认 Performance
- [ ] 顶部显示 `Frame N  Δ=X.XX ms (Y.Y FPS)`；数字每帧更新
- [ ] 下方折线图（PlotLines）显示最近 128 帧 Δ-time 曲线；y 上限 50ms
- [ ] 折线图下方表格 Name / Inclusive / Exclusive / Calls 四列
- [ ] 展开 "Frame" 节点 → 子节点 PollEvents / LayerUpdate
- [ ] 展开 "LayerUpdate" → 子节点 Render
- [ ] 展开 "Render" → 子节点 Shadow / Sky / MainPass / Particles / Bloom / Tonemap / DebugDraw / Grid
- [ ] 每个 bin 显示非零 inclusive ms（活动 pass）+ 合理 call count（typically 1/帧）

### 3. Profiler 面板 Memory tab（c6）

- [ ] 切到 Memory tab → 两个区域
- [ ] **Tracked Categories** 显示 "(no categories registered; ...)"（v0.9 范围内模块未 opt-in；API 已就位待未来 instrumentation）
- [ ] **Module Counts** 显示 ≥ 5 行：Scene::Entity count（17 左右）/ Asset::Registry size / Render::Pipeline cache / Render::Bloom mip count / Profiler::Sample bin count / Memory::Category count
- [ ] 末尾 disclaimer 说明 byte-accurate allocator hook 是 future work

### 4. Sample 15 debug_draw_minimal（c1）

- [ ] 跑 `build/bin/Debug/15_debug_draw_minimal.exe`
- [ ] 看到旋转 cube + 绿色 AABB 紧贴 cube 外 + 白色 wireframe sphere + 3 条彩色坐标轴 + 橙色三角形

## 已知不验收（与 v0.9 范围正交）

- **Schema dispatch 整骨（v0.8 c5 自承诺留 v0.9）**——调研后体量 ≥ 半天（GetFn/SetFn 签名 + 20+ lambda + dispatch），与 v0.9 主线正交；推延到 v0.9.5 patch milestone（先例：v0.6.5 / v0.8.5 都是从大 milestone 拆出的 patch）
- **Tracy 后端**——ADR-003 选定自实现 AutoProfile 路径，Tracy gate 保留 CMake stub 不接通
- **多线程 instrumentation**——Profiler / Memory 当前仅主线程；job system 上线时扩 thread-local，留 ADR-003 Consequences 段
- **byte-accurate per-allocator memory tracking**——本期是 logical introspection + opt-in counter API；heap-level hook 是 non-trivial refactor，留未来工作

## v0.9 retro · 参考引擎对比

本期引入两个新机制——按 milestone-end-checklist 步骤 3 必做对比：

### Profiler 后端（自实现 AutoProfile RAII）

- **Lumix** `vendor/LumixEngine/src/core/profiler.{h,cpp}`：与本期路径一致——自实现 sample bin + RAII scope + ImGui UI；Lumix 额外双轨支持 Tracy via 宏 dispatch
- **OrangeEditor**：仅自实现（ADR-003 方案 B）。Tracy 双轨是 superseding ADR 升级路径，当前不付工程量
- **结论**：选型合理；Lumix 双轨方案确认是 "B 的超集"，不是误选

### DebugDraw 公共面 wrap（engine 端封 OrangeRender）

- **Lumix**：`src/renderer/draw2d.cpp` + `WorldView::Vertex::abgr`——直接消费底层 RHI，无中间 wrap 层（因为 Lumix 引擎自己同时 own renderer + scene + editor，无 header isolation 跨横线）
- **Godot** `editor/plugins/`：editor 通过 `RenderingServer` 公共 API 调试绘制，wrap 层与 OrangeEditor 同款节奏（editor 不直接 include backend）
- **OrangeEditor**：选 Godot 节奏（wrap 在 engine 端，editor 通过 `Pipeline::GetDebugDrawScene()` 消费）；与 CLAUDE.md "Header isolation" invariant 一致
- **结论**：选型合理；Lumix 没 header isolation 需求所以无 wrap，与本项目情况不同

## v0.9 retro · commit 序列回顾

milestone-start-checklist 第 7 步草拟 8 个 commit（c0–c8）；实际落地：

| 草稿 | 实际 | 备注 |
|------|------|------|
| c0 | `14d8d3f` ADR-003 草稿 | 单独 commit，开 milestone 第一刀（与草稿一致）|
| c1 | `be33c49` DebugDraw wrap + sample 15 | 一次落地（与草稿一致）|
| c2 | `ab26ae7` editor 接通 DebugDraw + 选中 entity sphere | 一次落地（与草稿一致）|
| c3 | `8ad1ab1` Core::Profiler 后端 | 一次落地（与草稿一致）|
| c4 | `15b0b6c` AppHost finalize + Pipeline instrumentation | 一次落地（与草稿一致）|
| c5 | `bc136e2` ProfilerPanel | 一次落地（与草稿一致）|
| c6 | `64492cd` Memory API + Memory tab | 一次落地（与草稿一致）|
| c7 | **推延 v0.9.5** | 调研后体量 ≥ 半天，与主线正交，重新评估推延 |
| c8 | （本 commit）✅ + acceptance-checklist | （与草稿一致）|

**主要偏差**：c7 推延。**校准下次**：commit-plan 草拟时对"v0.X-1 自承诺留 v0.X"类型的 deferred 项，先在 start-checklist 步骤 7 估算实际工作量；超过 milestone 1/3 体量的应该拆独立 patch milestone 而非塞主 milestone。

**主要顺利**：Performance/Memory 两个机制的 Core API + Editor 面板 1:1 对位 commit 切片节奏稳定（c3/c5 / c4/c6 两组），开工时低估了 ProfilerPanel TabBar 重构成本，但 c5 内消化未拆 c5.1/c5.2。
