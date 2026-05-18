# Phase 6.5 B.2 · PBR + IBL 完整接入 验收清单

- 基准日期：2026-05-18
- 适用范围：Phase 6.5 B.2（Task 06.5-04 / 05 / 06 / 07）—— IBL 三件套烘焙 + EnvironmentComponent + sample `14_pbr_ibl`
- 设计意图：只列用户能在 sample / 编辑器内点击 / 拖拽 / 看效果的**核心**功能；B.1 已验项不复列

## 前置环境

1. `cmake --build build --config Debug` 一次性把 OrangeEditor + sample 14_pbr_ibl 都编出来
2. 按 `assets/environments/README.md` 步骤从 polyhaven.com 下 1 张 1K CC0 outdoor HDR，重命名 `default_outdoor.hdr` 放进 `assets/environments/`

## 核心功能

### 1. sample 14_pbr_ibl 默认模式（真实 HDR IBL）

跑 `build/bin/Debug/14_pbr_ibl.exe`：

- [ ] 看到 **3×3 暖橙球阵**（与 13_pbr_direct 相同布局）
- [ ] 右上角（metallic=1 roughness=0.1）**金属球清晰反映环境内容**（能识别出 HDR 大致色调 / 亮度分布）
- [ ] 中列（roughness=0.5）金属球反射**模糊化**；右列（roughness=0.9）几乎漫反射环境平均色
- [ ] 漫反射球（左下，metallic=0）**阴影区不再纯黑**，被环境光填充

### 2. sample 14_pbr_ibl furnace test 模式（能量守恒）

跑 `build/bin/Debug/14_pbr_ibl.exe --furnace`：

- [ ] 窗口标题显示 `(--furnace)`，控制台 print 一行 furnace 说明
- [ ] 9 球阵**整体接近白色**（程序化 1×1 全白 RGBA32F equirect + 关掉 direct 光，纯靠 IBL 驱动）
- [ ] 球与背景边界仍可辨识但**无明显偏暗 / 偏亮**（偏暗 = BRDF 能量损失；偏亮 = 重复计入）

### 3. 编辑器 EnvironmentComponent Inspector 调参

启动编辑器，在 Hierarchy 任选 / 新建一个实体挂 **Environment** component：

- [ ] Inspector 出现 **Environment** 段，含 Cubemap (HDR) / Tint / Intensity 三字段
- [ ] **Intensity 拖 0 → 4**：viewport 整场景 IBL 贡献明暗跟随（前提：场景内有 Material(pbr) 实体 + HDR 已在 default 路径）
- [ ] **Tint 改红**：IBL 贡献整体偏红

## 大节点回归

- [ ] **既有 sample 不退化**：跑 `13_pbr_direct.exe` 9 球阵渲染正常（dummy IBL 路径回退仍工作）
- [ ] **资产缺失 graceful**：删除 `assets/environments/default_outdoor.hdr` 后跑 `14_pbr_ibl.exe`，控制台 print warn 但**仍能渲染**（视觉等价 13_pbr_direct，金属球反射黑）
- [ ] **lint baseline 全绿**：`python scripts/check_invariants.py` + `python scripts/check_claude_md_drift.py` 各自退出码 0

## 已知 bug · sample 14_pbr_ibl baked IBL 接通路径 segfault（2026-05-18 抓到 + 续二分）

**现象**：`build/bin/Debug/14_pbr_ibl.exe --furnace` 启动后崩在第一次 Render，exit code 139（segfault）。默认模式（无 `assets/environments/default_outdoor.hdr` 时走 dummy IBL fallback）能正常跑——所以**只有真实 baked IBL 接通路径**触发崩溃；推测真实 HDR 路径同款症状（未测）。

**首轮二分（c10 ritual 期间）**：

