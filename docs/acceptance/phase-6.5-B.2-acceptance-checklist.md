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

## OR pipeline cache fix 落地 + 视觉验收（2026-05-19）

c10/c11/c12 三轮调查的 "已知 bug · sample 14_pbr_ibl baked IBL 接通路径 segfault" 段已删除（OR 端 commit `5715a9d` fix 落地 + commit `4eec1bf` 归档；详细调查方法学留 `vendor/OrangeRender/docs/incoming_bugs.md` "处理记录" 段 + memory `[[project-furnace-segfault-status]]`）。本节记录 OR fix 消费后的视觉验收实测结果。

### 步骤复盘

1. vendor/OrangeRender submodule bump 到 commit `4eec1bf`（含 cache lifecycle fix）
2. 撞 build incremental 陷阱：OR 端 `.lib` mtime > OE 端 `.exe` mtime 时 MSBuild 跳过 re-link（incremental check 错位），需删 .exe 强制 full link 才真的链接新符号；记入 [[reference-msvc-incremental-link-mtime-trap]]（待沉淀）
3. OR 端 `cmake --build vendor/OrangeRender/build --config Debug` + `cmake --install` 装到 `D:/3rdparty/install/`；OE 端删 `build/bin/Debug/*.exe` + `cmake --build` 强制 re-link
4. furnace smoke test 3 连跑 exit 124（GNU timeout SIGTERM 正常 kill）+ 9 行完整 init log → cache lifecycle fix 真生效，"baker 析构 → 第一帧 PBR pipeline segfault" 路径消失

### 视觉验收 6 项实测

| # | 验收项 | 结果 | 备注 |
|---|--------|------|------|
| 核心 1 | HDR 默认模式 | **部分 ✅** | 3×3 暖橙球阵 ✅ + 顶行左 metallic=1 r=0.1 清晰反映 HDR 环境 ✅ + 漫反射阴影填充 ✅；中 roughness 段密集白方块**采样伪影** ✗；顶行右 metallic=1 r=0.9 **偏暗** ✗ |
| 核心 2 | furnace 能量守恒 | **✗ fail** | 9 球阵远非"近似全白"——顶行右几乎纯黑、中行中灰 = 严重能量损失；底行漫反射球接近白 ✅（irradiance 路径正常） |
| 核心 3 | 编辑器 Environment | **部分 ✅** | schema 注册 + 3 字段显示 ✅；Cubemap 字段拖拽 / picker 无反应 ✗；Intensity 拖动 viewport 无响应 ✗ |
| 大节点 1 | 13_pbr_direct 回归 | ✅ | 9 球阵漂亮，dummy IBL fallback 退化正确 |
| 大节点 2 | 资产缺失 graceful | ✅ | 不崩 + 视觉等价 13_pbr_direct |
| 大节点 3 | lint baseline | ✅ | `check_invariants.py` 7 grandfathered / drift none detected |
| 额外 | 退出 crash | **已知 OR bug** | `BUG-2026-05-18-vma-shutdown-allocation-leak-assertion`（incoming_bugs.md 未处理段顶部）—— 登记时优先级 low + 预言"等 cache fix 落地后会成为首选可见症状"，应验 |

### 关键 diagnostic 信号

13_pbr_direct（不接 IBL，dummy 1×1 黑 cubemap fallback）9 球阵 r=0.9 金属球橙色 dim 但**不纯黑**（GGX wide highlight + albedo），14_pbr_ibl furnace 同位置几乎纯黑 → **direct GGX BRDF 数学正确，所有异常都集中在 IBL specular split-sum 路径**。后续修 bug 应聚焦 BRDF LUT split-sum 第二项 / prefiltered specular 烘焙 / multi-scatter compensation，**不是** GGX 本身。

### 后续 follow-up（不在本期范围）

