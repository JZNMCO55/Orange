# Phase 6.5 · 渲染真实感基线（PBR + IBL）milestone

- **状态**：ACTIVE — 起草于 2026-05-17；6 项 review 议题已全部决议（见末尾决议记录）；已并入 `docs/roadmap.md` 作为 Phase 6.5 outline 入口，本文件作为详细 companion（与 `editor-roadmap.md` 之于 Phase 6 同款节奏）；下一步开 B.1 commit-1 + OrangeRender audit session
- **路线图入口**：[`docs/roadmap.md`](./roadmap.md) Phase 6.5 节
- **参考实现**（2026-05-17 audit 确认）：Lumix `data/shaders/common.hlsli` (776 行 PBR 库：`F_Schlick` L465 / `D_GGX` L764 / V_Smith 等齐全) + `data/shaders/standard.hlsl`（monolithic PBR 主路径）+ `data/shaders/ibl_filter.hlsl`（**122 行**单文件做 BRDF LUT + irradiance + prefiltered specular 三种卷积）+ `render_module.h:267 EnvProbeInfo`（EnvironmentComponent ECS 对位）。我们方案 B 直接对标这套
- **驱动**：编辑器 v0.6.5 完工后 retro 发现"场景观感整体偏亮、几何体塑料感强"，根因是默认 mesh shader（`textured_mesh.frag.glsl`）只是开发期校验用的"棋盘 × 阴影"，没装任何 lighting model；ACES tonemap 在链尾，但上游喂进来的不带法线响应的图，tonemap 救不回真实感
- **目标观感对照**：Cocos Creator `standard.effect` / Godot `StandardMaterial3D` 的 default look —— Cook-Torrance GGX + Schlick fresnel + Smith G + Lambert diffuse + IBL（prefiltered specular cubemap + diffuse irradiance + BRDF LUT 2D）+ ACES tonemap
- **本 milestone 解锁**：编辑器 viewport 默认观感跃迁；后续 Phase 10 的 SSAO / SSR / 软阴影都建立在 PBR baseline 之上，不会再撞"shader 还在棋盘期"的尴尬
- **本 milestone 不解锁**：Reflection probes / Light probes / SH-based GI / 各向异性 / clear-coat / sheen — 全部 out-of-scope，留给后续单独 milestone 按需触发

---

## 范围划定

### In-scope

| 模块 | 内容 |
|------|------|
| Shader | 替换 `textured_mesh.frag` 路径为 `pbr.frag`，或并存（决议见 PBR-01）。Cook-Torrance + Schlick + GGX NDF + Smith G + Lambert |
| Material | `MaterialInstance` 五通道：baseColor / metallic / roughness / normal map / AO map。texture 槽 optional，未绑定时退化为 scalar uniform |
| IBL · BRDF LUT | 启动期 / 编译期一次性烘焙 2D R16G16F 256×256 split-sum LUT |
| IBL · Diffuse | 环境 cubemap → 32×32×6 irradiance cubemap（Lambertian 半球积分卷积） |
| IBL · Specular | 环境 cubemap → 多 mip prefiltered specular cubemap（GGX importance sampling 逐 mip 卷积，mip level ↔ roughness 一一对应） |
| 资产 | 新增 `EnvironmentComponent`（cubemap asset + intensity scalar + tint）；HDR equirect 输入 → 启动期 resample 到 cube；至少 1 张 default IBL（室外 sky） |
| Sample | 新增 `samples/14_pbr_ibl`：9 球阵（3 metallic × 3 roughness）+ 1 张 IBL 环境，对照 Frostbite / UE 经典 IBL 验证场景 |

### Out-of-scope（明确推迟）

| 项 | 推迟位置 |
|----|---------|
| SSAO | 已在 Phase 10-03 占位 |
| SSR | 已在 Phase 10-02 占位 |
| Reflection probes（多 IBL 体积） | 后续独立 milestone |
| Light probes（SH / volumetric GI） | 后续独立 milestone |
| 各向异性 / clear-coat / sheen / iridescence | BRDF 扩展项，按游戏需求拉动 |
| 物理 material 参数手册化 + 序列化版本演进 | 编辑器侧伴随工作 |
| 编辑器 PBR Inspector schema / Environment asset 浏览 / material preview thumbnail | 编辑器 milestone（v0.8 候选，独立立项） |

