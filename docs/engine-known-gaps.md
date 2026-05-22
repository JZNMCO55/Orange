# OrangeEngine 已知能力缺口

本文件登记 **编辑器 / sample / 游戏侧推进过程中发现的、需要 OrangeEngine 自身新增能力或修复的具体缺口**。与 OrangeRender 仓的 `vendor/OrangeRender/docs/incoming_feature.md` 同节奏：

- 每条以 `## GAP-<日期>-<short slug>` 开头，便于 commit / PR title 引用
- 包含：发现方 / 发现日期 / 一句话定性 / 触发场景 / 缺什么 / 期望验收 / 状态
- **处理流程**：发现 gap 的 session 只做**登记**，**不**在同一 session 里同时改引擎；引擎补强是显式独立 session 处理（精神同 CLAUDE.md "绝对不允许在同一个 session 内既向 OrangeRender 提 feature、又在本仓库消费 / 处理该 feature" —— 把同一纪律下沉到 OrangeEngine ↔ OrangeEditor / Game 关系）
- 落地后搬到本文件末尾的"处理记录"段，CHANGELOG 写详细修复点

## 登记门槛（低摩擦优先）

历史教训：本文件长期只有 1 条登记，原因不是没缺口，是登记摩擦让大量"卡了 5 分钟自己绕过去了"的缺口丢失。**摩擦门槛刻意降低**：

- **任何一次** 编辑器 / sample / 游戏侧推进**因引擎能力缺失暂停超过 5 分钟**，立即登记一条 GAP（即使后续发现是误解，留个待评审条目也比丢失信息好）
- 登记不需要写完整的"缺什么 / 期望验收"——**起初可以只写发现方 + 一句话定性 + 触发场景**，剩余字段后续 session 处理时补
- **登记 ≠ 承诺要做**。GAP 落地节奏由独立 session 评审决定，可以延后 / 合并 / 拒绝。**登记本身是几乎零成本的信息保留**
- 误登记 / 撤回成本低：发现是误解时直接在原条目下加一句"撤回原因"即可，不必删

只有同时满足两条才升级为"真正的工程负担登记"：(1) 已经独立 session 评审过 + (2) 决定要做。在那之前，GAP 条目是松散的需求收件箱。

## 与现有文档的关系

| 文件 | 职责 |
|------|------|
| `docs/design-plan.md` | Phase 1–5.5 task 级历史 |
| `docs/roadmap.md` | Phase 6+ 长期路线（前瞻、按计划） |
| `docs/editor-roadmap.md` | 编辑器路线（前瞻、按计划） |
| `docs/engine-known-gaps.md`（本文件） | **撞上即补**的具体能力缺口（响应消费者发现） |

一条 gap 落地后视情况：
- 如纯属"漏排期"的小补丁（如 `HierarchyComponent` 缺 helper），直接 commit + CHANGELOG
- 如属于完整 Phase 范围（如 PointLight + 多 light 支持），写回 `docs/roadmap.md` 对应 Phase 作为新 Task

---

## GAP-2026-05-11-point-light-and-visible-halo

- **发现方**：OrangeEditor v0.1 / v0.1.5 Demo Scene 设计
- **发现日期**：2026-05-11
- **一句话定性**：Render 公共面缺 PointLight / SpotLight 与可见光晕能力，导致 "点光源照亮黑暗环境 + 光体本身可见" 的典型场景（Ori-like 史莱姆发光 / 灯泡照明）无法实现

### 触发场景

第一款游戏构想里有 "史莱姆主角在黑暗环境中发光探索" 的核心机制：

- 史莱姆位置发出点光，照亮周围 mesh（地面 / 墙壁）
- 玩家能直接看到光本身（光晕 / 光球）
- 经典演示场景用一个灯泡 prop 来表现同一能力，使其也能作为 editor-roadmap v0.1.5 Editor Demo Scene v2 的典型展示项

当前引擎能力对照：

| 原子能力 | 现状 | 备注 |
|---------|------|------|
| DirectionalLight | ✅ | Phase 3 Task 05 |
| PointLight 公共接口 | ❌ | `include/orange/engine/render/LightComponent.h:8` 注释明说 "点光 / 聚光留给 Phase 4+"，但 Phase 4 / 5 都没做 —— 漏网项 |
| SpotLight 公共接口 | ❌ | 同上 |
| Pipeline 多 light 收集 | ❌ | 当前 Pipeline 取 first-found DirectionalLight 作主光 |
| Emissive Material + Bloom | ✅ | Phase 5 Task 02b + Phase 3 Task 03；可让物体 **看起来发光** 但 **不照亮** 周围 |
| Screen-space god rays | ✅ | Phase 5 Task 03；仅 DirectionalLight 阳光光柱，不适用 PointLight |
| Point light volumetric halo | ❌ | 真 froxel-based volumetric point light 是 Phase 10 量级；本 gap 走 billboard 近似 |

### 缺什么（按依赖拆）

#### G1 · `PointLightComponent` 公共接口

- `include/orange/engine/render/LightComponent.h` 加 `PointLight` struct：
  - position 跟 Transform 走（不存 component 上）
  - color (vec3，linear RGB)
  - intensity (float，标量乘子)
  - range (float，光照距离上限，culling 用)
  - attenuation 模型：先用经典 `1 / (1 + linear*d + quadratic*d²)` 或物理基 `1 / d² · smoothstep(0, range, d)` cutoff（实现时定一种）
  - castsShadow（保留字段但本 gap 不实现 omnidirectional shadow map）
- 序列化 Read/Write + SchemaVersion bump（参 CLAUDE.md "Serialization and reflection"：新版本 + migrator，不改已发版本）
- Inspector 段配套（属 editor-roadmap v0.4 范围，本 gap 仅引擎侧）

#### G2 · Pipeline 多 light 收集 + forward 多 light shading

- Pipeline 在 RenderScene 收集阶段把所有 PointLight 收进 light list（cap 上限暂定 8 个，超出截断 + warning log）
- per-frame UBO 把 light list 喂给 fragment shader（push-constant 容量不够，必须走 UBO）
- toon / rim_light fragment shader 加 point light loop：迭代到 cap 上限，距离衰减 + NdotL + range cutoff
- 多 light 排序 / tile-based culling / clustered shading 等高级优化 **不在本 gap 范围**，留给 Phase 10 渲染深化

#### G3 · 可见光晕（最经济版）

- **不做** froxel-based volumetric point light（Phase 10 Task 10-04 体量）
- billboard 自发光 sphere mesh + radial gradient texture + bloom 自动散光近似
- 可考虑提供内置 `PointLightHalo` 子 mesh（程序化生成 sphere + emissive material），editor 侧 v0.4 让 Light gizmo 默认挂这个 prop（gap 内只暴露 mesh / material 资源，editor 侧消费）
- 真正的 volumetric scattering 留给 Phase 10

### 期望验收

- editor-roadmap v0.1.5 Editor Demo Scene v2 中能挂一个 PointLight + halo prop，看到：
  - 史莱姆 / 灯泡 mesh 本身因 emissive + bloom 视觉发光
  - 周围 ground / wall mesh 接收 PointLight 贡献，距离衰减自然过渡
  - 黑暗环境（DirectionalLight intensity = 0 或不挂）下，整个场景仅由 PointLight 决定明暗
- 性能基线：cap=8 PointLight、1080p、Debug 配置主 pass 不显著掉帧（不要求严格 60 FPS，但不要数量级回归）
- 序列化 round-trip：PointLight scene save / load 字段无丢失

### 状态

- **登记**：2026-05-11
- **处理**：G1 + G2 已落地（2026-05-20，与音频集成 + GAP-2 cmake gate 同 session）；G3 halo billboard 留 v1.x Ori-like 视觉子模式拉动。详细落地点见本文件"处理记录"段
- **关联**：editor-roadmap.md v0.1.5 demo scene / v1.x Ori-like 视觉子模式
- **归属**：`docs/roadmap.md` Phase 10 · 渲染深化 Task 10-07（2026-05-12 拍板）
- **v0.7 retro 复审（2026-05-19）**：backlog 状态有效；非 v0.7 critical path 上；第一款游戏 fork 启动前不主动开工，与 v1.x Ori-like 视觉子模式同节奏（仅登记需求，撞上即升格）

---














## GAP-2026-05-19-editor-aux-passes-in-engine-pipeline