| 缺口 | 登记位置 |
|------|---------|
| PBR + IBL specular 质量缺陷（能量损失 + 采样伪影） | `docs/engine-known-gaps.md` GAP-2026-05-19-pbr-ibl-specular-quality |
| 编辑器 Environment wiring 缺口（AssetRef drop / picker + Pipeline runtime query） | `docs/engine-known-gaps.md` GAP-2026-05-19-editor-environment-component-wiring |
| sample 退出 VMA leak assert / segfault | 本 session 实测真实路径撞（不只是 ctest harness）→ 按 OR commit `0210909` 分支判断升**中**优先级；**OE 端**先二分 `Pipeline::Impl` 析构顺序（怀疑方向 2，最可能在 OE 端 cube IBL 三件套 / dummy IBL 三件套 / mainDescSet 在 VkDevice 销毁前未 reset）；OE 确认在 OE 后自家 fix，确认在 OR 才回 OR `.T1/.T2`。条目：`vendor/OrangeRender/docs/incoming_bugs.md` BUG-2026-05-18-vma-shutdown-allocation-leak-assertion |

### Task 06.5-07 ✅ 路径

按 `docs/milestone-end-checklist.md` 红线（渲染正确性问题 / 验收口径未达不能 ✅），**Task 06.5-07 仍不标 ✅**。前置条件：

1. GAP-2026-05-19-pbr-ibl-specular-quality 落地（OE 端独立 session）→ 重跑核心功能 2 视觉验收（furnace 9 球近似全白）+ 核心功能 1 (HDR 中 r 无伪影 + 高 r 不偏暗)
2. GAP-2026-05-19-editor-environment-component-wiring 落地（OE 端独立 session）→ 重跑核心功能 3 视觉验收（Cubemap 绑定 + Intensity / Tint 拖动 viewport 跟随）
3. （可选）OR `BUG-2026-05-18-vma-shutdown-allocation-leak-assertion` fix —— 不阻塞 Task 06.5-07 ✅（不影响渲染正确性，但影响 graceful exit；优先级 OR 端 low）

## 已知简化范围（不验收）

- **运行时 IBL 切换**：当前 `Pipeline::BakeIblFromWorld` 是启动期一次性触发；运行时切换 cubemap handle 后需重启 sample。完整运行时 IBL 切换路径推到 v0.8 编辑器伴随 milestone
- **Reflection probes / Light probes / SH-GI**：全部 out-of-scope；本期是**单全局 EnvironmentComponent**，多 environment 体积 blending 留独立 milestone
- **Environment 浏览器 / sky placeholder 渲染 / material thumbnail**：编辑器扩展推到 v0.8 编辑器 milestone（companion 文件 §RISK-6 决议）
- **离线 cook .hdr → ORTX**：当前 stb_image 启动期直接读 `.hdr`；离线烘焙缓存推到 Phase 9 资产管线

## B.2 retro · 关键事件

1. **commit 序列**：c1 IblBaker 骨架 + equirect→cube → c2 BRDF LUT → c3 irradiance → c4 prefiltered specular → c5 SetIblTextures 接通点 → c6 EnvironmentComponent + serializer → c7 HDR loader + Inspector schema → c8 sample 14_pbr_ibl + `Pipeline::BakeIblFromWorld` → c9 收尾。9 commit 全程紧贴开工 commit-plan，无偏差
2. **关键架构杠杆**：`Pipeline::BakeIblFromWorld` 高阶 API 让 sample / 编辑器只挂 `EnvironmentComponent` + 调一次即完成 IBL 全套接通，**不接触 RHITexture**（保 header isolation 红线）；RHI 操作全部藏在 `src/render/` 内部。这是 c5 SetIblTextures 接通点伏笔的最终兑现
3. **工作流违规复盘**：c7 两次抓住违规——(a) `STBI_NO_LINEAR` 宏语义颠倒（其实是关掉 `stbi_loadf` API 而非关掉 LDR→linear 转换），花一轮 build error 才发现；(b) 注释带 `Phase 6.5 c7` 触发 no-task-references lint，B.1 期已抓过一次同款 → B.2 c7 又中招，下次开工 ritual 第 7 步 commit-plan 草稿时显式 grep 自检
