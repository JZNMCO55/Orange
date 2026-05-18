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

**精确根因**：`CreateTexture(TexCube + (RenderTarget OR mipLevels>1))` 两条路径任一即触发 OrangeRender 端 device 状态破坏；**之后任何** `CreateGraphicsPipeline` 段错误。Crash 表面在 `Pipeline::Render` 内 PBR template pipeline 首次编译时 —— 但 root cause 在更早的 prefiltered cube 创建路径。

**登记位置**：`vendor/OrangeRender/docs/incoming_bugs.md` BUG-2026-05-18-cube-rt-or-multimip-poisons-device（接续 BUG-2026-05-18-baked-ibl-cube-sampling-segfault，是其下游精确化）

**修复回归路径**：OrangeRender 修完 cube-RT / cube-multi-mip 路径 + tag + bump vendor 后，撤本节，跑本文件顶部"核心功能 1 / 2"验收。一旦 furnace 模式 + 真实 HDR 模式两条路径都视觉过，Task 06.5-07 才补 ✅。

**OrangeEngine 端**：LogSink adapter 桥接持久保留（不撤），它是未来撞 OrangeRender 端 validation / 错误时的解锁工具。所有 ISO trace 已撤；BakeIblFromWorld 内部代码恢复到 c8 落地状态。

## 已知简化范围（不验收）

- **运行时 IBL 切换**：当前 `Pipeline::BakeIblFromWorld` 是启动期一次性触发；运行时切换 cubemap handle 后需重启 sample。完整运行时 IBL 切换路径推到 v0.8 编辑器伴随 milestone
- **Reflection probes / Light probes / SH-GI**：全部 out-of-scope；本期是**单全局 EnvironmentComponent**，多 environment 体积 blending 留独立 milestone
- **Environment 浏览器 / sky placeholder 渲染 / material thumbnail**：编辑器扩展推到 v0.8 编辑器 milestone（companion 文件 §RISK-6 决议）
- **离线 cook .hdr → ORTX**：当前 stb_image 启动期直接读 `.hdr`；离线烘焙缓存推到 Phase 9 资产管线

## B.2 retro · 关键事件

1. **commit 序列**：c1 IblBaker 骨架 + equirect→cube → c2 BRDF LUT → c3 irradiance → c4 prefiltered specular → c5 SetIblTextures 接通点 → c6 EnvironmentComponent + serializer → c7 HDR loader + Inspector schema → c8 sample 14_pbr_ibl + `Pipeline::BakeIblFromWorld` → c9 收尾。9 commit 全程紧贴开工 commit-plan，无偏差
2. **关键架构杠杆**：`Pipeline::BakeIblFromWorld` 高阶 API 让 sample / 编辑器只挂 `EnvironmentComponent` + 调一次即完成 IBL 全套接通，**不接触 RHITexture**（保 header isolation 红线）；RHI 操作全部藏在 `src/render/` 内部。这是 c5 SetIblTextures 接通点伏笔的最终兑现
3. **工作流违规复盘**：c7 两次抓住违规——(a) `STBI_NO_LINEAR` 宏语义颠倒（其实是关掉 `stbi_loadf` API 而非关掉 LDR→linear 转换），花一轮 build error 才发现；(b) 注释带 `Phase 6.5 c7` 触发 no-task-references lint，B.1 期已抓过一次同款 → B.2 c7 又中招，下次开工 ritual 第 7 步 commit-plan 草稿时显式 grep 自检