- **发现方**：OrangeEditor v0.8.5 ca0f112 commit 自审 + 跨仓 review
- **发现日期**：2026-05-19
- **一句话定性**：编辑器视觉辅助 pass / 编辑器审美默认值塞进 engine 公共 `Pipeline`，违反 "engine 不持游戏 / 编辑器审美决定" 中性原则；将来游戏侧 `find_package(OrangeEngine)` 消费时会带上整套 "Cocos 风默认观感"

### 触发场景

`ca0f112`（v0.8.5 编辑器视觉真实感整骨）为快速串通编辑器 viewport 观感，将三类"编辑器审美"加进 engine `Pipeline`：

1. **viewport grid pass**：`Pipeline::SetEditorGridEnabled` 公共 API + `grid.frag.glsl` 内置 shader + sceneDepth 比较 + ShaderReadOnly layout 翻转——`Pipeline` 公共面命名已标 `EditorGrid`，但 pass infra（descriptor pool / set layout / pipeline / render attachment 切换）仍在 engine 端编译进引擎，shipping 二进制也带这套
2. **dummy IBL ambient 默认值** (0.25, 0.25, 0.25)：原 (0, 0, 0) → 0.25 灰；选择"Cocos / Unity URP 默认 ambient 量级"是编辑器审美决定，不是引擎中性默认
3. **viewport clear color** 默认值：原 (0.05, 0.07, 0.10) 深蓝 → (0.12, 0.12, 0.13) 中性灰；同上属编辑器审美

引擎纪律对照：
- CLAUDE.md "Game-specific concepts forbidden in engine" 段精神类似——审美决定属游戏侧 / 编辑器侧 / 工具链侧，引擎只提原子能力
- 当前破口是"sky-dome pass 真属引擎共用（游戏运行时也消费 procedural sky）"+ "grid pass 严格属编辑器" 两条混在同一 commit，sky 留 engine 对、grid 留 engine 错——但拆开需要架构动作

### 缺什么（按依赖拆）

#### G1 · `IEditorAuxPass` 钩子或 `EditorRenderLayer` 自管 pass

候选方案：

1. **engine 提供 pass 钩子**：`Pipeline::SetAuxPassProvider(IAuxPassProvider*)`，让 editor 端实现 grid pass。引擎只保留 hook 与 sceneDepth + colorTarget 共享通路，不知道具体 pass 内容
2. **editor 端自管完整 grid pipeline**：`tools/OrangeEditor/EditorRenderLayer.cpp` 自建 fullscreen quad pass，把 `Pipeline` 输出的 colorTarget 作为 input + 自管 sceneDepth shared resource。要求引擎暴露 colorTarget / sceneDepth 的 `RHITexture*` view + 当前 layout state
3. **编译期 gate**：`ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES` cmake option，shipping 构建剔除整套——简单但耦合解不掉，仅遮蔽

#### G2 · 默认值口径

- dummy IBL ambient = 0.25 灰 / clear color = 中性灰：要么搬到 `EditorRenderLayer` 启动期写入 `Pipeline::SetAmbient(...)` / `Pipeline::SetClearColor(...)` 公共面，引擎 `Pipeline` 默认全 0；要么挂 `Pipeline::SetEngineProfile(EngineProfile::Editor / Game)` enum，profile 决定默认（架构更重，慎用）

### 期望验收

- engine `Pipeline` 公共头 grep 不到 "grid" / "EditorGrid" 字样（钩子方案接受 `IAuxPassProvider` 命名）
- shipping 构建（无 EditorRenderLayer 链接）不携带 grid shader / grid descriptor pool / grid pipeline 任何 GPU 资源
- editor 端仍正确显示 grid（视觉行为不变）

### 状态

- **登记**：2026-05-19（v0.8.5 milestone ✅ 时显式登记，作为已知归属债）
- **处理**：最小可行 cmake gate 已落地（2026-05-20，本 session）—— `ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES` cmake option 默认 ON 保持编辑器行为；shipping `-DORANGE_ENGINE_WITH_EDITOR_AUX_PASSES=OFF` 关掉 grid 渲染 + dummy IBL ambient 退回 (0,0,0) + clear color 退回深蓝。**完整 IAuxPassProvider 钩子 + engine 公共面 grep 不到 "EditorGrid" 字样的命名整骨未做**，留 v1.0 验收前 batch milestone（与 OrangeRender API 中性化原则同节奏）。详细落地点见本文件"处理记录"段
- **关联**：editor-roadmap v0.8.5（落地源头）；engine 公共 API 中性化原则
- **归属**：未拍板分配到具体 Phase；候选 Phase 7 / v1.0 验收前
- **v0.7 retro 复审（2026-05-19）**：backlog 状态有效；非 v0.7 critical path；shipping 二进制带 grid 资源是 cosmetic 不阻塞功能，等 v1.0 验收前批量整中性化时一起做（与 OrangeRender API 中性化原则同节奏）

---

## GAP-2026-05-19-pbr-push-constant-exceeds-spec-min

- **发现方**：跨仓 review（Phase 6.5 PBR + IBL milestone retro）
- **发现日期**：2026-05-19
- **一句话定性**：内置 `pbr.vert.glsl` push constant 总 160 B 超过 Vulkan 规范保证下限 `maxPushConstantsSize >= 128B`；桌面 GPU 普遍 256 B 不触发，但移动 / 老 Intel iGPU 会 vkCreatePipelineLayout fail —— 长期方案是切 per-instance material UBO

### 触发场景

`src/render/builtin_shaders/pbr.vert.glsl:32-37`：

```
mat4 uMVP    //  64
mat4 uModel  //  64
vec4 uBaseColor  //  16
vec4 uMRA        //  16
                = 160 B
```

`BuiltinMaterials::LoadPbr` 注释（`src/render/BuiltinMaterials.cpp:212-218`）已自承"在所有桌面级 GPU 的 maxPushConstantsSize（普遍 256 B）以内"，但**未在运行时 assert 验证**。本期顺手在 `Pipeline::SetupRhiResources` 加 init-time `device.GetCapabilities().mLimits.mMaxPushConstantsSize` 校验：< 160 B 时日志告警（不阻塞启动，允许其他材质路径继续；PBR 渲染会在 vkCreatePipelineLayout / vkCmdPushConstants 处自然 fail）。

### 缺什么（按依赖拆）

#### G1 · 长期：per-instance material UBO 路径

- 切 `set 1 binding 0` per-instance UBO 携带 (uBaseColor, uMRA, ...)；push constant 缩回 {uMVP, uModel} 128 B
- 依赖：`OrangeRender` 的 per-frame / per-instance descriptor set 重绑成本可接受（vkCmdBindDescriptorSets 调用频率上升）
- 同时支持 PushConstantRange multi-stage（fragment 直接读 push constant），二选一

#### G2 · 中期：runtime fallback

- 若设备 maxPushConstantsSize < 160 B，PBR pipeline 创建失败时退到 non-PBR `default.material`（textured 棋盘）+ ORANGE_LOG_WARN 一次性提示

#### G3 · 短期（本 GAP 登记同 session 已落）

- `Pipeline::SetupRhiResources` 加 init-time `mMaxPushConstantsSize >= 160` 校验 + 日志告警

### 期望验收

- 桌面 NVIDIA / AMD / Intel discrete GPU：启动期日志无告警
- 模拟 maxPushConstantsSize = 128 B 设备（VK_LAYER_LUNARG_device_simulation 或类似）：启动期日志出现 `ORANGE_LOG_WARN("Pipeline: device maxPushConstantsSize=128 < 160B required by PBR material...")` 一次，非 PBR sample (`01_minimal_window`) 仍能跑

### 状态

- **登记**：2026-05-19
- **处理**：G3 已落（与本 GAP 登记同 session，例外于"登记 ≠ 同 session 实现"惯例，原因：单点 init-time 校验体量过小且阻塞 review 闭环）；G1 / G2 未启动
- **关联**：Phase 6.5 PBR + IBL milestone；OrangeRender 未来"per-instance material UBO infra"或"multi-stage PushConstantRange"路径
- **归属**：G1 候选 Phase 10 渲染深化（per-instance material UBO 基础设施）；G2 候选 Phase 7+ 移动端 / iGPU 测试 trigger
- **v0.7 retro 复审（2026-05-19）**：G3 init-time 校验已生效，桌面 GPU 路径无影响；G1 / G2 是移动 / 老 iGPU 兼容性 backlog，等真撞上设备触发再开工（与 Phase 7+ 移动端测试 trigger 同节奏）

---

## GAP-2026-05-20-editor-fprintf-to-core-log-migration