---

## 跨仓依赖（OrangeRender 侧 audit / 需求）

**绝对不允许在本 milestone 同一 session 内既向 OrangeRender 提 feature 又消费**——CLAUDE.md 红线。本节列出**预估**需要跨仓 audit / 提需求的项；进 milestone 第一步是开一个 OrangeRender audit session 把 R1 ~ R4 真实状态量出来，缺什么按 `vendor/OrangeRender/docs/incoming_feature.md` 登记，等 OrangeRender 落地 + tag 后**新 session** 才 bump 消费。

**风险水平更新**（2026-05-17 Lumix audit 后）：R1 / R2 在 Lumix（Vulkan + DX12 双后端跑 IBL）属 RHI 标准能力，audit 大概率 pass，从"milestone-blocking 主要风险"降级为"常规验证项"；R3 / R4 风险水平不变。**方案 B 进一步的好消息**：B.1 阶段完全不依赖 R1 ~ R4 任何一项（PBR direct lighting 用现有 RHI 即可），audit session 与 B.1 实施可并行；audit 结论只影响 B.2。

**audit 启动时机已决**：**B.1 commit-1 当天**就开 OrangeRender audit session（与本仓 B.1 推进并行）。理由：audit 是独立 session 不占本仓上下文；最早开 = 最早收尾 = R 缺需求时 OrangeRender 侧 land + tag 可在 B.1 期间完成，B.2 起步零等待。

### R1 · cubemap 完整 binding 链 audit

- 已知：`vendor/OrangeRender/include/orange/rhi/RHICapabilities.h` 已声明 `mTextureCubeArray` / `mMaxTextureCubeSize`
- 未知：cubemap texture 创建（6 face upload）/ cube view / cube sampler bind 是否端到端通
- audit 步骤：开 OrangeRender 仓 session，写最小 Vulkan test —— 创建 6×64×64 cube → 上传 → bind 到 fragment shader → 采样验证。pass 才算通
- 若失败：登记 `FEATURE-2026-XX-cubemap-binding`

### R2 · mipmap level-by-level upload

- IBL prefiltered specular 必需：每个 mip level 用不同 roughness 跑卷积，结果写回**指定 mip level**
- audit：现有 `IRenderDevice::CreateTexture` / texture upload API 是否支持 `dst mip level` 参数
- 若失败：登记 `FEATURE-2026-XX-texture-mip-write`

### R3 · texture format R16G16F

- BRDF LUT 是 R16G16F 2D；HDR scene color R16G16B16A16F 已用应该没问题
- audit：R16G16F 是否在 `RHIFormat` 枚举内 + Vulkan backend 是否能创建该格式 texture
- 若失败：登记 `FEATURE-2026-XX-rg16f-format`

### R4 ·（可能）compute shader pipeline

- IBL prefilter / irradiance / BRDF LUT 三种烘焙：compute 路径快，graphics fullscreen pass fallback 也可接受
- audit：`RHICompute*` 系列 API 是否完整
- **如缺**：本 milestone 改走 graphics fallback（每个烘焙写成 fullscreen quad fragment shader），不强制提需求 —— 性能影响仅限**启动期一次性**烘焙，可接受

### audit session 落地后

R1 ~ R4 全部"通过 audit + 必要的 incoming_feature 落地完毕"后，再开**本 milestone 的实施 session**。审视卡点是 R2 —— 真的不通就 IBL specular 完全做不了，必须先跨仓修。

---

## Task Breakdown

> 任务字段沿用 `design-plan.md` 风格（描述 / 前置 / 输出 / 实现要点 / 验证 / 验收 / Critical Path）。Task 编号待并入 `roadmap.md` 时统一改 `10-XX`；草稿期用 `PBR-NN` 短编号。

> **关键架构选择（参考 Lumix `standard.hlsl`）**：**单一 monolithic PBR shader**，IBL 三纹理（BRDF LUT / irradiance / prefiltered specular）作为强制 sampler 槽，**没真实 IBL 时绑 1×1 黑色 dummy texture**。B.1 阶段直接绑 dummy，shader 内 IBL 贡献项 = 0 自然退化；B.2 阶段把 dummy 替换成真实烘焙产物，**零 shader 重构成本**。`textured_mesh.{vert,frag}` 保留作 dev-checker fallback 不删。

