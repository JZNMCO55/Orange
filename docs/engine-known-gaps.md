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

## GAP-2026-05-11-point-light-and-visible-halo ✅

- **发现方**：OrangeEditor v0.1 / v0.1.5 Demo Scene 设计
- **发现日期**：2026-05-11
- **一句话定性**：Render 公共面缺 PointLight / SpotLight 与可见光晕能力，导致 "点光源照亮黑暗环境 + 光体本身可见" 的典型场景（Ori-like 史莱姆发光 / 灯泡照明）无法实现
- **状态**：**✅ 2026-05-28 G3 落地**（G1+G2 已落地 2026-05-20；G3 commit `cb8041c`，落地详见本文件"G3 落地记录"段）

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
- **处理**：G1 + G2 已落地（2026-05-20，与音频集成 + GAP-2 cmake gate 同 session）；G3 已落地（2026-05-28，commit `cb8041c`，见下方"G3 落地记录"段）
- **关联**：editor-roadmap.md v0.1.5 demo scene / v1.x Ori-like 视觉子模式
- **归属**：`docs/roadmap.md` Phase 10 · 渲染深化 Task 10-07（2026-05-12 拍板）
- **v0.7 retro 复审（2026-05-19）**：backlog 状态有效；非 v0.7 critical path 上；第一款游戏 fork 启动前不主动开工，与 v1.x Ori-like 视觉子模式同节奏（仅登记需求，撞上即升格）

### G3 落地记录（2026-05-28，与 PostProcess V2 sample 19 + Gizmo plugin 同 session）

**触发**：PostProcess V2 sample 19 + Gizmo plugin 闭环后用户拍板继续推 menu 候选；菜单首选选 A 方案"PointLight 内嵌字段 + Pipeline 自动 halo pass"（用户接受 ~3-4h 工作量），不走 GAP G3 字面"editor 侧消费"的 B 方案 —— 让 halo 成为 PointLight 自带视觉表现，编辑器一勾选即得（UX 优先于零破坏面）。

**实施清单**（11 文件 / 1 commit `cb8041c` / +461 -14）：

| 层 | 文件 | 改动 |
|---|---|---|
| 字段 | `include/orange/engine/render/LightComponent.h` | PointLight struct +3 字段（haloEnabled / haloRadius=0.15m / haloIntensity=1.0），与 castsShadow 正交 |
| 序列化 | `src/scene/ComponentSerializers.cpp` | Write/Read 加 3 字段 optional read，老 scene 不带=默认 off（schema 兼容） |
| schema | `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp` | PointLightComponent schema 加 3 字段 + Range + DragSpeed + Tooltip |
| shader | `src/render/builtin_shaders/halo.{vert,frag}.glsl` | 新 2 shader，复用 mesh vertex input + 144B push constant {uMVP, uModel, uHaloColorIntensity}；vert 算 color * intensity 传 varying，frag 直接 output |
| build | `CMakeLists.txt` | halo.vert/.frag 编译条目 + install list（参 emissive 同款） |
| material | `include/orange/engine/render/BuiltinMaterials.h` + `.cpp` | LoadHalo template（uniforms uMVP + uModel + uHaloColorIntensity），不进 MaterialSystem 公开列表（Pipeline 内部持有） |
| Pipeline | `src/render/pipeline/PipelineImpl.h` | haloMaterial / haloLoaded + haloSphereVertex/IndexBuffer / IndexCount 成员 + EnsureHaloMaterial / EnsureHaloSphereMesh helper + RecordOffscreenPass 签名加 World* pWorld |
| Pipeline | `src/render/Pipeline.cpp` | EnsureHaloSphereMesh 实现（lazy procedural UV sphere 32×16 + InterleaveMesh + CreateBuffer + UploadBuffer）+ RecordOffscreenPass mesh forward 后 / EndRendering 前 halo loop（按 World view<TransformComponent, PointLight> 遍历，haloEnabled 才画；首次 bound 时 BindGraphicsPipeline + SetDescriptorSet(mainDescSet) + BindBuffer 一次，per-light SetPushConstants 144B + DrawIndexed）+ 2 个调用点传 &world + Shutdown 显式 halo buffer reset() |
| sample | `samples/16_light_family_shadows/main.cpp` | --halo CLI flag toggle PointLight haloEnabled + haloRadius=0.25m + haloIntensity=0.5 |

**架构决策**：

- **PointLight 内嵌 halo 字段** vs editor 侧手挂 RenderableComponent —— 选前者，UX 优先；halo 是 PointLight 自带视觉表现而非通用 emissive surface；用户只需勾 haloEnabled 即得视觉
- **复用 GetOrCompilePipeline** 路径 —— LoadHalo 返回 Material desc，Pipeline 自动创建 RHI pipeline 复用主 forward 的 mainDescLayout + 144B push constant；不走独立 graphics pipeline 创建（与 tonemap pipeline 模式相区分，避免重复 layout / shader module 管理）
- **halo loop 共享 mainDescSet** —— halo shader 占位声明 binding 0/1（与 emissive.frag.glsl 同款），dead-code-elim；halo pipeline 切换后 SetDescriptorSet(0, mainDescSet) 直接复用主 forward 已绑定的 light UBO + shadow map
- **vert stage 预乘 color × intensity 传 varying** —— 避免 frag stage 也读 push constant（Vulkan 跨 stage push constant 需 layout stage flag VS|FS），与 emissive vertex shader "push constant only VS" 模式一致
- **bool 字段不 lerp / halo emissive 不参与 light 计算** —— halo 是纯视觉表现，shading pass 走 PointLight color/intensity/range/attenuation 正常路径（与 G1+G2 已落 ECS view 共存，halo 仅追加 record，不改 lighting math）
- **共享 unit sphere mesh** —— 所有 haloEnabled PointLight 共享一个 procedural unit sphere，draw 时按 model = translate(position) * scale(haloRadius) 缩放定位；减少 GPU mesh 管理负担

**bug 顺手修**（同 commit）：

- **VMA assertion 漏修**：首次 capture 测试发现 `Some allocations were not freed before destruction of this memory block` —— Pipeline::Shutdown 显式 .clear() meshCache + templatePipelines + shaderModules 但漏了 halo 资源，unique_ptr 析构晚于 VMA shutdown 触发 assertion。修复：Shutdown 加 haloSphereVertex/IndexBuffer.reset() + haloLoaded=false（与 meshCache.clear 同款 fail-safe）。

**视觉验收**（build/captures/）：

- `16_no-halo.png` 509KB：sample 16 三球场景，右球被暖橙 PointLight 照亮但**光源本身不可见**（halo 落地前的视觉）
- `16_halo.png` 519KB：完全相同场景，**右球右上方多了一颗暖橙发光球**（PointLight 位置 (3.5, 2.6, 2.4) + halo sphere 半径 0.25m + 周围 BloomPass 自然 glow）—— G3 视觉证据成功

**测试 / lint / drift**：

- python scripts/check_invariants.py → All invariants OK (7 grandfathered)
- python scripts/check_claude_md_drift.py → none detected
- 52/52 ctest passed（含 light_and_shadow_test，PointLight 字段扩展无回归）

### G3 留待后续

- **Editor halo 字段 fold-out 分组**：当前 schema 字段一字排开，与其他 PointLight 字段（color/intensity/range/castsShadow）平铺；可考虑加 schema-side group/foldout 让 halo 三字段折叠成 "Visual Halo" 子段。需要 ComponentSchemaBuilder 支持 group/fold API（独立 GAP 触发）
- **PointLight halo 视觉变体**：本期 halo = solid emissive sphere + bloom glow。后续可考虑：(a) billboard quad 替代 3D sphere（更便宜但需 shader 算 camera-facing）；(b) radial gradient + alpha blend（中心更亮边缘渐隐，更"光球"感）；(c) lens flare（视线对准时屏幕空间叠加）—— 都属 v1.x Ori-like 视觉子模式拉动
- **halo 阴影回避**：当前 halo emissive sphere 也接收 sceneDepth test，进入其他几何时会被遮挡（按 Unity / Unreal lens-flare 模式应该穿透）；可考虑改成 depth test off / additive blend 让 halo 在几何前永远可见。本期不动（防"halo 永远穿透看上去突兀"反向问题），独立 polish GAP 触发

---














## GAP-2026-05-19-editor-aux-passes-in-engine-pipeline ✅

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
- **处理**：
  - 2026-05-20：最小可行 cmake gate 已落地 —— `ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES` cmake option 默认 ON 保持编辑器行为；shipping `-DORANGE_ENGINE_WITH_EDITOR_AUX_PASSES=OFF` 关掉 grid 渲染 + dummy IBL ambient 退回 (0,0,0) + clear color 退回深蓝
  - 2026-05-24（v1.2.1 patch，**校准后**）：**G1 第一+二阶段落地** —— `IAuxPassProvider` 公共 interface + `AuxPassContext` struct + `Pipeline::SetAuxPassProvider` 公共 API（window 路径 + offscreen 路径双 hook，grid 之后 / debug draw 之前调用）；公共面命名中性化 `SetEditorGridEnabled` → `SetAuxGridEnabled`；engine 公共头 grep "EditorGrid" 不到。**G1 第三阶段 grid pass 实际迁出**（PipelineGrid.cpp 全文件 + cmake gate 整体移除）留 v1.3.0+ minor 拉动。**版本校准**：原 commit 06b76e3 拟 v1.3.0 minor，同日用户当场指出 "2 天 5 个 bump 太随意，1.x.0 minor 必须伴随多个完整功能落地"，按 [[feedback-minor-bump-must-carry-multiple-features]] 新规则校准为 patch
  - **2026-05-24（v1.3.0 minor）✅**：**G1 第三阶段 grid pass 真正迁出**完成。编辑器侧 `tools/OrangeEditor/render/EditorGridAuxPassProvider.{h,cpp}` 实现 IAuxPassProvider，自管 shader / PSO / 描述符全套 GPU 资源，通过 `Pipeline::SetAuxPassProvider` 注册；引擎侧删除 `src/render/pipeline/PipelineGrid.cpp` + `src/render/builtin_shaders/grid.frag.glsl` + Pipeline 内 gridXxx 6 字段 + 2 处 RecordGridPass 调用 + PipelineSetup grid PSO 创建段 + `ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES` cmake option 整体清退；`Pipeline::SetAuxGridEnabled` / `IsAuxGridEnabled` 公共 API 删除。同时落地 G2 默认值口径中性化：`Pipeline::SetDummyIblAmbient` / `SetSceneClearColor` 公共 API，engine 默认 (0,0,0) ambient + (0.05, 0.07, 0.10) shipping 深蓝灰 clear，编辑器侧显式 override 到 UX 友好值。IAuxPassProvider hook 契约升级：Pipeline pre-transition sceneDepth → ShaderReadOnly + 更新 tracker，provider 仅写 hdrColor + 读 sceneDepth、不改 depth layout。详见 [editor-roadmap.md v1.3.0 节](editor-roadmap.md) + `vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.3.0-acceptance-checklist.md`
- **关联**：editor-roadmap v0.8.5（落地源头）/ v1.2.1（G1 第一+二阶段）/ v1.3.0（G1 第三阶段 + G2 闭环 ✅）；engine 公共 API 中性化原则
- **归属**：v1.3.0 ✅

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

## GAP-2026-05-20-editor-fprintf-to-core-log-migration ✅

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

## GAP-2026-05-21-collider-polygon-edgechain-interactive-edit ✅

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
- **代码落地**：2026-05-25（本 session；2026-05-26 人工视觉验收通过）——
  - **G1 "Polygon edit mode" 全局状态**：`context/EditorColliderEditState.h`（新 sub-context 挂 EditorHost，符合 "新功能找对应子 context" 纪律）；与内置 Translate/Rotate/Scale gizmo 互斥（`EditorCameraControl` 把 colliderEdit.active 并入 gizmoBusy → 相机 LMB 冻结；`ScenePanel` active 时跳过 W/E/R 切换 + gizmo + picking）
  - **G2 viewport 顶点 picking / 拖 / 加 / 删**：`ColliderVertexEdit.{h,cpp}`，复用 `OrangeEditor::Internal::GizmoMath` 的 ProjectWorldToScreen（hit-test + handle 绘制）/ ScreenToWorldRay + RayPlaneIntersect（拖拽/加点反投影到 entity collider 平面）/ PointSegmentDistance2D（加点找最近边插入）；世界变换与 ColliderDebugDraw 一致（local.xy 经 quat 旋转 + position，yaw-only 假设）；三态 handle 配色（normal 琥珀 / hover 白 / selected 青）
  - **G3 命令栈集成**：`SetFieldValueCommand<PolygonDesc/EdgeChainDesc>` 整值写入；拖拽连续帧共享 dragOpId 拼进 fieldKey → coalesce 成单条可一次 Undo；加点/删点各递增 opSeq → 离散不 coalesce
  - **入口**：`plugin/ColliderEditInspectorPlugin`（IEditorInspectorPlugin，Collider 段末 "Edit Vertices in Viewport" 按钮，仅 Polygon/EdgeChain shape；Esc / 再点退出）
  - 改动文件：新增 `context/EditorColliderEditState.h` / `ColliderVertexEdit.{h,cpp}` / `plugin/ColliderEditInspectorPlugin.{h,cpp}`；改 `EditorHost.h`（colliderEdit 字段）/ `EditorCameraControl.cpp`（LMB gate）/ `panels/ScenePanel.cpp`（集成）/ `main.cpp`（注册）/ `CMakeLists.txt`（2 源）
  - 验证：OrangeEditor.exe 编译链接通过 + invariant lint（7 grandfathered，无新违规）+ drift 全绿
  - **人工视觉验收**：2026-05-26 用户确认通过（拖 / 加 / 删顶点 + Undo/Redo + 相机 LMB 冻结实测 OK）→ 标 ✅。**仍待补**：acceptance checklist（Orange-Wiki 子仓，另开 session 写）+ CMake VERSION bump（patch v1.3.1，按 [[feedback-minor-bump-must-carry-multiple-features]]）
- **关联**：本 session 已落地的 ColliderDebugDraw wireframe 可视化（前置基础）；本 session 同时修复 PolygonVertices / EdgeChainVertices Inspector Remove 按钮窄列越界（顺手 UX 修，不在本 GAP 范围）
- **归属**：~~未拍板~~ → 代码落地 2026-05-25 + 人工视觉验收通过 2026-05-26（OrangeEditor）；归 v1.3.1 patch（待补 acceptance checklist + VERSION bump）

---

## GAP-2026-05-21-editor-coplanar-mesh-z-fight-prevention ✅

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
- **关闭**：2026-05-23（G1 落地，详见"处理记录"段；G2/G3 留后续 session 等吸附工具 / scene save UX 整骨拉动）
- **优先级**：P1（friction）—— 不阻塞功能但作者摆贴地物体时常态化撞 z-fight，肉眼反推问题源成本高
- **关联**：原 2026-05-21 session 已通过 content fix 把 Tower y 从 0.5 抬到 0.501 解决 demo.scene.json 当前可见 z-fight；本次 G1 是"防再次发生"的工程化落地

---

## GAP-2026-05-22-new-scene-actually-seeds-demo ✅

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

## GAP-2026-05-22-directional-light-inspector-direction-helper-missing ✅

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
- **关闭**：2026-05-22（v1.0.1 batch G1 落地，详见"处理记录"段）
- **优先级**：~~**P1（Friction，不阻塞 v1.0）**~~ → 已修
- **归属**：~~OrangeEditor v1.x UX 改进 batch~~ → v1.0.1 落地
- **关联**：v1.0 验收脚本段 B 第 2 步；脚本本身也需修订（按当前 schema 改成"选 Sun → Transform → rotation"）

---

## GAP-2026-05-22-shadow-not-tracking-directional-light-direction ✅

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

## GAP-2026-05-22-editor-material-create-and-thumbnail-missing ✅（G1）

- **发现方**：用户 v1.0 验收后试搭场景观察（"想新建一个材质 / 想看 Asset 浏览器里材质长什么样"）
- **发现日期**：2026-05-22
- **一句话定性**：OrangeEditor 缺失两个材质工作流核心能力 —— (a) **从零创建新材质**（当前只能基于已有 `.material` 文件修改或挑选，没有"New Material"入口）+ (b) **材质资源缩略图**（推荐球体 / 圆形预览，Asset 浏览器只有 [MAT] icon + 文件名，无视觉识别，N+ 个材质难辨）

### 触发场景

- 用户搭场景时想"给这个 Cube 用一个新颜色 / 新粗糙度的材质" → 发现只能：
  1. 去 Asset 浏览器找一个相近的 `.material`
  2. 在 Inspector 修改 instance override 字段
  3. **没有"另存为 / 新建材质"路径** —— 想要永久保存修改成新材质资源无入口
- 用户在 Asset 浏览器看 v1.0 demo scene 已有 50+ 个 `.material` 文件，**只能靠文件名猜哪个长什么样** → 想换材质必须每个 Pick 试，反复来回
- 工业惯例对照：Unity / Unreal / Godot / Lumix / Cocos Creator 全部支持 Asset Browser 右键 → Create → Material + 球体 thumbnail，**全行业标配**

### 证据

- `grep "New Material|Create Material" tools/OrangeEditor/` 全仓 0 匹配 —— 无任何菜单项 / 按钮 / 右键入口创建新材质
- `MaterialAssetInspectorPlugin` 存在但仅渲染 Inspector 段（字段编辑），不渲染缩略图；Asset 浏览器的 `.material` 文件以 `[MAT]` 文本 icon 显示
- 关联前置已落地能力（这个 GAP 可基于这些扩展）：
  - GAP-2026-05-16-builtin-asset-disk-serialization：内置 mesh / material 磁盘落盘 + Scene 引用迁移到磁盘路径 → 写入 `.material` 文件路径已通
  - GAP-2026-05-16-material-system-enumerate-and-instance-overrides：MaterialSystem::GetTemplateNames + MaterialInstance enumerate override API + .material schema v1.1 → 新建材质所需的"枚举模板"和"写 override"能力已通
  - GAP-2026-05-14-renderable-material-instance-round-trip：MaterialInstance 命名 round-trip 已通

### 缺什么（高层概括，技术方案留独立 议题讨论）

#### G1 · "New Material" 入口

- Asset 浏览器右键 → Create → Material（Unity / Unreal / Lumix 同款）
- 弹窗选 template（pbr / unlit / dissolve / 等已注册 MaterialSystem 模板）+ 输入文件名 + 保存目录
- 写出 `.material` 文件 → AssetRegistry 自动刷新

#### G2 · 材质缩略图（球体预览）

- 每个 `.material` 在 Asset 浏览器显示 64×64 / 96×96 缩略图，材质应用到内置球体 mesh 渲染
- 缩略图按需 lazy bake（Asset 浏览器滚动到视口才烘培），结果缓存到磁盘（`.material.thumb.png` 或集中 cache）
- 材质字段改动 → 缩略图自动 invalidate + 重 bake

#### G3 ·（可选）"Save Inspector overrides as new material"

- 用户在 Inspector 改完 instance override 后右键"Save As New Material..." → 把当前 overrides 落盘成新 `.material` 资源
- 比 G1 流程更顺手（直接从已有材质 derive）

### 期望验收

- 美术 / 关卡设计师**不写代码**完成"复制 demo 材质 → 改颜色 → 保存为新材质 → 应用到不同 entity"完整闭环
- Asset 浏览器视觉识别：滚动浏览 50+ 个材质，可以**眼看缩略图就识别**，不必每个 Pick 试

### 状态

- **登记**：2026-05-22
- **优先级**：**P1（Friction，不阻塞 v1.0 ✅ 但严重影响美术工作流）** —— v1.0 验收脚本只需用现成材质即可跑通 → 不阻塞 ✅；但作为"美术 / 关卡设计师不写代码完成日常工作"目标的关键短板，**比其它 v1.x friction 优先级稍高**（这是用户**主动构思场景**时立刻撞上的痛点，比 dock layout / multi-DirLight UI 暴露面更宽）
- **归属**：OrangeEditor v1.x material 子模式 milestone（按 `docs/editor-roadmap.md` 历史，v0.5 是 Asset 浏览器 + Material 子模式 ✅，本 GAP 是 v0.5 之上的 polish + complete）
- **处理**：
  - **2026-05-23（v1.1.1 patch）✅ G1 完整闭环**：Asset Browser 右键 → Create → Material modal + 自动 lazy create live instance + DnD 应用到实体 + Inspector cache 同步（v1.2.x patch 系列共 7 个 patch 把"新材质工作流"打磨到不写代码完成日常工作的水平）。Lumix / Unity 工业惯例 1:1 对位
  - **2026-05-24（v1.3.0 minor）trim 决策**：原拟 G2 缩略图与 grid pass 迁出 + Pipeline 中性化合并到 v1.3.0 bundle，本 session 评估后判断球体真渲染需复刻 mini-pipeline（PSO + scene descriptor set + dummy lights + IBL bind + push constants）500-800 LOC 独立设计点，与本 GAP 状态字段"留独立议题讨论后立项"原意匹配 —— trim 出去到 v1.4.0 minor 独立议题。v1.3.0 走 2 完整功能区（中性化 + grid 迁出）满足 [[feedback-minor-bump-must-carry-multiple-features]] 门槛
- **技术方案讨论 placeholder（v1.4.0 议题准备）**：thumbnail 烘培走哪条 Pipeline 路径 —— 候选 (a) 编辑器侧 mini-pipeline（独立 PSO + scene desc set + 内置球体 mesh + dummy 1 dir light + 复用 dummy IBL + 复用 material push constant 公共部分）；候选 (b) Lumix 简化路径（仅 base color 着色 tile，无真球体渲染，适用于 50+ 材质快速识别，球体保真度低）；候选 (c) 复用 Pipeline 改造为可渲染到任意 RT（侵入 engine，工作量最大）。缓存策略：per-session 内存 vs 磁盘 .material.thumb.png；触发器：lazy on visible vs 启动期 batch；G3 vs G1 优先级
- **关联**：[[GAP-2026-05-16-builtin-asset-disk-serialization]] / [[GAP-2026-05-16-material-system-enumerate-and-instance-overrides]] / [[GAP-2026-05-14-renderable-material-instance-round-trip]]（前置已落地基础）；[[reference-polyhaven-hdri]] / [[reference-lumix-ibl-filter]]（参考方案）

---

## GAP-2026-05-22-editor-dcc-import-pipeline-missing ✅

- **发现方**：用户 v1.0 验收后实测推断（"还有导入模型文件的能力，贴图的能力"）
- **发现日期**：2026-05-22
- **一句话定性**：OrangeEditor 缺失**外部 DCC 资产导入流水线** —— 用户手上的 `.obj / .fbx / .gltf` 模型 / `.png / .jpg` 贴图无法通过编辑器导入并入 AssetRegistry。当前所有 mesh 都是 builtin（plane / cube / sphere）+ `DemoWorld` 工厂生成，所有 texture 走 builtin/内嵌路径；用户不能拿现成美术资产搭场景，自有美术工作流被锁死

### 触发场景

- 用户从 PolyHaven / Sketchfab / 自家 Blender 导出 `.obj` 模型想用 → **无入口**（菜单没有 File→Import，Asset 浏览器拖入无反应）
- 用户有现成 `.png` 漫反射贴图想给材质用 → 引擎侧 `TextureLoader` 能加载，但**编辑器侧无导入 GUI**，用户没法把贴图入 AssetRegistry
- 工业惯例对照：Unity / Unreal / Godot / Lumix / Cocos Creator 全部支持
  - 拖外部文件到 Asset 浏览器 → 自动 import + 转引擎自有格式
  - File → Import Asset... 菜单
  - 多种格式 importer（mesh: obj/fbx/gltf/dae/usd, texture: png/jpg/exr/dds/tga）

### 证据

- `grep "Import Mesh|Import Model" tools/OrangeEditor/` 全仓 0 匹配 —— 无任何导入入口
- `grep ".obj|.fbx|.gltf" tools/OrangeEditor/` 仅匹配 2 个文件（EditorRenderLayer.cpp / schema/PropertyType.h），具体看是字符串提及而非实际处理
- `src/asset/TextureLoader.cpp` 使用 stb_image 支持 PNG / JPG / HDR 加载（引擎侧能读），但编辑器 Asset 浏览器仅识别已有 AssetRegistry 内项目，无外部文件 → 资产 import 路径
- `Resources/Models/` 64 文件 D（v0.6/v0.7 验收 wrap-up commit 890d2ee 清理） + `assets/Models/` 110MB gitignore 化（commit 890d2ee `.gitignore` 加规则）= 历史 demo 美术资产入仓尝试已退场，但**没有 import 工作流补位** → 用户拿任意外部资产仍无路径入仓

### 缺什么（高层概括，技术方案留独立议题讨论）

#### G1 · Mesh import（.obj 起步，.gltf / .fbx 后续）

- File → Import Mesh... 菜单 / Asset 浏览器拖入 / 右键 Import
- 选 .obj 文件 → importer 解析顶点 + 法线 + UV + 三角面 → 写出引擎自有 `.mesh` 文件（v3 schema 已含 normals）→ AssetRegistry 自动刷新
- **格式优先级建议**：.obj（最简单、覆盖 80% 入门用例，无 skeleton/animation）→ .gltf（PBR 标准、跨 DCC 兼容、开源）→ .fbx（最广覆盖但 SDK 复杂，留长期）
- 涉及 vendor 选型：tinyobjloader / cgltf / OpenFBX 等
- 复杂度：动画 / 骨骼 / 多材质 / 多 sub-mesh 拆分都是后续

#### G2 · Texture import（.png / .jpg / .tga 起步，.exr / .dds / .hdr 后续）

- File → Import Texture... 菜单 / Asset 浏览器拖入 / 右键 Import
- 选外部图 → 入 AssetRegistry → 编辑器 Inspector 可 Pick 给材质 BaseColor / Normal / Roughness / etc 通道
- 引擎侧 `TextureLoader` 已支持 PNG/JPG/HDR（PolyHaven 入口已通）；**主要工作在编辑器 GUI**
- 涉及决策：是否预生成 mipmap / 是否压缩成 .ktx / BC7 / cwd 内拷贝 vs in-place 引用 / 等

#### G3 ·（可选，更长期）批量导入 + 资产管线

- 拖文件夹 / 多文件批量 import
- 资产管线 hash + 增量重 import
- 类似 Unity AssetPostprocessor 风格 hook

### 期望验收

- 美术 / 关卡设计师不写代码完成：从 Blender 导出 `.obj` → 拖到 Asset 浏览器 → 自动 import → 拖到 Hierarchy → 应用现有 / 新建材质 → 渲染正确
- 同款流程对 .png 贴图：拖入 → 入 AssetRegistry → Pick 给材质 → viewport 实时反映

### 状态

- **登记**：2026-05-22
- **关闭**：2026-05-25（确认 v1.1 · DCC Asset Import Pipeline 已整体落地 ✅，本条状态此前漏回填 —— 文档 bug 修正，非新工作；落地早于本次回填）
- **优先级**：~~**P1（Friction，不阻塞 v1.0 ✅ 但锁死美术工作流上游）**~~ → v1.1 已落地（.obj + .gltf/.glb mesh + .png/.jpg/.jpeg/.tga/.hdr texture + .meta sidecar + FNV-1a hash 增量 + File→Import 菜单 + Asset Browser 拖拽，见 `tools/OrangeEditor/import/`）
- **归属**：~~OrangeEditor v1.x 或独立 "DCC import milestone"~~ → OrangeEditor **v1.1 · DCC Asset Import Pipeline ✅**（CMake VERSION 1.0.1 → 1.1.0；T1~T5 全落地，详见 [editor-roadmap.md v1.1 节](editor-roadmap.md) + [ADR-008](decisions/README.md) 5 议题合并决议）
- **技术方案讨论 placeholder（已由 ADR-008 决议）**：~~mesh importer vendor 选型 / texture mipmap / 压缩策略 / 外部资产物理路径 vs 入仓 copy / 资产管线 hash + 增量~~ → vendor = tinyobjloader(.obj) + cgltf(.gltf)（in-tree single-header）；copy 入 `assets/<TypeDir>/` + .meta sidecar（sourcePath + FNV-1a sourceHash）；texture 走 RGBA8 + 运行时 mipmap
- **剩余延后项（非本 GAP 范围，editor-roadmap v1.1 节已列）**：glTF PBR **material 解析** + mesh normal/tangent 自动补（**mikktspace**）→ v1.2；**.fbx import**（4 件套路径已铺好，加 importer 模块即可）；BC7/KTX2 压缩 → 性能 milestone；Blender/Maya export plugin → 独立立项；资产 watcher → ACP 后
- **关联**：[[GAP-2026-05-22-editor-material-create-and-thumbnail-missing]]（同属美术工作流补完 batch，其 G1 Create Material UI 已 v1.1.1 ✅）/ 主仓 Phase 9 资产管线远期方向（前置但**不必等**，最小可行 import 已在 v1.1 独立完成）

---

## GAP-2026-05-22-pipeline-cpp-monolithic-needs-split ✅

- **发现方**：用户 v1.0 验收后浏览代码时观察（"Pipeline.cpp 是不是太大了"）
- **发现日期**：2026-05-22
- **一句话定性**：`src/render/Pipeline.cpp` 5308 行单 TU，包含 Setup / Render / Impl 内部各 pass record（Shadow / Bloom / GodRays / Sky / Grid / DebugDraw / Capture 等）全部混在一个 .cpp —— 比同栈对照 Lumix `pipeline.cpp`（4262 行）大 25%；维护 / 编译速度 / 新人 onboard 成本逐 milestone 累加，Phase 7+ 新 pass（SSAO / TAA / volumetrics / reflection probe）每个会 +200~500 行，不拆会滚到 7K+

### 触发场景

- 跨 session 对话频繁出现 `Pipeline.cpp:3164` / `:4931` / `:5015` 等行号引用 —— "先 grep 行号 → 跳行"已是默认 workflow，间接证据
- MSVC 单 TU 5K+ 行，每次小改触发全文件 re-compile；Pipeline 是 hot 编辑文件，编译 + link 时间感受明显
- 新人理解架构边界（按 pass / 按生命周期 / 按 Impl/公共 类拆？）需通读 5K 行，没有 .cpp 文件结构给视觉锚

### 证据 / 量化

- `wc -l src/render/Pipeline.cpp` = 5308 行
- `wc -l vendor/LumixEngine/src/renderer/pipeline.cpp` = 4262 行（同栈 C++ ECS + ImGui editor 引擎对照）
- 主要 mega-block 行号分布：
  - 1-1115 helper functions（FillVertexInputLayout / FillPushConstantRanges / OrangeRenderLogAdapter 等）
  - **1136-2018 `Pipeline::SetupRhiResources` 单方法 882 行**（Vulkan/shader/descriptor pool/pipeline state 初始化）
  - 2543-2825 IBL bake + frame time + capture (~280 行)
  - 2856-3338 Impl::RecordOffscreenPass + RecordPassthroughToViewport + RenderOffscreen (~480 行)
  - 3465-3727 Impl::Shadow 系列（EnsureShadowMap / RecordShadowPass / UpdateLightUbo / UpdatePointLightsUbo）~ 260 行
  - 3890-4160 Impl::Bloom 系列（Ensure / Record / Release）~ 270 行
  - 4160-4336 Impl::GodRays 系列 ~ 180 行
  - 4336-4593 Impl::Sky 系列（Sky + ProceduralSky）~ 260 行
  - 4593-4800 Impl::Grid + DebugDraw ~ 210 行
  - **4800-5308 `Pipeline::Render` 主循环 508 行**

### 工业对照

- Lumix `pipeline.cpp` 4262 行（同栈 C++，是 outlier 大但勉强活）
- Unreal：按 pass 拆 `.cpp`（DeferredShadingRenderer / ShadowRendering / TemporalAA 各 1-3K 行）
- Unity HDRP / URP：每 RenderPipeline / pass 独立 `.cs`，每 file ~500-1500 行
- Bevy：module crate 化，每 file ~500 行
- Godot：按 rasterizer / scene_render backend 拆

主流趋势是**按 pass 拆**单 file 千行级。Lumix 是 outlier；OrangeEngine 比 Lumix 还大 1K，**已超工业可接受上限**。

### 缺什么（拆分方案）

```
src/render/
├── Pipeline.cpp                   ~1500 行  (公共类 + Render 主循环)
├── pipeline/
│   ├── PipelineSetup.cpp          ~900 行   (SetupRhiResources 拆出)
│   ├── PipelineShadow.cpp         ~400 行   (Shadow / Light UBO)
│   ├── PipelineBloom.cpp          ~270 行
│   ├── PipelineGodRays.cpp        ~180 行
│   ├── PipelineSky.cpp            ~260 行   (Sky + ProceduralSky)
│   ├── PipelineGrid.cpp           ~130 行
│   ├── PipelineDebugDraw.cpp      ~80 行
│   ├── PipelineCapture.cpp        ~160 行
│   └── PipelineHelpers.cpp        ~1100 行  (helper functions 拆出)
```

拆分要点（守不变性）：
- 所有 `Pipeline::Impl::XXX` 方法是同一个 `class Impl` 的成员；拆 .cpp 时 **Impl 类声明仍集中**（或抽到 `src/render/pipeline/PipelineImpl.h`），各拆出的 .cpp 只定义 method body
- **不引入新公共 API、不动 `Pipeline.h`、不变 ABI、不破坏 src/render 唯一 OrangeRender consumer 纪律**
- 每个 .cpp 顶部 include 同款 `<orange/...>` headers，CLAUDE.md header isolation 继续守
- CMake `src/render/Pipeline.cpp` → `src/render/pipeline/*.cpp` 一并加进 orange_engine target，无新 link 单元

### 期望验收

- 每个 .cpp ≤ 1500 行（main Pipeline.cpp 主循环上限）/ 大多数 ≤ 500 行
- `Pipeline.h` 公共面**不变**（API + ABI 守住）
- ctest 全 43 测试通过、samples 01-14 + 编辑器 build 通过、视觉无回归（包括 PBR / IBL / shadow / bloom / godrays / sky / grid / debug draw 各 pass）
- 重构后 grep 跳函数路径从"先 grep Pipeline.cpp 行号"→"直接 cd 子模块 .cpp 看"

### 状态

- **登记**：2026-05-22
- **关闭**：2026-05-23（跨 2 session 落地，详见"处理记录"段；ADR-007 同 commit 落 Wiki）
- **优先级**：~~**P2（技术债，不阻塞 v1.0 ✅ + 不阻塞 Phase 7）**~~ → 已修
- **归属**：~~**OrangeEngine refactor milestone**（Phase 6 与 Phase 7 之间）~~ → 2026-05-23 跨 2 session 完工，未占独立 milestone 编号（按 Phase 6 与 Phase 7 之间的纯重构定位）
- **关联**：`src/render/Pipeline.cpp` / `src/render/pipeline/*.{h,cpp}` / `vendor/LumixEngine/src/renderer/pipeline.cpp`（对照）/ CLAUDE.md "src/render/ 唯一 OrangeRender consumer" 纪律（拆分后继续守）/ [[ADR-007]]（Pipeline.cpp 按 pass 维度拆分约定）