- **发现方**：OrangeEditor v0.6 验收 retrospective（File dialog COM 修复 + menu bar 修补两个 session 末尾）
- **发现日期**：2026-05-20
- **一句话定性**：OrangeEditor 内 104 处 `fprintf(stderr/stdout)` 散布未迁移到 `Core::Log` / `ORANGE_LOG_*`，绕过 Console 面板 sink hook，用户在编辑器内看不到这些日志（只在命令行窗口可见）

### 触发场景

- v0.6 ### 3 验收 File dialog hang 诊断时，给 `ShowFileDialogImpl` 加 `fprintf(stderr, ...)` 用作根因定位—— **诊断信息只在命令行窗口可见**，编辑器 Console 面板（已经接 `SetLogSink` v0.8 c2）完全看不到
- 同款现象出现在编辑器启动诊断（`[OrangeEditor] applied window icon`）/ Save 路径失败（`Scene::Save failed`）/ Play 快照失败（`Play 快照落盘失败`）/ msyh.ttc 字体 fallback 等所有"用户可能想看"的日志
- 验收期间用户多次反馈"我看不到 console 怎么知道"——需要切到 VS / 命令行窗口看，与"编辑器自带 Console 面板"的 UX 预期割裂

### 现状基础设施（已就绪，仅需迁移消费）

- `Core::Log` 公共面完备（`include/orange/engine/core/Log.h`）：6 个 `ORANGE_LOG_*` 宏 + std::format（C++20，零依赖）+ spdlog 可选 backend (`ORANGE_ENGINE_WITH_SPDLOG`)
- `SetLogSink` 已 wire（v0.8 c2）：OrangeEditor Console 面板订阅 sink，所有 `ORANGE_LOG_*` 自动出现在面板内
- 引擎本体 (`src/`) 已经基本迁完（236 处 `ORANGE_LOG_*` vs 6 处 `fprintf` 残留）
- samples / tests 是 demo / 测试程序，fprintf 直出 stdout 合理，**不在迁移范围**

### 缺什么

- `tools/OrangeEditor/` 内 104 处 `fprintf(stderr/stdout)` 系统性 grep → 替换：
  - `fprintf(stderr, "[OrangeEditor] ... failed: ...\n", ...)` → `ORANGE_LOG_ERROR("..., ...)`
  - `fprintf(stdout, "[OrangeEditor] ...\n", ...)` → `ORANGE_LOG_INFO("...", ...)`
  - 调试用的 ad-hoc `fprintf` 视情况降为 `ORANGE_LOG_DEBUG` 或直接删
- 涉及文件（按 grep count 降序）：`EditorRenderLayer.cpp` (24) / `main.cpp` (19) / `AnimFsmFileIO.cpp` (19) / `DemoWorld.cpp` (12) / `MaterialFileIO.cpp` (11) / `command/AnimFsmCommands.cpp` (8) + 5 个文件各 ≤6 处
- 顺路清除残留：`src/animation/AnimationStateMachine.cpp` 的 5 处 fprintf（引擎本体仅剩这一处，搂草打兔子）

### 期望验收

- `grep -rn 'fprintf(stderr\|fprintf(stdout' tools/OrangeEditor/` 输出 0
- 启动 OrangeEditor 后 Console 面板能看到全部"启动诊断 + file dialog fail + Save 失败 + Play 快照失败"日志（level filter / search 正常工作）
- 命令行窗口仍能看到日志（spdlog backend OFF 时走 stderr fallback，ON 时走 spdlog console sink，两条路径都保留）
- `ORANGE_ENGINE_WITH_SPDLOG=ON` build 时验证完整日志链路（spdlog 不是迁移本身，但迁移完成后顺便确认 backend 切换不漏）

### 关于 fmt vs std::format 性能（同 session 讨论留底）

用户问"fmt 官方 benchmark 性能断档领先 std::format，是否切 fmt"。结论：**不切**，理由：

1. **spdlog backend 已经间接用 fmt**——`ORANGE_ENGINE_WITH_SPDLOG=ON` 时实际 sink output 走 fmt（spdlog 自己 vendor 了 fmt）。但 OrangeEngine 的 Core::Log **格式化在公共头 `std::format` 那一步就完成了**（`Format` template 直接 `std::format(fmt, args...)` 得到 std::string，再传 `Write(string_view)`），所以 std::format 性能差异确实落到引擎上
2. **但游戏引擎 log 不是 hot path**——典型 log 频率 < 1000 条/秒，I/O 与 sink mutex 是更大瓶颈；fmt benchmark 的"3× faster"是在百万级/秒的"格式化即整体瓶颈"测试条件下成立，**不映射到游戏引擎实际负载**
3. **公共头切 fmt 的代价**：`include/orange/engine/core/Log.h` 是 PUBLIC 头，切 fmt 会引入 `<fmt/format.h>` 公共依赖 → 下游游戏仓库要找 `find_package(fmt)` → 给"无实测性能问题"的优化加一份永久 ABI / 依赖维护成本
4. **判断标准**：等 Tracy profiler 量出 `std::format` 是 frame budget 的 > 0.1% 才切；目前 v0.9 Profiler 已经接通，可以实测后再决策

### 状态

- **登记**：2026-05-20
- **落地**：2026-05-21（独立 session 一次性完成所有 104 处迁移 + build/lint/drift 验收）
- **关联**：editor-roadmap v1.0 验收前 batch milestone（与 `GAP-2026-05-19-editor-aux-passes-in-engine-pipeline` 同节奏批处理）；engine-known-gaps `GAP-2026-05-19-editor-aux-passes` 的 IAuxPassProvider 整骨独立留 batch milestone
- **归属**：未拍板分配到具体 Phase；落地节奏走"撞上即补"通道

---

## GAP-2026-05-21-collider-polygon-edgechain-interactive-edit

- **发现方**：OrangeEditor 用户验收（Collider Polygon / EdgeChain Inspector 试用）
- **发现日期**：2026-05-21
- **一句话定性**：Polygon / EdgeChain 顶点编辑当前只能在 Inspector 内 DragFloat2 输数字，用户必须心算坐标 ↔ viewport 位置反向映射，无法直接在视口点击 / 拖拽顶点；这是 Collider 编辑 UX 长期断裂的下一环（继 ColliderDebugDraw wireframe 可视化之后）

### 触发场景

- 用户加 Polygon Collider 后：viewport 已能看到 wireframe（本 session 落地的 `tools/OrangeEditor/ColliderDebugDraw.{h,cpp}`），但调形状必须切到 Inspector 一行一行改 X / Y 数字
- 同款痛点 EdgeChain 更严重：典型用例（地形折线、自定义边界）顶点多达十几个，纯数字输入工作流不可用
- 工业标准做法是 viewport 内点击加点 / 拖拽现有点 / 双击或右键删点；OrangeEditor 当前 viewport 鼠标事件全部走 Gizmo + Selection，没有"polygon edit mode"的子状态机

### 缺什么（按依赖拆）

#### G1 · "Polygon edit mode" 全局状态

- `EditorSceneContext` 或新拆 `EditorColliderEditState` 加 "当前正在编辑哪个 entity 的哪个 collider 字段（Polygon / EdgeChain）"状态
- 与 Gizmo（TranslateGizmo / RotateGizmo / ScaleGizmo）互斥：进入 edit mode 时 Gizmo 隐藏 / 不响应鼠标
- 进入 / 退出入口：Inspector 段 "Edit in Viewport" 按钮 + Esc 退出
- 参考：`vendor/LumixEngine/src/editor/spline_editor.cpp`（同栈 spline 顶点编辑，与 polygon 顶点几何同型）

#### G2 · Viewport 顶点 picking

- ColliderDebugDraw 已经能画顶点连线；增加"顶点 hit-test"：屏幕 → 世界 ray，与 entity Transform XY 平面相交，找最近顶点（screen-space 距离 < N px 命中）
- ScenePanel 鼠标事件路由加分支：edit mode 期间 LMB 点击 → 选顶点 / 拖拽更新 / 空白处加新顶点；RMB 或双击 → 删除点中顶点
- 视觉反馈：选中顶点高亮（hover 与 picked 两态），未选 / 选中两色与现有 selection 色系一致

#### G3 · 命令栈集成（防 undo 栈爆）

- 每个顶点编辑动作走 `SetFieldValueCommand<PolygonDesc>` / `SetFieldValueCommand<EdgeChainDesc>` 整值写入
- 拖拽过程一帧一个命令会爆栈：参 v0.4 DragFloat 同款 coalesce 策略（按 fieldKey + 短时 window 合并），fieldKey 沿用 schema 注册的 `collider.shape` 串
- "加点" / "删点" 是离散动作，单独走一条命令；"拖点" 是连续动作走 coalesce