---

### B.1 子 milestone · PBR direct lighting（不依赖跨仓）

> **目标**：场景脱离"棋盘 × 阴影"塑料感，几何体按 N·L 明暗，metallic / roughness 物理感可调。约 3-5 天。

#### Task PBR-01 · monolithic PBR shader 落地（IBL 槽位 dummy）

- **描述**：新增 `src/render/builtin_shaders/pbr.{vert,frag}.glsl` —— **一份 shader 含 direct + IBL 全路径**，IBL 三纹理在 B.1 阶段绑全局 dummy 1×1 黑 cubemap / 2D 黑 LUT，自然退化为 direct-only。`textured_mesh` 不动，保留作 dev fallback
- **前置**：无（B.1 完全本仓内闭环，与 OrangeRender audit session 并行不阻塞）
- **输出**：`pbr.{vert,frag}.glsl` + `Pipeline` 内 PBR pass 接管 Renderable 默认渲染路径 + 全局 dummy IBL 纹理资源
- **实现要点**：
  - Cook-Torrance specular = `D(α) · G(α, NoL, NoV) · F(F0, VoH) / (4 · NoL · NoV)`
  - NDF：GGX / Trowbridge-Reitz（`vendor/Orange-Wiki/wiki/concepts/rendering/microfacet-theory.md`；参考 Lumix `common.hlsli:764 D_GGX`）
  - G：Smith GGX correlated（同 wiki；参考 Lumix `common.hlsli` V_Smith 段）
  - F：Schlick fresnel `F0 + (1 - F0)(1 - VoH)^5`（`vendor/Orange-Wiki/wiki/concepts/rendering/fresnel-reflectance.md`；Lumix `common.hlsli:465 F_Schlick`）
  - Diffuse：Lambert `albedo / π`（先不引入 disney-diffuse / burley，避免节外生枝；`vendor/Orange-Wiki/wiki/concepts/rendering/diffuse-brdf-models.md`）
  - 能量守恒：`kS = F; kD = (1 - kS)(1 - metallic)`
  - IBL 段写在 shader 内但 dummy 纹理采样结果 = 0，不需要 `#if HAS_IBL` 编译分支
- **验证**：sample 场景 9 球（3 metallic × 3 roughness）按 PBR 渲染，金属 / 塑料视觉区分明显；调 roughness 0→1 specular highlight 从 sharp 到 wide 连续；关掉所有 direct light → 全黑（dummy IBL 为 0 验证）
- **Critical Path**：是

#### Task PBR-02 · MaterialInstance 五通道 + texture binding（含 Inspector schema 同步）

- **描述**：扩展 `MaterialInstance` 暴露 baseColor / metallic / roughness / normal / AO 五通道；每通道可绑 texture，未绑时退化为 scalar uniform。**同 commit 序列内**完成编辑器 Inspector schema 同步（v0.2.5 schema-first 架构下加 5 个 PropertyDescriptor 是 hours 级工作，不允许 PBR ✅ 而 Inspector 看不到 metallic / roughness 字段的断层态）
- **前置**：PBR-01
- **输出**：
  - `MaterialInstance` 公共 API + shader 端 sample-or-uniform 分支
  - `tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp` 内 Material schema 加 5 字段，DragFloat / ColorEdit3 / AssetRef 控件按字段类型分配
- **实现要点**：
  - shader 端用 specialization constant 或 `#define HAS_METALLIC_MAP` 走 sample 路径；无 map 时编译器直接 dead-code-elim 掉 sampler 访问
  - **Environment 浏览 / material thumbnail / asset 预览**等真正的编辑器扩展留给 B.2 完工后的独立编辑器 milestone（v0.8 候选）
- **验证**：
  - JSON scene 直接写 metallic=0.9 / roughness=0.2 → 渲染金属感；切换 metallic 0/1 通道 → diffuse/specular 能量分配跟随
  - 编辑器 Inspector 选中 material 后能看到并实时拖拽编辑五字段
- **Critical Path**：是

#### Task PBR-03 · sample `13_pbr_direct` + B.1 验收

