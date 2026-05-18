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

## 已知简化范围（不验收）

- **运行时 IBL 切换**：当前 `Pipeline::BakeIblFromWorld` 是启动期一次性触发；运行时切换 cubemap handle 后需重启 sample。完整运行时 IBL 切换路径推到 v0.8 编辑器伴随 milestone
- **Reflection probes / Light probes / SH-GI**：全部 out-of-scope；本期是**单全局 EnvironmentComponent**，多 environment 体积 blending 留独立 milestone
- **Environment 浏览器 / sky placeholder 渲染 / material thumbnail**：编辑器扩展推到 v0.8 编辑器 milestone（companion 文件 §RISK-6 决议）
- **离线 cook .hdr → ORTX**：当前 stb_image 启动期直接读 `.hdr`；离线烘焙缓存推到 Phase 9 资产管线

## B.2 retro · 关键事件

1. **commit 序列**：c1 IblBaker 骨架 + equirect→cube → c2 BRDF LUT → c3 irradiance → c4 prefiltered specular → c5 SetIblTextures 接通点 → c6 EnvironmentComponent + serializer → c7 HDR loader + Inspector schema → c8 sample 14_pbr_ibl + `Pipeline::BakeIblFromWorld` → c9 收尾。9 commit 全程紧贴开工 commit-plan，无偏差
2. **关键架构杠杆**：`Pipeline::BakeIblFromWorld` 高阶 API 让 sample / 编辑器只挂 `EnvironmentComponent` + 调一次即完成 IBL 全套接通，**不接触 RHITexture**（保 header isolation 红线）；RHI 操作全部藏在 `src/render/` 内部。这是 c5 SetIblTextures 接通点伏笔的最终兑现
3. **工作流违规复盘**：c7 两次抓住违规——(a) `STBI_NO_LINEAR` 宏语义颠倒（其实是关掉 `stbi_loadf` API 而非关掉 LDR→linear 转换），花一轮 build error 才发现；(b) 注释带 `Phase 6.5 c7` 触发 no-task-references lint，B.1 期已抓过一次同款 → B.2 c7 又中招，下次开工 ritual 第 7 步 commit-plan 草稿时显式 grep 自检