### 期望验收

- Inspector 段 Polygon / EdgeChain 出现 "Edit in Viewport" 按钮，进入后 Gizmo 隐藏 + viewport 内见高亮顶点
- viewport 内：空白处 LMB 点击在该位置加新顶点；现有顶点 LMB 拖拽实时更新（拖拽期间 wireframe 实时跟随）；现有顶点 RMB / 双击删除
- 全部动作走命令栈：Undo / Redo 能逐步回放（拖拽一次 = 一条 coalesced 命令）
- Esc 退出 edit mode 恢复正常 Gizmo / Selection

### 状态

- **登记**：2026-05-21
- **关联**：本 session 已落地的 ColliderDebugDraw wireframe 可视化（前置基础）；本 session 同时修复 PolygonVertices / EdgeChainVertices Inspector Remove 按钮窄列越界（顺手 UX 修，不在本 GAP 范围）
- **归属**：未拍板；候选 OrangeEditor v0.x 独立 milestone（与 v0.7 Animation 子模式 / v1.0 验收 排序优先级未定）

---

## GAP-2026-05-21-editor-coplanar-mesh-z-fight-prevention

- **发现方**：OrangeEditor 用户验收（demo.scene.json Tower 底面 z-fight）
- **发现日期**：2026-05-21
- **一句话定性**：作者在场景里手动摆贴地 / 贴墙物体时，容易让 mesh 与 ground / 兄弟面**完全共面**（典型：cube 底面 y = ground 顶面 y），主 pass 渲染立刻出现 z-fighting 斜条纹；编辑器目前没有任何"作者无意共面"的检测 / 警告 / 吸附辅助，作者必须自己肉眼对照纹理瑕疵反推问题源

### 触发场景

- `demo.scene.json` Tower entity：cube.mesh + position.y = 0.5 + scale.y = 2.0 → 底面 y = -0.5；同时 Ground 是 plane.mesh + position.y = -0.5 → 顶面 y = -0.5；**两面在 y = -0.5 完全共面**，主 pass `depthCompareOp = LessOrEqual` 导致后画的 fragment 沿对角线随机覆盖前画的（diamond rasterization 的 tie-breaking 表现）
- 修法只能是 content 侧——把 Tower y 抬 1mm（0.5 → 0.501）让底面 y = -0.499 分开 ground 顶面 y = -0.5；shadow slope-scaled bias / polygon offset / reverse-z 都救不了 mesh 真的几何共面的情况
- 同款问题潜伏在任意"美术摆贴地物体"场景：墙体贴 ground、装饰物贴墙、楼梯踏面贴墙等。OrangeEditor v0.x 期人工对位摆物，不撞上才怪

### 引擎纪律对照

- CLAUDE.md `Pipeline::SetupRhiResources` depth compare 用 `LessOrEqual` 是工业标准选择（透明合批 / decal 等场景需要 ≤ 语义），切 `Less` 会引入新一类问题，不动
- shadow path 的 `depthBias = 0.005 + PCF` 已经在用，但 acne 与 mesh-vs-mesh z-fight 是两个独立问题，bias 治不了 mesh 共面

### 缺什么（按依赖拆）

#### G1 · 编辑器共面检测 + Inspector 警告

- 选中 entity 时，编辑器后台跑一次 AABB-vs-AABB 共面查询：本 entity 的 6 个面分别与场景内"邻近 entity"（一定距离阈值内）的对应面做 ε-equal 比较，命中则在 Inspector 的 Transform 段或新增 "Geometry Warnings" 段显示红字提示
- 提示文案至少给出：哪一面（top/bottom/front/back/left/right）与哪个 entity 的哪一面共面、当前两面间距（可能是 0 或 ±ε）
- 实现侧考虑：每帧 N² 共面查询成本高，只在 selection 变化或 Transform 改动时触发，缓存到下次失效；或者只对 selection 周边 R 米内的 entity 做查询

#### G2 · "贴地 / 贴边吸附"工具默认插入 ε 偏移

- v0.x 后续 Gizmo 增强（snap to ground / snap to face）时，吸附逻辑默认在贴合方向上插入 1mm（或可配置 ε）偏移而非真 0
- ε 值挂在 EditorState 或编辑器全局 config（默认 0.001，可调）
- 与 G1 配合：吸附插入 ε 后 G1 不再触发警告

#### G3 · scene 文件保存时的"安全距 lint"（可选）

- Save scene 时跑一次 G1 同款 AABB 共面扫描，若命中弹 modal 警告"以下 entity 与邻居共面，可能在运行时 z-fight：[列表]，是否继续保存？"
- 严格度低于 G1（保存仍允许），但避免 .scene.json checkin 到仓库时悄悄带共面瑕疵

### 期望验收

- 加载 demo.scene.json 时如 Tower y 退回 0.5（人为复原），Inspector 选中 Tower 立刻见红字警告"Tower.bottom 与 Ground.top 共面（间距 0.000m）"
- 用 "snap to ground" 工具把 Tower 拖到 ground 上，落点 y 自动是 -0.499（ε=0.001）而非 -0.5
- scene save 时若有共面，弹 modal 询问

### 状态

- **登记**：2026-05-21
- **关联**：本 session 已通过 content fix 把 Tower y 从 0.5 抬到 0.501 解决 demo.scene.json 当前可见 z-fight；本 GAP 是"防再次发生"的工程化方向
- **归属**：未拍板；候选 OrangeEditor v0.x 独立 milestone（G1 优先级 > G2 > G3；G1 可独立小 milestone 落地，G2 / G3 等吸附工具 / scene save UX 整骨时一起做）

---

## GAP-2026-05-22-new-scene-actually-seeds-demo

- **发现方**：OrangeEditor v1.0 验收脚本（作者本人跑，段 A 第 2 步）
- **发现日期**：2026-05-22
- **一句话定性**：`File → New Scene` 菜单 label 与实际行为严重不符 —— 用户期望"打开空白场景从零搭"，实际是"重建 World + 重新种 SeedDemoWorld（13 个 placeholder entity）"；对零基础用户具备误导性，体感"菜单点了没反应 / 打不开新场景"

### 触发场景

- v1.0 验收脚本（`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.0-acceptance-task-script.md`）段 A 第 2 步
- 启动 OrangeEditor → 自动加载 `assets/scenes/demo.scene.json`（PBR showcase 24 entities）→ 点 `File → New Scene` → 画面切到 SeedDemoWorld（13 entities，含 Floor/Wall/Cube/粒子/DragonBones placeholder）
- 程序员视角：知道 SceneOp::New 调 SeedDemoWorld，"画面有东西"是预期；用户视角：期望空场景但仍看到一堆 entity，体感"打不开"
- 这是 v1.0 验收脚本捕获的**第一个 Critical fail 信号**，完美兑现了脚本设计意图（程序员看不见的 UX bug 在零基础用户视角立刻显现）

### 证据

`tools/OrangeEditor/EditorRenderLayer.cpp:737-747` `SceneOp::New` 分支：

```cpp
case SceneOp::New: {
    mHost.scene.pWorld = std::make_unique<Orange::Engine::World>();
    mHost.scene.partition = Orange::Engine::Scene::WorldPartition{};
    SeedDemoWorld(mHost);  // 与启动期一致；后续真要"空场景"再做"New Empty"
    ...
```

代码注释直接承认这是已知设计取舍（"后续真要'空场景'再做'New Empty'"）。

### 缺什么

#### G1 · `New Scene` 行为改为真·空场景

- `SceneOp::New` 路径**移除** `SeedDemoWorld(mHost)` 调用，World 重建后保持空 entity 列表
- viewport 应只显示 grid + sky（默认设置）+ 顶部 toolbar，不含任何 ECS entity
- Hierarchy 面板显示空列表（或 "No entities" 占位文案）
- Inspector 显示 "No entity selected" 状态

#### G2 ·（可选）保留 "Reset to Demo" 入口

- 如果有意保留"程序化 demo 重置"工作流（用于演示 / 内部调试），加独立菜单项 `File → Reset to Demo Scene`，与 `New Scene` 拆开
- 优先级低 —— 真要演示 demo 用 `File → Open Scene → demo.scene.json` 走 disk-loaded 路径即可，未必需要程序化 reseed

### 期望验收