- **描述**：新 sample `samples/13_pbr_direct`：9 球阵（3 metallic × 3 roughness）+ 1 个 directional light，**不接 IBL**。validate Cook-Torrance + Schlick + GGX + Smith 数学正确性
- **前置**：PBR-01 / 02
- **输出**：`samples/13_pbr_direct/main.cpp` + scene asset
- **实现要点**：用 Khronos `glTF-Sample-Models/MetalRoughSpheres` 或自制等价 9 球
- **验证**：视觉效果与 wiki ref 图比对，metallic 轴与 roughness 轴的变化连续 + 物理直观；旋转 light 方向高光位置随之移动
- **Critical Path**：是

---

### B.2 子 milestone · IBL 完整接入（依赖跨仓 audit pass）

> **目标**：从"PBR direct only"升级为"PBR direct + IBL"，金属球能映环境、阴影区有环境填充。约 1.5-2 周。前置 OrangeRender audit session 跑通 R1 + R2 + R3。

#### Task PBR-04 · IBL 三种卷积烘焙（合并 BRDF LUT + irradiance + prefiltered specular）

- **描述**：单一烘焙路径产出 IBL 三件套 —— BRDF LUT（2D R16G16F 256×256）+ irradiance cubemap（32×32×6）+ prefiltered specular cubemap（多 mip，base 256×256×6，9 个 mip level）。**参考 Lumix `data/shaders/ibl_filter.hlsl` 122 行单文件多 entry point 风格**，不拆三个 task
- **烘焙时机已决**：**启动期一次性烘焙**（与 IBL prefilter 共享同一启动期烘焙路径；不走编译期 codegen——理由：IBL prefilter 必须启动期，多一条编译期路径属工程复杂度净增，省 ~50ms 一次性启动开销不值得）
- **前置**：R1 + R2 + R3 OrangeRender audit pass（cubemap binding + mipmap level-by-level upload + R16G16F format）
- **输出**：`src/render/builtin_shaders/ibl_bake.{vert,frag}.glsl`（或同名 compute）+ 启动期烘焙路径（环境 HDR 加载完毕后一次性触发，结果 cache 到 EnvironmentComponent 内；BRDF LUT 作为全局共享纹理只烘焙一次）
- **实现要点**：
  - **BRDF LUT**：split-sum 近似第二项预积分，输入 `(NoV, roughness)` → 输出 `(scale, bias)`。GGX importance sampling 1024+ 采样
  - **Irradiance**：环境 cube → 32×32×6 半球 Lambertian 卷积，每像素 ~512 cos-weighted samples（diffuse 低频，32 分辨率充分）
  - **Prefiltered specular**：每个 mip level 用对应 roughness 跑 GGX importance sampling 卷积，结果**逐 mip 写入**目标 cubemap（mip 0 ↔ roughness 0，mip N ↔ roughness 1）
  - 数学：`vendor/Orange-Wiki/wiki/concepts/rendering/environment-lighting.md` 的 split-sum 段 + `microfacet-theory.md` 的 GGX importance sampling 段
- **验证**：
  - LUT 数值对照 RTR 第 9 章参考表 ±2% 内
  - Lambertian sphere 仅 IBL 光照 → 颜色 ≈ 环境平均色
  - 金属球在 roughness 0 → 1 范围观察反射模糊连续过渡；mip-level 选择公式 `mipLevel = roughness * (mipCount - 1)`
- **Critical Path**：是（**风险点**：R2 不通 → 本 task 完全 blocked；Lumix 双后端跑通了，audit pass 概率高但仍需验证）

#### Task PBR-05 · IBL 接入 PBR shader（替换 dummy 槽位）

- **描述**：把 B.1 阶段的 dummy 1×1 黑色 IBL 纹理替换成 PBR-04 烘焙出的真实纹理。**shader 一行不改**（B.1 的 shader 已经预留 IBL 段）
- **前置**：PBR-04
- **输出**：纹理槽 binding 切换 + 全局 BRDF LUT 资源管理
- **实现要点**：
  - split-sum 近似：`L_specular_IBL = prefiltered.sampleLevel(R, roughness * maxLod) * (F0 * brdfLut.x + brdfLut.y)`
  - `L_diffuse_IBL = irradiance.sample(N) * albedo / π`
  - 总输出 = direct + (1 - F)(1 - metallic) * diffuse_IBL + specular_IBL
  - B.1 dummy 纹理保留作 fallback（场景未挂 EnvironmentComponent 时退化）