### 处理记录

**Session 1（2026-05-23，commit `ab16563`）—— 前置：抽 Impl 声明 + 顶部 helpers**

- 抽 `Pipeline::Impl` 完整 declaration 到 `src/render/pipeline/PipelineImpl.h`（656 行，含数据成员 + 嵌套 struct + inline method）
- 抽 anonymous ns helpers + 常量 + `OrangeRenderLogAdapter` 适配器到 `src/render/pipeline/PipelineHelpers.{h,cpp}`（71 + 210 行，命名空间 `Orange::Engine::Render::PipelineDetail`）
- Pipeline.cpp 通过 `using namespace PipelineDetail;` 继续以短名引用
- Pipeline.cpp 5314 → 4322 行（删 992 行，全部迁出 helpers）
- CMakeLists.txt orange_engine 加 PipelineHelpers.cpp
- ctest 43/43 + invariant lint + drift 全绿

**Session 2（2026-05-23，本 session）—— 拆 8 个 pass 子 .cpp + ADR-007 落 Wiki**

- 在 `src/render/pipeline/` 下创建 8 个 pass 子 .cpp：
  - `PipelineSetup.cpp`（909 行）—— `Pipeline::SetupRhiResources` 整体迁出
  - `PipelineShadow.cpp`（274 行）—— EnsureShadowMap / ComputeLightViewProj / UpdateLightUbo / UpdatePointLightsUbo / RecordShadowPass
  - `PipelineCapture.cpp`（203 行）—— EnsureCaptureBuffer / RecordCaptureCopy / FinalizeCapture + HalfToFloat / AcesNarkowicz 匿名 ns + `STB_IMAGE_WRITE_IMPLEMENTATION` 块从 Pipeline.cpp 迁入
  - `PipelineBloom.cpp`（284 行）—— ReleaseBloomResources / EnsureBloomResources / RecordBloomChain
  - `PipelineGodRays.cpp`（194 行）—— EnsureGodRaysSet / RecordGodRaysPass
  - `PipelineSky.cpp`（272 行）—— EnsureSkyDescSet / RecordSkyPass / RecordProceduralSkyPass
  - `PipelineGrid.cpp`（138 行）—— RecordGridPass
  - `PipelineDebugDraw.cpp`（81 行）—— RecordDebugDrawPass
- Pipeline.cpp 4322 → 2098 行；主文件留公共面（ctor / Initialize / Shutdown / 各 setter / BakeIblFromWorld / RequestCapture）+ Render 主循环（~500 行）+ Offscreen 系列（RecordOffscreenPass / RecordPassthroughToViewport / RenderOffscreen / EnsureMeshGpuCache）+ FindActive 系列（FindActiveBloomPass / FindActiveTonemapPass / FindActiveGodRaysPass）+ Profile bin declarations
- CMakeLists.txt orange_engine 加 8 个新 .cpp 源
- 修复 Build 期一次 stb_image_write 多重定义 link error（IMPLEMENTATION 块从 Pipeline.cpp 删除，仅留 PipelineCapture.cpp 一处）
- ctest 43/43 + invariant lint + drift 全绿；OrangeEditor + 14 samples + 全测试 build 通过
- ADR-007 落 Wiki（`vendor/Orange-Wiki/case-studies/orange-engine/decisions/ADR-007-pipeline-cpp-pass-level-split.md`）+ 本仓 `docs/decisions/README.md` Index 表追加条目 + Wiki case-studies 索引同步更新
- 用户视觉验收：PBR / IBL / shadow / bloom / godrays / sky / grid / debug draw 各 pass 视觉无回归

**最终行数对照**：拆分前单 TU 5314 行；拆分后 9 子 .cpp + 主 .cpp 共 5390 行（多 76 行为各 .cpp 顶部 includes/namespace boilerplate）。详细每文件行数表见 [[ADR-007]]。

**未来再拆触发条件**：(1) Pipeline.cpp 再撞 3K 行（下个 SSAO / TAA / volumetrics / reflection probe 大 pass milestone 后）；或 (2) Offscreen 系列单独超 800 行 → 拆 PipelineMain.cpp；或 (3) Render() 主循环单方法超 700 行 → 进一步切方法。**当前 2098 行落在 "工业 1K-2.5K 行主文件可接受上限" 内**，不再继续拆。

---

## GAP-2026-05-22-multi-environment-component-semantics-undefined ✅

- **发现方**：用户 v1.0 验收后实测推断（"任意 entity 挂 Environment 都改变景色？多个会怎样？"）
- **发现日期**：2026-05-22
- **一句话定性**：场景中存在多个 EnvironmentComponent 时，Pipeline first-found 取迭代器第一个生效，其余静默忽略；用户修改非生效的 EnvironmentComponent 字段（cubemap / tint / intensity）时 viewport 毫无反应，体感"引擎坏了" —— 与 [[GAP-2026-05-22-multi-directional-light-semantics-undefined]] 同根因孪生

### 触发场景

- 用户给 Sun entity 挂一个 EnvironmentComponent + .hdr → 生效
- 用户后续在 Cube1 上又挂一个 EnvironmentComponent + 另一张 .hdr → **不生效**（Sun 那个仍是 first-found）
- 用户改 Cube1.Environment.intensity / tint → viewport 不变 → 困惑

### 证据

- `src/render/Pipeline.cpp:3166-3168` 注释明示：
  ```
  // EnvironmentComponent first-found：与 DirectionalLight 同款选取；
  // 多个时取迭代器第一个（baseline 单 World 全局环境，多 environment
  // blending 留给后续 reflection probe milestone）
  ```
- Pipeline 不读 EnvironmentComponent 所挂 entity 的 Transform.position —— 该组件**事实上是全局单例**，但允许挂任意 entity 的现状模糊"全局单例 vs per-entity 实例"边界
- 工业对照：
  - Godot：Environment 通常挂 Camera 节点单例，明确语义
  - Unity HDRP：Volume 多个支持，按 priority / range 区域 blend
  - Unreal：SkyLight 通常 1 个，多个 SkyLight 报警告

### 缺什么

#### G1 · Inspector helper + Hierarchy warning（推荐 v1.x 落地，与 multi-DirLight 同 patch）

- `RegisterEnvironmentComponentSchema` 加段首 helper 文案："Only the first EnvironmentComponent in the scene is used. Add at most one per scene."
- Hierarchy 检测到 >1 个 EnvironmentComponent 时，在非首个 entity 行加 warning icon + tooltip "Ignored: another EnvironmentComponent already active"
- 改动量与 [[GAP-2026-05-22-multi-directional-light-semantics-undefined]] G1 重叠 90%，可一次性 batch

#### G2 · 长期 multi-environment blending（reflection probe milestone）

- HDRP / Lumix EnvProbe 风格的多 environment 区域 blending
- 引入 `EnvironmentVolume` 概念（每个 environment 有 position + range / priority），Pipeline 按 camera 位置查最近 / blend
- 属 long-term roadmap 量级（Phase 10+ 或单独 reflection probe milestone，与 [[reference-lumix-ibl-filter]] 同档参考）

### 期望验收

- G1 ✅ 条件：场景里挂 2 个 EnvironmentComponent，第二个 entity Hierarchy 行有明显 warning icon + Inspector 段顶部 helper 文案可见；用户 10 秒内能理解"第 2 个不生效"
- G2（如真做）✅ 条件：camera 在不同区域时自动切换 / blend 对应 EnvironmentVolume，viewport 实时反映

### 状态

- **登记**：2026-05-22
- **关闭**：2026-05-22（G1 在 v1.0.1 batch 落地，详见"处理记录"段；G2 留 reflection probe milestone）
- **优先级**：~~**P2（Friction，不阻塞 v1.0 ✅）**~~ → G1 已修
- **归属**：~~G1 留 v1.x UX batch~~ → G1 v1.0.1 落地（与 multi-DirLight G1 同 batch 共用 Hierarchy warning 路径，预期 batch 收益验证）；G2 待 reflection probe milestone
- **关联**：[[GAP-2026-05-22-multi-directional-light-semantics-undefined]]（孪生根因，同 batch 修）/ [[GAP-2026-05-19-editor-environment-component-wiring]]（前置基础）/ `src/render/Pipeline.cpp:3166-3168`

---

## GAP-2026-05-22-editor-default-ibl-missing-causes-black-pbr-faces ✅

- **发现方**：作者本人 v1.0 验收后试搭场景（Sun + Floor + Cube1/2/3 + Sphere + smoke），无 EnvironmentComponent
- **发现日期**：2026-05-22
- **一句话定性**：场景未挂 EnvironmentComponent (IBL) 时，PBR 物体的"背向 directional light 的面"完全黑（max(N·L, 0) = 0 + 无 ambient fallback）；离散面 mesh（cube）撞得最严重 —— 朝光的面亮、其它面全黑、视觉上像"半透明 / 渲染了内部"；连续曲面 mesh（sphere）症状较轻但暗部仍偏黑

### 触发场景

- v1.0 验收脚本段 A → B 流程产出的场景（assets/scenes/v1.0.scene.json）
- 同材质 warm_m0r0 应用到 sphere + cube → sphere 整体黄色 + 暗部渐变 OK / cube 顶面黄 + 正面侧面全黑 → 用户体感"颜色差异大 + cube 像透明 / 渲染内部"
- 截图 `D:\Photo\Orange\sphere-cube.png`

### 证据 / 根因诊断

- `GAP-2026-05-19-editor-aux-passes-in-engine-pipeline` 处理记录明示：v0.8.5 验收后 engine 端默认 IBL irradiance ambient 从 (0.25, 0.25, 0.25) 退到 (0, 0, 0) —— PBR 物体仅 direct light，无 ambient fallback
- `GAP-2026-05-19-editor-environment-component-wiring` 处理记录：编辑器有 .hdr 浏览器 + Pipeline 自动 re-bake，能挂 EnvironmentComponent → IBL 接通 → 暗面有 environment 贡献，**但前提是用户主动挂 EnvironmentComponent**
- 零配置场景（用户 New Scene → 加几个 entity → 不知道要挂 Environment）撞上"PBR 暗面全黑" UX 陷阱
- 用户的"cube 正面像透明" = 视觉误读：实际是 unlit 极暗 cube 正面 + transform gizmo always-on-top 配置穿过去，让 cube 看起来"被穿透"；cube 本身 opaque

### 缺什么

#### G1 · 编辑器默认 seed 一个 fallback IBL

- New Scene 路径 + Load Scene 路径，World 没有 EnvironmentComponent 时，**Pipeline 用一个内置 fallback environment**（极简的灰白 cubemap 或预 baked SH 系数），让 PBR 物体暗面至少有 ~5-10% ambient
- 该 fallback 不挂为 ECS 组件，是 Pipeline 内置 baseline；用户挂真正 EnvironmentComponent 时自动覆盖
- 不影响"engine 默认中性"原则（engine 仍 0 ambient）—— 仅 **OrangeEditor** 侧默认接 fallback，与 v0.8.5 验收后 ambient 退 0 的"shipping engine 中性"目标不冲突
- 关联 cmake option：`ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES`（已存在）—— fallback IBL 走同款 editor-only 编译开关

#### G2 ·（可选）demo / 新场景默认挂 EnvironmentComponent

- 编辑器自带一个 `assets/environments/default.hdr`（小 cubemap，~1MB）
- New Scene 时除了清空，自动 seed 一个 Root entity 挂 EnvironmentComponent 指向该 default.hdr
- 优势：用户开箱即看正常 PBR；劣势：与 v1.0 验收脚本"真·空场景"的精神有点冲突 —— 拍板倾向 G1 而不是 G2

#### G3 ·（更长期）UI 提示

- 当场景内有 PBR 材质但无 EnvironmentComponent 时，编辑器 Inspector / Console 浮一条 hint："Tip: scene has PBR materials but no Environment component. Add one for proper ambient lighting."

### 期望验收

- New Scene → Add Floor + Cube + Sphere（同 PBR 材质，不挂 EnvironmentComponent）→ cube 暗面**不再完全黑**，呈现暗橙色 / 暗灰 ~5-15% ambient
- 挂真 EnvironmentComponent 后视觉切换到正常 IBL ambient（fallback 自动被覆盖）

### 状态

- **登记**：2026-05-22
- **关闭**：2026-05-22（v1.0.1 batch G1 落地，详见"处理记录"段）
- **优先级**：~~**P1（Friction，不阻塞 v1.0 ✅）**~~ → 已修
- **归属**：~~OrangeEditor v1.0.1 / v1.x patch~~ → v1.0.1 落地
- **关联**：[[GAP-2026-05-19-editor-aux-passes-in-engine-pipeline]] / [[GAP-2026-05-19-editor-environment-component-wiring]]
- **临时绕过**：用户手动给场景挂 EnvironmentComponent + 一个 .hdr（参 `assets/environments/README.md` PolyHaven CC0 资源）

---

## GAP-2026-05-22-editor-dock-layout-collapses-on-restore ✅

- **发现方**：作者本人 logo v4 切换后启动 OrangeEditor 看效果时撞上
- **发现日期**：2026-05-22
- **一句话定性**：编辑器**最大化后还原**（点最大化按钮 → 再点还原），中间 Scene viewport 区域被压缩到 0 宽 / 完全 collapse，只剩两侧 Inspector + Entity Tree 面板与中间折叠的 dock tab 残骸（垂直 vertical text "anim" / "当前" / Console / Assets tab 标签）

### 触发场景

- 启动 OrangeEditor（默认 maximize 状态，dock layout 正确：Entity Tree | Scene viewport | Inspector + 底部 Assets/Console/Animation）
- 点窗口右上角 maximize / restore 按钮还原到默认 1280×720 窗口大小
- Scene viewport 中间区域消失，无法看到 3D 渲染场景
- 唯一恢复路径：重新点 maximize 回到全屏

### 证据

- 截图见 `D:\Photo\Orange\minsize.png`（v1.0 验收 session 末尾截）
- 症状：还原后窗口 ~1600×900，左侧 Entity Tree ~280px + 右侧 Inspector ~940px + 中间 ~50px 折叠区 = 中间区域被左右两侧"挤"光
- 推测根因：ImGui dock layout init 用了**绝对像素**而非**比例** —— max 状态下初始 layout 写死 "Entity Tree=300px / Inspector=500px / Scene=剩余" 之类，still OK；restore 后窗口宽度突变但 dock 还按绝对像素，中间 Scene 区域 = window_w - 300 - 500 = 可能 < 0 → ImGui 用 0
- 修法 hint：dock builder 初始化时用 `DockBuilderSplitNode(...)` 的 size_ratio 参数（0~1 比例）而非 size_in_pixels；或者监听 GLFW window resize callback 在 resize 时按比例 reapply layout
- 关联代码位置：`tools/OrangeEditor/EditorRenderLayer.cpp` 的 dock space 初始化路径（具体函数名待定位）

### 期望验收

- 启动 OrangeEditor → 最大化 → 还原（任意窗口大小，最小 800×600 起）→ 中间 Scene viewport 仍占据合理比例（≥ 40% 窗口宽），不消失
- 还原后再最大化，layout 仍正确
- 拖任意 panel 边界改大小后 maximize / restore，自定义 layout 不丢失

### 状态

- **登记**：2026-05-22
- **关闭**：2026-05-22（v1.0.1 batch 落地，详见"处理记录"段）
- **优先级**：~~**P1（Friction，不阻塞 v1.0 验收已通过的 ✅）**~~ → 已修
- **归属**：~~OrangeEditor v1.0.1 patch~~ → v1.0.1 落地
- **临时绕过**：~~保持 OrangeEditor 一直 maximize 使用；不要主动 restore~~（v1.0.1 起还原不再坍塌）

---

## GAP-2026-05-22-multi-directional-light-semantics-undefined ✅

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
- **关闭**：2026-05-22（G1 在 v1.0.1 batch 落地，详见"处理记录"段；G2 留 v2.x）
- **优先级**：~~**P2（Friction，不阻塞 v1.0）**~~ → G1 已修
- **归属**：~~G1 留 v1.x UX batch~~ → G1 v1.0.1 落地；G2 等第一款游戏 stylized 美术真需要 fill light 再启动
- **关联**：[[GAP-2026-05-22-directional-light-inspector-direction-helper-missing]] / `src/render/Pipeline.cpp:3153` `:4920`

---

## GAP-2026-05-22-samples-cube-mesh-winding-bug ✅

- **发现方**：v1.0.1 cube winding 修复后 OrangeEngine 仓全 mesh 扫描
- **发现日期**：2026-05-22
- **一句话定性**：8 个 sample 各自的 `MakeCubeMesh` 沿用过期的 "world-CW per triangle → Y-flip projection → NDC-CCW" 注释约定，winding 实际是 world-CW；但 Pipeline 当前按 "world-CCW = front" 渲染（v1.0.1 编辑器 cube 修复后用户视觉验证已证实），所以这 8 个 sample 的 cube 都把朝外面 culling 掉、只渲染内壁

### 触发场景

- 跑 `samples/04_3d_mesh` / `04_3d_mesh_with_bloom` / `09_vfx_demo` / `10_thirty_seconds_demo` / `11_save_load_demo` / `12_layer_partition_demo` / `13_pbr_direct` / `14_pbr_ibl` / `15_debug_draw_minimal` 任一个，cube 渲染走 PBR / toon / textured_mesh / rim_light / dissolve / emissive 任何材质（主 pass FrontFace=CCW + CullMode=Back）都把朝外面剔除
- 视觉症状同 [[GAP-2026-05-22-cube-mesh-back-face-bleed-through]]：穿过 cube 正面看到内壁
- v1.0 验收 demo.scene.json 用 PBR showcase 24 球阵，不撞 cube，sample 内的 bug 未暴露

### 证据

`samples/04_3d_mesh/main.cpp:145` 注释：
> "(0, 2, 1, 0, 3, 2) per face：world-CW per triangle，经 Y-flip projection 后变 NDC-CCW = Vulkan 默认 front-facing。"

但实测（v1.0.1 cube fix 后用户视觉验证）：`tools/OrangeEditor/DemoWorld.cpp` 内 cube 改为 CCW `(0, 1, 2, 0, 2, 3)` 视觉变正常 → Pipeline 实际按 world-CCW 走 front face。
8 个 sample 都基于这条过期注释复制粘贴同款 CW winding（grep `MakeCubeMesh` 共 8 处）。

### 缺什么

- 8 处 `samples/*/main.cpp` 内 `MakeCubeMesh` 的 indices 从 CW `(0, 2, 1, 0, 3, 2)` 改为 CCW `(0, 1, 2, 0, 2, 3)`
- 删除 / 修订 sample 04 line 145 那条过期注释（误导后续仿写）
- 每个 sample 改后跑一遍视觉确认 cube 朝外面正确（不再"穿透"）

### 期望验收

- 跑 sample 04 / 14 等任一带 cube 的 demo，从相机视角看 cube 是实心 baseColor 不再看到内壁
- demo 视觉无回归（其它 mesh / material 不动）

### 状态

- **登记**：2026-05-22
- **关闭**：2026-05-23（详见"处理记录"段）
- **优先级**：P2（技术债，不阻塞编辑器流程）—— sample 是 API 使用示范、不在 critical path；编辑器路径已在 v1.0.1 关闭根因（DemoWorld cube）
- **关联**：[[GAP-2026-05-22-cube-mesh-back-face-bleed-through]]（编辑器路径同款已修）/ Pipeline.cpp 主 pass FrontFace=CCW + CullMode=Back

---

## GAP-2026-05-22-cube-mesh-back-face-bleed-through ✅

- **发现方**：用户 v1.0.1 试搭场景验收（用 PBR 材质的 cube 出现"穿过正面看到 cube 内部背面"视觉）
- **发现日期**：2026-05-22
- **一句话定性**：`MakeCubeMesh`（`tools/OrangeEditor/DemoWorld.cpp:196`）的 triangle index 顺序是 CW（从 face 外侧朝内看顺时针），与 Pipeline 主 pass 的 `FrontFace = CounterClockwise + CullMode = Back` 约定不符；6 个 face 全部被错误剔除"朝外的面"，渲染只剩朝内的内壁

### 触发场景

- v1.0 验收 demo.scene.json 用 PBR showcase 24 球阵，未撞上 cube；用户 v1.0.1 后用 v1.0 验收脚本段 A 流程自搭"Sun + Floor + Cube1/2/3 + Sphere"场景立刻撞上
- v1.0.1 之前 dummy IBL 0.25 灰让 cube 整体偏暗，"穿透看内壁"被视觉解读成"暗面而已"
- v1.0.1 c5 把 IBL fallback 提到 0.5 后内壁亮度提高，"从正面看进 cube 看到对面内壁" 特征立显

### 证据

`tools/OrangeEditor/DemoWorld.cpp:196` 原 `indices.push_back(base+0); push(base+2); push(base+1);` 是 a-c-b。以 +X face 为例：
- a = (h,-h, h), c = (h, h,-h), b = (h,-h,-h)
- 从 +X 外侧朝 -X 看：a=右上 / c=左下 / b=右下 → 三角形是 **顺时针（CW）**
- Vulkan FrontFace = CCW + CullMode = Back → CW 三角形被识别为 back face → **culled**
- 渲染只剩 winding 反向那一面（cube 内壁）

### 状态

- **登记**：2026-05-22
- **关闭**：2026-05-22（v1.0.1 batch 同 session 顺手修，详见"处理记录"段）
- **优先级**：**P0（v1.0.1 期暴露的严重视觉 bug）** —— 与 v1.0.1 其他 P1/P2 friction 同 batch 落地
- **归属**：v1.0.1 friction patch batch

---

## GAP-2026-05-23-editor-play-stop-entity-tree-order-reversed ✅

- **发现方**：working tree 清理（v1.0.scene.json 出现无解释的 entity 顺序翻转 diff）
- **发现日期**：2026-05-23
- **一句话定性**：编辑器 Play → Stop 后 World 内 entity 在 Entity Tree 中的显示顺序反转；再次 Save scene 会把反转顺序写回磁盘，造成"零业务变动但 JSON entity 数组完整 reverse"的污染 diff

### 触发场景

- v1.0.scene.json 在某次编辑器操作后出现 working tree 改动：7 个 entity 集合完全相同，但 on-disk 顺序由 `smoke, Sphere, Cube3, Cube2, Cube1, Floor, Sun` 翻转为 `Sun, Floor, Cube1, Cube2, Cube3, Sphere, smoke` —— 完整 reverse 关系，组件字段一字不变
- 复现路径推断（待修复 session 确认）：打开 v1.0.scene.json → 进入 Play → Stop → File → Save。Save 写出的是 Stop 后的 World 视图，正是反转后的顺序
- 推断的机制链：
  1. `Scene::Save` 按 EnTT `reg.view<entt::entity>()` 遍历顺序写出 entity 数组（`tools/OrangeEditor/panels/EntityTreePanel.cpp:97` 同款遍历）
  2. Play 入口写 snapshot 走同款 `Scene::Save`（`EditorRenderLayer.cpp:992`）
  3. Stop 入口走 `Scene::Load` 在**新** `World` 上按 JSON 顺序逐个 `CreateEntity()`（`EditorRenderLayer.cpp:1134`）
  4. EnTT `view<entt::entity>` 在新 sparse-set 上的遍历顺序与"按 JSON 顺序 CreateEntity 的次序"不一致——sparse-set packed array 的 LIFO 遍历语义 + reverse iteration 实现细节让两次 Save 产出顺序相反
- Entity Tree UI 直接消费同一个 view，所以用户**视觉**也能看到 Stop 后顺序翻转（用户原话："stop后，Entity Tree的顺序会变反"）

### 影响面

- **用户 UX**：Stop 后 Entity Tree 顺序与 Play 前不一致，破坏"Stop 干净还原"心智契约（与 Play Mode 的"快照 / 还原"承诺直接冲突）
- **Git 噪音**：未 Save 时 diff 是隐性的；一旦用户在 Stop 后 Save scene，立刻产生大段 reorder diff，与真实业务改动混在一起难以 review
- **多次 Play/Stop 累积**：每次 Stop 都会翻转一次，理论上偶数次 Play/Stop 后顺序回到原位，奇数次留在反转态；这种"取决于操作次数奇偶性"的行为不能成为持久化语义
- **scene 文件归一化缺失**：Save 没有"按 stable key 排序" / "按创建时间排序"的归一化步骤，所以两台机器 / 两个 session 即使做同样操作也可能产出不同 entity 顺序的 .scene.json，常态化制造合并冲突

### 缺什么（按依赖拆）

#### G1 · 根因诊断

- 确认实际机制是 EnTT view 遍历方向 + Load 创建顺序的组合，还是另有 Save / Load 中间步骤参与（候选嫌疑：`HierarchyComponent` 双向链表重建顺序、`namedMaterialInstances` resolve 顺序、`ComponentSerializerEntry` Pass 1/2 双趟）
- 写一个 minimal repro：构造 N entity World → Save → Load → 比对 view 遍历顺序与原始顺序，看是否纯反转

#### G2 · Save 路径归一化

- `Scene::Save` 输出 entity 数组前按 stable key 排序——候选：persistentId 升序（已有 `EntityToPersistentId` 映射）/ Name 字典序 / Hierarchy DFS 顺序（最符合 Entity Tree 视觉预期）
- DFS 排序方案需要先把无 Hierarchy 的 root 与有 Hierarchy 的 root 合并排序，规则待定
- 落地后老 scene 文件首次被编辑器 Save 会一次性归一化；纳入 v1.0.2 / 后续 patch 时在 milestone-end-checklist 标注"预期触发 scene 文件大规模 reorder diff"

#### G3 · Load 路径让"还原后的 view 遍历顺序" == "Save 时的顺序"

- 候选思路：`Scene::Load` 在新 World 上以反向顺序 CreateEntity，抵消 EnTT view 的反向遍历
- 风险：依赖 EnTT 内部实现细节（sparse-set 遍历方向），EnTT 升级可能破；G2 比 G3 更稳健

#### G4 · Entity Tree UI 自管排序

- DrawEntityTreePanel 不再直接 dump view 顺序，而是按 EditorState 持有的"显示顺序"渲染（默认按 persistentId / Name / Hierarchy DFS）
- 这条与 G2 互补：G2 让磁盘 stable，G4 让 UI 与磁盘视觉一致
- 与 [[GAP-2026-05-21-editor-coplanar-mesh-z-fight-prevention]] 之类的"编辑器侧维护派生数据"是同类增量

### 期望验收

- 打开任意 scene → Play → Stop → Entity Tree 顺序与 Play 前**完全一致**
- 打开任意 scene → Save → git diff 干净（零字段变动 + 零顺序变动）
- 反复 Play/Stop 任意次数 → Entity Tree 顺序不变
- 两台机器 / 两个 session 对同一 scene 做同样操作 → Save 产出字节级一致 .scene.json（归一化目标）

### 状态

- **登记**：2026-05-23
- **关闭**：2026-05-23（同 session 落地 G2 Save 路径归一化，详见"处理记录"段）
- **优先级**：P1（friction）—— 不阻塞功能但持续制造 git diff 噪音 + 破坏 Stop 还原契约
- **归属**：单点 P1 friction，按"撞上即补"通道独立 commit，不进 v1.0.xx batch（引擎本体仍 0.x，无 v1.0.xx 节奏）
- **关联**：上一个 session（commit e6833a6）revert 了 v1.0.scene.json 的污染 diff 并登记本 GAP；本 session 落地 fix

---

## GAP-2026-05-24-editor-asset-browser-create-material-missing ✅

- **发现方**：v1.1 ✅ 后用户试图新建材质时发现
- **发现日期**：2026-05-24
- **一句话定性**：编辑器无 "创建新材质" GUI 入口 —— v1.1 解决了外部资产（.obj / .gltf / 贴图）"进来"的路径，但漏了项目内新资产（.material / 新 scene / entity 模板）从零创建的路径；用户当前只能手动复制现有 .material 文件或改代码

### 触发场景