- 启动 OrangeEditor → 点 `File → New Scene`：
  - Hierarchy 面板空白（或空状态占位）
  - Scene viewport 只剩 grid + sky，看不到任何 mesh / 粒子 / 灯
  - Inspector "No entity selected"
  - 顶部 menu bar scene 路径 indicator 显示 `[Untitled]`
- 然后能正常进段 A 第 3 步：`Create Entity` → Add Component → 等等，从零搭起

### 状态

- **登记**：2026-05-22
- **关闭**：2026-05-22（v1.0 验收前 G1 落地，详见"处理记录"段）
- **优先级**：~~**P0（v1.0 阻塞）**~~ → 已修
- **归属**：~~OrangeEditor v1.0 验收前修复 batch~~ → 同 session 落地
- **关联**：`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.0-acceptance-task-script.md` 段 A 第 2 步

---

## GAP-2026-05-22-directional-light-inspector-direction-helper-missing

- **发现方**：OrangeEditor v1.0 验收脚本（作者本人跑，段 B 第 2 步）
- **发现日期**：2026-05-22
- **一句话定性**：DirectionalLight Inspector 段无任何 helper / tooltip / readonly preview 提示"光向由 Transform.rotation 派生"；零基础用户看到段内只有 color / intensity / castsShadow 三个字段，找不到"方向"控件，无法自行推断要去 Transform 段改 rotation

### 触发场景

- 段 B 第 2 步用户问"我没看到 direction，这一步是设置 Light 的位置吗？"
- 注：脚本原文表述错误（"direction 拖到 (-0.3, -1.0, -0.3)"按已废 schema 写）；但即使脚本正确，UI 上也缺乏自发现性

### 证据

- `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp:41-62` DirectionalLight schema 仅 color / intensity / castsShadow（GAP-2026-05-16-directional-light-transform-decoupled 落地后）
- 代码注释 line 44-48 明确说"方向字段已废 ... 用户旋转 entity ... 即可改光向，与 Unity / Unreal / Godot 同款工业惯例"——心智模型正确，但**没暴露给 UI 用户**，只暴露给了读源码的开发者

### 缺什么

#### G1 · DirectionalLight schema 加 helper 文案

- Inspector 段开头加一行 TextDisabled / 浅色 helper："Direction is derived from Transform.rotation. Adjust rotation above to change light direction."
- 或在 castsShadow 之后加 readonly preview："Computed direction: (x, y, z)"，让用户改 Transform.rotation 时实时看到方向向量

#### G2 · Transform 段对带 Light component 的 entity 加 tooltip

- 选中带 DirectionalLight 的 entity 时，Transform.rotation 字段 tooltip：rotation 决定光向；position 不影响（方向光来自无穷远）。同样规则 PointLight 反之（position 决定光位、rotation 无效）

### 期望验收

- 零基础用户选 Sun → 在 DirectionalLight 段 5 秒内通过 helper 文案得出"去 Transform 改 rotation"结论
- 改 Transform.rotation 时能看到 computed direction 实时变化

### 状态

- **登记**：2026-05-22
- **优先级**：**P1（Friction，不阻塞 v1.0）** —— 流程"能走通"（用户经引导后能正确操作），仅缺 UX 自发现性
- **归属**：OrangeEditor v1.x UX 改进 batch
- **关联**：v1.0 验收脚本段 B 第 2 步；脚本本身也需修订（按当前 schema 改成"选 Sun → Transform → rotation"）

---

## GAP-2026-05-22-shadow-not-tracking-directional-light-direction

- **发现方**：OrangeEditor v1.0 验收脚本（作者本人跑，段 B / 段 C）
- **发现日期**：2026-05-22
- **一句话定性**：用户调整 Sun entity 后，地面 / cube 投影**不随光照变化**；根因待诊断——可能是 (A) Pipeline live-update 真 bug，或 (B) 用户改了 Transform.position 期望阴影变 = UX 误解（DirectionalLight 数学上不依赖 position）

### 触发场景

- v1.0 验收脚本段 B / 段 C，用户对 Sun entity 做"光照的移动"后阴影方向不变

### 证据

- `src/render/Pipeline.cpp:3164` 和 `:4931` 两处 RenderScene 入口都**每帧**从 `tc->rotation` 重新派生 `activeLightDir`，然后 `ComputeLightViewProj(activeLightDir)` 算 shadow VP；**理论上无缓存、是 live 的**
- 也就是说：改 Transform.rotation 阴影应该跟随；改 Transform.position 阴影应该不变（DirectionalLight 不依赖 position）

### 待诊断澄清问题

1. 用户测试时改的是 Sun.Transform.**rotation** 还是 **position**？
2. 若改 rotation 阴影不动 → 真 Pipeline bug，需深入调查（lightVP 缓存？shader uniform 同步？shadow descriptor live update？）
3. 若改 position 阴影不动 → 不是 bug，是 UX 误解，归并到 [[GAP-2026-05-22-directional-light-inspector-direction-helper-missing]] 的 G2

### 缺什么（视诊断结果分支）

**分支 A（真 bug，改 rotation 阴影不动）**：

- 调查 shadow pass 是否有缓存 lightVP / shadow descriptor 没每帧更新
- 若是 shader uniform 同步问题，确保 main pass 也每帧 push 新 lightVP
- 修复后回归：旋转 Sun Transform.rotation.Y 90° 看 demo.scene 阴影方向

**分支 B（UX 误解，改 position 期望阴影变）**：

- 归并到 [[GAP-2026-05-22-directional-light-inspector-direction-helper-missing]] 的 G2

### 期望验收

- 旋转 Sun.Transform.rotation.Y 0→90→180 → 地面投影方向同步旋转
- 改 Sun.Transform.position → 投影方向**不变**（正确行为）

### 状态

- **登记**：2026-05-22
- **关闭**：2026-05-22（**撤回** —— 分支 B 确认）
- **撤回原因**：用户现场验证 —— 改 Sun.Transform.**rotation** 时阴影正确跟随；改 Sun.Transform.**position** 阴影不变（这是 DirectionalLight 数学正确行为，光从无穷远来，position 不参与 ComputeLightViewProj，参 `src/render/Pipeline.cpp:3164` `:4931` 每帧从 rotation live 派生）
- **合并去向**：UX 自发现性问题归并到 [[GAP-2026-05-22-directional-light-inspector-direction-helper-missing]] G2 —— Transform.rotation 字段对带 DirectionalLight 的 entity 加 tooltip 说明"position 不影响光向"
- **优先级**：原 P0 撤销 → 合并条目维持 P1，不阻塞 v1.0
- **关联**：v1.0 验收脚本段 B / 段 C；[[GAP-2026-05-22-directional-light-inspector-direction-helper-missing]]

---

## GAP-2026-05-22-multi-directional-light-semantics-undefined

- **发现方**：OrangeEditor v1.0 验收讨论（用户提出"方向光是不是不应该能创建多个？感觉应该是全局光源"）
- **发现日期**：2026-05-22
- **一句话定性**：场景中存在 N 个 DirectionalLight 时，Pipeline 隐式只取迭代器第一个参与光照 / 阴影，多余的静默忽略，UI 无任何反馈 → 用户改第 2 个 DirLight 的颜色 / 方向 / 强度时 viewport 毫无反应，体感"引擎坏了"

### 触发场景

- 用户调研工业惯例时直觉认为"方向光是全局的，应该单例"
- 实测 Hierarchy 可建任意多个 DirectionalLight，Inspector 各自独立配置看似"都在工作"
- 但实际只第一个起效，其他完全无效

### 证据

- `src/render/Pipeline.cpp:3153-3164`（第一条 RenderScene 路径）：
  ```cpp
  auto view = reg.view<DirectionalLight>();
  if (!view.empty()) {
      const auto entity = view.front();   // ← first-found，无主光标记
      activeLight = &view.get<DirectionalLight>(entity);
      ...
  }
  ```
- `src/render/Pipeline.cpp:4920-4931`（RenderOffscreen 路径）：同款 first-found
- `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp:41-62` DirectionalLight schema 无任何 "primary / fill" 标识字段
- 无 warning log / Hierarchy badge / Inspector helper 提示多 DirLight 语义

### 工业对照（用户调研已收集）