- **验证**：把所有 direct light intensity 设 0，单靠 IBL 渲染 9 球阵 → 金属球清晰映出环境，粗糙塑料球漫反射环境平均色；furnace test（全白 IBL + 任意 PBR 材质应输出近似全白，验能量守恒）
- **Critical Path**：是

#### Task PBR-06 · EnvironmentComponent + 资产管线

- **描述**：World 上挂全局 `EnvironmentComponent`（cubemap asset + intensity scalar + tint）；Pipeline 把 environment 三纹理（BRDF LUT 全局共享 / irradiance / prefiltered specular 跟 environment 走）传给 PBR shader。**对标 Lumix `render_module.h:267 EnvProbeInfo`**
- **default IBL 已决**：从 **PolyHaven CC0 HDRI** 站选一张 outdoor scene 1K equirect（~5MB）入 `assets/environments/default_outdoor.hdr`，许可证 CC0 零摩擦；不走 ChatGPT 生成 8-bit equirect 转 HDR（banding + 接缝问题）也不走程序化天空（依赖 Phase 10-06 atmospheric scattering 时间线远）
- **前置**：PBR-05
- **输出**：`include/orange/engine/render/EnvironmentComponent.h` + 配套 loader / serializer / Pipeline 接入；至少 1 张 default IBL（PolyHaven outdoor 1K HDR equirect）放 `assets/environments/`
- **实现要点**：
  - 输入 asset 格式：HDR equirect（.hdr Radiance RGBE）—— 最常见的 HDR 环境贴图源；启动期 resample 到 cube；离线 cook 路径留 Phase 9
  - 编辑器侧：B.1 PBR-02 已经把 Material schema 覆盖；本 task 同时补 Environment schema 让 Inspector 能改 intensity / tint / cubemap 资产引用（Environment 浏览器 / sky placeholder 渲染留给 v0.8 编辑器 milestone）
- **验证**：demo.scene 切换不同环境 HDR → 整场景观感跟变
- **Critical Path**：是

#### Task PBR-07 · sample `14_pbr_ibl` + B.2 验收

- **描述**：`samples/13_pbr_direct` 升级为 `samples/14_pbr_ibl`（或并存）：9 球阵 + IBL 环境 + 1 directional light。参考 Frostbite / UE / glTF reference renderer 经典 IBL 验证场景
- **前置**：PBR-04 / 05 / 06
- **输出**：`samples/14_pbr_ibl/main.cpp` + scene asset + 至少 1 张 IBL 环境
- **验证**：
  - 视觉效果与 wiki ref 图比对，metallic 轴与 roughness 轴变化连续 + 物理直观
  - 切换 IBL 环境（晴天 / 室内 / 黄昏）整场景观感跟变
  - furnace test：白炉环境下任意材质球应输出近似白色
- **Critical Path**：是

---

## Acceptance checklist（user-testable，面向无代码能力测试人员）

> 按 `feedback_testing_instruction_audience.md` 纪律：仅 GUI 操作 + 视觉结果，不引用代码 / 终端 / 命令行。

### 功能区 1 · 默认观感跃迁

- [ ] 启动 OrangeEditor，打开 `assets/scenes/demo.scene`
- [ ] 场景里所有几何体（球 / cube / plane）现在有 N·L 明暗（朝向光的面亮、背光的面暗，不再是整张棋盘色）
- [ ] 阴影区域不再是 30% 灰平涂，而是有 IBL 环境光填充（隐约能看出周围环境的色调）

### 功能区 2 · Material 物理感

- [ ] Inspector 选中一个球，把 metallic 从 0 拖到 1：表面应从"塑料漫反射"过渡到"金属反射环境"
- [ ] 把 roughness 从 0 拖到 1：高光从"清晰小点"过渡到"模糊大片"再到"几乎平涂"
- [ ] 把 baseColor 改成红色：metallic=0 时整球变红，metallic=1 时高光带红色（金属吸收非反射颜色的物理表现）

### 功能区 3 · IBL 环境切换

- [ ] World 上挂个 EnvironmentComponent，切换 environment cubemap 到不同 HDR（室外 / 室内 / 黄昏）
- [ ] 切换后整场景色调跟随环境变化；金属球反射的环境内容也随之改变
- [ ] 把 environment intensity 调到 0：场景退化为"仅 direct light"，全部 plausible 但偏暗