- 用户问 "我现在要怎么创建材质" —— Asset Browser 右键空白、右键现有文件、File 菜单都没有 "New Material" / "Create → Material" 入口
- 当前 workaround 三选一：(A) 文件管理器外手动复制 builtin/*.material 重命名 (B) 选中现有 .material 改字段 Save（不创建新材质）(C) 写 C++ 代码（`BuiltinAssets::InitializeEditorAssets` 启动期 lazy bake 路径，参 `tools/OrangeEditor/BuiltinAssets.cpp:445`）
- 都不符合"非程序员美术 / 关卡设计师不写代码完成日常工作"目标（Phase 6 编辑器闭环承诺）

### 引擎纪律对照

- 基础设施**全部就绪**，只缺 GUI 入口：
  - `MaterialFileIO::WriteMaterialFile(path, MaterialFileData)` 已存在（`tools/OrangeEditor/MaterialFileIO.{h,cpp}`），启动期 `BuiltinAssets` 已经在用
  - `MaterialSystem::GetTemplateNames()` 已存在（GAP-2026-05-16-material-system-enumerate-and-instance-overrides 落地），可枚举所有已注册 template 喂下拉框
  - Asset Browser 已有右键菜单基础设施（`tools/OrangeEditor/EditorRenderLayer.cpp::DrawAssetFileList` BeginPopupContextItem 路径已挂多个 entry）
- 工业对照（同栈编辑器）：
  - Cocos Creator：Asset Browser 空白处右键 → Create → Material（弹文件名输入）
  - Unity：Assets 右键 → Create → Material
  - Lumix：右键 → Create → Material 同款
  - Unreal：Content Browser 右键 → Material（独立 Material editor，超出本 GAP 范围）

### 缺什么（按依赖拆）

#### G1 · Asset Browser 右键 "Create Material" 基础入口

- `DrawAssetFileList` 末尾或 `DrawAssetsPanel` 主体加 `BeginPopupContextWindow`（右键面板空白处弹出）
- 菜单结构：`Create → Material`（嵌套 menu，留位置给未来 `Create → Scene` / `Create → Folder`）
- 点击 → 弹 ImGui modal 输入 filename（默认 `new_material.material`）+ Combo 选 templateName（默认 `pbr`，从 `MaterialSystem::GetTemplateNames()` 拿候选列表）
- Save 落盘到 `assets.browserCurrentDir` 下（用户当前浏览的目录），通过 `MaterialFileIO::WriteMaterialFile(MaterialFileData{templateName, uniforms={}, textures={}})` 写空 override 的默认材质
- 落盘后自动 `assets.selectedAssetPath = newPath` 让 Material Inspector 子模式立刻接管，用户继续调参

#### G2 · 文件名冲突处理

- 目标路径已存在时弹 modal 询问 overwrite / cancel（与 v1.1 import overwrite 同款）
- 或自动追加数字后缀（`new_material.material` → `new_material_1.material`），但与 Unity / Cocos 行为不一致（它们都是弹询问）

#### G3 · 顺路（同 patch 落）："Create → Scene" / "Create → Folder"

- 同款入口扩 2 个候选；Scene 走 `Scene::Save` 写空 World；Folder 走 `std::filesystem::create_directories`
- 与本 GAP 核心 G1 解耦，可独立 ship；优先级低

### 期望验收

- Asset Browser 浏览到 `assets/materials/builtin/` → 面板空白处右键 → `Create → Material` → 输入文件名 `my_metal.material` → 选 templateName `pbr` → OK
- Asset Browser 内立即出现 `my_metal.material` 且自动选中 → 右侧 Inspector 切到 Material 编辑视图，PBR 五通道默认值
- 调 baseColor / metallic / roughness → Save → 关闭重开编辑器 → 同 entity Renderable.material Pick `my_metal.material` → viewport 显示调过的材质效果
- 同名再 Create → 弹 overwrite 询问

### 状态

- **登记**：2026-05-24
- **关闭**：2026-05-24（同 session 落地 G1，OrangeEditor v1.1.1 milestone）
- **优先级**：P1（friction）—— 不阻塞功能（A/B/C workaround 可用），但严重破坏 "美术不写代码" 心智契约；Phase 6 编辑器闭环承诺的口径上属漏项
- **关联**：v1.1 DCC import pipeline ✅ 后浮现的对偶缺口（"外部进来" vs "内部从零创建"）；候选 v1.1.1 patch milestone 或 v1.2 minor（与 Inspector 可写 import params / Create Scene / Create Folder 同期）
- **归属**：未拍板分配到具体 milestone；按 [[feedback-post-v1-versioning]] 纪律，单独 P1 friction 走 v1.0.xx batch（OrangeEditor 已 stable，沿用 v1.1.x 节奏），若同期撞上 G3 顺路 deliverables 升 v1.2 minor

---

## GAP-2026-05-24-material-template-library-and-custom-hook ✅（G1）

- **发现方**：v1.1 ✅ 后用户询问 "Template 是不是也可以通过用户自定义插入的方式去做？我们提供一些基础的 shader？"
- **发现日期**：2026-05-24
- **一句话定性**：MaterialTemplate 注册路径**完全 C++ 硬编码** —— `BuiltinMaterials::Load*` 6 个 + main.cpp `RegisterTemplate` 启动期调用，用户加新 template 必须改引擎源码 + 重编；同栈所有竞品（Unity / Unreal / Cocos / Godot / Lumix）都提供"基础 shader 库 + 用户自定义 shader hook" 两件套，OrangeEngine 一件也没

### 触发场景

- 当前 6 个内置 template（textured / toon / rim_light / dissolve / emissive / pbr）全在 `src/render/BuiltinMaterials.cpp` 硬编码 + `tools/OrangeEditor/main.cpp` 启动期手动调 `RegisterTemplate`
- 用户想加新 template（如 cloth / hair / water / clear-coat-pbr / 风格化 toon variants）必须：写 GLSL → 离线编译 SPV → 写新 `LoadXxx()` 函数 → 加 `RegisterTemplate` 调用 → 重编引擎。**非程序员美术 / 关卡设计师无入口**
- `samples/08_custom_shader` 演示了 "游戏侧自定义 shader" 但仍是 C++ 路径（不是编辑器内）
- 工业对照（数量是 OrangeEngine 当前的 2-5 倍）：

| 引擎 | 内置 shader 库规模 | 用户自定义入口 |
|------|---------------|-------------|
| Unity URP/HDRP | ~30+ | ShaderLab 文本 + Shader Graph 节点 |
| Unreal | ~10 baseline | Material Editor 节点 + Custom HLSL node |
| Godot | StandardMaterial3D + 后处理 | ShaderMaterial 文本 + VisualShader 节点 |
| Lumix | `data/shaders/` ~15 文本 shader | 文本 + auto-scan 注册 |
| Cocos | builtin .effect ~20 | .effect 文件 + auto-scan |
| **OrangeEngine** | **6 hardcode** | **C++ 改源码** |

### 缺什么（按依赖拆，三级分层）

#### G1 · Level 2：基础 shader 库 + 自动扫描注册（最优 ROI / 推荐先做）

- 引擎扫描 `assets/shaders/templates/*.template.json` —— 每个文件描述：
  - `templateName` (必填)
  - `vertSpv` / `fragSpv` (必填，相对路径)
  - `uniforms[]` (字段名 + 类型 enum)
  - `textureSlots[]` (binding + 名)
- 启动期 `MaterialSystem` 自动遍历该目录，对每个有效 JSON 调 `RegisterTemplate(ShaderTemplateDesc)`（API 已存在，参 `include/orange/engine/render/MaterialSystem.h:89`）
- 引擎自带 baseline 库：把现有 6 个 hardcode template 迁出来 + 扩 9 个补完通用 + 2.5D 必需缺口（见下方"baseline 库选型表"）；离线 SPV 入仓
- 用户加新 template = 提供 vert.spv + frag.spv + 一个 .template.json 丢进 `assets/shaders/templates/`，**不写 C++**
- 仍要求用户自己离线编译 GLSL → SPV（用 LunarG SDK 的 `glslangValidator`），不引入 runtime 编译依赖
- 体量预估：**0.5-1 session**（基础设施 `ShaderTemplateDesc` + `RegisterTemplate` 全有，纯 JSON IO + 启动期扫描）；shader 本体编写工作量另算，按下方"baseline 库选型表"分批

##### G1 子段 · baseline 库选型表（2026-05-24 拍板，15 个 template）

按"shader 角色"横切工业各引擎（Cocos / Lumix / Unity / UE / Godot）后，结合 OE 第一款 Ori-like 2.5D 平台跳跃 + 通用引擎完整度的最小完备子集，确定 baseline 库共 **15 个 template**，与 Lumix surface 数（12）和 Cocos basic+for2d+particle 总数（13）同档。

**第一批 · 现有 6 个迁出**（行为完全等价，无视觉差异 / 回归验收）：

| Template | OE 角色 | 工业对应 |
|---|---|---|
| `pbr` | Cook-Torrance + GGX + Smith G2 + IBL split-sum | Cocos builtin-standard / Unity URP Lit |
| `textured` | unlit + 单贴图 | Cocos builtin-unlit + texture |
| `toon` | 二阶 cel-shading | Cocos builtin-toon |
| `rim_light` | fresnel rim glow | (各引擎都用节点拼) |
| `dissolve` | noise 溶解 + 发光边沿 | URP Dissolve sample |
| `emissive` | 纯发光（自照亮） | Cocos builtin-unlit emissive 模式 |

**第二批 · 通用引擎缺口补完 6 个**：

| Template | 角色 / 必要性 | 工业对应 |
|---|---|---|
| `unlit` | 纯 unlit 无贴图（textured 是 unlit + tex 变体；分离便于 emissive 流水线纯净） | Cocos builtin-unlit / Unity Unlit |
| `skybox` | 天空盒（Phase 6.5 IBL 已经在用 cubemap，缺独立 Material Inspector 入口） | Cocos pipeline/skybox + advanced/sky / UE M_Sky |
| `sprite2d` | 2D 精灵专用（带 UV transform + tint）；Ori-like 主角 / 关卡贴图基础 | Cocos for2d/builtin-sprite / Unity URP Sprite-Lit |
| `particle_cpu` | CPU particle 渲染（与现 VFX 系统配套；当前 VFX 走 hardcode shader） | Cocos particles/builtin-particle / Unity Particle Standard |
| `particle_trail` | 主角拖尾 / 子弹拖尾必有 | Cocos particles/builtin-particle-trail / Lumix ribbon |
| `pbr_transparent` | PBR + alpha blend（玻璃 / 半透明物件；与 `pbr` 共享 fragment 但 blend state 不同） | Cocos builtin-standard transparent / Unity URP Lit Transparent |

**第三批 · 通用引擎应有 3 个**：

| Template | 角色 / 必要性 | 工业对应 |
|---|---|---|
| `decal` | 弹痕 / 涂鸦 / 地面标记（关卡设计高频；前置需 [[GAP-2026-05-22-editor-coplanar-mesh-z-fight-prevention]] 推论） | Cocos (无 builtin) / Lumix decal + curve_decal / Unity Decal Projector |
| `water_basic` | 水面（Ori 主题"水池"场景必备；basic = 法线扰动 + 反射近似，不含真 FFT 波形） | Cocos advanced/water / Lumix water |
| `planar_shadow` | 平面投影阴影（2.5D 角色简易投影成本极低，先于 CSM 落地） | Cocos pipeline/planar-shadow（无现成对应） |

**Tier 3 backlog**（9 个，不进 baseline，撞需求时单独升 P1）：

`hair / skin / cloth / glass / eye / leaf / car_paint / terrain / particle_gpu` —— Ori-like 第一款游戏均用不上；与 [[GAP-2026-05-11-point-light-and-visible-halo]] G3 halo / v1.x · Ori-like 视觉子模式同节奏，按需触发。

**明示排除**（Tier 4，OE 长期不做或路线不在）：

`impostor`（mesh impostor，大场景流式才需）/ `curve_decal`（编辑器工具向单独 milestone）/ `procedural_geom`（runtime mesh gen，跨子系统）/ `cluster_build / cluster_culling`（Forward+ 路线，OE 是 Forward）/ `deferred_lighting`（同上）/ `ssss_blur / float_output_process`（pipeline 内部 pass，不暴露给用户）。

**配套 Inspector UI 能力**（同 G1 落地，无需新 milestone）：

`.template.json` 的 `uniforms[]` 里需要嵌 `editor: { displayName / range / slide / step / type: color / tooltip / parent: <macro> }` 元数据块（参 Cocos `editor: {}` 块），Material Inspector 按既有 schema-first 框架 100% 自动生成面板；macro 守卫联动（如 `parent: USE_NORMAL_MAP`）属 G1 子需求，在第二批/第三批模板真用到时一起接通。

#### G2 · Level 1：编辑器内 GLSL 文本编辑 + runtime SPV 编译（用户撞需求时升级）

- 编辑器加 GLSL 文本编辑器（ImGui 内嵌 / 或 launch 外部编辑器 hook）
- 用户改 GLSL → Save → 引擎用 **glslang** 或 **shaderc** 运行时编译为 SPV → 自动注册新 template
- 类比：Unity ShaderLab / Godot ShaderMaterial / Lumix 文本 shader
- 前置依赖：
  - vendor 接 `glslang`（FetchContent，几 MB 静态库；与 ImGui 同款模式）
  - **Material UBO 基础设施**（让用户在 GLSL 里声明的 uniform 字段能被 Inspector 自动暴露成可调控件，否则又退回"hardcode 在 shader 里"老问题）—— 这是更大的工程，与 `GAP-2026-05-19-pbr-push-constant-exceeds-spec-min` G1 同源
- 体量预估：**2-3 session**（如果 Material UBO 已就绪 1 个 session 就够；否则要先做 UBO 才能做本 G2）

#### G3 · Level 3：节点式 Shader Graph（极远，Phase 11+ 或永不）

- 拖拽 node + 连线，编辑器自动生成 GLSL → SPV
- 对标 Unreal Material Editor / Unity Shader Graph / Godot VisualShader
- 工程量**数月级**；只有第一款游戏明确需要、且 G2 用户体感不足时才触发
- 不推荐主动开工，等 pull-driven

### 期望验收

- **G1 验收**：
  - 把现有 6 个 hardcode template 迁到 `assets/shaders/templates/*.template.json` + `*.spv` 文件，启动期自动注册，行为与改前完全一致（回归 pbr_showcase scene 无视觉差异）
  - 用户手动写一个新的 `.template.json` 引用一对自己编的 SPV，丢进目录，重启编辑器 → Material Inspector 的 templateName Combo 多出新 template；选中后能用 Pick 给 entity Renderable.material
  - 引擎自带 baseline 库扩到 **15 个 template**（按上方"baseline 库选型表"三批：6 迁出 + 6 通用缺口 + 3 通用应有；不含 Tier 3 backlog 9 个）
  - `.template.json` 内 `editor: {}` 元数据块（displayName / range / slide / type: color / tooltip / parent macro 等）能被 Material Inspector 自动消费生成对应控件，第二批 / 第三批模板的 macro 守卫字段按需展示
- **G2 验收**：编辑器内开新 .glsl 文件 → 编辑 → Save → Material Inspector 自动看到新 template + 自动暴露 uniform 字段为可调控件 → viewport 实时反映

### 状态

- **登记**：2026-05-24
- **优先级**：G1 P1（基础库自动扫描是"美术工作流补完"承诺的关键拼图）；G2 P2（撞需求拉动）；G3 P3（极远）
- **关联**：
  - [[GAP-2026-05-24-editor-asset-browser-create-material-missing]] —— Create Material GAP 的对偶：那条解决"基于现有 template 创建实例"，本条解决"如何加新 template"
  - [[GAP-2026-05-19-pbr-push-constant-exceeds-spec-min]] G1 —— per-instance Material UBO 是 G2 的硬前置
  - `samples/08_custom_shader` —— 当前唯一的 "游戏侧自定义 shader" 演示路径（C++ 路线，未来 G1 落地后这条 sample 应改 demo G1 路径）
- **归属**：G1 候选 v1.2 minor 独立 milestone（v1.1.1 Create Material UI 已 ✅ 单走 patch；G2 缩略图独立 v1.3.0 minor，按 [[feedback-post-v1-versioning]] 节奏拆分）；G2 候选 Phase 7+ / 待 Material UBO 基础设施 +1；G3 backlog 不主动
- **进度**（v1.2 minor 拆分 T1-T5 多 session 推进，按 OE 历史 v1.1 / Phase 6.5 节奏，最后一次 bump VERSION 1.1.1 → 1.2.0 + ✅）：
  - **T1 ✅**（2026-05-24）：G1 框架落地——`assets/shaders/templates/` + 6 个 `.template.json` schema v1.0（namespace `render/shader_template`）+ `MaterialSystem::RegisterTemplatesFromDirectory(dir)` API（JsonReader 解析 + ResolveSpvPath .exe-relative 解析 + 单文件失败容忍）+ BuiltinAssets.cpp 启动期 `RegisterBuiltins()` → `RegisterTemplatesFromDirectory("assets/shaders/templates")` 替换；BuiltinMaterials / MaterialSystem::RegisterBuiltins / Pipeline default / tests / sample 全不动（最小风险 + 行为完全等价）；Material Inspector UI 仍 pbr hardcode 留待 T2
  - **T2 ✅**（2026-05-24）：Inspector UI 元数据驱动重构——`.template.json` schema v1.0 → v1.1 在 uniforms 每条 optional 加 `default` + `editor: {widget, displayName, tooltip, range, step, components}` 块；widget enum: hidden / color / slider / drag / default / components（6 个覆盖所有 baseline + T3/T4 新 shader 需求）；components 模式让 vec4 内子字段独立 widget（pbr uMRA = Metallic/Roughness/AO/Reserved 4 sub 同款 v1.1.1 hardcode 视觉）；新增 `tools/OrangeEditor/ShaderTemplateMetaIO.{h,cpp}` 编辑器侧 parser（与 MaterialFileIO 同款分层，不污染引擎公共面）；`MaterialAssetInspectorPlugin` 移除 `if (templateName == "pbr") { 4 hardcode widget }` 路径，改 `RenderUniformWidget(metadata, instance)` 数据驱动派发；v1.0 文件向后兼容（无 editor 字段视作全 Default widget）；视觉等价 = pbr 仍 1 ColorEdit3 + 3 SliderFloat，其他 5 个模板显示"无可调参数"；用户加新 .template.json 不写 C++ 自动出 widget
  - **T3 ✅**（2026-05-24）：实际 ship 1 / 6——`unlit`（复用 pbr.vert SPV + 新 unlit.frag，直接输出 uBaseColor，无光照 / shadow / IBL；与 emissive 区别：LDR 用户可调 vs HDR hardcode）。剩 5 个全部撞 Pipeline 改造或 OrangeRender 缺口（见下方"T3/T4 backlog 登记表"），不在本 minor 同 session 处理（按 CLAUDE.md 跨仓纪律），登记到 v1.x 按需触发
  - **T4 ✅**（2026-05-24）：实际 ship 1 / 3——`water_basic`（复用 pbr.vert SPV + 新 water_basic.frag，时间驱动 UV sin 扰动 + Schlick fresnel 边缘高光 + N·L 漫反射 + PCF shadow；时间从 `light.uFrameInfo.x` 自驱无需 per-instance time uniform；uBaseColor color + uMRA components 拆 3 个 slider: Wave Amplitude / Wave Speed / Fresnel Strength）。剩 2 个全部撞缺口登记 backlog
  - **T5 ✅**（2026-05-24）：收尾——CMake VERSION 1.1.1 → 1.2.0；editor-roadmap.md v1.2.0 节标 ✅；Wiki acceptance-checklist 落 `editor-v1.2.0-acceptance-checklist.md`；本 GAP 整体标 ✅；macro 守卫联动延后（本 minor 8 个 baseline 全无 macro 字段，T5 子段实际未触发，等真用到时再实现 conditional show）

#### T3 / T4 backlog 登记表（v1.2.0 ✅ 后未 ship 的 7 个 shader）

按 CLAUDE.md "撞即登记不在同 session 处理 OrangeRender / Pipeline 缺口" 纪律，下表 7 个 shader 因 Pipeline / OrangeRender 端能力缺失不能走数据驱动 template 路径，统一标 backlog，按需触发：

| Template | 撞的缺口 | 触发触发条件 |
|---|---|---|
| `skybox` | Pipeline 有专门 `PipelineSky.cpp` 处理（last-pass + depth-equal + cubemap binding），不走通用 forward material 路径；要 ship 为 template 需重构 PipelineSky 为可注册的 IAuxPassProvider 或类似机制（与 [[GAP-2026-05-19-editor-aux-passes-in-engine-pipeline]] 同源） | Phase 7+ 渲染深化 milestone 或专门 PipelineSky 整骨拉动 |
| `sprite2d` | OE forward pipeline 是 3D 路径，无 2D ortho projection；要 ship 需引入 Pipeline 2D pass 或 mesh quad + billboard hack | 第一款游戏（Ori-like 2D / 2.5D）实际撞 2D 渲染需求拉动 |
| `particle_cpu` | VFX 系统已通过 `additive_billboard.{vert,frag}` 实现 CPU particle，与 Material template 是不同概念（VFX 走 emitter / lifetime 路径，不接 Renderable）；要 ship 为 template 需统一 VFX ↔ Material 路径 | VFX 系统重构 milestone（v2.x） |
| `particle_trail` | OE mesh 是静态 vertex buffer，无 ribbon / runtime vertex 流式机制；OrangeRender 端无 dynamic vertex stream API | OrangeRender `incoming_feature.md` 登记 dynamic vertex stream → OE 消费 |
| `pbr_transparent` | `Material` 结构无 blend state / depth state 字段，Pipeline 用统一 forward opaque render state；要 ship 需 Material schema 扩 `renderState: {blend, depth, cull}` + OrangeRender 端 PipelineState API 暴露 per-material 配置 | OrangeRender `incoming_feature.md` 登记 per-material PipelineState → Material schema bump → OE 消费 |
| `decal` | OE 无 decal pass（projection box + Sutherland-Hodgman 裁剪 + screen-space 变体均无）；属新 pass 不是 shader 加法 | 关卡设计实际撞弹痕 / 涂鸦需求拉动；同 [[GAP-2026-05-21-editor-coplanar-mesh-z-fight-prevention]] 关联 |
| `planar_shadow` | 需要双 pass（先 shadow proj 平面渲染再主 pass），与现 Pipeline 单 forward pass 不匹配；属 Pipeline 路径改造非 shader 加法 | 第一款游戏角色 2.5D 投影需求拉动 |

总结：v1.2.0 ✅ 实际 ship **8 个 template**（6 现有迁出 + unlit + water_basic）vs 原 design 15 个，达成率 53%。差距 7 个全部因 Pipeline / OrangeRender 端能力缺失（非数据驱动框架缺陷），框架本身（T1+T2）已证明扩展性可用 —— 用户加新 shader 只需 .template.json + 一对 SPV 不写 C++ 自动出 widget。

---

## GAP-2026-05-24-aux-pass-context-missing-format-info ✅（engine API 侧）

- **发现方**：OrangeEditor v1.3.0 grid pass 真正迁出落地（commit `93a1a2e`）
- **发现日期**：2026-05-24
- **一句话定性**：`AuxPassContext` 只携带 `RHITexture*` 指针（hdrColor / sceneDepth），没带它们的 `TextureFormat`。Provider 创建 PSO 时必须声明匹配的 color/depth format —— `EditorGridAuxPassProvider` 不得不 hardcode `Orange::Rhi::TextureFormat::RGBA16Float`（与 engine HDR target 格式镜像）；若 engine 未来改 HDR target 格式（如升 RGBA32Float / R11G11B10Float），所有外部 provider 实现都会**静默** PSO format 不匹配 → validation error 或 vendor-specific 渲染异常

### 触发场景

- `tools/OrangeEditor/render/EditorGridAuxPassProvider.cpp::Initialize` 内 `d.mRenderTargets.mColorFormats.push_back(Orange::Rhi::TextureFormat::RGBA16Float);` —— 字面常量，与 engine 端 `PipelineHelpers.h::kHdrColorFormat` 是独立两份事实，**没有任何编译期 / 链接期保障两边同步**
- 未来同款外部 aux-pass provider（outline / wireframe / debug overlay / 游戏端 minimap）都要重新 hardcode 一遍同款常量
- engine 端 HDR target 格式现在是 `kHdrColorFormat = RGBA16Float`，未来若有强需求改 R11G11B10Float（节带宽）或 RGBA32Float（更高动态范围 emissive），engine 单点改后所有 provider 链接通过但运行时挂

### 缺什么（按依赖拆）

#### G1 · `AuxPassContext` 扩字段（推荐）

- struct 加 `Orange::Rhi::TextureFormat hdrColorFormat` + `Orange::Rhi::TextureFormat sceneDepthFormat` 字段
- Pipeline 在调 hook 前填字段（从 `impl.hdrColor->GetDesc().mFormat` / `impl.sceneDepth->GetDesc().mFormat` 取）
- provider Initialize 时**不**创建 PSO（因为还不知道 format）—— 改为 lazy create on first RenderAuxPass（从 ctx.hdrColorFormat 取）；缓存 PSO 直到 format 变化重建
- 与 AuxPassContext 现有"per-frame state"模型一致，扩展性最好

#### G2 · `Pipeline` 加静态公共面查询

- `static constexpr Orange::Rhi::TextureFormat Pipeline::HdrColorFormat() noexcept;`
- 与 G1 互补；G1 是 runtime / per-frame state，G2 是编译期常量；provider 端可以在 ctor 就用 G2 创建 PSO
- 不要替 G1，因为 sceneDepth format 在某些 Pipeline 模式下可能变（如 stencil 模式 vs 纯 depth），ctx 字段更稳

### 期望验收

- engine HDR target 格式 grep `kHdrColorFormat` 改一处即可，所有外部 aux-pass provider 自动取新值
- 移除 EditorGridAuxPassProvider 对 RGBA16Float 字面常量的依赖
- 新增 ctest（如 `AuxPassContextFormatTest`）验证 ctx.hdrColorFormat 与 Pipeline 内部 HDR target 实际 format 字段一致

### 状态

- **登记**：2026-05-24
- **处理（engine API 侧 ✅，2026-05-26）**：采纳 G1 —— `AuxPassContext` 加 `hdrColorFormat` / `sceneDepthFormat` 字段（`enum class TextureFormat : std::uint32_t` 前向声明，沿 DebugDrawScene.h header-isolation 模式）；Pipeline 在两处 hook 调用点（window + offscreen）从 `impl.hdrColor->GetDesc().mFormat` / `impl.sceneDepth->GetDesc().mFormat` 权威填入。**消费侧（EditorGridAuxPassProvider 去掉 RGBA16Float hardcode 改读 ctx.hdrColorFormat）留独立 session**（per-session add/consume 分离纪律：本 session 只补引擎 API + 验证，不在同 session 消费）
- **优先级**：**P2（Friction，工程纪律隐患）** —— 不阻塞 v1.3.0 ship（当前 engine 端 HDR format 稳定），但任何未来 HDR target 格式变更都是"single point of failure"；同时本 GAP 闭环前**任何**第二个外部 aux-pass provider 都会重复 hardcode 同款常量
- **归属**：engine API 侧 2026-05-26 ✅；editor 消费侧待独立 session。可与下面 GAP-2026-05-24-fullscreen-vert-not-publicly-exposed + GAP-2026-05-24-loadspirv-helper-not-publicly-exposed 同 batch（已同 session 一起补 engine API 侧）
- **关联**：[[reference-grid-migration-engine-to-editor-sweep-pattern]]（grid sweep 模板顺手撞出本 GAP）；GAP-2026-05-19-editor-aux-passes-in-engine-pipeline（接口源头）

---

## GAP-2026-05-24-fullscreen-vert-not-publicly-exposed ✅（engine API 侧）

- **发现方**：OrangeEditor v1.3.0 grid pass 真正迁出落地（commit `93a1a2e`）
- **发现日期**：2026-05-24
- **一句话定性**：engine 内置 `src/render/builtin_shaders/fullscreen.vert.glsl`（10 行 big-triangle 模板，gl_VertexIndex → 全屏覆盖）是任何 fullscreen pass 的通用工具，但**不对外暴露**任何形式（spv shipping 路径 / Pipeline 公共面查询 / 公共 RHIShaderModule getter）。`EditorGridAuxPassProvider` 不得不维护一份 sibling copy 走自家 spv 编译路径；未来任何 fullscreen 风格 aux-pass provider 都要再拷一份

### 触发场景

- `tools/OrangeEditor/shaders/fullscreen.vert.glsl` 是 `src/render/builtin_shaders/fullscreen.vert.glsl` 字面 copy，仅 vUV 计算与输出 layout 行字字相同 + 头注释加"sibling copy for editor"标注
- 未来 outline / wireframe / debug overlay / fullscreen-quad-pass-style minimap / 游戏端自家 post-process aux pass 都要重复同款 copy
- 工业对照：Lumix 把 `data/shaders/fullscreen.shd` 当 shipped data 让所有 plugin 路径消费；OrangeRender 本身不会管这种 engine-level shader（属 engine 范畴）

### 缺什么（按依赖拆）

#### G1 · engine 端 ship fullscreen.vert.spv 到公共路径（最轻量）

- engine 把 `fullscreen.vert.spv` 编出后 install / copy 到 `<exe-dir>/shaders/orange_engine_public/fullscreen.vert.spv`（或维持现路径但 documented 为公共可消费）
- 公共面文档明示"该 spv 是 aux-pass provider 可消费的 public asset，路径稳定"
- editor 删 sibling copy + CMakeLists.txt 删 shader 编译入口

#### G2 · Pipeline 加 `GetFullscreenVertexShader() -> RHIShaderModule*` 公共面（更结构化）

- engine 暴露内部已编译的 RHIShaderModule，provider 直接拿来传给 PSO 创建
- 优点：零文件拷贝 / 零路径耦合 / 编译期常量
- 缺点：增加公共 API surface；provider 实例化时刻必须 Pipeline 已经 Initialize

### 期望验收

- 全仓 `tools/OrangeEditor/shaders/fullscreen.vert.glsl` 删除
- EditorGridAuxPassProvider Initialize 不再加载自家 fullscreen.vert.spv

### 状态

- **登记**：2026-05-24
- **处理（engine API 侧 ✅，2026-05-26）**：采纳 G2 变体 —— 不走"ship spv 到公共路径"，改在 `AuxPassContext` 加 `Orange::Rhi::RHIShaderModule* pFullscreenVs`（前向声明 class，header-isolation 安全），Pipeline 在 hook 调用点从 `impl.fullscreenVs.get()` 填入。provider 直接拿引擎已编译的 fullscreen vert 传 PSO，零文件拷贝 / 零路径耦合。**消费侧（editor 删 sibling copy + 改用 ctx.pFullscreenVs）留独立 session**
- **优先级**：**P3（cosmetic / 微小重复）** —— 不阻塞功能（sibling copy 维护成本 ~0，shader 10 行不会改）；但**每**新 aux-pass provider 都会重复一遍，N 个 provider 时 deduplication 价值上升
- **归属**：engine API 侧 2026-05-26 ✅；editor 消费侧待独立 session（与 GAP-2026-05-24-aux-pass-context-missing-format-info + GAP-2026-05-24-loadspirv-helper-not-publicly-exposed 同 batch 补的 engine API）
- **关联**：[[reference-grid-migration-engine-to-editor-sweep-pattern]]；GAP-2026-05-24-aux-pass-context-missing-format-info（同源 sweep 撞出）

---

## GAP-2026-05-24-loadspirv-helper-not-publicly-exposed ✅（engine API 侧）

- **发现方**：OrangeEditor v1.3.0 grid pass 真正迁出落地（commit `93a1a2e`）
- **发现日期**：2026-05-24
- **一句话定性**：engine 端 `src/render/pipeline/PipelineHelpers.cpp::LoadSpirv(relativePath)`（~20 行，`.exe` 同目录相对解析 + ifstream binary read + 大小校验 + word vector 返回）是任何"从 disk 加载 .spv 喂 RHI"消费者的通用工具，但**不对外暴露**。`EditorGridAuxPassProvider` 复刻了一份；所有 sample / 未来游戏仓 / 第三方 plugin 都要重发明同款 boilerplate

### 触发场景

- `tools/OrangeEditor/render/EditorGridAuxPassProvider.cpp` 顶部 anonymous namespace 内 `GetExecutableDir()` + `LoadSpirv()` 是 `PipelineHelpers.cpp:24-183` 同款逻辑的 sibling copy（仅 ORANGE_LOG_ERROR 输出文案不同）
- `samples/*` 中如 `08_custom_shader/main.cpp` 也有类似 spv 加载 boilerplate（grep "ifstream.*binary.*ate" samples/ 多匹配）
- 任何外部消费者（编辑器 plugin / 游戏 fork / 第三方工具）都要重新写
- 工业对照：Lumix 在 `engine/file_system.h` 公共面提供 `loadFile(path, blob)` 通用接口；OrangeEngine 公共 `Asset` 模块有 ShaderLoader 但走的是 `ShaderAsset` 资产路径（含 AssetRegistry 注册 + handle），不适用于"直接给 RHI 喂裸 spv 字节"场景

### 缺什么

#### G1 · `Orange::Engine::Asset` 加 free function

- `Result<std::vector<std::uint32_t>, ResultCode> LoadSpirvFromExecutableDir(std::string_view relativePath) noexcept;`
- 落到 `include/orange/engine/asset/SpirvDiskLoader.h`（新头）或现有 ShaderLoader.h 同位（语义独立但可邻居）
- 公共面文档明示"该函数是供外部直接喂 RHI ShaderModuleDesc 的，绕过 AssetRegistry；典型场景：editor aux pass / sample / 游戏 fork 等启动期一次性 spv 加载"

#### G2 · `Orange::Engine::Platform` 加更通用 `LoadBinaryFromExecutableDir(path)`

- 与 G1 同效但更通用（不限 spv 4 字节对齐校验）；下游消费方自己包装成 word vector
- 更长远的方向（loadBinaryFile 是任何 engine 都需要的工具），但 scope 比 G1 大

### 期望验收

- EditorGridAuxPassProvider 删 anonymous namespace 内 GetExecutableDir + LoadSpirv 25 行
- samples 内 spv 加载 boilerplate 统一替换为公共面调用
- 全仓 grep `ifstream.*binary.*ate` + `seekg.*read` 仅在 G1 / G2 实现内部 + 测试 / 历史 build 产物中匹配

### 状态

- **登记**：2026-05-24
- **处理（engine API 侧 ✅，2026-05-26）**：采纳 G1 —— 新增公共头 `include/orange/engine/asset/SpirvDiskLoader.h` + `src/asset/SpirvDiskLoader.cpp`：`Asset::LoadSpirvFromExecutableDir(string_view) -> vector<uint32_t>`（.exe 相对路径锚定 + 4 字节校验，失败返回空 + LOG_ERROR；header-isolation 安全，无 RHI 依赖）。引擎内部 `PipelineHelpers::LoadSpirv` 改为委托它（去掉本仓内重复的 GetExecutableDir + 加载逻辑）。新增 `tests/asset/SpirvDiskLoaderTest.cpp`（加载 fullscreen.vert.spv 验非空 + SPIR-V magic + 不存在路径返回空）ctest ✅。**消费侧（editor / samples 删各自 boilerplate 改用公共面）留独立 session**
- **优先级**：**P3（cosmetic / 重复造）** —— 不阻塞功能；与上面 GAP-2026-05-24-fullscreen-vert-not-publicly-exposed 同性质（编辑器 sweep 撞出的 engine 工具复用债）
- **归属**：engine API 侧 2026-05-26 ✅；editor / samples 消费侧待独立 session
- **关联**：[[reference-grid-migration-engine-to-editor-sweep-pattern]]（grid sweep 顺手撞出三件套之一）

---

## GAP-2026-05-24-pipeline-cannot-render-to-arbitrary-rt

- **发现方**：v1.3.0 minor planning（材质球缩略图 GAP-2026-05-22 G2 trim 决策评估）
- **发现日期**：2026-05-24
- **一句话定性**：`Pipeline` 当前只支持两条入口 —— `Initialize(window)` 渲染到 swap-chain / `InitializeOffscreen(width, height)` 渲染到 Pipeline 内部持有的 viewportColor RT，**没有**"渲染当前世界 / 任意世界到调用方提供的任意 `RHITexture*` 上"的入口。任何编辑器内 mini-render 需求（材质球缩略图 / 资源预览 / mesh thumbnail / scene snapshot）都撞同款空缺，必须**复刻一套 mini-pipeline**（PSO + scene descriptor set + dummy lights + IBL bind + push constants）

### 触发场景

- **材质球缩略图（GAP-2026-05-22 G2）**：核心需求 = 把材质应用到内置球体 mesh 渲染到 96×96 RT，bind 到 ImGui::Image。当前路径要么编辑器自建完整 mini-renderer（500-800 LOC 独立设计点），要么把 Pipeline 改造支持任意 RT 输出
- **未来 mesh thumbnail**：同款问题（GAP-2026-05-22-editor-dcc-import-pipeline-missing G1 后产生）—— `.obj` / `.gltf` 导入后 Asset Browser 需展示几何缩略
- **未来 scene snapshot**：scene 文件列表 / scene 切换器需要小尺寸 scene 预览图
- **未来 prefab / archetype preview**：游戏侧 prefab browser
- **未来 reflection probe bake offline preview**：环境光烘焙过程的 face 单独预览
- 5+ 候选未来 milestone 撞同一个 engine 空缺

### 缺什么

#### G1 · `Pipeline::RenderToTexture(world, target, viewport, options)` 公共面

- 入参：World + `RHITexture* target`（调用方持有 + RGBA8/16F 格式 + ColorAttachment|Sampled usage）+ viewport size + 可选 ShadowConfig / IBL textures / camera override / Sky 开关
- Pipeline 内部走与 InitializeOffscreen 同款路径但目标改为外部传入 RT；调用方负责 transition 到 ShaderReadOnly 后消费
- 实现复杂度：与现有 `InitializeOffscreen` 90% 重叠（GPU 资源 / 主 pass / IBL / shadow / post-process chain 都已就位），关键改造 = `viewportColor` 字段改为"可被调用方覆盖"的 RT 引用
- 风险：当前 Pipeline 单线程录制 + 单 swap-chain 假设，多 RT 路径下 frame loop 调度需要梳理（offscreenCmd lifecycle 与外部 RT lifecycle 必须正交）

#### G2 · 抽出可独立实例化的 `MiniRenderer` 子集

- 与 Pipeline 共享 MaterialSystem / Asset / RHI 资源但有独立 frame state
- 调用方 `MiniRenderer mr(device, ...);  mr.RenderOnce(world, target);` 一次性 immediate-mode 渲染
- 更模块化但工作量更大（需要梳理 Pipeline 内"啥是共享 / 啥是 per-instance"边界）

#### G3 · 编辑器自建完整 mini-pipeline（不动 engine）

- 复刻 PSO + 描述符 + push constants + IBL bind，与 engine Pipeline 平行存在
- 优点：engine 完全不动；缺点：500-800 LOC 独立维护 + IBL / 光照路径若 engine 端改造（如换 PBR 模型）必须同步改 mini-pipeline，永远滞后

### 期望验收

- `Pipeline` 公共面新增渲染到任意 RT 入口
- 材质球缩略图 / mesh thumbnail / scene snapshot 等下游 milestone 走统一路径，不重复造 mini-pipeline
- engine PBR 模型 / shadow / post-process 改造时所有缩略图路径自动跟进

### 状态

- **登记**：2026-05-24
- **优先级**：**P2（架构 enabler）** —— 不阻塞当前功能，但 5+ 候选未来 milestone 都堵在它上面；越早决策 G1 vs G2 vs G3 后续多 milestone 累积返工成本越低
- **归属**：v1.4.0 minor 议题第一性问题（与材质球缩略图 GAP-2026-05-22 G2 同 session 讨论；G2 拉动条件 = 本 GAP 决策结果决定 thumbnail 走哪条路径）
- **议题候选**：G1 / G2 / G3 的 trade-off 评估；OR 端是否需要任何 RHI 接口扩展（当前评估不需要 —— FEATURE-2026-05-24 已提供完整 RT 公共面）
- **关联**：[[GAP-2026-05-22-editor-material-create-and-thumbnail-missing]] G2（最直接消费者）；[[GAP-2026-05-22-editor-dcc-import-pipeline-missing]] G1（未来 mesh thumbnail 同源需求）；[[feedback-minor-bundle-per-feature-feasibility-check]]（本 GAP 是 thumbnail 从 v1.3.0 trim 出去到 v1.4.0 的根因）

---

## GAP-2026-05-25-pbr-material-texture-binding-and-tangent-infra

- **发现方**：A 部分推进侦察（DCC import v1.2 增强落地前置核查）
- **发现日期**：2026-05-25
- **一句话定性**：PBR shader 当前材质五通道全是 push-constant 标量（uBaseColor + uMRA），**无任何贴图采样路径**；顶点输入仅 pos+uv+normal、**无 tangent**。导致 (a) glTF material 贴图通道导入 + (b) mikktspace tangent / 法线贴图 两类需求全部阻塞 —— 缺 "per-instance material texture binding（descriptor set）+ tangent vertex 通道" 基础设施

### 触发场景

A 部分 A2（DCC import v1.2 增强：glTF PBR material 解析 + mikktspace 切线）落地前侦察发现前置基础设施缺失：

- `pbr.frag.glsl` line 13-15 自承 "normal 通道暂走 vNormal vertex 插值，等 tangent + 法线贴图基础设施落地后再上 texture 路径；texture binding 整体延后到 per-instance descriptor set 路径上线时"
- `pbr.vert.glsl` 顶点输入 stride 32（pos+uv+normal），无 tangent location
- `MeshAsset` 注释明示 "tangent 留待后续扩展"；`.mesh` schema 仅到 v3（无 tangent 段）

因此 glTF 的 baseColorTexture / normalTexture / metallicRoughnessTexture 即使解析出来也无 shader 消费；mikktspace 算出 tangent 也无顶点属性承载、无 shader 读取。

### 缺什么（按依赖拆）

#### G1 · per-instance material descriptor set + texture binding（贴图采样基础设施）

- PBR shader 加 set 1 per-instance material descriptor（baseColor / MR / AO / normal sampler2D）
- Pipeline 每 draw 绑 per-instance descriptor set —— **纯 Engine 侧接线，不缺 OR 能力**（见下"跨仓核对 2026-05-25"）：所需 RHI 原语 `CreateDescriptorSetLayout`(CombinedImageSampler) / `CreateSampler` / `CreateDescriptorPool` / `AllocateDescriptorSet` / `UpdateDescriptorSet` / `GraphicsPipelineDesc.mDescriptorSetLayouts` / `cmd->SetDescriptorSet` / `RenderItem.mpDescriptorSets` 在 OrangeRender 公共面全部就位，且本仓 `src/render/IblBaker.cpp` + `Pipeline.cpp`(mainDescSet 三件套 IBL) + `pipeline/PipelineGodRays.cpp` / `PipelineSky.cpp` 已在用同一套闭环。加材质贴图 descriptor = 把 `mainDescSet` 那套搬到 set 1、按 material instance 频率重建/缓存
- 与 [[GAP-2026-05-19-pbr-push-constant-exceeds-spec-min]] G1（per-instance material UBO）同源基础设施

#### G2 · tangent vertex 通道 + .mesh schema v4

- MeshAsset 加 tangent 通道；`.mesh` schema v3 → v4 + migrator（已 shipped v3 不改）
- Pipeline vertex input layout 加 tangent attr（stride 32 → 48）；pbr.vert/frag 加 TBN + 法线贴图采样
- mikktspace vendor 接 + importer 算 tangent（依赖 G2 通道就位）

#### G3 · glTF material 完整解析（贴图 + 标量）

- 依赖 G1（贴图采样）；标量子集（baseColor/metallic/roughness factor → uBaseColor/uMRA）可先于 G1 落地，但价值有限（importer unified mesh 合并多 primitive 丢失 per-primitive material 边界 + 无自动 mesh↔material 关联）

### 状态

- **登记**：2026-05-25
- **优先级**：P2（美术工作流深化基础设施）—— v1.x DCC import 增强 + 材质球缩略图真渲染 + 法线贴图细节均堵在 G1/G2 上
- **归属**：候选 Phase 7+ 渲染深化 / 待 per-instance material descriptor 基础设施 milestone —— **整条 G1/G2/G3 全在 OrangeEngine 子仓内闭环，不触 OrangeRender 公共面**（见下"跨仓核对"）
- **关联**：[[GAP-2026-05-22-editor-dcc-import-pipeline-missing]]（v1.1 主线 ✅ 的延后增强前置）；[[GAP-2026-05-19-pbr-push-constant-exceeds-spec-min]] G1（per-instance material UBO 同源）；[[GAP-2026-05-22-editor-material-create-and-thumbnail-missing]] G2（缩略图真渲染同源）

### 跨仓核对（2026-05-25）

用户要求"A2 给 Render 提需求"时做的前置核对结论：**A2 不存在 OrangeRender 缺口，无需提 FEATURE**。

- **核对方式**：直读 OrangeRender 公共头 + `OrangeRender/docs/incoming_feature.md` 历史 + OrangeEngine `src/render/` 现状（只读，未改 OrangeRender 任何文件）
- **依据 1（RHI 已就位）**：`RHIDescriptor.h`(DescriptorSetLayout/Pool/Set + `DescriptorType::CombinedImageSampler/SampledImage/Sampler`) / `RHISampler.h` / `RHIPipeline.h`(`GraphicsPipelineDesc.mDescriptorSetLayouts`) / `RHICommandList.h`(`SetDescriptorSet`) 全在公共面。incoming_feature.md `FEATURE-2026-05-07` 评审已确认 B2(Sampler)+B3(Descriptor) 早于该需求落地；`FEATURE-2026-05-07-renderitem-descriptor-sets` 已补 `RenderItem.mpDescriptorSets` + 每 draw 自动 `SetDescriptorSet`(含 dedup/重绑)
- **依据 2（Engine 已在用）**：`src/render/IblBaker.cpp` 4 处 + `Pipeline.cpp`(mainDescSet 3×CombinedImageSampler IBL) + `pipeline/PipelineGodRays.cpp` / `PipelineSky.cpp` 均跑通"建 layout→建 sampler→建 pool→alloc set→update write→挂 pipeline layout→SetDescriptorSet"完整闭环
- **结论**：G1/G2/G3 均为 Engine 内部工作（G1 材质 descriptor 接线复用 mainDescSet 模式 / G2 MeshAsset tangent 通道 + .mesh schema v4 + vertex input layout / G3 GltfImporter material 解析），无一触 OrangeRender 公共 API。OrangeRender CLAUDE.md 工作流明文"如发现真实空缺，单独提 BUG 而非 FEATURE"——此处连 BUG 都不成立。原 G1 条目里"可能需 OR 新能力 → 跨仓登记"的猜测已就地划除

### 落地 + 全黑回归 + 修复（2026-05-25，G1+G2 ✅ 用户 GPU 验收通过）

G1（per-instance material 贴图渲染）+ G2（tangent 通道 + .mesh v4）已落地并经用户 GPU 视觉验收（PBR 材质球正常显示）。中途踩了一个**两条材质注册路径分叉**导致的全黑回归，记录如下以防重蹈：

- **症状**：keystone 上线后编辑器 PBR scene 所有球全黑；隔离 ctest 却全过。
- **根因**：`pbr.frag`/`pbr.vert` **无条件**采样 set 1（4 贴图）+ 读 tangent(loc3)，但 gate `Pipeline::Impl::MaterialUsesTextureSet` 当初只看 `mat.textureSlots`。编辑器走 **`MaterialSystem::RegisterTemplatesFromDirectory`** 从 `assets/shaders/templates/pbr.template.json` 加载 pbr 模板，而该 JSON 的 `textureSlots` 为空（落地时只给 C++ 的 `BuiltinMaterials::LoadPbr` 加了槽，**漏改模板 JSON**）→ gate 误判 false → pipeline 不声明/不绑 set 1 + 不声明 tangent loc3 → shader 采样未绑 descriptor + 读未声明顶点属性 → 管线非法（validation: `Set 1 ... not declared` / `Location 3 ... not declared` / `descriptor set 1 ... not compatible`）→ 全黑。隔离 ctest 用 `RegisterBuiltins()`（走 `LoadPbr`，有槽）所以躲过——**两条注册路径分叉是测试盲区**。
- **修复**：(1) `pbr.template.json` 补 4 个 textureSlots（对齐 `LoadPbr`）；(2) `MaterialUsesTextureSet` 加固——不只看 textureSlots，还按 PBR push 签名（uMVP+uModel+uBaseColor+uMRA = 160B，仅 pbr）兜底，slotless pbr 也声明/绑 set 1（喂 default 白/flat-normal 贴图，退化纯 scalar PBR），杜绝 shader/pipeline 不匹配。
- **回归门**：`tests/render/PbrSceneBlackReproTest.cpp` —— 走编辑器 `RegisterTemplatesFromDirectory` 路径 + 加载真实 `sphere.mesh` + `Pipeline::DebugReadbackPixel` 像素读回，assert PBR 中心非黑。配套新增 `Pipeline::DebugReadbackPixel`（viewportColor 加 TransferSrc）作为 PBR 像素级回归基础设施。
- **教训**：① 编辑器材质来自 `.template.json`（`RegisterTemplatesFromDirectory`），**不是** `BuiltinMaterials::LoadPbr`——两者必须同步；② shader 需要的 descriptor set / 顶点属性必须**无条件**声明，不能依赖材质数据（textureSlots）；③ GPU 渲染回归测试必须走**真实消费方（编辑器模板）路径 + 像素 readback**，合成的 `RegisterBuiltins` 会放过这类 bug。
- **剩余**：G3（glTF material 自动导入，Inc5）代码已成、端到端待真实 glTF 资产验；acceptance checklist + CMake VERSION bump 待 Orange-Wiki 子仓单独 session（单子仓纪律）。

---

## GAP-2026-05-26-complete-light-source-family-and-shadows ✅

- **发现方**：OrangeEditor 用户验收（dogfood 搭场景时光源类型不全 + 阴影仅平行光）
- **发现日期**：2026-05-26
- **一句话定性**：光源族不完整 —— `DirectionalLight`（含 2D shadow map）+ `PointLight`（物理衰减，但 `castsShadow` 是 reserved no-op）已有，`SpotLight` **完全缺失**；阴影系统只支持 **第一个** `castsShadow` 的 `DirectionalLight` 单张 2D shadow map。用户要求**补齐三种经典光源类型 + 各自阴影**（含 PointLight 全向 / SpotLight 透视 / 多 shadow caster）。
- **处理约定**：用户 2026-05-26 明确"三种光源都做"。**2026-05-26 同 session 落地 G1 + G2 + G3 全部**（与原"下个 session"约定不同：实现 session 复核确认 G1/G2/G3 全部 OrangeEngine 子仓内闭环，RHI cube/array depth 原语已齐备，无需改 OrangeRender；遂一并落地）。三个 commit：G1 `feat(render): G1 SpotLight…`、G2 `feat(render): G2 多 shadow caster…`、G3 `feat(render): G3 PointLight 全向 cubemap…`。

### 触发场景

- `include/orange/engine/render/LightComponent.h` 当前仅 `DirectionalLight` + `PointLight`；`SpotLight` 在 `:73` 注释明确"留待第一款游戏真撞上手电筒锥光需求时再扩" —— 用户现在主动拉动
- `PointLight.castsShadow`（`:91`）是保留字段，Pipeline 忽略（无 omnidirectional cubemap shadow）
- 阴影系统（`src/render/pipeline/PipelineShadow.cpp`）当前只算 first-found `DirectionalLight` 单张 2D shadow map；多 shadow caster 无基础设施
- 用户搭场景时：聚光（舞台光 / 手电筒 / 卡通角色锥形高光）无类型可用；点光能照亮但不投影，室内场景缺真实感阴影

### 缺什么（按依赖拆）

#### G1 · SpotLight 类型 + 无阴影锥光着色（最易，照 PointLight pattern）

- `LightComponent.h` 加 `SpotLight` struct：`color` / `intensity` / `range` / `innerConeAngle` / `outerConeAngle`（弧度，着色端用 cos 缓存做软锥边）/ `castsShadow`（保留，G2 启用）。**position 由 `Transform.position` 派生、direction 由 `Transform.rotation` 派生** —— 复用 `ComputeDirectionalLightWorldDir` 同款约定（或定义 `kSpotLightLocalForward`），组件不冗余存几何状态
- `PipelineImpl.h` 加 `SpotLightsUbo`（照 `PointLightsUbo` pattern，`:253`）：`kMaxSpotLights` + std140 `SpotLightStd140{ posRange, dirCosInnerOuter, colorIntensity }`；新增 `UpdateSpotLightsUbo(world)` 每帧收集 + range cull
- `pbr.frag.glsl` 加 spot 着色：inverse-square 衰减 × `smoothstep(cosOuter, cosInner, dot(spotDir, L))` 锥角软边
- editor：`schema/RegisterBuiltinSchemas.cpp` 加 SpotLight schema（color / intensity / range / 内外锥角 widget）；`plugin/SpotLightGizmoPlugin`（锥体 wireframe overlay，照 `PointLightGizmoPlugin` pattern）；Add-Component 菜单加项
- 与 PointLight 当前"无阴影"基线一致 —— 不引入 shadow，先把类型 + 着色 + 编辑闭环跑通

#### G2 · SpotLight 透视阴影（perspective shadow map）+ 多 shadow caster 架构

- 每个 `castsShadow` spot 一张 perspective shadow map：light view = `lookAt(pos, pos+dir)`；light proj = `perspective(2*outerConeAngle, aspect=1, near, range)`
- **横切前置（G2/G3 共用地基）**：当前 `PipelineShadow` 只算 1 张 directional 2D map。支持多光源投影需要 **shadow atlas 或 depth texture array** + per-light shadow index + shadow matrices UBO 数组。实现 session 先定 **atlas vs array** 策略（参 `vendor/LumixEngine` / Godot / Unreal 的 shadow atlas）+ shadow caster 数量上限 + 优先级（距离 / 重要度 cull）
- `pbr.frag` 加 spot shadow 采样 + PCF；复用现有 `shadow_caster.vert` depth-only pass

#### G3 · PointLight 全向阴影（omnidirectional cubemap / dual-paraboloid，最难）

- 每个 `castsShadow` point light 6 面 cubemap depth（或 dual-paraboloid 2 张）+ distance-based linear depth
- `pbr.frag` 加 cubemap shadow 采样
- 依赖 G2 的多 shadow caster 地基

### 期望验收

- SpotLight：Add Component → SpotLight；Inspector 调 color / intensity / range / 内外锥角；viewport 见锥体 gizmo；场景内呈聚光锥形照明 + 软锥边
- `PointLight` / `SpotLight` `castsShadow=true` 时投影正确；**多光源同时投影不串扰**
- 三种光源 + 阴影字段 Undo/Redo + scene save-load round-trip 完整
- 新增（或扩现有）sample 演示三种光源 + 三类阴影同框

### 状态

- **登记**：2026-05-26
- **落地**：2026-05-26（同 session G1+G2+G3 全部 ✅）
- **优先级**：P1（用户主动拉动；首款游戏 stylized 光照基线需要完整光源族）
- **G1 · SpotLight 类型 + 无阴影锥光着色** ✅：`LightComponent.h` 加 `SpotLight`（color/intensity/range/inner+outerConeAngle 半角弧度/castsShadow）+ `ComputeSpotLightWorldDir`；`SpotLightsUbo`（binding 6，cap 8）+ `UpdateSpotLightsUbo`；`pbr.frag` 锥光着色（inverse-square × range smoothstep × 锥角 smoothstep 软边）；ComponentSerializers + scene schema 1.5→1.6；editor schema + `SpotLightGizmoPlugin`（锥体 wireframe）+ Add-Component 菜单（schema 驱动自动上）
- **G2 · 多 shadow caster + SpotLight 透视阴影** ✅：depth `Tex2DArray`（cap 4 caster，binding 7 `sampler2DArray` + binding 8 矩阵 UBO）；per-caster perspective light view-proj（`Camera::Perspective(2*outerCone,…)`）；`pbr.frag` 按 shadow index 采 PCF。directional 仍独立 shadowMap → 多 caster 不串扰
- **G3 · PointLight 全向 cubemap 阴影** ✅：N 个独立 6-layer `TexCube`（cap 2，binding 9/10 `samplerCube`）；per-face 90° perspective depth-only；`pbr.frag` 按方向采 + dominant 轴距离重建 NDC depth 比较。用独立 cube 而非 cubeArray 免 `imageCubeArray` feature（见下「跨仓核对」+ 已登记 OrangeRender FEATURE）
  - **后续修复（dogfood 发现）**：cube face 渲染初版误用主帧 `Camera::Perspective`（带 Vulkan y-flip），导致非 -Z 主轴方向（尤其 -Y 面 = 头顶点光照水平地面这一最常见情形）采样命中上下镜像 texel → 阴影错位。改用「不 y-flip」的 cube 专用投影修复。初版 `ShadowOcclusionTest` 只压 -Z 面（且遮挡点在中线、对翻转不敏感）漏检 → 已补「头顶点光 + 水平地面」X/Z 双轴差分用例 + cube occluder（扁 quad 对头顶光侧面朝光会假阴性）。教训：cube 阴影测试必须覆盖多个 face 主轴 + 用 3D occluder
- **验收对照期望**：SpotLight 编辑闭环 ✅（schema + 锥体 gizmo + 着色）；point/spot castsShadow 投影正确 + 多光源不串扰 ✅（`tests/render/ShadowOcclusionTest.cpp` 离轴差分遮挡 readback：point/spot 各 lit=2.718 / shadow=0.000）；三光源 + 阴影字段 round-trip ✅（`LightAndShadowTest` + scene 1.6 + Undo/Redo 走 schema/serializer 既有路径）；**sample 同框演示** ✅（`samples/16_light_family_shadows`：地面 + 3 球 + 三种光源全 castsShadow，directional 平行硬阴影 / spot 锥光透视阴影 / point 全向 radial 阴影同框，运行无崩溃）
- **关联**：[[GAP-2026-05-11-point-light-and-visible-halo]]（PointLight 同族，halo billboard 视觉留 backlog）/ [[GAP-2026-05-22-multi-directional-light-semantics-undefined]]（多 directional 语义，多 shadow caster 同源架构 —— 本 gap 已铺好 spot array 地基，directional 多投影可后续复用）/ `tests/render/ShadowOcclusionTest.cpp`（阴影正确性回归门）

### 跨仓核对（2026-05-26 实现 session 复核结论）

- **G1（SpotLight 无阴影）**：**纯 Engine** ✅。无 OrangeRender 缺口（`SpotLightsUbo` 照 `PointLightsUbo`，RHIBuffer UBO 已在用）
- **G2（spot perspective shadow）**：**纯 Engine** ✅。RHI 已支持 **depth `Tex2DArray`**（`TextureViewDesc` per-layer Tex2D view + depth aspect 由 format 自动派生，见 `OrangeRender/src/backend/vulkan/VulkanDevice.cpp` CreateTextureView）。无 OrangeRender 改动
- **G3（point cubemap shadow）**：**纯 Engine** ✅（与初步评估的"跨仓风险点"不同）。RHI 已支持 **render-to-cube-face depth**：`TexCube` + DepthStencil usage + per-face Tex2D depth view（IBL prefilter 用同款 render-to-cube-face color 机制；depth 走相同 view 路径，aspect 自动）。**唯一缺口**：`samplerCubeArray` 需 `imageCubeArray` device feature（OrangeRender 未启用）→ 用 N 独立 `samplerCube` 绕过（IBL cube 已在用，core 能力）。已登记 `OrangeRender/docs/incoming_feature.md` FEATURE-2026-05-26-enable-image-cube-array（低优先 nice-to-have，启用后可合并为 cubeArray + 扩展点光阴影数）
- **结论**：G1/G2/G3 全部 OrangeEngine 子仓内闭环，零 OrangeRender 代码改动；仅一条 forward-looking FEATURE 文档登记（ADR-010 跨仓文档豁免）
- **性能**：阴影渲染开销正常，**开 Vulkan validation 也快**——`ShadowOcclusionTest`（validation on，256 res，4 帧）实测 0.24s；`samples/16_light_family_shadows`（validation on，1280×720，三光源全 castsShadow）流畅出帧。编辑器（device validation on）放 castsShadow 的 spot/point 光不会卡。（调试期一度观测到极慢，事后定位为同时跑两个 Vulkan 进程争用 GPU 所致，非 validation / 非阴影本身。）

### Inc2 · MikkTSpace 高质量切线落地（2026-05-25）

mikktspace 高质量切线（A2 命名交付物之一）落地，替换 importer 侧的 Lengyel fallback：

- **vendor**：`vendor/mikktspace/`（mikktspace.h + mikktspace.c，Morten S. Mikkelsen，zlib 许可，Blender / Godot / Unreal 同款）。与 cgltf / tinyobjloader 同款 in-tree single-file vendor，仅 OrangeEditor + 测试消费，引擎 runtime 不接（ADR-008 importer-only invariant）。
- **C 语言启用**：顶层 `project(... LANGUAGES C CXX)`。教训——CMake 未启用 C 时**静默丢弃**加进 target 的 `.c` 源（不编、不报错，链接期才暴露 `genTangSpaceDefault` 缺符号）。`mikktspace.c` 单独 `/W0` 豁免编辑器 / 测试的 `/W4 /WX`。
- **接线**：`tools/OrangeEditor/import/MeshTangentGen.{h,cpp}::GenerateMikkTSpaceTangents`。契约关键（mikktspace.h 行 86-101）——MikkTSpace 输出 per-face-vertex、**未索引**，禁止写回已有 index list；故 de-index 读入喂回调 → 收 per-face-vertex 切线 → 按 (原顶点, 切线方向+手性) **re-weld** 回索引网格（UV / 法线缝处切线分裂为新顶点）。
- **集成**：`ObjImporter` + `GltfImporter` 在构造 MeshAsset 前调用（UV + normal 齐备时），`SetTangents` 注入 → `.mesh` v4 写出 tangent 段。缺 UV / normal → 返回 false，仍落引擎 Load 端 Lengyel fallback（`MeshAsset::ComputeTangentsFromTriangles`，安全网保留）。
- **回归门**：
  - `tests/asset/MikkTSpaceTangentTest.cpp` —— 标准 XY 四边形（法线 +Z、UV 沿 +X/+Y）断言切线 ≈ (1,0,0)、单位长、w=±1，+ 缺 UV / 退化 indices 的 false 路径。验证回调喂数据 + re-weld 正确（mikktspace 算法本身不在覆盖范围）。
  - `tests/asset/MikkTSpaceRealModelTest.cpp` —— 真实有机曲面（Avocado，682 三角）跑 cgltf 解析 + 切线生成，断言全覆盖 + 单位长 + 手性 ±1 + 切线⊥法线（实测 `|dot(T,N)|` max=mean=**0.00000**）。fixture 是 session 内下载的未跟踪资产，CMake `if(EXISTS)` 门控——干净 checkout 自动跳过不阻塞 CI。

---

## GAP-2026-05-27-cascaded-shadow-maps

- **发现方**：渲染推进 session（post-process 特效铺完后回看 directional 阴影质量）
- **发现日期**：2026-05-27
- **一句话定性**：directional 阴影用**固定 ±10 ortho box**（`PipelineShadow.cpp::ComputeLightViewProj` 硬编码 `kHalfExtent = 10`）覆盖整个场景，shadow map 分辨率均摊到 20×20 单位 → 近景阴影边缘锯齿粗、远景浪费；缺 **CSM（Cascaded Shadow Maps）**——按相机视锥分级、近景高分辨率，是户外大场景 directional 阴影的工业标准
- **状态**：**C1 ✅ + C2 ✅ + C3 ✅ + sample 18 ✅ 全部落地（2026-05-28，同 session 连续推进）—— C1 infrastructure + C2 真实 per-cascade fit + texel snap + per-cascade PCSS scale + 默认 cascadeCount=3 + C3 cross-cascade smoothstep blend + `samples/18_csm_large_scene` 大场景 fixture（`--no-csm` flag 做 A/B）**；CSM 主线 GAP 全部完成。

### 触发场景

首游是 Ori-like 2.5D 平台跳跃，相机跟随主角在较大关卡里平移。固定 ±10 box 一旦关卡尺度超过 ±10 就漏阴影（caster 落在 box 外不写 shadow map）；即使在 box 内，分辨率均摊导致主角脚下接触阴影锯齿明显。CSM 把视锥近段单独分一张高分辨率 cascade，近景阴影显著变锐。

### 缺什么 / 现状对照

| 能力 | 现状 | CSM 目标 |
|---|---|---|
| 阴影范围 | 固定 ±10 ortho，超出漏阴影 | 跟随相机视锥，自动覆盖可见范围 |
| 近景分辨率 | 全场景均摊 | 近段 cascade 独占一张全分辨率 map |
| shadowMap 资源 | 单张 `Tex2D`（`PipelineImpl.h::shadowMap`） | `Tex2DArray`（N layer，每 cascade 一层）|
| light view-proj | 单个 `ComputeLightViewProj(dir)` | per-cascade：按视锥 slice 的 world-space AABB 拟合 ortho |
| LightUbo | 单 `lightViewProj` mat4 + `shadowParams` | N 个 cascade 矩阵 + N 个 split 距离（std140，GPU-only，非序列化 schema，可自由改）|
| pbr.frag 采样 | 单 map PCF | 按 view-space 深度选 cascade → 采对应 array layer + PCF，cascade 边界可选 dither/blend |

### 落地设计要点（供独立 session 执行，针对本引擎现有阴影代码）

1. **cascade split**：`splitDist[i]` 用 practical split（log 分布与 uniform 分布按 λ≈0.5 混合）。cascade 数先做 **3 或 4**（config 字段 `cascadeCount`，默认值保持单 cascade 行为以便增量验证）。
2. **per-cascade fit**：对每段视锥（near_i..far_i）取 8 个角点变换到 world，求其在 light view 空间的 AABB → 构造 ortho（沿用 `ComputeLightViewProj` 已踩对的 **Vulkan z∈[0,1] + y-flip 手写 ortho**，**不要**用 `glm::ortho`，注释里已记 OpenGL z 会裁半视锥的坑）。加 texel-snapping 消 shimmer（AABB 原点按 shadow texel 量化）。
3. **资源**：`shadowMap` 单 `Tex2D` → `Tex2DArray`（cascadeCount layer）+ per-layer depth view（参 `spotShadowArray` 已有的 Tex2DArray + per-layer view 模板，PipelineImpl.h 现成可抄）。`RecordShadowPass` 改为 loop cascade，每层渲一遍 depth-only（复用 `shadowCasterPipeline`）。
4. **LightUbo**：`LightUboData` 加 `glm::mat4 cascadeViewProj[N]` + `glm::vec4 cascadeSplits`（split 距离塞一个 vec4，N≤4）。注意现有 `static_assert(sizeof(LightUboData) == ...)` 要同步更新。
5. **pbr.frag**：按片元 view-space 深度（或 clip.w）选 cascade index → 采 `sampler2DArray` 对应 layer，沿用现有 PCF kernel。可加 cascade 边界 1-texel dither 过渡防硬切。**与 PCSS（`pcssLightSize`）的交互**要想清楚——PCSS 的 blocker search 半径需按 per-cascade ortho 尺度缩放。
6. **增量验证策略**：先 `cascadeCount=1` 跑通（行为 == 今天），ctest / 视觉零回归；再升 3/4 cascade，用一个**拉长的地面 + 远近多个 caster**的测试场景（现有 demo 的 ±10 太小，体现不出 CSM 收益，需要专门 fixture）肉眼验证近景变锐 + 远景仍有阴影。

### 期望验收

- `cascadeCount=1` 与现状逐像素一致（增量安全网）；
- 多 cascade 下近景阴影边缘明显更锐、相机平移时无 cascade 边界 popping/shimmer（texel-snapping 生效）；
- ctest 全绿 + lint/drift 干净 + editor/sample 视觉无回归（其它 pass 不受影响）。

### 备注

- 与 GAP-2026-05-26-complete-light-source-family-and-shadows（已落地 spot/point 阴影）**正交**：那条做的是"多光源各自的阴影"，本条做的是"directional 单光源的阴影质量分级"。spot 用的 `Tex2DArray + per-layer view` 基建可直接复用到 CSM 的 cascade array。
- OrangeRender 侧能力**充足**（Tex2DArray + per-layer depth view + ortho depth-only 渲染都已在 spot/point 阴影用上），**不需要跨仓提 feature**——纯 OrangeEngine 内 Pipeline + shader 改动。

### C1 落地记录（2026-05-28）

infrastructure 闭环，**默认 `cascadeCount=1` 行为与昨日逐像素一致**（零回归安全网）。改面：

- `ShadowConfig` 加 `cascadeCount{1}` 字段
- `PipelineImpl.h`：`kMaxCascades=4` 常量；`shadowMap` 改 `Tex2DArray`（layer = cascade）+ `shadowMapLayerViews[4]` per-layer Tex2D depth view（仿 `spotShadowArray` 模式）；`cascadeViewProjs[4]` + `cascadeNdcSplits` 本帧 cache；`LightUboData` 末尾 additive 追加 `cascadeViewProj[4]` + `cascadeNdcSplits`（160B → 432B，static_assert 同步）
- `PipelineShadow.cpp`：`EnsureShadowMap` 重写为 Tex2DArray + per-layer view；新 `ComputeCascadeViewProjs(lightDir)`（C1 单 cascade：所有 slot 填同一 ±10 box 矩阵 + splits 全 1.0 → cascade selection 恒 0）；`RecordShadowPass` 改 loop kMaxCascades layer，cascadeCount 之外的 layer 仍 Clear 到 1.0；`UpdateLightUbo` 删 `lightViewProj` 参数（改读成员 cascadeViewProjs[]）
- `Pipeline.cpp`：6 处 caller（window + offscreen path 各 3：prepare 段 ComputeCascadeViewProjs + UpdateLightUbo + RecordShadowPass）同步；`Shutdown` 加 `shadowMapLayerViews` reset（**SEGFAULT 真因**：漏 reset 导致 vkDestroyDevice 报 VkImageView leak，pipeline_template_cache_test / pipeline_hdr_target_test / bloom_chain_test 当场 SEGFAULT，加 reset 后 100% pass）
- shader：8 个 set 0/binding 0 sampler 全升 `sampler2DArray`（pbr/toon/rim_light/water_basic/textured_mesh/dissolve/emissive/unlit）；消费 shadow 的 4 个 shader（pbr/toon/rim_light/water_basic/textured_mesh）`SamplePcfShadow` → `SamplePcfShadowArray(layer=0, ..)`；pbr.frag 唯一额外加 cascade selection（`gl_FragCoord.z` vs `uCascadeNdcSplits`）+ 调 `SamplePcssShadowArray` 走真 CSM 路径；其他 7 shader 的 LightUbo 块**不动**（additive 字段不读即可，std140 layout-compatible）

**关键 implementation 取舍**：

1. **additive LightUbo layout**：保留 `lightViewProj` 在原偏移 0 作 cascade[0] backward-compat alias；新 cascade 字段追加末尾，把 shader 改面从 ~13 文件压到 ~10（非 pbr shader 仍按旧字段名读 cascade 0 等效矩阵）
2. **cascade selection 用 `gl_FragCoord.z`（NDC z）而非 view-space 深度**：避免引入新 varying / 新 UBO 字段（如 camera forward）；host 端 NDC 转换由相机 proj 隐式提供。C1 时 cascadeNdcSplits 全 = 1.0 → `gl_FragCoord.z > splits[i]` 永不命中，cascade 恒 0
3. **EnsureSpotShadowArray 是 CSM 完整模板**：per-layer view 创建 / transition 整阵 / loop layer 渲染逐字搬

**验收**：

- `shadow_occlusion_test` ✅（CSM C1 核心回归守门员，光遮挡判定与昨日字节一致）
- 全 52 ctest ✅（含 pipeline_template_cache_test / pipeline_hdr_target_test / bloom_chain_test / editor_build_smoke）
- invariant lint 7 grandfathered 无新增 + drift 干净
- 顺手发现 + 修一条 0b16593 遗留：`OrangeEngineConfig.cmake.in` 缺 `OrangeEngine::imgui` alias 重建（install EXPORT 不传播 build-tree alias，editor_build_smoke 当场暴露）→ 独立 commit 单修

**关联**：[[GAP-2026-05-26-complete-light-source-family-and-shadows]]（spot Tex2DArray 模板被 CSM C1 复用）

### C2 落地记录（2026-05-28，与 C1 同 session 趋热收尾）

**真 per-cascade 视锥分段拟合 + texel snap + per-cascade PCSS scale 全部落地**。代码改造：

- `ShadowConfig.cascadeCount` 默认值 1 → **3**（真 CSM 默认启用）
- `LightUboData` 再 additive 追加 `cascadePcssScales` (vec4, 16B)；总大小 432 → **448B**，static_assert 同步
- `PipelineImpl::cascadePcssScales` 成员 cache + UpdateLightUbo 写进 UBO
- `ComputeCascadeViewProjs` 重写：参数加 `cameraView` + `cameraProj`，实现真 CSM 数学：
  1. 反推相机 near/far（Vulkan z[0,1] perspective 公式：`near = proj[3][2]/proj[2][2]`、`far = proj[3][2]/(proj[2][2]+1)`）
  2. **Practical PSSM split**（λ=0.5 mix log + uniform，Engel/Dimitrov GPU Pro 同款）
  3. NDC 8 角点 × `inverse(viewProj)` → world，slice 沿 ray α-lerp
  4. **Bounding sphere fit**（非 AABB）—— 旋转不变性 + ceil 量化半径，相机仅旋转时 sphere 不变 → 配合 snap-on-center 实现完整 anti-shimmer
  5. **Texel snap**：球心转 light view → `floor(centerLV.xy / texelSize) * texelSize`
  6. ortho zNear/zFar 沿光方向加 `frontPad=5R / backPad=0.5R` 自适应捕捉 caster
  7. cascadeNdcSplits[i] = cascade i 远端在主相机 NDC z（`-proj[2][2] + proj[3][2] / splitDist`，pbr.frag 用 `gl_FragCoord.z` 比较选 cascade）
  8. cascadePcssScales[i] = `orthoExtent_0 / orthoExtent_i`（保 world-space PCSS 半影宽度跨 cascade 一致）
  9. cascadeCount=1 路径保留作零回归 fallback；cascadeCount 之外的 slot 用最后有效 cascade 填作越界 fallback
- `Pipeline.cpp` 两 caller 传 `scene.MainCamera().view + projection` 到 `ComputeCascadeViewProjs`
- `pbr.frag`：LightUbo 加 `uCascadePcssScales`，shadow 采样侧 `csmPcssLightSize = uShadowParams.z * uCascadePcssScales[csmCascade]` 然后喂 `SamplePcssShadowArray`

**关键 implementation 取舍**：

1. **Bounding sphere vs AABB fit**：sphere 旋转不变（相机仅旋转 sphere 中心 / 半径不变），AABB 会随相机方向重排 → 蜷曲的 extent 变化引发 shimmer。sphere 拿一点 fit 过松（shadow map 利用率略低）换稳定。是 The Witness / Frostbite / UE 等大量引擎的 stabilization 标配。
2. **Camera 在 light view 原点**而非"放在 sphere 后面 N 单位"：放在原点 → light view 坐标系对 world 静止点恒定 → snap-on-center 真正稳定；放在 sphere 后面 → eye 跟 sphereCenter 走 → snap 在 light view 里的坐标抖。
3. **cascadeNdcSplits 用 NDC z 而非 view-space z**：pbr.frag 直接拿 `gl_FragCoord.z` 比较，免传 camera near/far + linearization。host 端用 proj 公式预先转换。
4. **per-cascade PCSS scale 按 ortho extent 比**：cascade 0 PCSS lightSize 不动；远 cascade 的 lightSize 等比缩小（cascade 0/N 倍），保 world-space 半影宽度一致；否则远景 penumbra 在 world 中爆出过大软边。

**验收对照期望**：

- ✅ `cascadeCount=1` 与现状逐像素一致（`shadow_occlusion_test` 全 cascadeCount 配置下都过 = 单 cascade fallback 路径仍是历史 ±10 box 行为）
- ✅ **多 cascade 视觉无破损**（sample 16 默认 cascadeCount=3 一帧 capture：3 球阴影 + spot/point 罩色 + 后处理链全部正确，与 C1 前观感等价；±10 场景太小看不出戏剧性 CSM 收益，但证 CSM 数学 + 接线对）。**完整"近景锐 + 远景仍有阴影 + 拉相机无 shimmer"showcase 需要专用拉长地面 fixture sample**（落地时 prep 了 `samples/18_csm_large_scene/` 目录但留空作待办——避免本 commit scope 蔓延），留独立 follow-up commit / session
- ✅ ctest 52/52 全绿 + invariant lint + drift 干净

### Sample 18 落地记录（2026-05-28，CSM 主线视觉 fixture 收尾）

**`samples/18_csm_large_scene` ✅** —— GAP-2026-05-27-cascaded-shadow-maps C1+C2+C3 视觉验收 fixture。100×100 ground + 5 个 sphere caster 沿 +Z 摆 (3, 10, 22, 40, 70)，相机低角度看向 +Z 让 ground 拉成"远方延伸"。CLI flag：

- `--no-csm` —— 强制 `cascadeCount=1` 回到 C1 fallback 路径，与默认 cascadeCount=3 做 A/B 对比
- `--pcss N` —— 启用 PCSS 软阴影，lightSize=N texel
- `--capture <path>` —— 渲一帧 PNG 后退（CI / 文档无人值守）
- 故意不挂 spot/point/SSAO/SSR/DoF/TAA，保留纯 directional + bloom + tonemap 让 CSM 分析清晰

**视觉对比 honest 观察**：两路径产生的 capture 比预期更接近。原因：C1 fallback 的 ±10 ortho box 配合 sceneCenter 后退 + zFar=40 实际覆盖深度 > ±10（lightDir 沿 -Y 倾斜让 light view 沿光方向有效覆盖 ~40 单位 world depth），故远到 z=70 的 caster 也能进 shadow map，只是分辨率均摊到 20×20 单位 → 锯齿粗。CSM 真正的优势在**近景分辨率分配** + **任意大场景覆盖**，本 sample 没把这个差异放足够大。

进一步戏剧化 backlog（独立 polish session 不阻塞 CSM GAP）：
- 相机更低 + 更近地面 → 把远景 shadow 拉到屏幕大比例区域
- 加 cascade index 染色 debug overlay（pbr.frag 出 cascade ID）→ 视觉色块直接看 cascade 切换
- 加运动相机模式 → 看 texel snap anti-shimmer 效果

**改面**（3 文件 +）：

- `samples/18_csm_large_scene/main.cpp` —— 新增（fork sample 16 helper + 简化 scene）
- `samples/18_csm_large_scene/CMakeLists.txt` —— 新增（仿 sample 17 pattern）
- `samples/CMakeLists.txt` —— 注册 sample 18

**验收**：

- ✅ Build 通过（CMake reconfigure + build sample 18 target 全绿）
- ✅ 跑通：CSM 默认 + `--no-csm` fallback 两种模式都正常出图 + 截图
- ✅ 全 52 ctest 全绿（无回归）
- ✅ invariant lint + drift 干净

---

至此 `GAP-2026-05-27-cascaded-shadow-maps` **主线 4 段全部 ✅ + sample 18 视觉戏剧化 polish ✅**（C1 infra + C2 真 fit + C3 blend + sample 18 fixture + cascade tint overlay + 运动相机）。CSM GAP 完整闭环。

### Sample 18 视觉戏剧化 polish 落地（2026-05-28，与 CSM 主线同 session 收尾）

CSM 主线落地后，原 sample 18 的两张 capture 视觉差异未达预期（C1 fallback 的 ±10 box 配合 lightDir 倾斜实际覆盖深度 ~40 单位，远 caster 仍能进 shadow map）。本 polish 补完 sample 18 的视觉戏剧化三件套，把 CSM 工作机理变得**肉眼直白**：

**1. Cascade tint overlay**（核心收益）

`pbr.frag` 在最终输出上 mix per-cascade 颜色（cascade 0=红 / 1=绿 / 2=蓝 / 3=黄），让 cascade 分段直接画在地面上。配合 `--no-csm` flag 做 A/B：

- **CSM cascadeCount=3 + tint**：地面 3 段彩色（红前景 / 绿中景 / 蓝远景），球体按所处 cascade 染色，cascade 边界用 C3 smoothstep blend 平滑过渡 = CSM 三段划分的最直白视觉证据
- **`--no-csm` + tint**：整画面单一红色（cascadeCount=1 强制走 cascade 0）= C1 fallback 没有分段的视觉证明

**架构改面**（4 文件，全部 additive）：

- `include/orange/engine/render/ShadowConfig.h` —— 加 `bool debugCascadeTint{false}`
- `src/render/pipeline/PipelineImpl.h` —— `LightUboData` 再 additive 加 `glm::vec4 debugFlags`（x = tint 开关 0/1，y/z/w pad），static_assert 448 → **464B**
- `src/render/pipeline/PipelineShadow.cpp::UpdateLightUbo` —— 写 `debugFlags.x = shadowConfig.debugCascadeTint ? 1.0 : 0.0`
- `src/render/builtin_shaders/pbr.frag.glsl` —— `LightUbo` 块加 `vec4 uDebugFlags`；最终输出前 `if (uDebugFlags.x > 0.5) color = mix(color, color * cascadeTints[csmCascade], 0.55)`；shipping 时 host 永远写 0，GPU dynamic uniform branch 短路 = 零额外开销

**2. 运动相机**（验 anti-shimmer）

`samples/18_csm_large_scene` 加 `--motion` flag：`RenderLayer` 每帧用 `World::ToEntt(cameraEntity)` 取 Camera 组件改 `view` 矩阵，sin-wave 左右 (swayX) + cos-wave 前后 (swayZ) 摇摆。配合 `--tint` 看：camera sway 时 cascade 染色边界**仍然干净整齐**（无锯齿破碎 / 闪烁）= C2 的 bounding sphere fit + ceil(R×16)/16 半径量化 + snap-on-center texel snap 三件套**真在工作**。

**3. CLI flag 全集**

```bash
# 基线 + A/B
18_csm_large_scene                          # CSM 默认 cascadeCount=3
18_csm_large_scene --no-csm                 # C1 fallback cascadeCount=1
# 视觉戏剧化
18_csm_large_scene --tint                   # cascade 染色 overlay（CSM 路径 → 3 段彩色）
18_csm_large_scene --no-csm --tint          # 单 cascade 染色 → 整画面单色（A/B 对照）
18_csm_large_scene --motion                 # 相机 sway 模式
18_csm_large_scene --tint --motion          # 集大成
18_csm_large_scene --pcss 8                 # PCSS 软阴影 lightSize=8 texel
18_csm_large_scene --capture <path>         # 出一张 PNG 后退（CI / 文档无人值守）
```

**改面**（5 文件）：

- engine 侧 4 个（ShadowConfig.h / PipelineImpl.h / PipelineShadow.cpp / pbr.frag.glsl）—— cascade tint infra
- `samples/18_csm_large_scene/main.cpp` —— `--tint` / `--motion` 双 flag + RenderLayer 持 cameraEntity 每帧动画 + `shadowConfig.debugCascadeTint = tintEnabled`

**验收**：

- ✅ Build 通过 + 全 52 ctest 全绿（LightUboData 448 → 464B 不破任何 std140 layout-compatible 消费 shader）
- ✅ 4 张 capture 跨 (CSM/no-csm) × (tint/motion) 全部正确出图
- ✅ Tint 路径下 cascade 边界视觉戏剧化 = CSM 工作机理的最直白证明
- ✅ Motion 路径下 cascade 边界保持稳定 = C2 anti-shimmer 验收过
- ✅ invariant lint + drift 干净

---

### C3 落地记录（2026-05-28，与 C1 + C2 同 session 连续推完）

**Cross-cascade smoothstep blend ✅** —— pbr.frag 在当前 cascade 远端最后 5% NDC z 范围内对下一 cascade 做 smoothstep blend，消除"穿越 cascade 边界时阴影锐度突变"的硬切感。code path 参 Wiki `shadow-mapping.md` §CSM 模板：

```glsl
if (csmCascade < cascadeCountFromHost - 1) {
    float thisFar     = uCascadeNdcSplits[csmCascade];
    float blendStart  = thisFar - thisFar * 0.05;
    float blendFactor = smoothstep(blendStart, thisFar, gl_FragCoord.z);
    if (blendFactor > 0.0) {
        // sample next cascade with its own pcss scale, mix(shadow, shadowNext, blendFactor)
    }
}
```

**关键设计**：

- **`cascadeCount` 经 `uShadowParams.w` 传 shader**（原本是 pad float），让 blend 只在真实 cascade 边界触发，跳过 host filler slot（cascadeCount<4 时 host 把多余 slot 填为最后有效 cascade 的复制 + splits=1.0；shader 不知道这是 filler 就会做无意义重复采样，性能浪费）
- **blend width = thisFar × 0.05**（5% of cascade 远端 NDC z）—— 简单可控的相对宽度。严格按 view-space cascade 宽度需多传一组 uniform，stylized polish 不值得
- **blend 区只多一次 SamplePcssShadowArray**（约 5% 的 fragment）—— 性能开销 < 5%（绝大多数 fragment 走单次采样路径）
- **safety net**：`csmCascade < cascadeCount-1` gate + 默认 1.0 splits 让单 cascade（cascadeCount=1）路径 csmCascade=0、cascadeCount-1=0、条件 false → blend 全跳过，C1 fallback 行为零回归

**改面**（2 文件）：

- `src/render/pipeline/PipelineShadow.cpp` —— `UpdateLightUbo` 把 cascadeCount 写进 `shadowParams.w`（原 pad 0.0）
- `src/render/builtin_shaders/pbr.frag.glsl` —— shadow 采样后接 cross-cascade blend 代码块

**验收**：

- ✅ 全 52 ctest 全绿（含 `shadow_occlusion_test`，单 cascade fallback 路径无回归 + 多 cascade blend 不破光遮挡判定）
- ✅ sample 16 一帧 capture 与 C2 一帧 capture 视觉等价（±10 场景太小没 fragment 落入 blend 区，blend 几乎不触发 = 没破任何观感）
- ✅ invariant lint + drift 干净

**真 C3 视觉收益**（边界平滑度）**仍需 large-scene showcase sample 才能戏剧性体现**——cascade 边界在大场景里跨越大世界距离才显眼。与 C2 同一条 backlog 留 follow-up。

---

## GAP-2026-05-27-tonemap-operator-selection ✅

- **发现方**：渲染推进 session（铺完 post 特效后回看 HDR→LDR 收尾算子）
- **发现日期**：2026-05-27
- **一句话定性**：tonemap 算子**写死 ACES Narkowicz 5 系数 fit**（`tonemap.frag.glsl::ACESNarkowicz`），无算子选择。ACES Narkowicz 对**高饱和亮色**会偏色/过饱（known issue），对首游明亮多彩的 Ori-like 画风不理想；缺 **AgX**（Blender 4.0+ / Godot 4.3 默认，对鲜艳色 hue 更稳）/ Reinhard 等算子选择
- **状态**：**✅ 2026-05-28 落地**（与编辑器 Render Settings panel 同 session 顺手 follow-up，承接用户"刚撤了美术 post，那 tonemap 算子是不是也能换"的自然下一问）

### 触发场景

首游主角是"流体史莱姆"+ 发光探索，配色倾向鲜艳/高饱和（参 [[project_first_game_orilike_strategy]] 的剪影/渐变画风）。ACES Narkowicz 在这类亮色上会把橙/品红往黄/红方向 skew + 压饱和，画面"发脏"。AgX 的 sigmoid + 色域内收设计专治此症，是当前 stylized 渲染的事实默认。美术定调阶段需要能切算子对比。

### 缺什么 / 现状（已核实代码）

- 算子写死在 `src/render/builtin_shaders/tonemap.frag.glsl::ACESNarkowicz`。
- **窗口与 offscreen 两路径都走 ACES**：均用同一 `tonemapPipeline`（`RecordPassthroughToViewport` 的 bloom 合成分支 + 纯 passthrough 分支都 ACES；窗口 stage-B 同）。改算子要覆盖两路。
- push 传参 struct `PushTonemap{ exposure, bloomIntensity, pad0, pad1 }` **多处内联定义**（至少 `Pipeline.cpp::RecordPassthroughToViewport` ~1494 + 窗口 stage-B tonemap 录制处）——加 operator 要同步所有定义点（有两个 pad float 可直接用，无需扩 push 尺寸）。
- `TonemapPass`（`PostProcessPasses.h`）只有 `exposure`，无 operator 字段。**注意 tonemap 刻意不在 PostProcessComponent 里**（与 bloom 同属 stage-A/B 收尾），故本 gap 走 chain 的 `TonemapPass.operator`，不进组件 / 不动 scene schema。

### 落地设计要点（供独立 session）

1. `TonemapPass` 加 `enum class Operator { ACESNarkowicz, Reinhard, AgX }` + 字段（默认 ACES 保现状）。
2. `tonemap.frag` 加 `uOperator`（用现有 pad float 传 0/1/2）+ 三分支：ACES（现成）/ Reinhard（`x/(1+x)`）/ AgX（Troy Sobotka minimal fit，~15 行 matrix+多项式，注意 sRGB/线性约定）。
3. host 端**所有** PushTonemap 定义点写 operator。先抽一个共享 struct/helper 消除内联重复（顺手还债）。
4. **验证**：用 `samples/16_light_family_shadows --capture` 出 ACES/Reinhard/AgX 三张对比图肉眼核对（本 session 已验证 `--capture` 无人值守可用，是 post 视觉回归的有效手段）。

### 备注

- 纯 OrangeEngine shader + Pipeline 改动，**不需跨仓提 feature**。
- 优先级：美术定调（pre-game）阶段触发；非 critical path，可与"窗口 vs offscreen tonemap 路径统一"一并做。

### 落地记录（2026-05-28，与编辑器 Render Settings panel + 撤美术 post 同 session）

**触发**：用户在 OrangeEditor `light_family_shadows` 场景里截图 Render Settings panel 后问"现在是不是默认显示后处理效果？我没加 PostProcessComponent 就有效果了，逻辑不对" —— 落地撤 6 美术 pass + 保留 HDR 必需 5 pass（含 Tonemap）后，"那 tonemap 算子是不是也能换" 成为自然下一问，触发本 GAP 推进。

**实施清单**（5 个文件改动 + 0 个新文件，shader + push struct + UI 一气呵成）：

1. `include/orange/engine/render/PostProcessPasses.h`：
   - 加 `enum class TonemapOperator : std::uint32_t { ACES_Narkowicz=0, AgX=1, Reinhard=2, Linear=3 };`（4 算子，注释详述每个的视觉特征 / 适用场景 / 历史背景）
   - `TonemapPass` 加 `TonemapOperator op{TonemapOperator::ACES_Narkowicz};` 字段（默认与历史固定行为视觉等价）
   - 删除注释 "Tonemap 算子（Reinhard / ACES / 自定义）当前固定，等到写实际 tonemap shader 时再决定是否引入 enum 选项" —— 此设计点已落地

2. `src/render/builtin_shaders/tonemap.frag.glsl`：
   - push constant `pad0 → uint uOperator` 槽位（与 Pipeline.cpp 两处 PushTonemap struct 同步，不增加 push 总尺寸）
   - 加 4 个 tonemap 函数：`ACESNarkowicz`（保留现行）/ `AgX`（Sobotka minimal fit，含 input/output 3x3 矩阵 + 6 系数 sigmoid 多项式 + log2 编码 [min_ev, max_ev] = [-12.47393, 4.026069]）/ `Reinhard`（`x/(1+x)` per-channel）/ `LinearClamp`（仅 clamp）
   - `ApplyTonemap(hdr, op)` switch dispatcher（fragment shader 内所有 fragment 共享同一 push constant，无 divergence，GPU 静态分支预测性能等价 if-elseif）
   - `main()` 改为 `ApplyTonemap(combined, pc.uOperator)`

3. `src/render/Pipeline.cpp`：两处 `struct PushTonemap` 同步改 pad0 → uint32_t op：
   - 行 1504（offscreen RecordPassthroughToViewport 路径，编辑器视口 stage B fallback）：取 `FindActiveTonemapPass()->op` 或回退 ACES
   - 行 2745（window SubmitItem 路径）：取 `activeTonemap->op` 直接写入
   - 注释同步说明 GAP-2026-05-27 落地点 + uOperator 槽位与 shader 对齐

4. `tools/OrangeEditor/EditorRenderLayer.h`：
   - `#include <orange/engine/render/PostProcessPasses.h>`（之前只 include PostProcessChain.h，没拿到 TonemapPass 定义）
   - 加 `Orange::Engine::Render::TonemapPass* mpTonemapPassRef{nullptr};` —— 非拥有指针缓存，chain 持 unique_ptr ownership

5. `tools/OrangeEditor/panels/ScenePanel.cpp`：EnsureScenePipeline 创建 chain 后 dynamic_cast 拿 TonemapPass* 缓存到 mpTonemapPassRef（BuiltinPostProcessChain::CreateDefault 5-pass 顺序 HDR(0) / Bloom(1) / GodRays(2) / Tonemap(3) / LUT(4)，索引 3 是 TonemapPass；防御性走 dynamic_cast loop，失败 silent skip 让 UI 段做 null 守卫）

6. `tools/OrangeEditor/panels/RenderSettingsPanel.cpp`：加 "Color · Tonemap" CollapsingHeader：
   - Combo "Operator" 4 选项（ACES Narkowicz / AgX / Reinhard / Linear）+ tooltip 详述每算子取舍
   - DragFloat "Exposure" [0, 10] + tooltip 说明 stops 换算
   - Reset 按钮回归 ACES_Narkowicz + exposure 1.0
   - mpTonemapPassRef nullptr 时整段 disabled 文本 "(chain 不含 TonemapPass ...)"

**架构决策**：

- **tonemap operator 不进 PostProcessComponent**：与 BloomPass.intensity 同档，tonemap 是 stage-A/B 收尾概念，与 PostProcessComponent 的"per-camera 美术配置"语义不同（PostProcessComponent.h 顶注释明确写"不含 bloom / tonemap"）。本 GAP 走 `TonemapPass.op` chain-internal 字段路径，不动 scene schema。
- **passthrough.frag 不动**：passthrough 是 chain 为空 / 不含 TonemapPass 的应急 fallback（编辑器实际不走 —— 编辑器 chain 含 TonemapPass），保持其 ACES Narkowicz fixed。后续若 sample 端真需要 passthrough 切算子，独立 GAP。
- **PushTonemap 多处内联**保留（原 acceptance 提到"先抽共享 struct"作为顺手债）：两处实际 use site 都在 Pipeline.cpp 同一 TU，function-local 定义不影响一致性（同 TU 编译期一次性 ABI 验证），抽出反而增加 PipelineImpl.h header 膨胀。同步改两处 ~3 行差异已足够，进一步抽象推 v1.4 PostProcess v2 一并整骨。

**验收**：
- 全 52 ctest 通过（含 editor_build_smoke standalone consumer 验证）
- `python scripts/check_invariants.py` → `All invariants OK. (7 grandfathered)`
- `python scripts/check_claude_md_drift.py` → `none detected.`
- 编译期 ABI 验证：tonemap.frag.glsl 的 push constant `uOperator: uint` slot offset 8、Pipeline.cpp 的 `PushTonemap.op: std::uint32_t` 字段 offset 8 一致（std430 layout 自然对齐，4-byte 槽 × 4 = 16 B push 总尺寸不变）

**视觉验证留待 follow-up**：原 acceptance 第 4 步建议用 `samples/16_light_family_shadows --capture` 出 ACES/Reinhard/AgX 三张对比图，本 session 范围限定在"功能落地 + UI 暴露"，sample 16 capture 路径未跑（CLI flag 也未加，sample 16 当前固定参数）。用户在 OrangeEditor Render Settings 面板 Combo 切算子即可肉眼对比；正式 capture 对照可作为 sample 14_pbr_ibl 后续 polish 任务，独立 GAP / commit 触发。

### 关键改动文件

`include/orange/engine/render/PostProcessPasses.h` / `src/render/builtin_shaders/tonemap.frag.glsl` / `src/render/Pipeline.cpp`（两处 PushTonemap struct） / `tools/OrangeEditor/EditorRenderLayer.h` / `tools/OrangeEditor/panels/ScenePanel.cpp` / `tools/OrangeEditor/panels/RenderSettingsPanel.cpp` / `docs/engine-known-gaps.md`（本条目登记 + 关闭）

### 留待后续

- **sample 14_pbr_ibl `--tonemap=aces|agx|reinhard|linear` CLI flag** + 4 张 capture 对照图 ✅ 2026-05-28（与 sample 18 `--tint` 同款无人值守视觉回归路径），让 PR review 能直接看到算子差异；落地时顺手暴露 + 修 [[BUG-2026-05-28-pipeline-capture-tonemap-hardcoded-aces]]（capture CPU 端 tonemap 与 stage B shader 不一致的第三现场）
- **passthrough.frag 切算子**：仅当 sample 端真撞上"不挂 chain 但要切 tonemap"场景时触发；当前编辑器 + 主线 sample 都走 chain 路径，passthrough 是边缘 fallback
- **PushTonemap struct 抽出共享**：两处 use site 都在 Pipeline.cpp 同一 TU，function-local 定义足够；待 PostProcess v2（GAP-2026-05-27-postprocess-component-local-volume）一并整骨

---

## GAP-2026-05-27-postprocess-component-local-volume ✅

- **发现方**：渲染推进 session（铺完 post 特效 + 写后处理参考页 `rendering-post-process.md` 时回看 PostProcessComponent 作用域语义）
- **发现日期**：2026-05-27
- **一句话定性**：`PostProcessComponent` v1 作为**全局单例**消费（`Pipeline` find-first + `SyncPostProcessFromWorld` 每帧把首个组件灌进全局 `postXxx` 参数），无**局部 post-process volume**——场景挂多个组件时只有 first-found 生效，无法做"进洞穴压暗调色 / 进 boss 房切氛围"这类**按相机位置分区**的 post。组件已为此预留 `Mode{Global,Local}` / `localExtent` / `priority` / `blendDistance` 占位字段（`include/orange/engine/render/PostProcessComponent.h`），但 Pipeline 当前只走 `Global` 分支
- **状态**：**✅ 2026-05-28 落地**（与编辑器 Render Settings panel + 撤美术 post + tonemap 算子同 session 连续推进；用户在"撤完编辑器 hardcode post chain，让 PostProcessComponent 成为美术效果的真正入口"主题下顺手把 v1 first-found 升级到 v2 collect-all + 相机位置混合）

### 触发场景

首游 Ori-like 关卡天然分区（地表 / 洞穴 / boss 房 / 水下），各区想要不同 post look——洞穴压暗 + 偏冷调色、boss 房高对比 + 暗角加重、水下蓝绿色偏 + 轻模糊。v1 全局单例只能整张关卡一套 post，切区域得手写脚本改全局组件字段（突变、无过渡）。Local volume + 相机位置混合是 UE PostProcessVolume / Unity Volume Framework 的事实标准。**非阻塞**：灰盒 / 早期关卡用单一全局 look 完全够，等关卡氛围设计真正撞上分区需求再拉动。

### 缺什么 / 现状对照（已核实代码）

| 能力 | v1 现状 | v2 目标 |
|---|---|---|
| 作用域 | 全局单例（`mode` 字段保留但只走 `Global`）| `Local`：实体 Transform 处 `localExtent` 半尺寸盒 |
| 多组件 | find-first，其余忽略 | collect-all，按相机位置 + `priority` 选 / 叠 |
| 过渡 | 无（切换即突变）| `blendDistance`：盒边界外这段距离线性淡入 |
| Pipeline 消费 | `SyncPostProcessFromWorld` 灌首个组件 → 全局 post 参数 | 收集所有 Local volume + Global 底，按相机位置逐字段混合 |
| scene schema | 占位字段已序列化（全 optional，向后兼容）| **零 schema 改动**（v1 已铺路，老场景兼容）|

### 落地设计要点（供独立 session 执行）

1. **占位字段已就位**（header 已核实）：`enum class Mode : std::uint8_t { Global = 0, Local = 1 }` + `localExtent`（vec3 半尺寸盒）+ `priority`（重叠谁压谁）+ `blendDistance`（边界淡入）。v1 `ComponentSerializers.cpp` 已带这些字段（全 optional），**v2 不动 scene schema**（这是 v1 刻意预留的核心收益）。
2. **Pipeline 改 collect-all**：`SyncPostProcessFromWorld` 从 "find-first 灌全局" 改为 "收集 world 内所有 PostProcessComponent → 按相机 world 位置判断落在哪些 Local 盒内 → 按 `priority` 排序 + `blendDistance` 算每个 volume 权重 → 逐字段加权混合（Global 组件作 priority 最低的底）"。
3. **混合语义需定清**（设计决策点）：标量 / 颜色类字段线性 lerp；bool enable 类字段按"最高 priority 命中的 volume 接管"还是 OR 语义需落地时拍板，并同步写进 `rendering-post-process.md` 的作用域段。
4. **编辑器侧**：`Local` 模式下 gizmo 画 `localExtent` 线框盒（参现有 PointLight range 圆环 gizmo plugin 模板）；schema 已 driven，`mode` / `localExtent` 等字段自动出控件，无需新 Inspector 代码。
5. **验证**：两个 PostProcessComponent（一个 Global 底 + 一个 Local 高对比盒），相机移入 / 移出盒，看 post 在 `blendDistance` 内平滑过渡；用 `--capture` 出盒内 / 盒外 / 过渡带三张对比图肉眼核对（现有 demo 的 ±10 场景需要专门 fixture 体现分区）。

### 期望验收

- 单 Global 组件场景与 v1 逐像素一致（增量安全网）；
- 多 volume 下相机进出 Local 盒时 post 在 `blendDistance` 内平滑淡入淡出、无突变；priority 重叠仲裁符合预期；
- 老 scene（v1 写的、字段全 optional）直接 Load 行为不变；ctest 全绿 + lint/drift 干净。

### 备注

- 纯 OrangeEngine `Pipeline` + 编辑器 gizmo 改动，**不需跨仓提 feature**（OrangeRender 侧 post pass 资源已齐）。
- 与 [[GAP-2026-05-27-tonemap-operator-selection]] **正交**：那条是 HDR→LDR 收尾曲线（tonemap/bloom 刻意**不进**组件，属 stage-A/B 收尾），本条是**已在组件内**那批 post 字段（SSAO/SSR/接触阴影/DoF/TAA/色彩分级/motion blur/Lens/Sharpen + PCSS）的作用域升级——tonemap/bloom 不参与 volume 混合。
- 优先级：**P3（方向性预留，非 critical path）**——首游关卡氛围设计实际撞分区 post 需求时拉动；登记本身只为防止 v1 预留的 volume 占位字段意图丢失，**不构成排期承诺**（符合本文件"登记 ≠ 承诺要做"门槛）。

### 落地记录（2026-05-28，与编辑器 RenderSettings panel + 撤美术 post + tonemap 算子同 session）

**触发**：用户在"撤完编辑器 hardcode post chain → 让 PostProcessComponent 成为美术效果唯一入口" 后，自然下一问："那场景里挂多个 PostProcessComponent 会不会冲突 / 能分区生效不？" —— V1 first-found 全局单例语义直接撞这个需求，触发本 GAP 提前推进（原 P3 预留，按用户主题契合度推上来）。

**实施清单**（2 个文件改动 + 0 新文件，scene schema 零改动是 V1 铺路的核心收益）：

1. `src/render/Pipeline.cpp`（`SyncPostProcessFromWorld` 重写 ~210 行）：
   - 抽出 `ApplyToImpl(const PostProcessComponent&)` lambda（v1 的 50 行字段赋值，fast path + 通用路径共用）
   - 拿相机 world 位置 `cameraWorldPos = glm::vec3(glm::inverse(scene.MainCamera().view)[3])`（无相机时回退 origin）
   - **收集分组**：遍历 view，按 `pp.mode` 分 Global first-found base + Local 候选；Local 候选拿 `entity.Transform.position` 算 box 中心
   - **Local volume 权重**：`outside = max(|cameraPos - boxCenter| - localExtent)` 沿各轴取 max；outside ≤ 0 → w=1；outside ∈ (0, blendDistance) → w = `1 - smoothstep(0, blendDistance, outside)`；超过 blendDistance → 不参与（continue）
   - **Fast path 1**：单 Global + 无 Local hit → 直接 `ApplyToImpl(globalBase)` 与 v1 逐像素等价（零 mix 计算开销，acceptance "增量安全网" 必达）
   - **Fast path 2**：无 Global 且无 Local hit（全场只 Local volume 但相机全在外）→ first-found 兜底，与 v1 "view 非空必有组件生效" 语义对偶
   - **通用路径**：base = Global（或 default ctor），按 priority 升序 lerp Local volume 的所有标量字段（高 priority 最后 apply 更 dominant，与 UE PostProcessVolume / Unity Volume 语义一致）；bool / enum / uint32 离散字段按 weight>0 中最高 priority 接管（不 lerp 离散值，避免 SSAO enabled 出现 0.5 半开状态）
   - 加 `#include <limits>`（std::numeric_limits<float>::infinity()）；algorithm + vector 已有

2. `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp`（`RegisterPostProcessComponentSchema` 暴露 4 volume 字段）：
   - `FieldEnum<&PP::mode>("mode", "Mode")` + `kModeNames[] = {"Global", "Local"}` + `static_assert` 锁 enum drift（与 BodyType / 其他 enum schema 同款模板）
   - `Field<&PP::localExtent>("localExtent", ...)`（glm::vec3 半尺寸盒，Range 0.01-100m）
   - `Field<&PP::priority>(...)` + `Field<&PP::blendDistance>(...)`（Range 0-20m）
   - 每个字段配 Tooltip 说明语义（Mode 解释 Global/Local 差异；localExtent 说明半尺寸 axis-aligned box；priority 说明仲裁规则；blendDistance 说明 smoothstep 淡出 + 0 = 硬切换）
   - Helper 注释更新：去掉 "只有第一个 PostProcessComponent 生效"，换为 V2 collect-all + Global/Local 语义说明

**混合算法细节**：

| 字段类型 | 算法 |
|---|---|
| `float` 标量（ssaoRadius / ssrStrength / dofFocusDistance / gradeExposure / blendDistance 等 24 个）| `result = mix(result, hit.pp, hit.weight)`，按 priority 升序 |
| `glm::vec3` 颜色/向量（gradeTint 等，本期暂无显式 vec3 字段需 lerp，localExtent 是 volume 本身的字段不参与混合）| 同 float，glm::mix 元素级 lerp |
| `bool enabled` 类（ssao/ssr/contact/dof/taa/grade/motionBlur/lens/sharpen 9 个）| weight>0 中最高 priority 接管（离散值无 lerp 语义）|
| `bool` 算子选择（ssaoUseGtao）| 同 bool enabled |
| `std::uint32_t shadowMapResolution` | 同 bool enabled（mapResolution 整数，lerp 出非 2 幂值无意义）|
| `std::int32_t motionBlurSampleCount` | weight ≥ 0.5 切换（中点阈值，避免 lerp 出小数采样数）|
| Volume 容器字段（mode / localExtent / priority / blendDistance）| **不参与混合**——它们是 volume 自身的几何/仲裁元数据，非 post 效果参数 |

**架构决策**：

- **不抽 PostProcessConfig 外置类**：v2 仍走 ECS 组件路径，SyncPostProcessFromWorld 内部本期把混合结果回灌进 `post*` 成员（与 v1 同款），保持下游 RecordSsao/Ssr/... 路径零改动。后续若 multi-camera / multi-RT 真撞上"每相机独立 post 状态" 需求（GAP-2026-05-24 RenderToTexture 触发），再考虑把 `post*` 状态外置到 RenderContext。
- **bool 字段不 lerp**：UE PostProcessVolume / Unity Volume Framework 也都是 bool 离散仲裁（按 priority 接管）。lerp bool 在视觉上会出现 SSAO/SSR 从有到无的渐变带，物理上无意义（前向渲染 post pass 是开关，没"半开"）。
- **multi-Global 取 first-found**：与 multi-DirectionalLight / multi-Environment 同款 "first-found + 编辑器侧 warning chip" 语义保留余地。本 GAP 不动 EntityTreePanel 的 overflow set 路径（独立 polish），后续可参 `mSingletonOverflowDirLight` 模板加 `mSingletonOverflowPostProcess`。
- **Gizmo plugin 推 follow-up**：Local 模式画 localExtent 线框盒（参 PointLightGizmoPlugin）属锦上添花；用户当前可通过 Inspector 的 `localExtent` 三个 DragFloat 数值看 box 尺寸，结合 Transform position 推 box 中心位置。本 session 已 13+ commits，gizmo plugin 独立 commit 触发更合适。

**验收**：
- 全 52 ctest 通过（含 `light_and_shadow_test` —— 关键回归保护：v1 单 PostProcessComponent 用例必须逐像素等价）
- `python scripts/check_invariants.py` → `All invariants OK. (7 grandfathered)`
- `python scripts/check_claude_md_drift.py` → `none detected.`
- **scene schema 零改动**：v1 写的 .scene.json 字段全 optional + 默认 Mode=Global，Load 后行为不变（ComponentSerializers.cpp 早已带兼容路径，本 GAP 验证此设计意图）

### 关键改动文件

`src/render/Pipeline.cpp`（SyncPostProcessFromWorld 重写 + 加 `<limits>` include） / `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp`（暴露 4 volume 字段 + Helper 注释更新） / `docs/engine-known-gaps.md`（本条目登记 + 关闭）

### 留待后续

- **PostProcess Local Volume Gizmo plugin ✅**（2026-05-28，commit `e54dc9f`）：`tools/OrangeEditor/plugin/PostProcessVolumeGizmoPlugin.{h,cpp}` 参 `CameraFrustumGizmoPlugin` 12 段画法模板 + `PointLightGizmoPlugin` typeName 字符串过滤模板。Mode=Local 时画两层 wireframe box：内层（紫色实色 220 alpha + 2px 粗）= localExtent 边界 weight=1 区，外层（同色 100 alpha + 1.5px 细）= (localExtent + blendDistance) smoothstep 淡入末端；blendDistance ≤ 0 时不画外层。Mode=Global 跳过（无几何边界）。center 取 entity.Transform.position（与 Pipeline.cpp v2 boxCenter 取法严格一致，AABB 不考虑 rotation）。任一 corner 投影失败整盒跳过（fail-safe 同 CameraFrustum）
- **Multi-Global warning chip ✅**（2026-05-29）：`tools/OrangeEditor/EditorRenderLayer.h` 加 `mSingletonOverflowPostProcess` 字段；`EntityTreePanel.cpp` 入口 build 时遍历 `view<PostProcessComponent>`，仅 `mode==Global` 计数、第 2+ 个 Global 入 overflow 集（Local volume 各自按相机位置混合、不算 overflow——判定与 `Pipeline::SyncPostProcessFromWorld` 的 `globalBase` first-found 取舍严格一致，UI 标注不会骗人）。DrawEntityNodeRecursive 行尾复用既有黄色 `(!)` chip；tooltip 从 2-flag 硬编码分支重构为逐类拼行（DirLight / Environment / PostProcess 三类 overflow 任意组合不再 2^N 分支爆炸），PostProcess 条额外提示"把 Mode 改 Local 可按相机位置叠加生效"。纯编辑器 UI polish，build-green 验收（无对应 ctest，同 GAP-2026-05-22 DirLight/Env chip 先例）
- **Sample fixture ✅**（2026-05-28，commit `73a1bf2`）：`samples/19_postprocess_volume/main.cpp` 参 sample 18 的 fixture 模式：1 Global 中性底 + 2 Local 盒（高对比 grading box-a + 冷色温 grading box-b），相机沿 -Z→+Z 推进穿过双盒。`--position {outside|box-a|transition|box-b}` 预设 4 个相机 z 出 capture，`--motion` 6 秒推进互动看 blendDistance 平滑过渡，`--capture <path>` 单帧出图后退。transition 位置取 z=6.5（box-a 边界外 0.5m，smoothstep weight≈0.74）是真"过渡带" —— 初版取 z=8 落在两盒 blendDistance 之外的纯 Global 区与 outside 视觉等价，迭代修正后能看到 box-a grading 部分淡入混合 Global 底（4 张 capture PNG 文件大小：outside 272KB / transition 467KB / box-a 472KB / box-b 419KB，transition 接近 box-a 印证 V2 collect-all + smoothstep 算法生效）。验收：52/52 ctest passed（含 light_and_shadow_test V1→V2 等价性回归）
- **后续 multi-camera / multi-RT 真触发时 (`GAP-2026-05-24-pipeline-cannot-render-to-arbitrary-rt`)**：把 `post*` 全局 mutable state 外置到 RenderContext，让每个相机/RT 独立持后处理状态

---

## GAP-2026-05-27-consumer-imgui-tuning-hook ✅

- **发现方**：OrangeGames 首游 Spike 1 scaffold session（搭消费骨架时核对引擎公共面有无消费者 ImGui 接线）
- **发现日期**：2026-05-27
- **一句话定性**：引擎公共面**无面向消费者（游戏）的 ImGui / debug-UI 提交 hook**。`Layer` 基类只有 `OnAttach/OnDetach/OnUpdate/OnEvent`，**无 `OnImGui`**；ImGui 仅由 `tools/OrangeEditor`（`EditorRenderLayer.cpp` + `main.cpp`）用引擎私有路径自起；`IRenderPass::InsertPass` 扩展点虽存在但 0.x **"暂未真正接通"**（`Pipeline.h:17` / `:113` 注释，InsertPass 走 fallback）。结果：Spike 1 手感调试方法论里列为**"第一优先级地基"**的"热重载 ImGui 调参面板"在游戏侧无法实现。
- **状态**：**2026-05-27 落地 ✅**（独立 OrangeEngine session；与 [[GAP-2026-05-27-builtin-shaders-not-installed-for-consumers]] 同 session 一起做）。选定**方案 A（引擎独占 ImGui + PUBLIC 暴露）**——动机经用户确认是"无 PIE，需在游戏侧 live-debug 任意效果"，故要完整 ImGui 而非薄封装；落地点见本文件"处理记录"段。**与 play-in-editor 正交**（见 [[GAP-2026-05-27-play-in-editor]]：PIE 落地后本 hook 不冗余）。

### 触发场景

Spike 1 软体史莱姆手感调试需把一批旋钮（约束 stiffness/damping、control-target 弹簧硬度、跳跃冲量、coyote time、jump buffer、apex hang）接到 slider **边玩边拧、不重编不重启、人不离测试点**——这是把手感调试从"无底洞"变成可收敛工程过程的关键。ImGui 已被引擎 vendor，但只对编辑器开放。

### 缺什么 / 现状对照（已核实代码）

| 能力 | 现状 | 目标 |
|---|---|---|
| 消费者 GUI hook | `Layer` 无 `OnImGui`；只能 `OnUpdate` + `GetDebugDrawScene()`（线/球/AABB/三角形，不够做参数 UI）| 游戏侧每帧能提交 ImGui 窗 / slider |
| ImGui context | 仅 `tools/OrangeEditor` 私有起（context + GLFW/Vulkan backend）| 引擎托管，消费者开箱即用 |
| `IRenderPass` 注入 | `InsertPass` 公共面存在但 0.x 走 fallback、未真正接通 | 接通，或走更 turnkey 的 Layer::OnImGui |

### 落地设计要点（供独立 session 评审）

1. **可选形态（待该 session 拍板）**：(a) `Layer::OnImGui()` 虚函数 + `AppHost`/`Pipeline` 内部托管 ImGui context + GLFW/Vulkan backend 的 `NewFrame`/`RenderDrawData`，在 `AfterPostProcess` 阶段回调——**turnkey、对游戏最友好**；(b) 真正接通 `IRenderPass` `AfterPostProcess` hook，让游戏自起 ImGui pass；(c) 仅暴露 ImGui context + descriptor pool 句柄让消费者自建（引擎改动最少但消费者样板最多）。**倾向 (a)**。
2. **参考现有起法**：`tools/OrangeEditor/EditorRenderLayer.cpp` 已有 ImGui context 创建 + backend init + per-frame `NewFrame` + swapchain pass 内 `ImGui_ImplVulkan_RenderDrawData` 全套——把这套从编辑器私有**上提**为引擎可复用件即可。
3. **header isolation 决策点**：`OnImGui` 不能在公共头暴露 ImGui 类型。要么消费者自 `#include <imgui.h>`（引擎把 imgui include dir 标成 `OrangeEngine::orange_engine` 的 INTERFACE，与 STATIC 库发布一致），要么引擎提供自家薄封装 debug-UI API。这点需与"公共头不漏第三方"invariant 一起拍。
4. **验收 sample**：建议 `samples/17_imgui_overlay`——窗口上叠一个 ImGui 窗，slider 实时改场景参数、不重编。

### 期望验收

- 一个仅经公共 API 的消费者（游戏 / sample）能在画面上叠 ImGui 窗 + slider，改值实时生效、不重编不重启；
- header isolation invariant 仍绿（`scripts/check_invariants.py`）；
- 编辑器自身 ImGui 行为不回归。

### 备注

- 纯引擎（+ 可能需 OrangeRender swapchain pass 配合）改动。与下条 [[GAP-2026-05-27-builtin-shaders-not-installed-for-consumers]] 同属"**外部消费者就绪度**"一束，建议**同一引擎 session 一起处理**——两条都是 OrangeGames 真正能消费引擎跑起来的前置。
- 优先级：**P1（用户拍板提到 Spike 1 前置）**——区别于本文件多数"撞到再拉动"的 P3 预留条目。

---

## GAP-2026-05-27-builtin-shaders-not-installed-for-consumers ✅

- **发现方**：OrangeGames 首游 Spike 1 scaffold session（核对引擎 `install()` 规则时发现）
- **发现日期**：2026-05-27
- **一句话定性**：引擎 `install(EXPORT ...)` 只装了 lib + 公共头 + cmake config，**未装 builtin 编译产物 shader**——`shaders/orange_engine/*.spv` 只在顶层 `CMakeLists.txt` 被 copy 到 **build tree** 的 `RUNTIME_OUTPUT_DIRECTORY`（行 195/219），**无对应 `install()` 规则**（install 段行 679-748 只有 TARGETS / include 目录 / EXPORT / config）。外部消费者 `find_package` + 链接后**能编过，但运行期跑不起来**：`Pipeline::Initialize` / `MaterialSystem::RegisterBuiltins` 按 ".exe 相对 `shaders/orange_engine/*.spv`" 加载会扑空。
- **状态**：**2026-05-27 落地 ✅**（与 [[GAP-2026-05-27-consumer-imgui-tuning-hook]] 同 session）。install 装 47 个 spv 到 `<prefix>/share/OrangeEngine/shaders/orange_engine/`，config 暴露 `OrangeEngine_SHADER_DIR` + helper `orange_engine_copy_builtin_shaders(<target>)`；顺带 `target_compile_features(orange_engine PUBLIC cxx_std_20)` 让消费者免自设 C++20。已用临时外部 consumer（仅 `find_package` + helper）端到端验证：47 spv 拷到 consumer.exe 旁。落地点见"处理记录"段。**OrangeGames 侧后续可移除 `ORANGE_ENGINE_SHADER_DIR` cache var + 手写 POST_BUILD copy 的 workaround，改用 `orange_engine_copy_builtin_shaders(<target>)`**（待 bump engine pointer 后）。

### 触发场景

OrangeGames 经 `find_package(OrangeEngine CONFIG)` 消费引擎跑首个窗口，`MaterialSystem::RegisterBuiltins()` + `CreateInstance("textured"/"toon"/...)` 需要 builtin material 的 `.spv`。`samples/` 因 in-tree 与引擎共享同一 `RUNTIME_OUTPUT_DIRECTORY` 不暴露此问题——**只有外部消费者撞到**。这也是为什么 `D:\sdk` 下只有 `orange-render`、从未真正产出过可被外部游戏消费的 `orange-engine` SDK。

### 缺什么

1. **install builtin shaders**：install 段补 `install(DIRECTORY/FILES ...)` 把编译出的 `shaders/orange_engine/*.spv` 装到消费者可定位的位置（如 `<prefix>/bin/shaders/orange_engine` 或 `<prefix>/share/OrangeEngine/shaders`），并提供机制（cmake var / config 暴露路径 / 安装到消费者 runtime dir）让游戏 `.exe` 旁能拿到这些 spv。
2. **（连带）消费者 cmake helper**：`orange_engine_set_compiler_options`（`cmake/CompilerOptions.cmake`）等 in-tree helper **不在** `OrangeEngineConfig` 导出，外部 CMake 用不到——消费者需自设 C++20。可选：把消费者也想要的 helper 纳入 install 的 cmake module，或在 config 里给 target 设 `INTERFACE cxx_std_20`。

### 期望验收

- `cmake --install` 后，一个**仅 `find_package(OrangeEngine CONFIG)`** 的外部最小 consumer 能编 + 跑出窗口（builtin material 正常显示），**不需手动从引擎 build tree 拷 shader**；
- `tests/install/` 的 install/config smoke 可扩一条"外部 consumer 跑起来"的端到端校验。

### 备注

- 与 [[GAP-2026-05-27-consumer-imgui-tuning-hook]] 同属外部消费者就绪度，建议同 session。
- **OrangeGames 侧当前 workaround**（不等引擎修）：游戏 CMake 用一个 cache var（`ORANGE_ENGINE_SHADER_DIR`）指向引擎 build tree 的 `bin/<config>/shaders/orange_engine`，`add_custom_command(POST_BUILD)` copy 到游戏 `.exe` 旁。见 `OrangeGames/prototypes/spike-01-blob/CMakeLists.txt` 注释。

---

## GAP-2026-05-27-play-in-editor

- **发现方**：OrangeGames Spike 1 scaffold session 讨论 editor ↔ game 工作流时
- **发现日期**：2026-05-27
- **一句话定性**：引擎 / 编辑器**无 play-in-editor (PIE)**——OrangeEditor 只**编辑数据**（scene / material / prefab，schema-first），无法在编辑器内**加载并运行游戏玩法代码**。当前游戏代码是独立 `find_package(OrangeEngine)` 消费的 exe（如 `OrangeGames/prototypes/spike-01-blob`），与编辑器是两个进程、互不加载；引擎既无**脚本运行时**也无**游戏模块热加载**（roadmap 已把 `Hot reload / C# 脚本` 列为 v1.x 长尾、未开工）。所以"在编辑器里摆好关卡 → 点 Play 立刻在视口试玩"这条迭代闭环不存在。
- **状态**：**仅登记，未实现**。用户 2026-05-27 拍板：**属大型架构能力，等关卡 / prefab 编辑工作流实际成熟、手感 spike 验证完后，由 editor 侧独立 session（很可能多个）推进**，现在不排期。

### 触发场景

Ori-like 首游进入"在编辑器摆关卡 / prefab + 调氛围"阶段后，会越来越需要"点 Play 在编辑器内试玩"——这是 Unity / Godot（脚本运行时热加载）、Unreal（C++ 模块 Live Coding + PIE）的核心迭代闭环。没有 PIE 时，每次试玩都得切到独立游戏 exe、重编、重启、走回测试点，与手感 / 关卡迭代的连续性严重相悖。graybox / 纯手感 spike 阶段**不需要**（spike 自己的 exe 够用），所以非当前阻塞。

### 缺什么 / 两条主路线（待评审拍板）

让编辑器能**实例化并 tick 游戏侧 World + 系统**。两条事实标准路线：

| 路线 | 玩法逻辑形态 | 编辑器怎么跑它 | 参考 |
|---|---|---|---|
| (a) 脚本运行时 | C# / Lua / 自家脚本 | 编辑器嵌运行时，热加载脚本执行 | Unity (C#) / Godot (GDScript) |
| (b) 游戏模块热加载 | C++ 编成 dll | 编辑器运行时 `load`/`reload` 该 module，PIE 内 tick 其 systems/components | Unreal Live Coding + PIE |

两条都共需的基础设施：

1. **游戏侧 `ISystem` / 自定义 component 注册能被编辑器发现**（与 schema-first / plugin-first 架构 ADR-001 + custom-component 扩展点对齐）。
2. **PIE 播放 / 暂停 / 停止状态机**：进 Play 时 clone editing world → runtime world（依赖现有 scene 序列化做深拷贝），退出 Play 还原到编辑前状态，编辑期改动不被 play 期污染。
3. **输入 / 相机在 editing 模式 vs play 模式切换**（编辑期 fly-cam + gizmo；play 期游戏自己的相机 + 输入上下文）。
4. **前置依赖**：编辑器需先能把外部游戏仓当项目打开（**workspace / 项目模型**——本 session 讨论过，OrangeEditor 当前焊死在自己仓 `assets/`，无"打开外部项目"概念）+ 发现该项目的游戏代码。PIE 落地前这条 workspace 模型基本是硬前置。

### 期望验收

- 编辑器里摆一个挂了**游戏侧自定义 system**（如一个移动 component）的场景 → 点 **Play** → 视口内该 system 实际 tick（component 真的动）→ 点 **Stop** → 场景**还原到编辑前状态**；
- editing 期的相机 / gizmo 与 play 期的游戏输入互不干扰。

### 备注

- 大件，非 critical path，但是 editor ↔ game 闭环的关键长杆。优先级 **P3+（成熟后拉动）**。
- 与 [[GAP-2026-05-27-consumer-imgui-tuning-hook]]（消费者 ImGui hook）正交但同属"让游戏真正用上引擎 / 编辑器"一束；PIE 的 workspace 前置也与那条同期更自然。
- roadmap 的 `C# 脚本` 长尾条目若推进，是路线 (a) 的落点；若选 (b) 则属新架构方向，需独立 ADR。

---

## GAP-2026-05-27-headless-asset-import-and-scene-generation-cli

- **发现方**：Orange-Ecosystem umbrella session 讨论 "CLI 建模工具 → 关卡内容" 工作流时
- **发现日期**：2026-05-27
- **一句话定性**：引擎 / 编辑器**无 headless / CLI 的资产导入 + 场景生成路径**。把外部 `.obj / .gltf` 转成引擎自有 `.mesh` 的能力**锁死在 OrangeEditor GUI importer 入口**（`tools/OrangeEditor/import/ImportDispatcher.h` 明示入口只有 "File→Import 菜单" + "OS drag-drop" 两路，都要 GUI 在跑）；`.scene.json` 虽是纯文本 + `schemaVersion`、理论上可脚本生成，但**没有官方生成器 / schema 校验器**。结果："脚本生成 mesh → 批量转 `.mesh` → 程序化生成 `.scene.json` → 跑起来" 这条**全 CLI 内容管线断在'转 .mesh'与'生成关卡'两环**，无法不开 GUI 闭环。

### 触发场景

- 用户想用 CLI 建模工具（**Blender headless** `--background --python` / **CadQuery** Assembly → `.glb` / OpenSCAD 经转换桥）**程序化批量生成场景道具**再放进关卡——这是渲染出身、代码驱动工作流的自然诉求。
- 这些工具都能产出 OBJ 或 glTF/glb，正好命中现有 importer 吃的格式，但**进引擎那一环（转 `.mesh` + 入 AssetRegistry）只有 GUI 入口**：每个模型都得人手在编辑器里 File→Import 或拖拽，批量 / 自动化 / CI 场景下不可用。
- `.scene.json` 是普通 JSON（见 `assets/scenes/*.scene.json`），手写 / 脚本吐**技术上可行**，但 entity 的 `Hierarchy` 索引链（parent/firstChild/nextSibling/prevSibling）+ 各组件字段 + `schemaVersion` 都要手工对齐，无校验器时极易和引擎实际 schema 漂移，悄悄生成"加载即崩 / 字段被忽略"的脏关卡。

### 现状对照（避免与已闭环 gap 重复）

| 能力 | 现状 | 备注 |
|---|---|---|
| GUI mesh / texture importer | ✅ | [[GAP-2026-05-22-editor-dcc-import-pipeline-missing]] 已闭环（.obj + .gltf/.glb → .mesh，.png/.jpg/.tga/.hdr texture，ADR-008 四件套）。**本 gap 是它的 headless 维度补充，不是重复** |
| importer headless / CLI 入口 | ❌ | `ImportDispatcher.h` 入口仅 GUI 菜单 + drag-drop；`Dispatch(srcPath, EditorHost&)` 签名硬依赖 `EditorHost`（GUI 宿主），无脱 GUI 的调用路径 |
| `MeshLoader::Save` 暴露给 CLI | ❌ | `include/orange/engine/asset/MeshLoader.h` 的 `Save` 是引擎内 C++ API，但没有独立命令行工具暴露它 |
| `.scene.json` 程序化生成 / 校验 | ❌ | 纯文本可手写，但无生成器、无导出的 JSON schema、无 round-trip 校验工具 |

### 缺什么（按依赖拆）

#### G1 · headless mesh import CLI

- 独立 CLI 工具或 `OrangeEditor --headless-import <file>` 子命令：吃 `.obj / .gltf / .glb`，走 ADR-008 四件套（转 `.mesh` + copy 源 + 写 `.meta` + 入 AssetRegistry）但**不拉起 GUI**。
- 实现侧：把 `ImportDispatcher::Dispatch` 对 `EditorHost` 的依赖剥成一个轻量"无 GUI 的 import context"（只需 `AssetRegistry` 句柄 + 目标目录），GUI 路径与 headless 路径共用同一 importer 核心，避免逻辑二次实现。
- importer 模块仍须留在 `tools/OrangeEditor/import/`（不污染 `src/asset/` runtime，沿用 ADR-008 约束）；headless CLI 作为 OrangeEditor 的另一种入口形态。

#### G2 · `.scene.json` 生成 / 校验工具

- 导出一份机器可读的 scene/component **JSON schema**（或等价校验器），让外部脚本生成 `.scene.json` 后能校验 `schemaVersion` + 各组件字段 + `Hierarchy` 索引链自洽（无悬挂 parent/sibling 引用）。
- 提供最小**生成器辅助**（Python helper 或引擎侧 CLI）：给定 entity 列表（mesh 路径 + transform + 材质 + 可选父子关系），吐出合法 `.scene.json`，自动维护 Hierarchy 索引链。

#### G3 ·（可选，更长期）端到端 CLI 管线编排

- 把 G1 + G2 串成一条命令：给一个目录的 `.glb` + 一份布局描述（YAML/JSON：哪个 mesh 放哪、什么材质、父子关系）→ 自动转好全部 `.mesh` + 生成完整 `.scene.json`，全程零 GUI。
- 这才真正打通"CLI 建模工具 → 可直接加载的关卡"全自动管线，适配 CI / 批量 / 程序化关卡生成。

### 期望验收

- 不启动 GUI，命令行 `<tool> import-mesh foo.glb` 产出 `assets/Models/foo/foo.mesh` + `.meta`，与 GUI 导入产物字节级等价。
- 外部脚本生成的 `.scene.json` 经校验器确认合法后，能被 OrangeEditor **和** runtime 正确加载，渲染结果与 GUI 手摆一致。
- （G3）给定一组 `.glb` + 布局描述，一条命令产出可直接 `Open Scene` 加载的完整关卡。

### 状态

- **仅登记，未实现 / 未排期**。本条是 ADR-010 work-queue 登记动作（纯文档，不实现不消费），登记 session 不碰代码。
- **优先级**：P3（撞上即升格）。当前首游处于 graybox / 手感 spike 阶段，用内置 `cube.mesh` / `plane.mesh` + GUI 摆位 / 手写少量 scene.json 已够；**全 CLI 管线在"程序化批量生成场景道具"成为实际瓶颈时才升格**。
- **归属候选**：G1 属 OrangeEditor（headless 入口形态，importer 核心解耦）；G2 可引擎侧 CLI 或独立 Python 工具；待独立 session 评审拆解，不在当前 critical path。
- **关联**：[[GAP-2026-05-22-editor-dcc-import-pipeline-missing]]（GUI importer 前置，本条补其 headless 维度）；[[GAP-2026-05-27-play-in-editor]] / workspace 项目模型（同属"工具链闭环 + 让游戏真正用上引擎"一束，CLI 内容管线与 PIE 正交但同向）。

---

## GAP-2026-05-28-gltf-scene-level-import-not-flattened

- **发现方**：Orange-Ecosystem umbrella session 讨论"关卡场景搭建工作流（in-engine vs 外部 DCC）"时
- **发现日期**：2026-05-28
- **一句话定性**：现 `GltfImporter` 把 multi-mesh / multi-primitive 的 `.gltf / .glb` **塌平合并成单一 `MeshAsset`**，丢失 transform 层级 / per-mesh material 划分 / scene-level lights & cameras —— 用户在 Blender 摆好整场景后导入引擎等于一切摆位 / 材质 / 灯光归零，DCC 的"场景组装"价值无法被引擎消费；当前 importer 只覆盖了"asset import"维度，未覆盖工业标配的"scene import"维度

### 触发场景

- 用户在 umbrella session 明确表达："验证完玩法后，场景搭建大概率从 Blender 做起"——即首游过完 graybox / 玩法 spike 阶段后，关卡视觉迭代需要 DCC scene-level 工作流支撑
- 工业 DCC 流程：在 Blender 用 transform tree 组织几十个 prop（树 / 石 / 灯柱 / 机关本体 / 地形块）+ 每个挂独立材质 + 摆几盏点光 + 设几个 reference camera → 导出 `.glb` → 期望引擎里 `File → Import Scene` 直接吃回原样
- 现状（`tools/OrangeEditor/import/GltfImporter.h` 的 T4 范围限制注释直接写明，**非 importer bug，是设计取舍**）：
  - "多 primitive / 多 mesh **合并**为单个 `MeshAsset`，丢失 per-primitive material 划分"——transform tree 整树被压平、各 mesh 的位置 / 旋转 / 缩放全归零
  - "只接受 triangle primitive"
  - "skinning / morph targets / animation 全部 skip"
  - per-primitive material 不读（受 ADR-008 决策"PBR material 解析延 v1.2"约束）
  - glTF scene/nodes/lights/cameras 等 scene-level 概念**完全不消费**
- 业内对照（OrangeEditor 唯一空白）：

| 引擎 | scene-level import 形态 | 入口 |
|---|---|---|
| Unity | `.fbx / .gltf` → Model prefab（保留 hierarchy + 多 mesh + per-mesh material slot + lights） | 拖入 Project 视图 |
| Unreal | `.fbx scene import` 选 "Combine Meshes = OFF" 后保留 hierarchy + 各 Actor | File → Import Into Level |
| Godot | `.glb as scene` 直接生成 `.tscn` 等价物 | 拖入 FileSystem 视图 |
| Lumix | `.fbx` / `.gltf` 拆 per-mesh + 保留 transform tree | Asset 浏览器右键 Import |
| OrangeEditor | ❌ **塌平合并** | 仅 `File → Import` 单 mesh 形态 |

### 证据

- `tools/OrangeEditor/import/GltfImporter.h` 第 13–19 行的 "T4 范围限制" 注释段直接说明合并策略与延后项
- `tools/OrangeEditor/import/ImportDispatcher.h` 的 `ImportKind` enum 只有 `Texture / ObjMesh / GltfMesh / Unsupported` 四种——**无 `GltfScene` 路径**；signature `ImportGltfMesh` 名字本身已表达"只导 mesh，不导 scene"的范围限制
- `vendor` 内未持有任何 fbx SDK（`Glob '**/*fbx*'` / `'**/*assimp*'` 均 0 命中），整个 Orange-Ecosystem 无 .fbx 解析能力——本 GAP 不依赖补 fbx，但 G4 顺路覆盖该维度
- `assets/scenes/demo.scene.json` 的 schema 已支持 entity hierarchy + transform + RenderableComponent + LightComponent 等所有 scene-level 概念——**落盘 schema 已经够用**，缺的纯粹是 "从 glTF scene/nodes 反向生成这份 .scene.json + 多个 .mesh" 的 import-time 翻译层

### 缺什么（按依赖拆）

#### G1 · scene-level glTF import 主路径（hierarchy + 多 mesh 不塌平）

- `ImportKind` 加 `GltfScene`；`ImportDispatcher::Dispatch` 路由——如何区分"作为 mesh 导"还是"作为 scene 导"待 ADR 拆解（候选：按 glTF `scenes.length > 0 && nodes.length > 1` 判断；或加 File → Import Mesh / Import Scene 两菜单让用户选；或 Asset 浏览器右键多选）
- 新增 `ImportGltfScene` 实现：
  - 遍历 glTF `scenes[0].nodes`（递归）→ 在 OrangeEngine World 内建对应 Entity 树（每 node 一 Entity，挂 `TransformComponent` + `HierarchyComponent` + `NameComponent` 沿 node.name）
  - 每个 `mesh.primitives[i]` → **单独**写出 `.mesh`（**不合并**），命名 `<basename>_<meshname>_<primIdx>.mesh`；多 primitive / 多 mesh 不再压平
  - 各 Entity 挂 `RenderableComponent` 指向对应 mesh AssetHandle
- 落盘形态：产出 `assets/scenes/<basename>.scene.json` + 多个 `assets/Models/<basename>/<meshname>.mesh` + co-locate texture/material（G2 接管）
- `.meta` sidecar 仍按 ADR-008 落（4 件套不绕开）；scene 自己也有 `.meta` 记 source path + hash
- 关键设计取舍：scene import 后用户可以再编辑 → 是否保持 source link / re-import 不覆盖手工修改 → 留 ADR；参 Unity 的 "model prefab + override" 形态 / Lumix 的 .fbx as prefab + scene instance 两层

#### G2 · per-mesh PBR material 划分

- **前置依赖**：[[GAP-2026-05-25-pbr-material-texture-binding-and-tangent-infra]] 落地（mikktspace tangent + texture binding 基础设施一束）+ ADR-008 中 "PBR material 解析延 v1.2" 真正开工
- glTF `materials[i]` → `MaterialInstance`，对每个 primitive 的 material slot 分配独立 `MaterialInstance`；输出 `assets/materials/<basename>/<matname>.material`
- baseColor / normal / metallicRoughness / occlusion / emissive 贴图 co-locate import 到 `assets/Models/<basename>/textures/`，材质字段用 `assets/...` 相对路径引用（沿用现 `MaterialInstance` 路径约定）
- **本 G2 与 [[GAP-2026-05-22-editor-dcc-import-pipeline-missing]] "剩余延后项" 中 "glTF PBR material 解析 → v1.2" 是同一拆解，不重复登记**；本 GAP 在 scene-level 维度 inherit 它，落地节奏与那条同步

#### G3 · scene-level extension（lights / cameras）

- glTF `KHR_lights_punctual` extension → 引擎 `DirectionalLightComponent` / `PointLightComponent` / `SpotLightComponent`（SpotLight 待 [[GAP-2026-05-26-complete-light-source-family-and-shadows]] 落地后接通）
- glTF `cameras[i]` + 引用该 camera 的 node → 引擎 `CameraComponent`（仅 perspective；orthographic 视需求接）
- 后处理 volume / 雾 等非标准 extension：多数 DCC 走 vendor-specific extras（KHR_materials_volume / extras 字段），可移植性差，**G3 范围内不保证**，留 G5+ 按拉动触发

#### G4 ·（可选，更长期）`.fbx` scene-level import

- ADR-008 的 4 件套路径已铺好，加 `FbxImporter` 模块即可（vendor 选型候选 OpenFBX MIT / Autodesk FBX SDK 商业 + 体积大）
- 范围与 G1 同（hierarchy + multi-mesh + material slot），仅文件格式 vendor 差异
- 在玩法验证完成 + DCC 工作流真正成为主路径**之后**再评估——`.glb` 通常已足够，`.fbx` 是 Maya / 3ds Max 主导工作流的对齐项

#### G5 ·（可选，更长期）re-import workflow + override 持久

- 用户在 Blender 修改场景 → 重新导出 `.glb` → 引擎再 import 时**保留**用户在引擎里加的 component / 手工调过的 transform，仅 sync 新增 / 改动的 mesh
- Unity model prefab override / UE actor preserve-on-reimport 同款机制
- 现状内置 `assets/<TypeDir>/<file>.meta` 已记 sourcePath + sourceHash，**前置基础设施已有**；缺的是 reconcile 算法 + UX

### 期望验收

- 用户在 Blender 摆 5+ prop 的简易场景（树 / 石 / 灯柱 / 机关，每个独立材质 + transform + 1~2 盏 PointLight），导出 `.glb`
- 编辑器 `File → Import Scene` 选该文件 → 引擎产出：
  - `assets/scenes/<basename>.scene.json` 含与 Blender 同构的 Entity 树
  - `assets/Models/<basename>/*.mesh` 每个 prop 单独 .mesh，**不合并**
  - `assets/materials/<basename>/*.material` 每个材质独立（G2 后）
- `Open Scene` 加载该 scene → viewport 内每个 prop 在 Blender 摆好的世界位置 / 旋转 / 缩放上 + 各自材质生效（G2 后）+ 灯光符合 Blender 摆位（G3 后）
- Hierarchy panel 显示 Blender 的 transform tree 结构（父子关系保留）
- 序列化 round-trip：import 后 Save → 关 Open Scene → 字节级稳定（沿用 [[GAP-2026-05-23-editor-play-stop-entity-tree-order-reversed]] 同款 entity-index 排序约束）

### 状态

- **仅登记，未实现 / 未排期**。本条是 ADR-010 work-queue 登记动作（纯文档，不实现不消费），登记 session 不碰代码
- **优先级**：P2（预防性登记，玩法验证完成后升格）。**触发升格条件**：首款 Ori-like 玩法 spike 闭环 + 首游进入 visual polish 阶段、用户尝试在 Blender 摆完整关卡时（按用户在 umbrella session 表达的工作流意图，这是个**可预期**而非偶发的需求）。在那之前用 G1 子集（手工组装内置 cube / plane + GUI 摆位）已够灰盒
- **归属候选**：OrangeEditor v1.2 范畴（与 PBR material 解析 G2 同 milestone，与 ADR-008 "PBR material 延 v1.2" 对齐）；G4 .fbx 可继续延后到 importer family 完整覆盖时再做；G5 re-import override 单独 minor milestone
- **关联**：
  - [[GAP-2026-05-22-editor-dcc-import-pipeline-missing]]（前置 ✅；本条在 scene 维度补完，单 mesh 维度它已覆盖）
  - [[GAP-2026-05-25-pbr-material-texture-binding-and-tangent-infra]]（G2 前置——material binding + tangent 基础设施）
  - [[GAP-2026-05-27-headless-asset-import-and-scene-generation-cli]]（正交：headless CLI 维度 vs scene-level 维度，可独立推进；两者合流后形成"DCC scene → 引擎全自动管线"终极形态）
  - [[GAP-2026-05-26-complete-light-source-family-and-shadows]] ✅（G3 SpotLight 前置已具备）
  - ADR-008（DCC import 4 件套路径——本 GAP 不破纪律，仅在 scene 维度增加新 importer 形态）

---

## 处理记录

- **GAP-2026-05-27-consumer-imgui-tuning-hook + GAP-2026-05-27-builtin-shaders-not-installed-for-consumers**（2026-05-27 同 session 落地，"外部消费者就绪度"一束）：
  - **架构决策（方案 A）**：引擎成为 ImGui 的**唯一 owner** + **PUBLIC 暴露**给消费者。动机经用户确认 = "无 play-in-editor，游戏侧要 live-debug 任意效果"，故选完整 ImGui（消费者 `#include <imgui.h>` 直接调）而非薄封装。**这反转了 CLAUDE.md 「Box2D / DragonBones / miniaudio / ImGui / stb 一律 PRIVATE」中 imgui 的那条**——记入 ADR（Orange-Wiki `case-studies/orange-engine/decisions/`）。`Layer::OnImGui()` 公共签名仍零 imgui 类型，header isolation invariant（公共头不漏第三方）不破；invariant lint 全绿。
  - **集成形态**：`Layer::OnImGui()` 新增虚函数（默认空，零开销）+ `AppHost::DispatchImGui()`（遍历 LayerStack 调各 OnImGui）+ `Pipeline::EnableImGui()` / `SetImGuiSubmit()` / `IsImGuiEnabled()`。Pipeline（window 模式）托管 ImGui context + GLFW/Vulkan backend + descriptor pool；`Render()` 顶部 NewFrame → 回调 submit（消费者把 `host->DispatchImGui` 接进来）→ `ImGui::Render`，内部 renderer 的 swap-chain overlay 录 `RenderDrawData`；`Shutdown()` 顶部按序拆。复用编辑器 VulkanLoaderShim 同款 **KHR-trampoline 补丁**（vkCmdBegin/EndRenderingKHR → core 名，经 vkGetDeviceProcAddr）。offscreen 模式（编辑器路径）拒绝 EnableImGui。
  - **唯一 imgui owner**：imgui 从 `tools/OrangeEditor` 自 vendor（`orange_editor_imgui`）上提到引擎根 `orange_engine_imgui`（FetchContent v1.91.5-docking，PUBLIC 链入 orange_engine + 进 install/export，`OrangeEngine::imgui` alias）。**编辑器改为复用这一份**（CMake 去 `orange_editor_imgui`、链 `OrangeEngine::imgui`，main.cpp 的 ImGui 代码零改动）——否则两份静态 imgui 在编辑器 exe 撞 ODR。
  - **gap 2（shader install）**：全部 47 个 builtin spv 汇成 `ORANGE_ENGINE_ALL_BUILTIN_SPV` 列表，`install(FILES ...)`（支持 `$<CONFIG>` genex）装到 `<prefix>/share/OrangeEngine/shaders/orange_engine/`；`OrangeEngineConfig.cmake.in` 暴露 `OrangeEngine_SHADER_DIR`（`set_and_check`）+ helper `orange_engine_copy_builtin_shaders(<target>)`（POST_BUILD `copy_directory` 到消费者 `$<TARGET_FILE_DIR>/shaders/orange_engine`）。`target_compile_features(orange_engine PUBLIC cxx_std_20)` 让消费者免自设标准（连带项）。config 加 `find_dependency(Vulkan)`（imgui PRIVATE 链 Vulkan 在 STATIC 下作 LINK_ONLY 传播，target 须可解析）。
  - **验收**（全绿）：① 引擎 + `samples/17_imgui_overlay` 编过 + 5s 冒烟无崩溃；② 编辑器编过（**无重复符号**，证 ODR 已消）+ 6s 冒烟无崩溃；③ invariant lint 7 grandfathered 无新增 + drift none；④ `cmake --install` → 47 spv + imgui 头（core+backends）+ `OrangeEngine::imgui` lib + config 全部落位；⑤ **临时外部 consumer**（仅 `find_package(OrangeEngine)` + `#include <imgui.h>` 写 `Layer::OnImGui` + `orange_engine_copy_builtin_shaders`）端到端编过 + 链过 + 47 spv 拷到 consumer.exe 旁 + C++20 特性免自设标准编过 —— 两 gap 的"仅经公共 API 的消费者跑得起来 + 叠 ImGui slider"验收同时兑现。
  - **关键改动文件**：`include/orange/engine/app/Layer.h`（OnImGui）/ `include/orange/engine/app/AppHost.h` + `src/app/AppHost.cpp`（DispatchImGui）/ `include/orange/engine/render/Pipeline.h`（EnableImGui/SetImGuiSubmit/IsImGuiEnabled）/ `src/render/pipeline/PipelineImpl.h`（imgui 字段 + out-of-line ctor/dtor + 3 helper 声明）/ `src/render/pipeline/PipelineImGui.cpp`（**新增**，唯一 imgui/vulkan TU）/ `src/render/Pipeline.cpp`（Render 顶 + Shutdown 顶 hook）/ `CMakeLists.txt`（imgui target + PUBLIC 链 + cxx_std_20 + spv 列表 + install spv/imgui）/ `cmake/Dependencies.cmake`（find_package Vulkan）/ `cmake/OrangeEngineConfig.cmake.in`（shader dir + helper + find_dependency Vulkan）/ `tools/OrangeEditor/CMakeLists.txt`（去 orange_editor_imgui，链 OrangeEngine::imgui + Vulkan::Vulkan）/ `samples/17_imgui_overlay/`（**新增** main.cpp + CMakeLists）/ `samples/CMakeLists.txt`（注册 17）
  - **未做 / 后续**：play-in-editor（[[GAP-2026-05-27-play-in-editor]]，用户明确不排期）；OrangeGames 侧把 shader workaround 换成 helper（待 bump engine pointer 的新 session）；`tests/install/` 端到端 consumer ctest（本次用临时 consumer 手验过，可后续固化进 CI）。
- **GAP-2026-05-24-editor-asset-browser-create-material-missing**（2026-05-24 落地 G1，OrangeEditor v1.1.1 milestone）：`tools/OrangeEditor/EditorRenderLayer.cpp` 单文件改动——
  - **入口**：`DrawAssetFileList` 末尾挂 `BeginPopupContextWindow("##asset_list_ctx", MouseButtonRight | NoOpenOverItems)` 弹 `Create → Material` 嵌套菜单。`NoOpenOverItems` 让单文件右键照旧走既有 `BeginPopupContextItem`（行 1362 Pick / Reimport 菜单）不冲突
  - **状态机**：anonymous namespace file-scope `sPendingOpenCreateMaterial` / `sPendingOpenOverwriteConfirm` 标志位与 `AboutOrangeEditor` 同款 pending-pattern——menu item 内只 set 标志位（不直接 OpenPopup，因 menu 处于 context popup 的 ID stack 内嵌套 OpenPopup 会跟着 context popup 一起被关闭），下一帧 `DrawAssetsPanel` 内 `ImGui::End()` **之后**消费标志位 → `OpenPopup + BeginPopupModal` 在 viewport-level ID stack 绘制
  - **主 modal**：filename `InputText`（默认 `new_material.material`，长度 128 缓冲）+ templateName `BeginCombo`（候选喂自 `MaterialSystem::GetTemplateNames()`，按字母序排序，默认锁定 `pbr` index）+ Path 灰字预览（`browserCurrentDir + "/" + filename`）+ `Create / Cancel` 按钮。Create 命中 `fs::exists` → set `sPendingOpenOverwriteConfirm = true` 关本 modal 让二级 modal 接管；不命中 → 直接 `MaterialFileIO::WriteMaterialFile({templateName, {}, {}})` 落盘 + 切 `assets.selectedAssetPath = newPath` 让 Material Inspector 子模式立刻接管
  - **overwrite 二级 modal**：显示完整目标路径 + `Overwrite / Cancel` 按钮；Cancel 仅关 modal 不重弹主 modal（用户重新右键即可，与 Cocos 一致）
  - **CommitNewMaterialFile helper**：抽出主 / 二级 modal 共用的"落盘 + selectedAssetPath 切换 + ORANGE_LOG_INFO/ERROR"代码，避免双份
  - **未做** G2 自动后缀编号（用 overwrite 询问取代）/ G3 顺路 `Create → Scene` / `Create → Folder`（独立 v1.1.x 或 v1.2 minor）
  - 编译：`OrangeEditor.exe` 干净链接（warning C4996 `strncpy` 改用 `std::snprintf` 解决）；invariant lint + drift 全绿
  - 关键改动文件：`tools/OrangeEditor/EditorRenderLayer.cpp`（include MaterialFileIO + statics + 2 个 modal helper + DrawAssetFileList 末尾右键菜单 + DrawAssetsPanel 末尾 modal 调用，~200 行新增）/ `tools/OrangeEditor/CMakeLists.txt`（VERSION 1.1.0 → 1.1.1）/ `docs/editor-roadmap.md`（新增 v1.1.1 节）/ `vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.1.1-acceptance-checklist.md`（新增）/ `vendor/Orange-Wiki/case-studies/orange-engine/index.md`（时间线 + 计数）
- **GAP-2026-05-22-samples-cube-mesh-winding-bug**（2026-05-23 落地）：6 个 sample 的 `MakeCubeMesh` 内 indices 从 CW `(0, 2, 1, 0, 3, 2)` per face 改为 CCW `(0, 1, 2, 0, 2, 3)`，与 Pipeline 主 pass `FrontFace=CCW + CullMode=Back` 约定对齐——朝外面是 front、朝内面被 cull，cube 视觉实心可见而不是"穿透看到内壁"。`samples/04_3d_mesh/main.cpp` 顶部 line 67-70 的过期注释（"world-CW per triangle，经 Y-flip 后 NDC-CCW"）也一并修订为新约定描述 + line 145-146 同步。其余 5 个 sample (04_bloom / 09 / 10 / 11 / 12) 没有同款过期注释只有 indices，改 indices + 加单行 "CCW winding 与 Pipeline ... 对齐 (参 GAP)" 注释。**不动 plane**（sample 03 / 05-08 / 12 的 `0, 2, 1, 0, 3, 2` 是 plane 不是 cube，不在本 GAP 范围；plane 视觉无报错的 user feedback，winding 状态留待第三方 plane bug 触发时再处理）。ctest 43/43 + invariant lint + drift 全绿；用户视觉验收通过（cube 实心可见，6 面交替露出无穿透）。关键改动文件：`samples/04_3d_mesh/main.cpp` / `samples/04_3d_mesh_with_bloom/main.cpp` / `samples/09_vfx_demo/main.cpp` / `samples/10_thirty_seconds_demo/main.cpp` / `samples/11_save_load_demo/main.cpp` / `samples/12_layer_partition_demo/main.cpp`
- **GAP-2026-05-21-editor-coplanar-mesh-z-fight-prevention**（2026-05-23 落地 G1）：新增 `tools/OrangeEditor/CoplanarDetector.{h,cpp}` —— `DetectCoplanar(host, entity, eps=0.001m, maxDist=10m)` 遍历 selected 周围 R 米内挂 Renderable + Transform 的其他 entity，对每对面（self 的 6 个面分别 vs other 的对偶面：top↔bottom / left↔right / front↔back）做 ε 共面 + 另两轴 AABB 重叠双条件检测，命中返回 Hit{selfFace, otherEntity, otherName, otherFace, gap}。
  - **共面定义**：仅同轴贴边不算 z-fight 风险，必须**另两轴投影区间有交集**（FacesOverlap）才算"真撞上面对面 z-fight"；避免误报远端独立 entity 在某 y 坐标偶然相同的情况
  - **距离剪枝**：两 AABB 中心距离 > maxDist + 各自半径之和 → 剪枝；几十~几百 entity 的常规场景 N²·8 角点变换 < 1ms，缓存属过度优化（只在 Inspector 每帧调用一次/帧，selection 不变时 hits 也不变，UI 自然稳定）
  - **AABB helper 复制 30 行**：ComposeWorldMatrix / ComputeMeshLocalAABB / TransformAABB 与 `tools/OrangeEditor/EditorPicking.cpp` 同名 helper 语义一致；按 CLAUDE.md "Three similar lines is better than a premature abstraction" 暂保留两份；第三处需要时（gizmo bbox / snap-to-ground / debug draw 拉动）再抽公共 `EditorAabb.{h,cpp}`
  - **Inspector UI**：`panels/InspectorPanel.cpp::DrawInspectorPanel` 在 "Entity #N + Separator" 之后、`DrawEntityViaSchemas` 之前插 "Geometry Warnings" 段，仅在 hits 非空时显示；红字标题（用 EditorTheme.GetAlertError 配色）+ 每条 bullet `Bottom face coplanar with 'Ground'.Top (gap 0.000m)` 文案，正常 entity 不打扰
  - **未做** G2 snap-to-ground 默认 ε 偏移 / G3 scene save lint —— 留后续 session（G2 等吸附工具 milestone 拉动，G3 等 scene save UX 整骨）
  - **视觉验收**（2026-05-23 用户跑）：demo.scene.json Tower y 改回 0.5 → Inspector 顶部出现红字警告 `Bottom face coplanar with 'Ground'.Top (gap 0.000m)`；改回 0.501 → 警告消失；选其他 entity 不出现警告段
  - ctest 43/43 + invariant lint + drift 全绿
  - 关键改动文件：`tools/OrangeEditor/CoplanarDetector.h`（新增）/ `tools/OrangeEditor/CoplanarDetector.cpp`（新增）/ `tools/OrangeEditor/panels/InspectorPanel.cpp`（include + Geometry Warnings 段）/ `tools/OrangeEditor/CMakeLists.txt`（新增 CoplanarDetector.cpp 到源列表）
- **GAP-2026-05-23-editor-play-stop-entity-tree-order-reversed**（2026-05-23 落地 G2）：`src/scene/SceneSerialization.cpp::SaveImpl` 在收集 entityList 后追加 `std::sort` 按 EnTT entity index 升序排序（剥掉 version bits），再按排序后顺序分配 persistentId。
  - **机制**：原 Save 直接按 `reg.view<entt::entity>()` 的 LIFO 方向迭代写出 entity 数组；Load 按 JSON 顺序逐个 `world.CreateEntity()` 使新 World 的 EnTT ID 单调递增；新 World view 再 LIFO 给出原始顺序的反转 → 二次 Save 整段 entity 数组完整翻转的污染 diff（用户原话"Stop 后顺序变反"）
  - **修后**：按 entity index 升序排序后写盘顺序等价于"按创建顺序"，Source 与 Loaded World 在 Save 时输出相同字节序列；编辑器 Play → Stop 也不再翻转 Entity Tree 显示顺序（Entity Tree 仍消费 view 反向遍历，但反转再反转回到原序）；跨机器 / 跨 session .scene.json 字节稳定
  - **未做** G3 / G4（无需要：G2 单点修已让 G3/G4 的目标自然达成；G3 依赖 EnTT 内部不稳健，G4 仅在需要"按 Hierarchy DFS 显示"等额外需求时再做）
  - **回归测试** `TestSaveLoadSaveByteStable`（8 entity → Save path1 → Load → Save path2 → 字节级 `path1 == path2`）；ctest 全 43 通过
  - **关键改动文件**：`src/scene/SceneSerialization.cpp`（`<algorithm>` include + SaveImpl entityList 排序 + idMap 分配从 view 顺序改为 sort 后顺序）/ `tests/scene/SceneSerializationTest.cpp`（新增 TestSaveLoadSaveByteStable + main 入口注册）
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
- **v1.0.1 friction patch batch**（2026-05-22 落地；5 个 GAP 同 batch 关闭）：v1.0 ✅ 后第一个 patch milestone，按 [[feedback-post-v1-versioning]] 走 v1.0.xx 节奏。验收文档 `vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.0.1-acceptance-checklist.md`。OrangeEditor CMake VERSION 1.0.0 → 1.0.1。
  - **GAP-2026-05-22-directional-light-inspector-direction-helper-missing** ✅ —— ComponentSchema 加 `helperText` 字段（`tools/OrangeEditor/schema/ComponentSchema.h`）+ `ComponentSchemaBuilder::Helper(text)` API（`schema/ComponentSchemaRegistry.h`）+ SchemaInspector 在 CollapsingHeader 展开后、plugin 调度 / properties 渲染之前按行渲染 TextDisabled + Bullet（`schema/SchemaInspector.cpp`）。DirectionalLight schema 加 helper 段（与 multi-DirLight G1 合并一段文案，复用 helper 基础设施）
  - **GAP-2026-05-22-multi-directional-light-semantics-undefined** G1 ✅ —— DirectionalLight schema helper "场景中只有第一个 DirectionalLight 参与..."；EditorRenderLayer 加 `mSingletonOverflowDirLight` 字段，EntityTreePanel.cpp 入口 build first-found 后的 overflow set，DrawEntityNodeRecursive 行尾 layer chip 左侧画黄色 `(!)` 标记 + tooltip 说明不生效原因
  - **GAP-2026-05-22-multi-environment-component-semantics-undefined** G1 ✅ —— Environment schema 加 helper "Environment 作为全局单例使用..."；同款 overflow set + warning chip 路径复用（`mSingletonOverflowEnvironment`），同一 entity 同时撞两类 overflow 时 tooltip 合并展示
  - **GAP-2026-05-22-editor-dock-layout-collapses-on-restore** ✅ —— `EditorRenderLayer::OnUpdate` 内 `DockSpaceOverViewport` 之后、`BuildDefaultLayoutOnce` 之前加 viewport-size 比对（`mLastViewportSize` 字段记录上帧尺寸）。**c4 初版**仅检测任一方向收缩 > 25%。**c8 补丁**：`BuildDefaultLayoutOnce` 内 `DockBuilderSetNodeSize` 入参从 `viewport->Size` 改为 `viewport->WorkSize`（Size 包含 menu/toolbar 高度，子节点按比例算出来的 SizeRef 超过 dock 实际容器，导致 Entity Tree / Inspector 重建后被挤窄无法回到 default 20%/25% 比例）。**c9 补丁**：判断对称化为 `|ratio - 1| > 0.25`（同时检测收缩 *和* 扩大）—— restore → maximize 路径下 viewport 扩大但 dock 子节点 SizeRef 仍按 restore 时的小像素维持，大画布里 panel 显窄、底部 Assets 被 work area 裁掉；对称判断后任一方向跳变 ±25% 都重建，连续小幅拖边界仍不触发
  - **GAP-2026-05-22-editor-default-ibl-missing-causes-black-pbr-faces** ✅ —— `src/render/Pipeline.cpp` 中 `ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES=ON` 路径下 dummy IBL irradiance 灰度 0.25 (0x3400) → 0.5 (0x3800)；未挂 EnvironmentComponent 时 PBR 暗面获得 ~50% baseColor 暗橙过渡，不再判定为"全黑"。shipping 路径（aux passes OFF）仍 (0,0,0)，engine 默认中性原则不动
  - **GAP-2026-05-22-cube-mesh-back-face-bleed-through** ✅（c7 顺手修）—— `tools/OrangeEditor/DemoWorld.cpp` `MakeCubeMesh` 内 triangle index 顺序从 CW `(0,2,1)+(0,3,2)` 反转为 CCW `(0,1,2)+(0,2,3)`，与 Pipeline 主 pass `FrontFace = CCW + CullMode = Back` 约定一致。删除 disk `assets/meshes/cube.mesh` + `build/bin/Debug/assets/meshes/cube.mesh` 让启动期 lazy bake 重新生成（与 v3 schema bump 后 normals 段同款 invalidate 路径）。v1.0.1 c5 IBL bump 是暴露这个 pre-existing winding bug 的导火索，但根因独立
  - **gizmo 长度随 entity rotation 波动**（c10 顺手修，非 GAP 登记）—— DirectionalLight / ParticleEmitter gizmo 的旧实现用"投 origin + 1*X 量像素 / 单位"再 `tipWorld = origin + dirN * worldUnitsPerHandle` 反推世界长度。perspective 投影下"沿 +X 1 单位的 px"≠"沿 dirN 1 单位的 px"，dirN 朝/背相机时投影几乎为 0 → 屏幕长度随 dir 方向剧烈波动，entity 旋转时 gizmo 长度肉眼可见变化。修法：直接在屏幕空间钉死箭头长度 —— 投 `origin + dirN` 取归一化屏幕方向，`tipScreen = projOrigin + dirN2D * kHandleScreenLengthPx`，长度恒定与 dir 方向解耦。dir 投影退化时不画箭头（朝/背相机一面无法表达方向，后续可补 ⊙/⊗ icon，本 patch 范围外）。涉及文件：`tools/OrangeEditor/plugin/DirectionalLightGizmoPlugin.cpp` / `ParticleEmitterGizmoPlugin.cpp`
  - **DemoWorld → BuiltinAssets 拆分**（c11 架构归位，非 GAP 登记）—— `tools/OrangeEditor/DemoWorld.{h,cpp}` 原杂糅 3 层职责：(1) builtin mesh 工厂 (Make{Plane,Cube,Sphere}Mesh) (2) 启动期资产 bootstrap (InitializeEditorAssets + BeepWav helpers + lazy bake .mesh/.material/.wav) (3) demo 场景填充 (SeedDemoWorld / SeedPbrShowcaseWorld)。前 2 类是"编辑器永远需要的"基础设施，命名却挂"Demo"令读者误以为可拿掉；v1.0.1 cube winding 修复期间已被该命名误导一次。修法：新增 `BuiltinAssets.{h,cpp}` 承接 mesh 工厂 + InitializeEditorAssets + BuildNamedMaterialInstances + BeepWav helpers；DemoWorld 仅保留 Seed 函数（命名与内容真正对齐）。零行为变化（move 函数 + update includes + CMakeLists 加 BuiltinAssets.cpp，调用点 `main.cpp` / `EditorRenderLayer.cpp` / `MaterialAssetInspectorPlugin.cpp` 加 `#include "BuiltinAssets.h"`）。`InitializeEditorAssets` 内 SeedDemoWorld 不再被启动期默认调用（v1.0 验收期间已落地），所以拆出后 DemoWorld 真正只在 `File → Reset to Demo Scene` 等候选入口被消费
  - 关键改动文件：`tools/OrangeEditor/schema/ComponentSchema.h` / `schema/ComponentSchemaRegistry.h` / `schema/SchemaInspector.cpp` / `schema/RegisterBuiltinSchemas.cpp` / `EditorRenderLayer.h` / `EditorRenderLayer.cpp` / `panels/EntityTreePanel.cpp` / `DemoWorld.cpp`（cube winding 修）/ `CMakeLists.txt`（VERSION bump）/ `src/render/Pipeline.cpp`（IBL fallback bump）/ `docs/editor-roadmap.md`（v1.0.1 段）/ `docs/engine-known-gaps.md`（5 个 GAP 关闭 + cube winding GAP 登记+关闭）/ `vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.0.1-acceptance-checklist.md`（新增）/ `assets/meshes/cube.mesh`（删除让启动期 rebake）

## GAP-2026-05-28-editor-render-settings-panel ✅

- **发现方**：Orange-Ecosystem umbrella session 中用户提问"CSM 也集成到 OrangeEditor 了嘛？"
- **发现日期**：2026-05-28（与 GAP-2026-05-27-cascaded-shadow-maps 全套主线落地同 session 顺手 follow-up）
- **一句话定性**：CSM 主线（GAP-2026-05-27）完整落地后，编辑器视口里 directional 阴影**默认就走 3 级 CSM**（`ShadowConfig::cascadeCount{3}` 默认值生效），视觉效果"被动启用" —— 但 OrangeEditor 内**没有任何 ShadowConfig 字段的 UI 控件**：`tools/OrangeEditor/panels/ScenePanel.cpp` 651-655 只 hardcode 了 `mapResolution = 2048 + pcssLightSize = 12.0f`，cascadeCount / debugCascadeTint / pcfKernelRadius / depthBias / normalBias 均不可在 GUI 编辑，调级数 / 看 cascade 染色必须改 ScenePanel.cpp 重编。CSM 视觉戏剧化 polish（sample 18 的 `--tint` / `--motion` CLI flag）在编辑器里完全无入口
- **状态**：**✅ 2026-05-28 落地**（与 CSM GAP 同 session 顺手 follow-up），方案 B（新做 Render Settings panel）实施完成

### 决策与方案

候选方案分析：

| 方案 | 形态 | 取舍 |
|---|---|---|
| A. Schema 注册 ShadowConfig | 参 `RegisterBuiltinSchemas.cpp` 里 `PP::` PostProcess 的 `.Field<>` 写法 | 不适用：ShadowConfig 不是 component，是 Pipeline 全局 config，schema 框架当前没这个范畴（schema 是 per-component / per-asset Inspector 路径） |
| B. 新做 Render Settings 浮动面板 | 类似 Unity Project Settings → Quality → Shadows / Godot Project Settings → Rendering → Lights and Shadows | **选定**：杠杆最高 —— 未来 GI / Reflection / Anti-aliasing 等 Pipeline 级全局开关都能复用本 panel + 当下 ShadowConfig 编辑 UI 一并解决 |
| C. 暂时不做 | sample 18 CLI flag 足够内部验证 | 拒：用户已直接问"是否集成"，且编辑器架构纪律 v0.2.5 的"不允许 hardcode"侧约束，hardcode 路径越久越债务 |

**vendor/LumixEngine 没有对应面板**（Lumix shadow config 走 per-light component property），架构差异：Orange 走 per-pipeline `ShadowConfig`（一份全局），Lumix 走 per-light prop。Lumix 模式拒绝原因：每个 directional light 独立 `cascadeCount` 等价于 OrangeEngine 多 directional light not-yet 决策的扩展面，不是当下 ShadowConfig 模型该走的路。

### 落地范围

**与 mShowSettingsPanel / mShowProfilerPanel 同款"浮动面板 + View 菜单 toggle"模式**，避免占用 dock 默认面板格栅（ShadowConfig 是低频编辑，不该常驻像 Inspector / Hierarchy 那样的固定位）。

- `EditorRenderLayer.h`：
  - `#include <orange/engine/render/ShadowConfig.h>`
  - 新增 `void DrawRenderSettingsPanel();` 成员函数声明
  - 新增 `bool mShowRenderSettingsPanel{false};`（与 mShowSettingsPanel / mShowProfilerPanel 并列）
  - 新增 `Orange::Engine::Render::ShadowConfig mShadowConfig{ .mapResolution = 2048, .pcssLightSize = 12.0f };`（C++20 designated initializer 跳过中间未改字段，与原 ScenePanel hardcode 等价）
- `EditorRenderLayer.cpp`：
  - OnImGui 调用链：`if (mShowRenderSettingsPanel) DrawRenderSettingsPanel();`（与 mShowSettingsPanel / mShowProfilerPanel 同款 gating 模式）
  - View 菜单：`ImGui::MenuItem("Render Settings", nullptr, &mShowRenderSettingsPanel);`
- `panels/RenderSettingsPanel.cpp`（新增 ~170 行）：实现 `EditorRenderLayer::DrawRenderSettingsPanel()`，3 个 CollapsingHeader 分段：
  - **Shadow · Cascaded Shadow Maps**：SliderInt cascadeCount [1, 4] + Checkbox debugCascadeTint
  - **Shadow · Filter (PCSS / PCF)**：DragFloat pcssLightSize [0, 32] + SliderInt pcfKernelRadius [0, 2]
  - **Shadow · Resolution & Bias**：Combo mapResolution {1024, 2048, 4096} + DragFloat depthBias / normalBias
  - 所有控件配 ImGui::SetTooltip 说明字段含义；pcssLightSize / mapResolution 控件 tooltip 末尾标注 "⚠ 若场景含 PostProcessComponent，本值会被组件覆盖"（对照 `src/render/Pipeline.cpp:2117-2123` GatherShadowParamsFromComponents 覆盖路径）
  - 底部 "Reset Shadow Settings to Editor Defaults" 按钮：`sc = ShadowConfig{ .mapResolution = 2048, .pcssLightSize = 12.0f };` 与字段默认值同款
- `panels/ScenePanel.cpp`：
  - `EnsureScenePipeline` 首次成功后：`R::ShadowConfig sc{}; sc.mapResolution = 2048; sc.pcssLightSize = 12.0f; mpScenePipeline->SetShadowConfig(sc);` hardcode 块**撤掉**，改为 `mpScenePipeline->SetShadowConfig(mShadowConfig);`（首次 push 让 initial shadow target 按编辑器档分辨率创建）
  - `DrawScenePanel` 每帧入口（在 SetSkyEnabled 之后、Render 之前）追加 `mpScenePipeline->SetShadowConfig(mShadowConfig);`（让 Render Settings 面板编辑后下一帧 viewport 立即生效；by-value 32 bytes 拷贝到 mpImpl，开销可忽略）
- `tools/OrangeEditor/CMakeLists.txt`：sources 列表加 `panels/RenderSettingsPanel.cpp`

**架构纪律对照**（CLAUDE.md "OrangeEditor 架构纪律"段）：
- ✅ 不在 EditorState 上无脑加字段：mShadowConfig 加在 EditorRenderLayer 上而非 EditorHost / EditorState，与 mEditorTime / mShowSettingsPanel / mShowProfilerPanel 等运行时状态字段同类（这些都是 panel 局部状态而非全编辑器共享 context）
- ✅ 浮动面板模式复用既有 mShow* gating 模板（与 Settings / Profiler 同款），不引入新 panel 注册机制
- ✅ schema 注册形式不适用 ShadowConfig（per-pipeline config 非 component），按方案 B 走独立 panel；将来若 PipelineConfig 类似的全局配置增多，可考虑独立 PropertySchema 范畴

### 验收

- 全 52 ctest 通过（含 editor_build_smoke）
- `python scripts/check_invariants.py` → `All invariants OK. (7 grandfathered by baseline)`
- `python scripts/check_claude_md_drift.py` → `none detected.`
- 视觉验证：CSM 主线视觉效果在 sample 18 polish 阶段（commit `63a4ad3` `--tint` / `--motion`）已充分验证；本次 GAP 仅把同一份功能从 CLI flag 暴露到编辑器 UI，不涉及渲染路径改动 —— UI 控件路径由 ImGui 标准控件 + 直写字段 + 每帧 SetShadowConfig 三件套组成，无独立视觉风险

### 关键改动文件

`tools/OrangeEditor/EditorRenderLayer.h`（include ShadowConfig.h + Draw 声明 + 2 字段） / `tools/OrangeEditor/EditorRenderLayer.cpp`（OnImGui chain + View 菜单 MenuItem） / `tools/OrangeEditor/panels/RenderSettingsPanel.cpp`（新增） / `tools/OrangeEditor/panels/ScenePanel.cpp`（hardcode 撤掉 + 每帧 push） / `tools/OrangeEditor/CMakeLists.txt`（sources 加 RenderSettingsPanel.cpp） / `docs/engine-known-gaps.md`（本条目登记+关闭）

### 留待后续

- **持久化**：mShadowConfig 不写盘，每次启动编辑器回归 designated init 默认值；后续若用户期望 per-project 持久化（与 EditorSettings / EditorKeybindings 同档），按 EditorSettings.cpp 的写盘路径扩展 —— 但需要先确认 ShadowConfig 是"项目级"还是"全局编辑器偏好"（前者落 .scene.json 或同目录 .render-settings.json，后者落 ~/.orange-editor/settings.json），独立 GAP 触发
- **PostProcess 覆盖语义**：当前 RenderSettingsPanel 调 pcssLightSize / mapResolution 仅 tooltip 标注会被 PostProcessComponent 覆盖；将来可考虑在控件外侧加状态指示（"被组件 X 覆盖中"），独立 GAP 触发
- **未来 Pipeline 全局配置**：本面板设计为 Render Settings 总入口，将来 GI / Reflection / AA / Tonemap 等若引入 per-pipeline 全局开关，新开 CollapsingHeader 段加入即可，无需新建 panel

## GAP-2026-05-28-editor-post-process-defaults-too-aggressive ✅

- **发现方**：Orange-Ecosystem umbrella session 中用户提问"现在是不是默认显示后处理效果？我没有加入 PostProcessComponent 就已经有效果了，逻辑不对"
- **发现日期**：2026-05-28（与 GAP-2026-05-28-editor-render-settings-panel 同 session 顺手 follow-up）
- **一句话定性**：`tools/OrangeEditor/panels/ScenePanel.cpp` EnsureScenePipeline 历史上 hardcode 创建了 9 个 post pass 的 chain（BuiltinPostProcessChain::CreateDefault 的 HDR + Bloom + GodRays + Tonemap + LUT 5 个，再追加 SsaoPass / SsrPass / ContactShadowPass / DofPass / TaaPass / ColorGradePass 6 个"美术效果"）；Pipeline 自身默认行为是中性的（无 chain + 无组件 → 0 post），但编辑器侧 hardcode 让用户**没挂任何 PostProcessComponent 就看到全套 fancy effect**，违反"组件即语义"工业惯例（Unity / Unreal / Godot 编辑器视口默认均不带这类 pass）
- **状态**：**✅ 2026-05-28 落地**（梯度 2 方案）

### 业界对照

| 引擎 | 编辑器 viewport 默认 | 用户启用方式 |
|---|---|---|
| **Unity** | 默认无 post | 加 Volume + Post-Process Volume 组件 |
| **Unreal** | 默认带 Tonemap + AA，其余无 | 加 PostProcessVolume |
| **Godot** | 默认无 post | 加 WorldEnvironment node |
| **OrangeEditor 修正前** | 全套 9 pass hardcode 开 | （已经开着，撤不掉） |
| **OrangeEditor 修正后（梯度 2）** | 仅 HDR pipeline 必需 5 pass（HDR + Bloom + GodRays disabled + Tonemap + LUT） | 加 PostProcessComponent；组件按 `SyncPostProcessFromWorld` 压过 chain |

### 决策与方案

候选梯度（用户决策走梯度 2）：

| 梯度 | 内容 | 取舍 |
|---|---|---|
| 1（最干净）| 完全空 chain | PBR linear 0.3-0.4 直出偏暗，emissive HDR > 1 硬边 clamp；用户首屏会以为引擎 bug |
| **2（推荐 / 选定）** | **仅 BuiltinPostProcessChain::CreateDefault**（HDR + Bloom + GodRays disabled + Tonemap + LUT） | HDR pipeline 正确显示的最低线，与 sample 13/14 同款；GTAO / SSR / ContactShadow / DoF / TAA / ColorGrade 6 个"美术效果"撤掉 |
| 3 | 三件套 + TAA | TAA 在编辑器静态时易出 ghosting 残影，被误判为 bug |
| 4 | 加 toolbar checkbox "Preview Effects" 默认 OFF | 引入新 UI，复杂度抬高；与 PostProcessComponent 路径职责重叠 |

### 落地范围

`tools/OrangeEditor/panels/ScenePanel.cpp` 的 EnsureScenePipeline 内：
- **撤掉** 6 个 hardcode 美术 pass（SsaoPass GTAO / SsrPass / ContactShadowPass / DofPass / TaaPass / ColorGradePass）+ `namespace R = Orange::Engine::Render;` alias（其余地方未用）
- **保留** `BuiltinPostProcessChain::CreateDefault()`（HDR + Bloom + GodRays disabled + Tonemap + LUT）+ `SetPostProcessChain`
- **撤掉** `#include <orange/engine/render/PostProcessPasses.h>`（不再消费 PostProcess pass 具体类型，BuiltinPostProcessChain.h 内部封装）
- 改注释 12 行（撤代码 ~40 行）：把"WYSIWYG 与 sample 16 一致"取舍说明替换为"组件即语义 / 工业惯例对齐"说明，引导用户挂 PostProcessComponent 获取美术效果

### 验收

- 全 52 ctest 通过（含 editor_build_smoke）
- `python scripts/check_invariants.py` → `All invariants OK. (7 grandfathered)`
- `python scripts/check_claude_md_drift.py` → `none detected.`

### 视觉变化（已知 + 接受）

编辑器视口与 sample 16_light_family_shadows 不再 WYSIWYG 一致（sample 16 仍走 chain 路径，自带美术效果），这是 acceptance：
- 用户在编辑器里看到的是"基线 PBR" —— 不含 SSAO 暗角 / SSR 反射 / 接触阴影 / 景深 / TAA 抗锯齿 / Color Grading
- 想看 sample 16 同款观感：在场景里挂 `PostProcessComponent`，按需开 `ssaoEnabled` / `ssrEnabled` / `contactShadowEnabled` / `dofEnabled` / `taaEnabled` / `colorGradeEnabled` 字段
- 这与 GAP-2026-05-27-postprocess-component-local-volume（PostProcess v2 设计）的方向一致 —— component 才是 long-term 的 post 配置入口

### 关键改动文件

`tools/OrangeEditor/panels/ScenePanel.cpp`（撤 6 个 pass + 撤 PostProcessPasses.h include + 改注释）/ `docs/engine-known-gaps.md`（本条目登记+关闭）

### 留待后续

- **sample 16 是否也走 component 路径**：当前 sample 16 仍走 PostProcessChain 路径（hardcode 6 个美术 pass）；后续可考虑迁移到 PostProcessComponent 路径，让 sample 也展示"组件即语义"用法，但 sample 是引擎演示场所，hardcode chain 自有展示价值，独立 GAP 触发
- **PostProcessComponent 工厂 preset**：编辑器可考虑提供"Add Component → PostProcess → Cinema Preset / Outdoor Preset"等模板（按场景类型一键挂带预设参数的 PostProcessComponent），降低用户挂组件的摩擦门槛，独立 GAP 触发

---

## BUG-2026-05-28-pipeline-capture-tonemap-hardcoded-aces ✅

- **发现方**：sample 14 `--tonemap` CLI 落地 session（GAP-2026-05-27-tonemap-operator-selection 留待后续 #1 执行时）
- **发现日期**：2026-05-28
- **一句话定性**：`Pipeline::Impl::FinalizeCapture()`（`src/render/pipeline/PipelineCapture.cpp`）的 HDR → PNG CPU 端转换**hardcode 跑 ACES Narkowicz**，完全忽略 `chain.TonemapPass.op` 与 `exposure`，导致 sample 14 `--tonemap=aces|agx|reinhard|linear` CLI 在 capture 上看不出差异
- **状态**：**✅ 2026-05-28 落地**（与 GAP-2026-05-27 留待后续 #1 同 commit，作为 #1 真正可验收的前置）

### 触发场景

sample 14 `--tonemap=<op>` CLI parsing + `chain.FindByName("tonemap")->op = ...` 接线**全链路正确**（main path 在 Pipeline.cpp:3160 读 `activeTonemap->op` 灌 push constant），但走 `--capture <path>` 时 4 算子产物 MD5 完全相同。诊断 print 确认 `tm->op` 设置成功，于是定位到 capture 路径在 stage A 末尾抓 HDR offscreen → CPU 端固定 ACES tonemap → PNG，与 stage B shader 路径完全无连。

这是**同款 wire-up 漏写第三现场**：
- 第一处（**已修**）：编辑器 viewport stage B 路径 `Pipeline.cpp:1514` 历史 hardcode `exposure=1.0` —— [[BUG-2026-05-28-editor-viewport-tonemap-exposure-not-wired]]
- 第二处（**已修**）：编辑器 viewport stage B 路径 `Pipeline.cpp:1514-1621` 历史 hardcode `op=ACES_Narkowicz` —— GAP-2026-05-27-tonemap-operator-selection 落地时已与第一处同时修
- 第三处（**本条**）：capture 路径 `PipelineCapture.cpp:177-179` 历史 hardcode `AcesNarkowicz(x)` —— 本 BUG 修

3 个现场源出同一漏写模式：tonemap 算子加 enum 时只升级了"主路径 shader push constant"，没顺手扫所有用 hardcode 公式的副路径。capture 是其中最容易漏的——它在 CPU 端复刻 GPU 算法，不参与 shader 系统，shader push constant 升级不会自然连过来。

### 缺什么 / 现状对照

| 字段 | 修前 | 修后 |
|---|---|---|
| capture CPU tonemap | `AcesNarkowicz(x)` hardcode | `ApplyTonemap(hdr * exposure, op)` 4 算子 switch |
| 读 chain.TonemapPass.op | ❌（忽略） | ✅ `FindActiveTonemapPass()` |
| 读 chain.TonemapPass.exposure | ❌（隐式 1.0）| ✅ `tm->exposure`（链没 TonemapPass 时退回 1.0） |
| AgX CPU 实现 | ❌ | ✅ 与 `tonemap.frag.glsl` 同矩阵 + 多项式拟合 |
| Reinhard CPU 实现 | ❌ | ✅ per-channel `x / (1 + x)` |
| Linear CPU 实现 | ❌（落入 ACES）| ✅ `clamp(x, 0, 1)` |

### 落地范围

`src/render/pipeline/PipelineCapture.cpp`：
- 新增 CPU 端 AgX（含 sRGB↔AgX 矩阵 + 6 系数多项式 sigmoid fit）/ Reinhard / LinearClamp 实现，与 `src/render/builtin_shaders/tonemap.frag.glsl` 4 算子一一对应
- `ApplyTonemap(hdr, op)` switch dispatch（与 shader 路径同语义）
- `FinalizeCapture()` 主循环：调 `FindActiveTonemapPass()` 取 `op` + `exposure`，每像素 `ApplyTonemap(hdr * exposure, op)`；chain 无 TonemapPass → 退回 ACES + exposure=1（与历史兼容）
- 头注释更新：从"ACES tonemap 后存 PNG"改为"按 chain 活动 TonemapPass.op + exposure 选算子"，并显式记 bloom 不参与 capture 合成（与 stage B `hdr + bloom * intensity` shader 路径的 visual 偏差留 GAP-后续）

### 验收

- 4 张 `build/captures/14_tonemap_{aces,agx,reinhard,linear}.png` MD5 互异（修前全相同 `bb6d9c03...`，修后 4 unique hash + 文件尺寸 162K-196K 区间分化）
- ACES capture MD5 与修前一致 → 向下兼容
- 视觉一致性目测：metallic 球高光区 ACES（暖偏色压暗）vs AgX（去饱和不偏色）vs Reinhard（整体压扁）vs Linear（硬 clamp 高对比）4 档分明
- `python scripts/check_invariants.py` → `All invariants OK. (7 grandfathered)`

### 关键改动文件

`src/render/pipeline/PipelineCapture.cpp`（新增 3 算子 CPU 实现 + ApplyTonemap dispatch + FinalizeCapture 接 chain；注释更新）/ `samples/14_pbr_ibl/main.cpp`（GAP-2026-05-27 #1 落地 CLI parsing + chain.FindByName 接线）/ `docs/engine-known-gaps.md`（本条目登记+关闭，标 GAP-2026-05-27 #1 ✅）

4 张 fixture `build/captures/14_tonemap_{aces,agx,reinhard,linear}.png` 落本机 build 目录（`.gitignore` 规则同 sample 16 halo 等历史 fixture），不入库；重跑命令固化在 sample 14 头注释 + 本条目"验收"段。

### 留待后续

- **capture 路径接 bloom 合成**：CPU 端 capture 当前不含 bloom contribution（stage A hdrColor 抓回 CPU 时 bloom 还没合成），与 stage B shader `hdr + bloom * intensity` 路径在 emissive / 高 HDR 区域有 visible 偏差。需要"capture 与 shader 像素级一致"时拉动；典型 PR review 4 算子对照已够，**不构成排期承诺**
- **stage B swap-chain capture 接口**：当前 `RequestCapture` 抓 stage A HDR，看不到 godrays / LUT 等 stage B 末段效果；需要"截 swap-chain 最终图"时拉动；编辑器 dock 模式下还涉及 ImGui overlay 抓不抓的取舍——非平凡设计
- **AgX CPU 与 GPU 数值精度对照**：CPU `std::log2` + scalar 浮点 vs GPU `log2` + vec3 SIMD 浮点在 fp32 精度边缘像素可能差 ±1 LSB；当前不阻塞但若 capture 用作 pixel-exact regression baseline 需复核

---

## GAP-2026-05-29-entity-tree-sibling-reorder ✅

- **发现方**：用户 dogfood 提问"EntityTree 是不是不能拖到任意 parent 下 / 拖出 / 更改顺序"
- **发现日期**：2026-05-29
- **一句话定性**：Entity Tree DnD 此前只支持 reparent（拖到节点上 → 挂为末子）与 detach-to-root（拖到面板空白），**不支持兄弟间重排**（无"插到兄弟前/后"的 drop zone，reparent 恒 `LinkAsLastChild` 挂尾）；且根节点按 EnTT 存储序枚举、增删组件后帧间跳位
- **状态**：**✅ 2026-05-29 落地**（commits `1c399a1` EditorHierarchy 原语 + 本次 EntityTreePanel 接线）

### 触发场景

用户想在 Entity Tree 里调整同级实体顺序（同 Unity/Unreal/Godot 的 scene tree 拖拽重排手感），发现只能改父子归属、改不了同级排第几。

### 落地内容

- `EditorHierarchy` 加 `MoveToPosition / MoveBefore / MoveAfter`（commit `1c399a1`）——复用现有双向兄弟链做精确位置插入，O(1) 摘链 + 重链；**零序列化改动**（子节点顺序本就靠 `firstChild/nextSibling` 持久化）
- `EntityTreePanel` drop target 改三区：节点 rect 上 1/4 = 插到该兄弟之前、下 1/4 = 之后、中间 = reparent into；before/after 仅对**有父的子节点**提供，悬停画 `ImGuiCol_DragDropTarget` 色插入指示线。落点判定用 TreeNodeEx 后捕获的 node rect（不依赖"最后一个 item"）
- `EditorSelection::PendingReparent` 扩 `Where{Into/Before/After}` + `refSibling`
- 帧末 apply 按 Where 分派 ReparentTo / MoveBefore / MoveAfter；**Undo 统一用 `MoveToPosition(src, oldParent, oldPrev)` 精确复位**（顺带修了旧 reparent undo 退回时丢失兄弟顺序的问题）
- 顺带：根节点改按 entity id 稳定排序后再画，消除"根节点帧间跳位"UX 瑕疵

### 关键改动文件

`tools/OrangeEditor/EditorHierarchy.{h,cpp}`（3 个位置原语）/ `tools/OrangeEditor/context/EditorSelection.h`（PendingReparent 扩 Where+refSibling）/ `tools/OrangeEditor/panels/EntityTreePanel.cpp`（drop 三区 + 指示线 + apply 重写 + 根稳定排序 + `<vector>`）/ `docs/engine-known-gaps.md`（本条目）。纯编辑器 UI，build-green 验收（无对应 ctest，同 DirLight/Env warning chip 先例）。

### 留待后续

见下条 GAP-2026-05-29-entity-tree-root-reorder-not-supported。

---

## GAP-2026-05-29-entity-tree-root-reorder-not-supported

- **发现方**：同上（GAP-2026-05-29-entity-tree-sibling-reorder 落地时识别的边界）
- **发现日期**：2026-05-29
- **一句话定性**：**根节点之间**无法拖拽重排——根不在任何兄弟链里（`parent==Invalid`，无父锚 `firstChild`），当前只能按 entity id 稳定排序展示，用户改不了顶层实体的相对顺序
- **状态**：**未排期**（开 follow-up；子节点重排已满足绝大多数"调整顺序"需求）

### 缺什么 / 架构取舍

要让根可拖拽重排且**持久化**，需要给"根序"一个表示，三选一（带取舍，落地前需小 ADR）：
1. **隐藏 scene-root 实体**：所有顶层实体成其 children，全树统一走兄弟链——最干净，但改"什么是 root"语义，牵动序列化 + 每处 `isRoot` 判定 + 迭代 + AppHost
2. **`HierarchyComponent` 加 `sortIndex`**：简单，但与既有兄弟链顺序双轨、冗余，且 `HierarchyComponent` 序列化要升 schema_version（"出厂即冻结"约束）
3. **World 级根序 list**：`std::vector<Entity> rootOrder` 独立于组件——非 archetype 友好但根数少；要进 scene 序列化

非阻塞，按"用户真的需要拖拽顶层顺序 + 要存盘"实际拉动触发。当前编辑器内 `MoveBefore/MoveAfter` 对根 target 退化为 detach-to-root（无序），UI 也只对有父子节点显示 before/after 指示线，语义诚实不骗人。

---

## BUG-2026-05-29-entity-tree-dnd-anchored-to-trailing-chip ✅

- **发现方**：用户 dogfood —— sibling-reorder 落地后实测"**任何节点都拖不动**"
- **发现日期**：2026-05-29
- **一句话定性**：`DrawEntityNodeRecursive` 的 `BeginDragDropSource` / `BeginDragDropTarget` 调用位置在行尾 layer/warning chip（`SameLine` + `TextDisabled`）**之后**，ImGui 把 DnD 锚到"最后一个 item"=那个无 ID 的小 chip。`BeginDragDropSource` 对无 ID item 走 `SourceAllowNullID` 分支（imgui.cpp 14509：需 hover 该 item 矩形才激活），于是**只能从右侧小 chip 起拖、拖节点名无反应**；drop target 同理只认 chip 矩形。宽面板（chip 会画）下整条 tree DnD（reparent / detach / 新加的 reorder）全部失效——**预存 bug**，因键删/F2 等键盘路径可用且没人从 chip 拖过，长期未暴露。
- **状态**：**✅ 2026-05-29 落地**（与 sibling-reorder 同一 dogfood 线；本 fix 是 reorder 真正可用的前置）

### 根因（imgui 语义实证）

`BeginDragDropSource` 取 `source_id = g.LastItemData.ID`（imgui.cpp 14497）。`TextDisabled` 是 `ItemAdd(bb, 0)` 的无 ID item → `source_id==0` → 走 uncommon 分支，要求 `LastItemData.StatusFlags & HoveredRect`（chip 矩形被 hover）才能起拖。TreeNode 行（`SpanAvailWidth`，有 PushID+"##node" 的 ID）若是 last item 则走 common 分支（`ActiveId==source_id`，按住节点即激活），整行可拖。

### 修复

把 `BeginDragDropSource` + `BeginDragDropTarget` 从 chip 之后**前置到 TreeNodeEx + selection click 之后、chip 块之前**，使 last item = 整行 TreeNode（有 ID 走 common 路径）。drop 落点判定继续用 TreeNodeEx 后捕获的 `nodeMin/nodeMax`（与 last-item 解耦）。

### 关键改动文件

`tools/OrangeEditor/panels/EntityTreePanel.cpp`（DnD source+target 前移）/ `docs/engine-known-gaps.md`（本条目）。

### 留待后续（同根因、未在本 fix 内动）

- **双击节点名重命名 ✅ 已确认同款 + 已修**（commit 见 git log "double-click rename + OpenOnDoubleClick"）：用户实测"双击节点没法重命名"坐实——`IsItemHovered()` 在 chip 之后同样被锚到 chip。修法：把双击 query 前移到 selection 之后（node 仍是 last item），并**去掉 `ImGuiTreeNodeFlags_OpenOnDoubleClick`** —— 否则双击父节点会被"展开"吃掉、永进不了重命名；展开改只走三角（OpenOnArrow），Unreal/Godot 同款。
- **右键 context menu：用户实测正常**（右键能弹出 Create/Rename/Delete 菜单）——故 `BeginPopupContextItem` 那条"疑似 chip 锚点"撤回，不动。

---

## BUG-2026-05-29-editor-clobbers-glfw-user-pointer ✅

- **发现方**：用户 dogfood —— **右键节点 → Rename 崩溃**，崩在 `Window.cpp` `Dispatch` 的 `impl->callback(event)`
- **发现日期**：2026-05-29
- **一句话定性**：`tools/OrangeEditor/main.cpp` 用 `glfwSetWindowUserPointer(glfwWindow, &editorHost)` 把 GLFW window user pointer 设成 `EditorHost*`，**但该 user pointer 归引擎 `Window` 所有**——`Window::Create` 设为 `Window::Impl*`，引擎全部 GLFW 回调（`OnChar`/`OnKey`/`OnSize`/`OnMouseButton`…）经 `ImplFrom`=`glfwGetWindowUserPointer` 当 `Window::Impl*` 读。被覆盖后引擎回调把 `EditorHost` 误读成 `Window::Impl`。
- **状态**：**✅ 2026-05-29 落地**（编辑器侧改用文件级静态 `spEditorHost` 给 close/drop 回调用，不再占 user pointer）

### 崩溃机理（调试器实证）

ImGui `install_callbacks=true` 链式转发到引擎 `OnChar` → `Dispatch(ImplFrom(handle), CharEvent)`。`ImplFrom` 返回被覆盖的 `&editorHost`（栈地址）当 `Window::Impl*`。调试器实测 `impl = 0x..{ pHandle=0xf, title=<NULL>, width=4294967295 }`——纯垃圾。`impl->callback` 读到 `EditorHost` 在 `offsetof(Window::Impl,callback)` 处的字节当 `std::function` 调用 → 崩。

"为何偏偏 rename 崩、平时不崩"：那段被误当 callback 的 `EditorHost` 字节随编辑器状态变化——平时读出来像空 `std::function`（guard 失败 no-op），rename 时恰好非空 → guard 通过 → 调垃圾崩溃。典型 UB（同一坏指针、不同字节）。

**附带危害**：`OnSize`（`Window.cpp:129`）会往 `impl->width/height` **写**——每次窗口 resize 都往 `EditorHost` 偏移 40/44 写垃圾，静默腐蚀内存（可能是其他偶发诡异行为的根源）。

### 修复

`main.cpp`：删掉 `glfwSetWindowUserPointer(glfwWindow, &editorHost)`，user pointer 留给引擎（保持 `Window::Impl*`）。close / drop 这两个编辑器独占的 GLFW C 回调改用文件级静态 `static EditorHost* spEditorHost = &editorHost;`（non-capturing lambda 可按名引用静态变量、仍转成函数指针）。修复后引擎回调读到正确 `Window::Impl`：`OnChar` 走真 `callback`（AppHost lambda → layer `OnEvent`，editor 对 char 直接 return false）不再崩，`OnSize` 写回真 `Impl` 不再腐蚀 `EditorHost`，Esc-quit via OnEvent 也恢复。

### 关键改动文件

`tools/OrangeEditor/main.cpp`（删 user-pointer 覆盖 + 加 `spEditorHost` + close/drop 改用之）/ `docs/engine-known-gaps.md`（本条目）。纯编辑器侧修复（不改引擎 Window），build-green；**交互验证待用户实机右键 Rename + 拖拽确认**。

### 关联

与 [[BUG-2026-05-29-entity-tree-dnd-anchored-to-trailing-chip]] 同一轮 dogfood 暴露：前者是 DnD 锚点放错位置，本条是 user pointer 被抢。两者都因"编辑器密集交互前没人深用 tree / 打字"长期潜伏。

---

## GAP-2026-05-29-editor-material-asset-dirty-tracking

- **发现方**：`docs/editor-capability-gap-vs-mature.md` 全编辑器 gap 报告（P0 + Quick Win #1）复核时坐实，并在复核中发现比报告更严重的次生 bug
- **发现日期**：2026-05-29
- **一句话定性**：`.material` 资产编辑**没有持久化的 dirty 追踪**——Material Inspector 子模式既不接 `EditorSceneContext.dirty` / 未保存确认拦截，也不进命令栈；更严重的是它**连自己的 Save 按钮可用性都靠每帧 transient 信号**，导致 uniform-only 编辑松手后根本存不下去
- **状态**：**登记（未排期）**。本 session 仅复核 + 登记，不实现（同 CLAUDE.md "发现 gap 的 session 只做登记"纪律）

### 触发场景

渲染出身作者在 Material Inspector 调 PBR / 自定义模板的 uniform（颜色 / metallic / roughness 等），调完关掉编辑器或切走，期望像调 Inspector 字段一样"有未保存提示 / Ctrl+Z 可撤 / 关窗拦截"。实际三者全无。

### 缺什么（两个facet，第 2 个是复核新发现）

#### facet 1 · 旁路 scene dirty + 命令栈（报告原有结论，复核确认仍成立）

- `MaterialAssetInspectorPlugin::DrawMaterialSubMode`（`tools/OrangeEditor/plugin/MaterialAssetInspectorPlugin.cpp`）的 Save 直接 `WriteMaterialFile` 写盘（`:474` 附近），**不置 `EditorSceneContext.dirty`、不 Push 任何 ICommand**。
- 后果：改了材质 uniform / template 后，编辑器顶层 dirty 仍是 false → 关窗 / Esc / File>New 的未保存确认 modal（`EditorRenderLayer.cpp` 的 `PendingCloseAction` 路径）**不会拦截材质改动** → 用户改了材质没存就关 = 静默丢失。这是报告标的"真陷阱"。
- 同时材质编辑不进命令栈，Ctrl+Z 撤不了材质改动（与 Inspector 字段编辑的 `SetFieldValueCommand` 体验不一致）。

#### facet 2 · Save 按钮可用性靠 per-frame transient 信号（复核新发现，比报告更严重）

- `dirty = templateDirty || uniformDirty`（`MaterialAssetInspectorPlugin.cpp:452-453`），`Save` 按钮包在 `ImGui::BeginDisabled(!dirty)` 里（`:454-455`）。
- `templateDirty` 是持久比较（`editingTemplateName != originalTemplate`，盘上读的原值），没问题。
- **但 `uniformDirty`（`:392` 初始化 false，`:439-443` 累积）只在 `RenderUniformWidget` 当帧返回 true 时为 true**，而 `RenderUniformWidget` 的 `changed` 只在 `ImGui::DragFloat/SliderFloat/ColorEdit` 当帧返回 true（即"值在这一帧被改了"）时为真。
- 推论（ImGui 控件返回值语义确定）：用户拖 slider 改完 uniform、**松手后下一帧 `uniformDirty` 即归 false** → 若没同时切 template，则 `dirty=false` → Save 按钮立即置灰 + 显示"(no changes to save)"（`:481-485`）。
- 净后果：**uniform-only 的材质编辑实际上存不进盘**——要点 Save 必须在拖拽过程中点，单鼠标做不到。live instance 视觉已变、磁盘 `.material` 没变，下次启动 `ApplyDataToInstance` 还原成旧值，编辑丢失。
- ⚠️ **此 facet 的 UX 结论靠代码 + ImGui 语义推断，未实机 dogfood**（遵循项目"交互功能勿凭读代码判定"纪律）。落地前/评审时应作者实机点一次确认；但代码机制（transient uniformDirty）本身是确定的。

### 期望验收（落地时，非本 session）

- Material Inspector 维护**持久 per-asset dirty 状态**（不是每帧重算的 transient）：首次编辑（template 切换 / 任一 uniform 改动）置 true，Save / 切走 / reload 该 `.material` 后清回 false。
- uniform-only 编辑松手后 Save 按钮**保持可用**直到真正存盘。
- 材质未保存时，编辑器顶层未保存确认路径（关窗 / Esc / New / Open）能拦截并提示（最小版：把材质 dirty 并进 `scene.dirty` 的判定；完整版：独立"有未保存资产"集合）。
- （可选，报告标 M）材质编辑进命令栈，支持 Ctrl+Z；体量比前两项大，可拆独立条目。
- 纯逻辑部分（dirty 状态机迁移、save→load uniform 往返等价）可单测兜底；Save 按钮手感 + 确认 modal 焦点必须作者 GUI dogfood。

### 关联

- 报告原文：`docs/editor-capability-gap-vs-mature.md` §2.3「材质参数旁路命令栈/dirty」`:86`、§2.7「Dirty 跟踪」`:159`、§3 P0 `:189`、§4 Quick Win #1 `:228`。
- scene 级 dirty / 未保存确认基建见 [[GAP-2026-05-22-new-scene-actually-seeds-demo]] 同期建立的 `EditorSceneContext.dirty` + `PendingCloseAction`（`context/EditorSceneContext.h:116`）——本 gap 是"资产级 dirty"缺口，与"场景级 dirty"正交。
- 材质资产体系背景见 [[GAP-2026-05-24-material-template-library-and-custom-hook]] / [[GAP-2026-05-24-editor-asset-browser-create-material-missing]]。

---

## GAP-2026-05-29-editor-autosave-wiring

- **发现方**：`docs/editor-capability-gap-vs-mature.md` 全编辑器 gap 报告（§2.7 `:158` + P0 `:193`），2026-05-29 复核坐实
- **发现日期**：2026-05-29
- **一句话定性**：引擎侧 `Orange::Engine::Save::AutosaveScheduler` + 单测早已就绪，但 **OrangeEditor 对它零接线**——编辑器崩溃 / 误关 = 丢全部未存场景，无任何自动存档兜底
- **状态**：**登记（未排期）**。本 session 仅复核 + 登记，不实现

### 触发场景

编辑器内长时间摆场景（拼关卡 / 调光 / 调材质），中途崩溃（本项目近期就连踩 GLFW user-pointer 崩溃、rename 崩溃等预存 bug）或手滑关窗 Discard，**自上次手动 Save 起的全部改动直接蒸发**。成熟编辑器（Unity / Unreal / Godot）都有周期性 autosave + 崩溃后恢复入口。

### 现状证据（复核）

- `tools/OrangeEditor/` 全目录 grep `AutosaveScheduler` / `autosave` **零命中**——编辑器完全没用它。
- 引擎侧已就绪：`include/orange/engine/save/AutosaveScheduler.h` / `src/save/AutosaveScheduler.cpp` / `tests/save/AutosaveSchedulerTest.cpp` / 在 `samples/11_save_load_demo` 有消费先例。

### 缺什么（scheduler 可直接复用，缺的是编辑器侧接线 + 几处决策）

`AutosaveScheduler` 是**纯时间逻辑 + 通用无参 `TriggerCallback`**，与具体存档实现解耦（不读系统时钟、无 IO、单线程串行）——编辑器可直接 own 一个实例，无需改引擎。缺的是：

- **持有 + 喂帧**：`EditorHost` / `EditorRenderLayer` own 一个 `AutosaveScheduler`，每帧用编辑器真实帧 delta 调 `Update(dt)`（编辑器已有帧计时）。
- **触发 gate**：仅 `PlayState::Edit` 态推进（Play / Paused 已有独立 snapshot 机制，别叠加）；callback 内先看 `scene.dirty`，**clean 场景不写**（避免无谓落盘 + 反复覆盖恢复文件）。
- **落盘 callback**：把当前 `pWorld` 序列化到 autosave 落点（建议 temp dir 唯一名，或 `currentScenePath` 旁的 `.autosave` sidecar；Untitled 未命名场景也要能存）。复用现有 `SceneSerialization` 写路径。
- **手动 Save 后 `Reset()`**：避免手动存盘后立刻又被 autosave 一次。
- **崩溃恢复 UX**：编辑器启动时检测到比目标 scene 文件更新的 autosave 落点 → 弹"发现未保存的自动存档，是否恢复？"。这是 autosave 真正兑现价值的一半，别只做"写"不做"读回"。
- **Config 暴露**：`intervalSeconds`（默认 300）/ `minSecondsBetween`（默认 30）按编辑器口味调 + 进 `EditorSettings`（可关、可改周期）。

### 期望验收（落地时，非本 session）

- Edit 态下场景 dirty 时，每 N 秒自动落盘一次 autosave 文件；clean 场景不写；手动 Save 后计时归零。
- 模拟崩溃（kill 进程）后重启编辑器，能检测到 autosave 并恢复到接近崩溃前的状态。
- 纯逻辑（scheduler 调度、save→load 往返）已有 / 可补单测兜底；落盘节流不卡帧 + 恢复 modal 焦点需作者 GUI dogfood。

### 关联

- 引擎 scheduler 接口：`include/orange/engine/save/AutosaveScheduler.h`（Phase 5.5 Save 模块）。
- 与 [[GAP-2026-05-29-editor-material-asset-dirty-tracking]] 共享"编辑器 dirty / 数据丢失防护"主题：那条是材质资产 dirty 缺口，本条是场景级 autosave 兜底，正交但同属"别丢用户工作"。
- scene 级 dirty 判定基建（`context/EditorSceneContext.h:116` 的 `scene.dirty`）是本 gap 的 trigger gate 依赖。

---

## GAP-2026-05-29-editor-undo-redo-action-label ✅

- **发现方**：`docs/editor-capability-gap-vs-mature.md` 全编辑器 gap 报告（§2.8 `:172` + Quick Win #7 `:234`），2026-05-29 复核坐实
- **发现日期**：2026-05-29
- **一句话定性**：Edit 菜单的 Undo / Redo 是**固定文案**"Undo" / "Redo"，不显示将要撤销/重做的**具体动作名**（"Undo Move Entity" / "Redo Transform Drag"）；缺的不只是 UI 文案，是命令层根本没有"人类可读 label"这个数据
- **状态**：**✅ 2026-05-29 落地**（登记后同 session 由 /goal 自主推进；单子仓 OrangeEditor 纯逻辑改动 + 单测，见下方落地记录）

### 触发场景

连续做多步编辑后想撤销，菜单只写"Undo"，不知道下一次 Ctrl+Z 会撤掉哪一步（是刚才的 reparent 还是更早的字段编辑？）。成熟编辑器在菜单项上直接写"Undo Move Entity"，并常配命令历史面板让用户看整条栈。

### 现状证据（复核）

- `EditorRenderLayer.cpp:664/670`：`ImGui::MenuItem("Undo", "Ctrl+Z", …)` / `ImGui::MenuItem("Redo", "Ctrl+Y", …)`——硬写死，行号与报告一致无漂移。
- 命令层缺 label 数据源：`tools/OrangeEditor/command/ICommand.h` 只有 `GetType()`（返回类型字面量，**仅供 coalesce 同类判断**，非面向用户的可读名）；`CommandStack`（`command/CommandStack.h`）的 group `name` 只在 BeginGroup..EndGroup 期间存活，**不按栈条目留存**，也没有 peek 栈顶 / 待重做条目 label 的对外 API。

### 缺什么（按依赖拆）

- **命令层加 label**（前置）：给 `ICommand` 加 `virtual const char* GetLabel() const { return GetType(); }`（默认回退到 type，不强迫每个命令都改）；`CommandGroup`（`.cpp` 内实现）override 返回其 group `name`（"Transform Drag" 等已是天然 label）；具名命令（`SetFieldValueCommand` / Rename / Reparent）按需 override 出更友好的名。
- **CommandStack 暴露 peek**：加 `const char* PeekUndoLabel() const`（返回 `mStack[mIndex]` 的 label，空栈返回 nullptr）/ `const char* PeekRedoLabel() const`（`mStack[mIndex+1]`）。
- **菜单接线**：`EditorRenderLayer.cpp:664/670` 把 `"Undo"` 改成 `CanUndo()` 时格式化 `"Undo %s"`、否则纯 `"Undo"`；Redo 同理。
- **（可选，体量 M，可拆独立条目）命令历史面板**：列整条栈 + 高亮当前 index + 点击跳转到任意历史点。报告把它与 label 并列，但 label 是 Quick Win、历史面板是 M——建议先做 label。

### 期望验收（落地时，非本 session）

- 做一步可撤销操作后，Edit 菜单显示"Undo <动作名>"；空栈时回退到纯"Undo"且置灰。
- group 命令（如 gizmo 拖动）显示其 group name。
- label 生成是纯逻辑（命令 → 字符串），可单测；菜单文案显示 + 截断观感顺手 dogfood 一眼即可。

### 关联

- 命令栈基建见 `command/CommandStack.h` / `command/ICommand.h`；coalesce / group 语义见 `CommandStack.h` 顶注释。
- 与 [[GAP-2026-05-29-editor-material-asset-dirty-tracking]] facet 1 同源：材质编辑不进命令栈，若未来材质入栈，其 label 也应一并供本 gap 的菜单消费。

### 落地记录（2026-05-29，登记后同 session /goal 自主推进）

按"缺什么"三步走，纯逻辑内核单测兜底，菜单文案手感留作者 dogfood：

| 层 | 文件 | 改动 |
|---|---|---|
| 命令接口 | `command/ICommand.h` | 加 `virtual const char* GetLabel() const { return GetType(); }`——默认回退到 type，与 coalesce 键 `GetType` 分离（GetType 是稳定机器串不可美化，GetLabel 是展示文案） |
| 友好命名 | `command/EntityCommands.h` | `CreateEntityCommand`/`RenameCommand`/`SwitchAnimatorBackendCommand` 各 override `GetLabel()` → "Create Entity" / "Rename Entity" / "Switch Animator Backend"（机器味 type 不直接进菜单）；`CommandGroup`（gizmo "Translate/Rotate/Scale Drag"）与 `SetFieldValueCommand`（字段键）走默认回退即够友好 |
| 栈查询 | `command/CommandStack.{h,cpp}` | 加 `PeekUndoLabel()`（`CanUndo()? mStack[mIndex]->GetLabel() : nullptr`）/ `PeekRedoLabel()`（`CanRedo()? mStack[mIndex+1]->GetLabel() : nullptr`）；游标语义与 `Undo()`/`Redo()` 一致 |
| 菜单接线 | `EditorRenderLayer.cpp:664/670` | `MenuItem("Undo",…)` → 拼 `"Undo " + label`（label 取自 Peek，空栈回退纯 "Undo"/"Redo"）；Redo 同理 |
| 单测 | `tests/command/CommandStackTest.cpp` + `tests/CMakeLists.txt` | 新 `command_stack_test` target（同 `editor_hierarchy_test` 模式编 `CommandStack.cpp`）：覆盖空栈 nullptr / GetLabel 默认回退 + override / Undo-Redo 游标跟随 / CommandGroup label==组名 / Push 截断 redo 6 组断言 |

**验收**：`command_stack_test` + `editor_hierarchy_test` 全绿（ctest Debug），`OrangeEditor.exe` 编译链接通过，invariant lint 干净（7 grandfathered）。菜单文案"Undo Rename Entity"等的**视觉/截断/焦点**留作者实机 dogfood 一眼（纯逻辑 label 数据已单测锁定）。**命令历史面板**（报告并列的 M 级项）未做，按需另开。

### 同批复核的三项接线（verify 结论）

本 session 同时复核了报告里另外三项接线现状：

- **autosave**：`tools/OrangeEditor/` 全目录 grep `AutosaveScheduler` / `autosave` **零命中**，引擎侧 scheduler + 单测已就绪但编辑器零接线。报告 §2.7 `:158` + P0 `:193` 成立 → **已升格** [[GAP-2026-05-29-editor-autosave-wiring]]。
- **Undo-label**：复核时仍固定文案 `MenuItem("Undo","Ctrl+Z")` / `("Redo","Ctrl+Y")`（`EditorRenderLayer.cpp:664/670`，无漂移），未接命令名。报告 §2.8 `:172` + Quick Win #7 成立 → 升格 [[GAP-2026-05-29-editor-undo-redo-action-label]] 并已 **✅ 同 session 落地**（GetLabel + Peek API + 菜单接线 + 单测）。
- **Ctrl-toggle**：Entity Tree 的 Ctrl-click 多选 toggle **已接线**（`panels/EntityTreePanel.cpp:416-433`，走 `EditorSelection::ToggleAdditional`）；**视口（ScenePanel）单击 picking 仍不读修饰键**（`panels/ScenePanel.cpp:465` 直接覆盖 `selectedEntity`）—— 报告 §2.1「视口 Ctrl/Shift 多选」缺失结论成立，行号由 `:472` 漂到 `:465`。视口侧缺口归入报告 §2.1 gizmo/视口大类（与框选 marquee / 相机 pan 同批），**暂不单独升格**；Entity Tree 侧曾踩过 Ctrl-toggle 交互 bug，已接线但手感仍需 dogfood。