| 引擎 | 允许多个 | 多个时的处理 |
|------|---------|------------|
| Unity URP | ✅ | Main Light 参与阴影；其他作为 additional 只贡献漫反射 |
| Unreal | ✅ | 推荐一个 movable 做 dynamic shadow，其他 stationary 关阴影 |
| Godot 4 | ✅ | 多个叠加，但只一个 DirectionalLight3D 能开 shadow |
| Lumix | ✅ | 类似 "main directional" 概念 |
| **OrangeEngine 现状** | ✅（但语义未定义） | first-found，多余静默忽略 |

**结论**：禁止多个会锁死 stylized fill light / 双月奇幻场景 / 时段切换等合法用例，与全部工业惯例背离 → **不禁止建多个，而是补足主光语义 + UI 反馈**。

### 缺什么

#### G1 · Inspector helper + Hierarchy warning（推荐 v1.x 落地）

- `RegisterDirectionalLightSchema` 加段首 helper 文案：`"Only the first DirectionalLight in the scene participates in lighting and shadow. Disable additional ones to avoid surprises."`
- Hierarchy 面板检测到 >1 个 enabled DirectionalLight 时，在非首个 entity 行加 warning icon + tooltip "Ignored: scene has another DirectionalLight as primary"

#### G2 · IsPrimary 字段 + fill light 多光叠加（v2.x，待 stylized 美术真需要再做）

- `DirectionalLight` 加 `bool isPrimary = false` 字段（serialize + schema bump）
- Pipeline 优先选 `isPrimary=true` 的，其次 fallback first-found
- 其他 DirLight 作为 fill light 进 UBO，只参与漫反射不参与 shadow（对标 Unity URP 模式）
- 阴影仍只主光一份，避免多 shadow map 成本爆炸

#### 不做

- **不禁止建多个** —— 违反所有工业惯例，锁死扩展性

### 期望验收

- G1 ✅ 条件：建 2 个 DirectionalLight，第二个 entity Hierarchy 行有明显 warning + Inspector 段顶部 helper 文案可见；用户 10 秒内能理解"第 2 个不生效"
- G2（如真做）✅ 条件：勾选某个 DirLight 的 IsPrimary → 该光参与阴影；其余 N-1 个仍贡献漫反射，viewport 视觉合理叠加

### 状态

- **登记**：2026-05-22
- **优先级**：**P2（Friction，不阻塞 v1.0）** —— v1.0 验收脚本只用 1 个 Sun，单 DirLight 流程完整 work；多 DirLight 是 polish / 美术工作流问题
- **归属**：G1 留 v1.x UX batch（与 [[GAP-2026-05-22-directional-light-inspector-direction-helper-missing]] 同 batch 落更合算）；G2 待第一款游戏 stylized 美术真需要 fill light 时再启动
- **关联**：[[GAP-2026-05-22-directional-light-inspector-direction-helper-missing]] / `src/render/Pipeline.cpp:3153` `:4920`

---

## 处理记录

- **GAP-2026-05-11-point-light-and-visible-halo**（2026-05-20 落地 G1+G2，G3 留 v1.x）：
  - **G1 PointLightComponent 公共面** ✅ —— `include/orange/engine/render/LightComponent.h` 新增 `struct PointLight { color, intensity, range, castsShadow }`，position 由 entity.Transform.position 派生（与 DirectionalLight 走 Transform.rotation 同款约定）；`src/scene/ComponentSerializers.cpp` Has/Write/Read PointLight + 注册到 GetBuiltinComponentSerializers；scene/world schema v1.4 → v1.5（minor bump，backward-compat：旧 scene 无 PointLight 字段时 entity 不参与点光路径）
  - **G2 Pipeline 多 light 收集 + shader 多 light loop** ✅ —— Pipeline 新增 `PointLightsUboData`（cap=8 PointLightStd140 + count）+ binding 5 独立 UBO（只 PBR shader 引用，其他 shader dead-code），`UpdatePointLightsUbo(world)` 每帧收集 cap=8 first-found PointLight 进 UBO + 超出 first-found 截断 + 一次性 warn；mainDescPool size += 1 UB；PBR fragment shader `pbr.frag.glsl` 加 `for i in 0..count` point light loop（物理基 inverse-square + smoothstep range cutoff，与 Cocos pointLight / Godot OmniLight 同款）；Shutdown 路径 pointLightsUbo.reset() 配套，VMA 无 leak（ctest 40/40 通过）
  - **G2 编辑器侧** ✅ —— `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp` 新增 PointLight schema（4 字段 + tooltip）+ Inspector 段；`tools/OrangeEditor/plugin/PointLightGizmoPlugin.{h,cpp}` 新增 viewport overlay（黄色填充圆点 + XZ 平面 36 段 range 圆环 stroke）
  - **G3 halo billboard** —— 留 v1.x Ori-like 视觉子模式拉动时再做；本期范围外
  - **castsShadow 字段**：保留但 Pipeline 忽略；omnidirectional cubemap shadow 是 long-term roadmap 量级
  - 关键改动文件：`include/orange/engine/render/LightComponent.h` / `src/scene/ComponentSerializers.cpp` / `src/scene/SceneSerialization.cpp`（schema bump）/ `src/render/Pipeline.cpp`（UBO + UpdatePointLightsUbo + descriptor layout binding 5）/ `src/render/builtin_shaders/pbr.frag.glsl`（point light loop）/ `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp` / `tools/OrangeEditor/plugin/PointLightGizmoPlugin.{h,cpp}`（新增）/ `tools/OrangeEditor/main.cpp`（plugin 注册）/ `tools/OrangeEditor/CMakeLists.txt`
- **GAP-2026-05-19-editor-aux-passes-in-engine-pipeline**（2026-05-20 落地最小可行 cmake gate；完整 IAuxPassProvider 钩子留 v1.0 验收前 batch milestone）：
  - 新增 `ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES` cmake option（`cmake/Dependencies.cmake`），默认 ON 保持 dev / editor 构建行为不变；shipping 显式 `-DORANGE_ENGINE_WITH_EDITOR_AUX_PASSES=OFF` 关掉，engine 端三项编辑器审美默认全数剔除：
    - `Pipeline::SetEditorGridEnabled` setter OFF 时强 false（grid 永远不渲染，资源仍创建但 RecordGridPass 调用路径不触发）
    - dummy IBL irradiance ambient 从 (0.25, 0.25, 0.25) hardcode 退回 (0, 0, 0)（PBR 物体仅 direct light，engine 默认中性）
    - viewport clear color 从中性灰 (0.12, 0.12, 0.13) 退回深蓝 (0.05, 0.07, 0.10)（与 v0.8.5 之前 sample 视觉一致）
  - **完整 IAuxPassProvider 钩子** + engine 公共头 grep 不到 "EditorGrid" 字样的命名整骨：未落，归 v1.0 验收前 batch milestone（与本 GAP 当初"等 v1.0 验收前批量整中性化时一起做"复审一致）；shipping engine 二进制仍带 grid shader SPV + 资源创建代码（占内存但不画），完整剔除待命名整骨随 IAuxPassProvider 落地一并完成
  - 关键改动文件：`cmake/Dependencies.cmake`（option 定义）/ `CMakeLists.txt`（target_compile_definitions）/ `src/render/Pipeline.cpp`（三处 #ifdef）
- **音频集成编辑器**（2026-05-20 落地）：本次顺手完成，**非 GAP 范畴**（用户 goal 直接命名）。详细参见 `vendor/Orange-Wiki/case-studies/orange-engine/milestones/phase-3/audio-and-point-light-and-aux-passes-gate-acceptance-checklist.md`。
  - 引擎侧：`include/orange/engine/audio/AudioSourceComponent.h`（sound handle + playOnAwake / loop / volume / pitch 五字段，PureData）+ `src/scene/ComponentSerializers.cpp` Has/Write/ReadAudioSource + 注册到 GetBuiltinComponentSerializers；scene/world schema v1.3 → v1.4
  - 编辑器侧：`PropertyType.h` AssetKind 加 Sound；EditorHost 加 audioEngine 字段（编辑器进程级全局 mixer）；RegisterBuiltinSchemas 加 AudioSource schema（含 Sound AssetRef）；新增 `AudioSourceInspectorPlugin`（Inspector ParseEnd 钩子 Play / Stop 试播按钮）+ `AudioAssetInspectorPlugin`（选中 .wav 接管 Inspector 显示预览）；Asset 浏览器 .wav/.ogg/.mp3/.flac → "[SND]" icon + "Pick to AudioSource.sound" 右键菜单；EditorRenderLayer Play Mode tick：进入 Play 遍历 view<AudioSourceComponent> 实例化 SoundInstance + playOnAwake 立即 Start + volume 实时 sync，退出 Play 清表（SoundInstance 析构自动 ma_sound_uninit）；DemoWorld lazy bake `assets/sounds/beep.wav`（440ms 880Hz "叮"声，与 sample 07 BeepWav helper 同款算法，编辑器内联匿名 ns 避免跨目录 include）
  - 关键改动文件：`include/orange/engine/audio/AudioSourceComponent.h`（新增）/ `src/scene/ComponentSerializers.cpp` / `src/scene/SceneSerialization.cpp` / `tools/OrangeEditor/EditorHost.h` / `tools/OrangeEditor/EditorRenderLayer.{h,cpp}` / `tools/OrangeEditor/schema/PropertyType.h` / `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp` / `tools/OrangeEditor/plugin/AudioSourceInspectorPlugin.{h,cpp}`（新增）/ `tools/OrangeEditor/plugin/AudioAssetInspectorPlugin.{h,cpp}`（新增）/ `tools/OrangeEditor/DemoWorld.cpp` / `tools/OrangeEditor/main.cpp` / `tools/OrangeEditor/CMakeLists.txt`