### 功能区 4 · sample 验证

- [ ] 跑 `samples/14_pbr_ibl.exe`，看到 9 球阵
- [ ] 9 球的视觉效果与 milestone PR 描述里的 reference 图基本一致

---

## 风险与未决项

| ID | 风险 | 范围 | 触发条件 | 缓解 |
|----|------|------|----------|------|
| RISK-1 | R2 mipmap level-by-level upload 不通 | B.2 only | OrangeRender audit 阶段确认 | **已降级**（Lumix Vulkan + DX12 双后端跑通，RHI 标准能力）。仍 audit 验证；若万一不通则开 OrangeRender feature session 解决，期间 B.1 可正常推进 |
| RISK-2 | R1 cubemap binding 不通 | B.2 only | OrangeRender audit 阶段确认 | **已降级**（同 RISK-1 理由）。若不通则 B.2 blocked，B.1 不受影响 |
| ~~RISK-3~~（已决） | ~~BRDF LUT 编译期 vs 启动期方案未决~~ | ~~B.2~~ | ~~实施期~~ | **2026-05-17 决：启动期一次性烘焙**（与 IBL prefilter 共享同一启动路径）。若实施期发现启动 GPU 开销超出预期再重开讨论 |
| RISK-4 | HDR equirect → cube resample 视觉伪影（极点过采样 / 接缝） | B.2 | 实施期 | 已知问题，wiki `environment-lighting.md` 有标准解法（filterable importance sampling + box filter at seams）；可参考 Lumix `ibl_filter.hlsl` 实现 |
| RISK-5 | metallic / roughness "看起来对了"但能量不守恒 | B.1 + B.2 | 实施期 | sample `14_pbr_ibl` 内置 furnace test（全白 IBL + 任意 PBR 材质应输出近似全白；不平衡的 BRDF 会偏暗 / 偏亮）|
| RISK-6 | 落地后编辑器侧 schema 没跟上 | 落地阶段 | B.1 / B.2 任一完工时 | **2026-05-17 决：分两段**——B.1 Material schema 同步紧跟 PBR-02 同 commit 序列（hours 级，不另立 milestone）；B.2 Environment schema 同步紧跟 PBR-06；完整编辑器扩展（Environment 浏览 / material thumbnail）作为独立 v0.8 编辑器 milestone 在 B.2 完工后启动 |
| RISK-7 | B.1 dummy IBL 纹理资源管理与正式 IBL 切换不平滑 | B.2 起步阶段 | B.2 实施期 | dummy IBL 是 `Pipeline` 启动期注册的全局 1×1 黑 texture；B.2 替换路径设计：`EnvironmentComponent` 优先级 > dummy，没挂 component 自动 fallback dummy。**B.1 阶段就把 fallback 路径写完整**，避免 B.2 临时改 |

---

## milestone 规模 / 节奏

**方案 B（两步走）已锁定**（2026-05-17 user 决定）。整体规模约 2-3 周；Lumix audit 后跨仓风险降级，B.1 完全本仓闭环可与 OrangeRender audit session 并行。

| sub-milestone | Task 范围 | 预估 | 跨仓依赖 |
|---|---|---|---|
| **B.1** PBR direct lighting | PBR-01 / 02 / 03 | 3-5 天 | 无 —— 完全本仓闭环 |
| **B.2** IBL 完整接入 | PBR-04 / 05 / 06 / 07 | 1.5-2 周 | R1 + R2 + R3 audit pass（cubemap / mipmap upload / R16G16F），audit 与 B.1 并行 |

**关键架构杠杆**：monolithic PBR shader + dummy 1×1 黑 IBL 纹理（参考 Lumix `standard.hlsl`）—— B.1 写一份 shader 同时含 direct + IBL 段，IBL 段用 dummy 纹理退化为 0；B.2 仅替换 dummy 为真实烘焙产物，**shader 一行不改**。这条让 B.1 → B.2 衔接零重构成本，也是把 IBL 三 task（BRDF LUT / irradiance / prefilter）合并成单 Task PBR-04 的依据。

---

## wiki citations（实施期必读）

