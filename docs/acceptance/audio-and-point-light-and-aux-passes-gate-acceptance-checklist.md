# 音频集成 + GAP-1 PointLight + GAP-2 编辑器审美 cmake gate 验收清单

- 基准日期：2026-05-20
- 适用范围：本 session 三件并行交付
  - **Audio**：AudioSourceComponent + 编辑器 Schema / Asset 浏览器 / Inspector 试播 / Play Mode tick / lazy bake beep.wav
  - **GAP-1**：PointLight component + Pipeline 多 light UBO（cap=8）+ PBR shader point light loop + 编辑器 Schema / Gizmo
  - **GAP-2**（最小可行）：`ORANGE_ENGINE_WITH_EDITOR_AUX_PASSES` cmake option 包住 grid pass / dummy IBL ambient / clear color 三项编辑器审美默认；shipping 显式 `-DORANGE_ENGINE_WITH_EDITOR_AUX_PASSES=OFF` 干净
- 设计意图：用户在编辑器内能完成"挂音频→Play 听到声音 / 加 PointLight 照亮 PBR 物体"两个端到端流程

## 前置环境

1. `git pull && cmake --build build --config Debug --target OrangeEditor`
2. 启动 `build/bin/Debug/OrangeEditor.exe`；首次启动自动 lazy bake `assets/sounds/beep.wav`（开发者 git add 入仓后续无需再 bake）

## 核心功能

### 1. Audio 编辑器集成

- [ ] Assets tab 浏览到 `assets/sounds/beep.wav`，文件名前缀显示 `[SND]` icon
- [ ] 选中场景任一实体 → Inspector → `+ Add Component` 菜单包含 `Audio Source`；点添加后 Inspector 出现 Audio Source 段（Sound / Play On Awake / Loop / Volume / Pitch 五字段）
- [ ] Audio Source 段末尾 `Test Playback` 区域显示 `(no sound assigned)`；从 Assets tab 拖 `beep.wav` 到 Sound 字段后该提示消失，Play / Stop 按钮启用
- [ ] 点 `Play` 按钮听到 880Hz 短促"叮"声；点 `Stop` 立即静音
- [ ] Assets tab 选中 `beep.wav`（左键单击）→ Inspector 切到 Sound Asset 子模式，显示路径 + 字节大小 + Preview Play / Stop 按钮；点 Play 听到声音
- [ ] 右键 `beep.wav` → `Pick to AudioSource.sound` 把资源挂到当前选中实体；Inspector Audio Source 段 Sound 字段更新
- [ ] 实体勾选 `Play On Awake` → 点 toolbar Play → 进入 Play Mode 瞬间听到声音；点 Stop 退出 Play Mode 实例化的 SoundInstance 全数释放
- [ ] Save Scene → 重新 Open Scene → Audio Source 五字段值与挂载的 sound 资源 round-trip 一致

### 2. PointLight（GAP-2026-05-11）

- [ ] `+ Add Component` 菜单包含 `Point Light`；添加后 Inspector 显示 Color / Intensity / Range (m) / Casts Shadow 四字段
- [ ] viewport 内挂 PointLight 的实体位置出现**黄色填充小圆**（中心 icon）+ **XZ 平面 range 圆环**（半透黄色细线，圆心 = entity，半径 = Range 字段值）；拖 Range slider 圆环大小实时跟随
- [ ] 场景内放一颗挂 `pbr.material` 的 sphere + 一盏 PointLight（intensity=20, range=3, color=橙）放在 sphere 旁 1m，关掉 DirectionalLight（Inspector intensity=0）后 sphere 仍被橙色照亮；移动 PointLight 远到 range 外，sphere 回到只受 IBL ambient 的灰白塑料态
- [ ] 同场景挂 8+ 个 PointLight（超 cap 上限）→ 控制台日志 `Pipeline: scene has more than 8 PointLights; extras ignored.`（一次性 warn）
- [ ] Save Scene → 重新 Open Scene → PointLight 四字段值 round-trip 一致；scene/world schema 写出 v1.5

### 3. GAP-2 编辑器审美 cmake gate

- [ ] Debug / Editor 默认构建（不显式 `-DORANGE_ENGINE_WITH_EDITOR_AUX_PASSES`）下，编辑器视觉与 v0.8.5 ✅ 完全一致：viewport 中性灰背景 + grid 显示 + PBR 物体在无 EnvironmentComponent 时显示灰白塑料
- [ ] `cmake -S . -B build_shipping -DCMAKE_PREFIX_PATH=... -DORANGE_ENGINE_WITH_EDITOR_AUX_PASSES=OFF` 配出 shipping 构建 → 任一 sample exe（如 `01_minimal_window`）clear color 退回深蓝 (0.05, 0.07, 0.10)；PBR 物体（如 sample 13）在无 Environment 时退回 (0,0,0) ambient（仅 direct light）；编辑器即使调 `SetEditorGridEnabled(true)` grid 也不显示
- [ ] `ctest --test-dir build -C Debug` 40/40 通过（无 pipeline 测试回归）

## 已知不验收（本期范围外，已登记后续 follow-up）

- IAuxPassProvider 钩子真做出 + engine 公共头 grep 不到 "EditorGrid" 字样的命名整骨 —— 留 v1.0 验收前 batch milestone（见 `docs/engine-known-gaps.md` GAP-2026-05-19-editor-aux-passes-in-engine-pipeline 落地记录段）
- PointLight halo billboard（GAP G3 内置 emissive sphere mesh + 默认 halo material） —— v1.x Ori-like 视觉子模式拉动时再实施
- PointLight castsShadow 字段实现（omnidirectional cubemap shadow）—— 长期 roadmap 量级，本期 Pipeline 忽略
- Audio 试播按钮支持 loop / pitch 字段实时生效 —— SoundInstance 公共面尚未暴露这两条 setter，留 Audio 模块下一版本扩展
- Audio 3D positional / panning —— AudioEngine 当前是 2D mixer，留 v1.x 按需