- **GAP-2026-05-19-pbr-ibl-specular-quality**（2026-05-19 落地）：multi-scatter compensation (Fdez-Aguero 2019 / Filament `light_indirect.fs` 同款 `1 + F0·(1/brdf.y - 1)`) 接到 PBR shader IBL specular 段 + `BakePrefilteredEnvironment` sampleCount 1024 → 4096；顺路修 BUG-2026-05-18-vma-shutdown OE 端漏 reset baked IBL 三件套。视觉 furnace 9 球阵接近全白；HDR 中 roughness 段密集白方块大幅消失；顶行右 metallic=1 r=0.9 不再偏暗。详细见上文条目末尾"落地记录"节。关键改动文件：`src/render/builtin_shaders/pbr.frag.glsl` / `src/render/Pipeline.cpp` / `samples/14_pbr_ibl/main.cpp`（--capture / --exit-after flag 无人值守视觉验收路径）
- **GAP-2026-05-19-editor-environment-component-wiring**（2026-05-19 落地）：Asset 浏览器 ext 映射加 .hdr / .exr ([HDR] icon)；Pipeline 加 `lastBakedCubemap` AssetHandle + Render 入口每帧 query first-found EnvironmentComponent.cubemap 自动 re-bake；Inspector 拖换 cubemap / 改 Intensity / Tint 字段在 viewport 视觉实时跟随（uIblFactor UBO 路径早已 live，cubemap auto-rebake 让 IBL 三件套与 component.cubemap 保持一致）。详细见上文条目末尾"落地记录"节。关键改动文件：`src/render/Pipeline.cpp` / `tools/OrangeEditor/EditorRenderLayer.cpp`
- **GAP-2026-05-15-camera-editor-vs-runtime-separation**（2026-05-19 落地）：Pipeline 加 `SetEditorCameraOverride(const Camera*)` 入口 + RenderScene 加 `OverrideMainCamera(const Camera&)`；编辑器 ScenePanel 不再 mutate ECS Camera 组件，改 push 编辑器轨道相机给 Pipeline override。CameraFrustumGizmoPlugin 改读 `component->projection` 真实矩阵（projection 反映用户设置 fov / aspect / near / far，view 仍由 entity.Transform 推维持 UX）。`ApplyEditorCameraToWorld` 函数删除（不再被调用）。详细见上文条目末尾"落地记录"节。关键改动文件：`include/orange/engine/render/Pipeline.h` / `include/orange/engine/render/RenderScene.h` / `src/render/Pipeline.cpp` / `tools/OrangeEditor/EditorRenderLayer.h` / `tools/OrangeEditor/panels/ScenePanel.cpp` / `tools/OrangeEditor/EditorCameraControl.{h,cpp}` / `tools/OrangeEditor/plugin/CameraFrustumGizmoPlugin.{h,cpp}` / `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp`
- **GAP-2026-05-17-editor-first-frame-flash**（2026-05-19 落地）：glfwMaximizeWindow 后插入"连续 PollEvents + 比对 framebuffer size 直到稳态"循环（最多 32 轮 / ~几十 ms 超时；Windows 通常 1-2 轮就回）。等 GLFW size 更新到 maximized 物理尺寸后再继续 RenderDevice / Renderer 创建，swap-chain 一上来就是正确尺寸，消除"左上 1600×900 渲染内容 + 其余白色 buffer"一闪而过的 surface↔swap-chain 错配伪影。详细见上文条目末尾"落地记录"节。关键改动文件：`tools/OrangeEditor/main.cpp`
- **GAP-2026-05-17-mesh-loader-supported-version-symbol**（2026-05-17 落地）：在 GAP-2026-05-17-mesh-vertex-normals 落地 session 顺手修——`tests/asset/AssetRegistryTest.cpp` 79 / 290 行 `MeshLoader::kSupportedVersion` 引用改为 `MeshLoader::kVersionV1`（fixture 字节结构本就是 v1 形态）。详见 mesh-vertex-normals 条目落地记录段"测试修复（顺手）"。
- **GAP-2026-05-14-renderable-material-instance-round-trip**（2026-05-14 落地）：`ComponentSerializerEntry.h` 加 `Render::MaterialInstance` forward decl + `namedMaterialInstances` 字段到 SaveContext / LoadContext；`SceneSerialization.h` 的 SaveOptions / LoadOptions 同步加字段；WriteRenderable 写出 materialInstanceId 字符串，ReadRenderable 按 id 正向查表赋指针；SceneSchemaVersion 1.0 → 1.1。编辑器侧 `DemoWorld.cpp::BuildNamedMaterialInstances` 落地，BUG-2 现象消失。详见上文条目末尾。
- **GAP-2026-05-20-editor-fprintf-to-core-log-migration**（2026-05-21 落地）：tools/OrangeEditor/ 内 104 处 `std::fprintf(stderr/stdout)` 系统性迁移到 `ORANGE_LOG_*` 宏（`std::format` 占位 `%s`/`%u`/`%zu` → `{}`），按消息语义分配 level：
  - **ERROR**：Scene/Asset Load 失败 / VfxSystem::Initialize 失败 / RegisterLoader 失败 / Begin/EndFrame 失败 / Vulkan resolve / Save 落盘失败等"功能失效"路径
  - **WARN**：字体 fallback / MaterialSystem::RegisterBuiltins 失败（非致命）/ .material template 未注册回退 / parameters/states/transitions/conditions 字段缺失或重名（单条跳过）/ VfxSystem 跳过 / Animator 参数未注册等"degraded but continue"路径
  - **INFO**：启动诊断（Vulkan handles / swap-chain / dock ready / icon applied）/ scene Save/Load 成功 / Play 状态切换 / Esc 退出请求 / clean shutdown 等"用户想看的进度"路径
  - **DEBUG**：`[vfx-diag]` 每秒 live particles 计数（周期性 noise，正常运行时不显示）
  - 顺路：`src/animation/AnimationStateMachine.cpp` 5 处 fprintf（引擎本体最后残留）也清掉
  - 验收：`grep 'fprintf(stderr\|fprintf(stdout' tools/OrangeEditor/ src/` 输出 0（src/core/Log.cpp 内 1 处 stderr fallback 是 Core::Log 自己的兜底 sink，不算迁移目标）；OrangeEditor.exe build 通过；invariant lint + drift 全绿
  - 关键改动文件（按 grep count 降序）：`tools/OrangeEditor/EditorRenderLayer.cpp` (24) / `tools/OrangeEditor/main.cpp` (19) / `tools/OrangeEditor/AnimFsmFileIO.cpp` (19) / `tools/OrangeEditor/DemoWorld.cpp` (12) / `tools/OrangeEditor/MaterialFileIO.cpp` (11) / `tools/OrangeEditor/command/AnimFsmCommands.cpp` (8) / `tools/OrangeEditor/VulkanLoaderShim.cpp` (6) / `tools/OrangeEditor/panels/ScenePanel.cpp` (2) / `src/animation/AnimationStateMachine.cpp` (5) / `tools/OrangeEditor/schema/ComponentSchemaRegistry.cpp` (1) / `tools/OrangeEditor/branding/EditorWindowIcon.cpp` (1) / `tools/OrangeEditor/panels/LayersPanel.cpp` (1)