- ✅ crash **不**在 `Pipeline::BakeIblFromWorld` 内部（烘焙日志 "三件套烘焙完成" 出来了）
- ✅ crash **不**在 baker 生命周期 / 子资源 view 析构（不调 SetIblTextures 后烘焙跑完不崩）
- ✅ 1×1 与 16×8 furnace equirect 均崩 —— 不是 size 边角 case
- ✅ 只接 baked irradiance（compute path 烘焙）单项也崩 —— 不是 prefilter graphics pass 子资源 view 特有
- ✅ 两套 transition API（`ResourceState::ShaderResource` / `TextureLayout::ShaderReadOnly`）最终都映到 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`，barrier 设置正确
- ✅ OrangeRender `UpdateDescriptorSet` 的 `imageLayout` 字段写 `SHADER_READ_ONLY_OPTIMAL`，与 image 实际 layout 一致
- ❌ Pipeline 端 explicit re-transition baked image 同 layout 无效

**OrangeRender 端首轮评审（commit `0f9ea4c`）**：T1 ctest 等价路径（compute-imageCube-write → ShaderResource → samplerCube 采样）**未复现** → bug 根因不在 OrangeRender 端 cube + Sampled|Storage 路径；交付 T3 LogSink 公共 API（`Orange::SetLogSink` / `LogCategory::Validation`），让消费方继续二分。

**OrangeEngine 端续二分（本 session 2026-05-18 续接）**：

LogSink 接通：在 `src/render/Pipeline.cpp::Initialize` 头部注册 `OrangeRenderLogAdapter`，把 `Orange::Log*` 的所有输出（含 Validation 类别）转入本仓 `ORANGE_LOG_*`。结果——**Vulkan validation layer 完全沉默**，确认非 layout/descriptor/usage 校验失败，是驱动层 segfault。

ISO 二分矩阵（在 `IblBaker::BakePrefilteredEnvironment` 的 `CreateTexture(cubeDesc)` 出口立即 return，跳过 6×9=54 subview/DescriptorPool/cmd 渲染全套）：

| 测试 | cube usage | mipLevels | first-frame CreateGraphicsPipeline 结果 |
|---|---|---|---|
| ISO-F | `Sampled\|RenderTarget\|TransferSrc` | 9 | **崩** |
| ISO-G | `Sampled\|RenderTarget\|TransferSrc` | 1 | **崩** |
| ISO-H | `Sampled\|TransferSrc` | 1 | ✅ 跑通 22861 帧 |
| ISO-I | `Sampled\|TransferSrc` | 9 | **崩** |

**精确根因**（c11 当下结论，已被 c12 推翻 ⚠️）：`CreateTexture(TexCube + (RenderTarget OR mipLevels>1))` 两条路径任一即触发 OrangeRender 端 device 状态破坏；**之后任何** `CreateGraphicsPipeline` 段错误。Crash 表面在 `Pipeline::Render` 内 PBR template pipeline 首次编译时 —— 但 root cause 在更早的 prefiltered cube 创建路径。

**登记位置**：`vendor/OrangeRender/docs/incoming_bugs.md` BUG-2026-05-18-cube-rt-or-multimip-poisons-device（接续 BUG-2026-05-18-baked-ibl-cube-sampling-segfault，是其下游精确化）

**c11 → c12 评审走偏复盘**：上述"精确根因"是**假设伪装成事实**——ISO-F/G/I 崩 vs ISO-H 跑通的二分结果是事实，"CreateTexture(cube + RT/mip>1) 污染 device" 是从二分结果推出的**假设**。报告方写报告时未明确"症状 vs 假设"分离，OR 端按假设拆 .T1/.T2/.T3 三方向（cube + RT/mip 路径单独 ctest 复现失败 → 假设伪）+ confirm A' 证伪 vkCmdDraw UB 假设，归档后 OE 端**真因仍未解**。c12 推翻该假设并锁定真实根因（见下）。

### c12 续二分（本 session 2026-05-18）—— 真因锁定

**新增工具**：
- `cmake/CompilerOptions.cmake` 加 `ORANGE_ENGINE_WITH_ASAN` option（MSVC `/fsanitize=address` + `_DISABLE_VECTOR_ANNOTATION=1` 让 ABI 与 OR 预编译 .lib 兼容）
- `build-asan/` 独立 build dir 跑 ASan 14_pbr_ibl
- `VK_LAYER_LUNARG_api_dump` layer dump 完整 Vulkan API call 序列（baseline / ASan 两条路径各一份）
- `Pipeline.cpp::CreateOrGetTemplatePipeline` 临时加 dump（A/B/C/D 锚点 + desc 字段）二分崩点位置后已撤回

**症状（事实层）**：
1. baseline 14_pbr_ibl --furnace 在第一帧 PBR template pipeline 创建时 segfault (0xC0000005 = ACCESS_VIOLATION)
2. **ASan build 跑 14_pbr_ibl --furnace 完整启动 → baker 三件套烘焙完成 → frame loop 稳定跑 11s CPU 700MB RSS 无任何 warning** → 排除典型 UAF / heap-OOB / double-free / leak（这些 ASan 必抓）
3. baseline 与 ASan build 在 Pipeline.cpp:709 调用 CreateGraphicsPipeline 前的 desc 字段**完全一致**：shaderStages=2, vertexBindings=1, vertexAttrs=3, descSetLayouts=1, colorFormats=1, depthFormat=14 (D32Float), pushRanges=1 (Vertex/0/160B); 各 raw pointer non-null
4. baseline api_dump 末尾最后一条 Vulkan call: `vkCreateGraphicsPipelines` → **VK_SUCCESS** —— GPU pipeline 编译**成功**
5. 加细粒度 dump 后 baseline 输出顺序：`A: before CreateGraphicsPipeline` 打出来，`B: after CreateGraphicsPipeline` **没打** → 崩点在 OR `VulkanDevice::CreateGraphicsPipeline` 函数体内（vkCreateGraphicsPipelines VK_SUCCESS 后的 post-create 段）

**证据链（fact-derived）**：
1. OR `VulkanDevice::CreateGraphicsPipeline` (vendor/OrangeRender/src/backend/vulkan/VulkanDevice.cpp:609-638)：line 611 `mPipelineCache.find(desc)` + line 636 `mPipelineCache.emplace(desc, holder)` —— **cache 持有 desc 整体副本**
2. `mPipelineCache` 类型：`unordered_map<GraphicsPipelineDesc, shared_ptr<VulkanPipelineHandle>, GraphicsPipelineDescHash, GraphicsPipelineDescEq>`
3. OR `VulkanDedup.cpp::ResolveHandle` (line 79-83) + `HashSetLayouts` (85-93) + `EqSetLayouts` (95-103)：hash 和 equal 函数读 `desc.mDescriptorSetLayouts[i]` 时调 `ResolveHandle(p)` → `static_cast<VulkanDescriptorSetLayout*>(p)->GetHandle()` —— **deref raw RHIDescriptorSetLayout 指针**
4. OE `IblBaker::BakePrefilteredEnvironment` (src/render/IblBaker.cpp:812-1021)：唯一**创建 graphics pipeline** 的 baker 路径（其余 3 条 compute）。`desc.mDescriptorSetLayouts` 内含 `baker.Impl::prefilterLayout` 的 raw pointer → 进 mPipelineCache emplace 后留下 desc 副本
5. `Pipeline::BakeIblFromWorld` 函数返回时 baker 局部变量析构 → `baker.Impl::prefilterLayout` (unique_ptr<RHIDescriptorSetLayout>) 释放 → **mPipelineCache 内 desc 副本中的 raw pointer 变 dangling**
6. 第一帧 PBR `CreateGraphicsPipeline` → `mPipelineCache.find/emplace` 触发遍历 / rehash → `HashSetLayouts(baker entry)` / `EqSetLayouts(baker entry, PBR desc)` → `ResolveHandle(dangling)` → **deref freed memory → segfault**

**根因（假设，未补充验证；交 OR 端 fix 时附实验提案）**：
**OR `mPipelineCache` cache lifecycle bug**——cache 持有 desc 副本含 raw `RHIDescriptorSetLayout*` / `RHIShaderModule*`，cache 寿命与 caller-owned 对象生命周期解耦，调用方不必维持 raw pointer 等寿至 cache lifetime；hash/equal deref 这些 raw pointer 时若调用方对象已析构 → dangling deref → segfault。

**ASan 不抓的原因**：OR 是预编译 .lib（D:/3rdparty/install），编译时**不带 ASan instrumentation**。OE 端 free 的 memory 被 ASan 标 poisoned，但 OR 端 read 不通过 ASan check（instrumentation 是编译时插桩）。ASan allocator 的 quarantine 还推迟 reuse → OR 端 read 拿到旧的"valid-looking" handle → 不崩。这套 deduction 完美解释 ASan / baseline 行为差异，并间接确认 dangling deref 假设。

**登记位置（新）**：`vendor/OrangeRender/docs/incoming_bugs.md` BUG-2026-05-18-pipeline-cache-dangling-desc-raw-pointer（本 session 追加；接续 BUG-2026-05-18-cube-rt-or-multimip-poisons-device 的下游精确化 / 推翻其 ISO 假设）

**修复回归路径**：OR 端 fix cache lifecycle（候选方向：(a) cache key 用 `VkDescriptorSetLayout` / `VkShaderModule` 原生 handle 而非 raw RHI* 指针；(b) cache value 持 shared_ptr<RHIDescriptorSetLayout> 把生命周期接管过来；(c) cache 用纯 hash 不 deep compare —— OR 端自行选）+ tag + bump vendor 后，撤本"已知 bug"段，跑本文件顶部"核心功能 1 / 2"验收。

**OrangeEngine 端保留**：
- LogSink adapter 桥接（`OrangeRenderLogAdapter`，Pipeline.cpp:299-327 + Initialize/Shutdown 注册点）—— 未来撞 OR 端 validation 时复用
- `ORANGE_ENGINE_WITH_ASAN` cmake option —— 未来类 Heisenbug 调查复用
- BakeIblFromWorld / IblBaker 代码恢复到 c8 落地状态（不加 workaround，按双向纪律等 OR 端 fix）

## 已知简化范围（不验收）

- **运行时 IBL 切换**：当前 `Pipeline::BakeIblFromWorld` 是启动期一次性触发；运行时切换 cubemap handle 后需重启 sample。完整运行时 IBL 切换路径推到 v0.8 编辑器伴随 milestone
- **Reflection probes / Light probes / SH-GI**：全部 out-of-scope；本期是**单全局 EnvironmentComponent**，多 environment 体积 blending 留独立 milestone
- **Environment 浏览器 / sky placeholder 渲染 / material thumbnail**：编辑器扩展推到 v0.8 编辑器 milestone（companion 文件 §RISK-6 决议）
- **离线 cook .hdr → ORTX**：当前 stb_image 启动期直接读 `.hdr`；离线烘焙缓存推到 Phase 9 资产管线

## B.2 retro · 关键事件

1. **commit 序列**：c1 IblBaker 骨架 + equirect→cube → c2 BRDF LUT → c3 irradiance → c4 prefiltered specular → c5 SetIblTextures 接通点 → c6 EnvironmentComponent + serializer → c7 HDR loader + Inspector schema → c8 sample 14_pbr_ibl + `Pipeline::BakeIblFromWorld` → c9 收尾。9 commit 全程紧贴开工 commit-plan，无偏差
2. **关键架构杠杆**：`Pipeline::BakeIblFromWorld` 高阶 API 让 sample / 编辑器只挂 `EnvironmentComponent` + 调一次即完成 IBL 全套接通，**不接触 RHITexture**（保 header isolation 红线）；RHI 操作全部藏在 `src/render/` 内部。这是 c5 SetIblTextures 接通点伏笔的最终兑现
3. **工作流违规复盘**：c7 两次抓住违规——(a) `STBI_NO_LINEAR` 宏语义颠倒（其实是关掉 `stbi_loadf` API 而非关掉 LDR→linear 转换），花一轮 build error 才发现；(b) 注释带 `Phase 6.5 c7` 触发 no-task-references lint，B.1 期已抓过一次同款 → B.2 c7 又中招，下次开工 ritual 第 7 步 commit-plan 草稿时显式 grep 自检