| 主题 | 路径 |
|------|------|
| 反射方程基础 | `vendor/Orange-Wiki/wiki/concepts/rendering/brdf-foundations.md` |
| Microfacet 模型（GGX NDF + Smith G） | `vendor/Orange-Wiki/wiki/concepts/rendering/microfacet-theory.md` |
| Fresnel（Schlick） | `vendor/Orange-Wiki/wiki/concepts/rendering/fresnel-reflectance.md` |
| Diffuse BRDF（Lambert vs Disney/Burley） | `vendor/Orange-Wiki/wiki/concepts/rendering/diffuse-brdf-models.md` |
| Lighting 系统 + IBL split-sum | `vendor/Orange-Wiki/wiki/concepts/rendering/environment-lighting.md` |
| Lighting 总览 | `vendor/Orange-Wiki/wiki/concepts/rendering/lighting-and-shading.md` |
| AO（out-of-scope 但参考） | `vendor/Orange-Wiki/wiki/concepts/rendering/ambient-occlusion.md` |

---

## 决议记录（review 议题 closure log）

6 项 review 议题全部决议完毕（2026-05-17）。**草稿状态从"等 review"切到"等执行"**：剩余工作是把本文件内容并入 `docs/roadmap.md` 作为新 Phase 6.5，然后开 B.1 commit-1（同时开 OrangeRender audit session）。

| 议题 | 决议 | 理由摘要 |
|------|------|----------|
| 1 · 拆分方案 | **方案 B 两步走** | B.1 PBR direct lighting（不依赖跨仓）+ B.2 IBL 完整接入；先把"塑料 → 真实"视觉跃迁拿到手，IBL 加深度 |
| 2 · 归并位置 | **新 Phase 6.5 · 渲染真实感基线** | 对位 Phase 5.5 Save Game 先例（独立小 phase 承接基础设施）；不塞 Phase 10——Phase 10 是按需启用可选项，PBR baseline 是其前提条件 |
| 3 · BRDF LUT 烘焙时机 | **启动期一次性烘焙** | 与 IBL prefilter 共享同一启动路径；多一条编译期 codegen 路径属工程复杂度净增，省 ~50ms 不值；Lumix 同款 |
| 4 · default IBL 环境 | **PolyHaven CC0 HDRI** | 工业标准真 HDRI，CC0 零摩擦许可证；不走 ChatGPT 8→HDR（banding + equirect 接缝）；不走程序化天空（依赖 Phase 10-06 时间线远） |
| 5 · 编辑器伴随 | **分两段** | B.1 Material schema 同步紧跟 PBR-02（hours 级同 commit 序列）；B.2 Environment schema 紧跟 PBR-06；真正的编辑器扩展（Environment 浏览 / material thumbnail）独立 v0.8 milestone 在 B.2 后启动 |
| 6 · audit session 时机 | **B.1 commit-1 当天开** | audit 独立 session 不占本仓上下文；最早开 = 最早收尾 = R 缺需求时 OrangeRender 侧可在 B.1 期间 land + tag，B.2 起步零等待 |

---

## 落地下一步（执行 checklist）

1. **并入 `docs/roadmap.md`**：本文件内容（Task 编号从 PBR-NN 改为 Phase 6.5 Task 编号）插入到现 Phase 6 节后、Phase 7 节前
2. **开 OrangeRender audit session**（独立 session）：跑 R1 / R2 / R3 minimal Vulkan test；audit session 与本仓 B.1 commit-1 同日启动
3. **开 B.1 commit-1**：Task PBR-01 monolithic PBR shader + dummy IBL 槽
4. **B.1 完工 ritual**：跑 `scripts/check_invariants.py` + `scripts/check_claude_md_drift.py`，标 Phase 6.5 B.1 ✅
5. **检查 audit 收尾状态**：B.2 commit-1 之前确认 R1/R2/R3 audit 全 pass（或 OrangeRender 侧需求已 land + bump 完）；卡住的话拉 B.2 起步时间，**不**在同 session 一边推 B.2 一边等 OrangeRender——CLAUDE.md 红线
6. **开 B.2**：PBR-04 → PBR-05 → PBR-06 → PBR-07 顺序执行
7. **Phase 6.5 完工 ritual**：跑两份 lint，标 Phase 6.5 ✅，拉 v0.8 编辑器伴随 milestone