- **GAP-2026-05-14-scene-serializer-extension**（2026-05-14 落地）：`include/orange/engine/scene/ComponentSerializerEntry.h` 公共化 ComponentSerializerEntry / SaveContext / LoadContext / ComponentKind / EntityToPersistentId / PersistentIdToEntity；SaveOptions / LoadOptions 增加 `std::span<const ComponentSerializerEntry> extraSerializers{}`；Save/Load 路由 extra 条目（冲突检测 + Pass 1 PureData + Pass 2 BackendDependent）。OrangeEditor v0.3 c2 已完整消费。详见上文条目末尾。
- **GAP-2026-05-17-asset-registry-handle-to-path**（2026-05-17 落地）：发现 `AssetRegistry::PathOf<T>` 公共 API 早已存在（GAP 登记时漏看），实际只需 `MaterialFileIO::BuildDataFromInstance` 加可选 `const AssetRegistry*` 参数 + 内部消费 PathOf。详细见上文条目末尾"落地记录"节。涉及 commit：`784bf1a`。关键改动文件：`tools/OrangeEditor/MaterialFileIO.{h,cpp}` / `tests/render/MaterialFileIOTest.cpp`（TestTextureRoundTripWithRegistry）
- **GAP-2026-05-16-material-system-enumerate-and-instance-overrides**（2026-05-17 落地）：MaterialSystem::GetTemplateNames + MaterialInstance enumerate override API + .material schema v1.0 → v1.1（uniforms/textures）+ Editor 端 MaterialFileIO helper + 4 新测试。详细见上文条目末尾"落地记录"节。涉及 commit：`3b9718d`（C1）/ `6b598ba`（C2）/ `b7c92d3`（C3）。关键改动文件：`include/orange/engine/render/MaterialSystem.h` / `include/orange/engine/render/MaterialInstance.h` / `src/render/MaterialSystem.cpp` / `src/render/MaterialInstance.cpp` / `tools/OrangeEditor/MaterialFileIO.{h,cpp}`（新增）/ `tools/OrangeEditor/DemoWorld.cpp` / `tools/OrangeEditor/panels/InspectorPanel.cpp` / `tools/OrangeEditor/CMakeLists.txt` / `tests/render/MaterialFileIOTest.cpp`（新增）/ `tests/render/MaterialInterfaceTest.cpp` / `tests/render/MaterialSystemTest.cpp` / `tests/CMakeLists.txt`
- **GAP-2026-05-16-directional-light-transform-decoupled**（2026-05-17 落地）：DirectionalLight 删 direction 字段 + Pipeline 改用 entity.Transform.rotation 派生方向 + ReadDirectionalLight v1 migrator 旧 scene direction 字段自动转 TC.rotation + 7 sample/DemoWorld/编辑器 schema/gizmo plugin/2 tests 全数迁移。详细见上文条目末尾"落地记录"节。关键改动文件：`include/orange/engine/render/LightComponent.h` / `src/render/Pipeline.cpp` / `src/scene/ComponentSerializers.cpp` / `samples/{05,06,07,08,09,12}*/main.cpp` / `tools/OrangeEditor/DemoWorld.cpp` / `tools/OrangeEditor/plugin/DirectionalLightGizmoPlugin.cpp` / `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp` / `tests/render/LightAndShadowTest.cpp` / `tests/scene/SceneSerializationTest.cpp`
- **GAP-2026-05-17-scene-layer-component**（2026-05-17 落地）：LayerComponent + WorldPartition 公共面 + SceneSerialization 多文件 + manifest + Render/Physics layer.visible 过滤 + sample。详细见上文条目末尾"落地记录"节。关键改动文件：`include/orange/engine/scene/LayerComponent.h` / `include/orange/engine/scene/WorldPartition.h` / `include/orange/engine/scene/SceneSerialization.h`（SaveSplit/LoadSplit + LoadOptions.assignLayerId）/ `include/orange/engine/render/RenderScene.h`（Collect 加 partition 参数）/ `include/orange/engine/render/Pipeline.h`（SetWorldPartition）/ `include/orange/engine/physics/PhysicsWorld.h`（SetBodyEnabled/IsBodyEnabled）/ `include/orange/engine/physics/LayerVisibilitySync.h` / `src/scene/WorldPartition.cpp` / `src/scene/SceneSerialization.cpp`（SaveImpl 抽取 + SaveSplit/LoadSplit + scene/world 1.2 + scene/manifest 1.0）/ `src/scene/ComponentSerializers.cpp`（Layer 序列化器注册）/ `src/render/RenderScene.cpp` / `src/render/Pipeline.cpp` / `src/physics/PhysicsWorld.cpp` / `src/physics/LayerVisibilitySync.cpp` / `samples/12_layer_partition_demo/`
- **GAP-2026-05-16-builtin-asset-disk-serialization**（2026-05-16 落地）：内置 mesh / material 磁盘落盘 + Scene 引用迁移到磁盘路径。详细见上文条目末尾"落地记录"节。涉及 commit：`222bd3f`（G1 + 部分 G4）/ `60eaa40`（G2 + G4 剩余）。关键改动文件：`include/orange/engine/asset/MeshLoader.h` / `src/asset/MeshLoader.cpp` / `tools/OrangeEditor/DemoWorld.cpp` / `src/scene/ComponentSerializers.cpp` / `assets/scenes/demo.scene.json` / `assets/meshes/*.mesh` / `assets/materials/builtin/*.material`
- **GAP-2026-05-17-mesh-vertex-normals**（2026-05-17 落地）：MeshAsset 加 VertexNormal3 + helper（ComputeFlat/SmoothNormalsFromTriangles）+ MeshLoader v2 → v3 schema bump（hasNormals + normals 段，Load 兼容 v1/v2/v3 + fallback 补算）+ Pipeline InterleavedVertex stride 20→32 加 normal attr + 6 内置 vert shader + 1 sample shader 加 inNormal（Path A 单 VID）+ toon/rim/fresnel frag 切 vNormal 替换 dFdx fallback + 8 sample/DemoWorld mesh 工厂调 ComputeSmoothNormalsFromTriangles + 顺手修 AssetRegistryTest 陈旧 kSupportedVersion 常量引用。ctest 全 43 测试通过。详细见上文条目末尾"落地记录"节。关键改动文件：`include/orange/engine/asset/MeshAsset.h` / `include/orange/engine/asset/MeshLoader.h` / `src/asset/MeshAsset.cpp`（新增） / `src/asset/MeshLoader.cpp` / `src/render/Pipeline.cpp` / `src/render/builtin_shaders/{textured_mesh,toon,rim_light,dissolve,emissive,shadow_caster}.vert.glsl` / `src/render/builtin_shaders/{toon,rim_light}.frag.glsl` / `CMakeLists.txt` / `samples/0[3-9]*/main.cpp` / `samples/1[0-2]*/main.cpp` / `samples/08_custom_shader/shaders/fresnel.{vert,frag}.glsl` / `tools/OrangeEditor/DemoWorld.cpp` / `tests/asset/AssetRegistryTest.cpp`
- **GAP-2026-05-22-new-scene-actually-seeds-demo**（2026-05-22 落地，G1 only）：`tools/OrangeEditor/EditorRenderLayer.cpp::ApplyPendingSceneOp` `SceneOp::New` 分支移除 `SeedDemoWorld(mHost)` 调用 —— New Scene 后 World 真正空，Hierarchy 不再含 13 个 demo placeholder entity。同步删 `#include "DemoWorld.h"`（main.cpp 启动期 fallback + Reset to Demo 候选保留 DemoWorld 文件不动）；log message "new scene (seeded demo world)" → "new empty scene"；注释直接说明设计取舍（v1.0 验收脚本段 A 第 2 步首例 Critical fail，程序员便利与零基础用户预期冲突由后者胜）。G2（独立 "Reset to Demo Scene" 菜单项）按当时评估"未必需要"未做，真要演示走 `Open Scene → demo.scene.json` 即可。关键改动文件：`tools/OrangeEditor/EditorRenderLayer.cpp`
- **GAP-2026-05-22-shadow-not-tracking-directional-light-direction**（2026-05-22 撤回）：用户现场验证 —— 改 Sun.Transform.rotation 阴影正确跟随；改 Sun.Transform.position 阴影不变（DirectionalLight 数学正确行为，参 `src/render/Pipeline.cpp:3164` `:4931` 每帧 live 派生）。**非 bug，是 UX 误解**——位置无关性未通过 UI 暴露给零基础用户。合并到 [[GAP-2026-05-22-directional-light-inspector-direction-helper-missing]] G2（Transform.rotation 字段对带 DirectionalLight 的 entity 加 tooltip）。无代码改动。
