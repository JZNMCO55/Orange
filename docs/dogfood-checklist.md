# OrangeEditor / OrangeEngine Dogfood Checklist

本文件登记**已落地（编译 + ctest 绿）但仅 headless 验证、视觉 / 交互手感待人工 dogfood** 的能力点。每条给出：commit / 怎么触发 / 看什么 / 通过判据。dogfood 通过后在条目前打 ✅。

> 约定：headless 测试只能验"不崩 + 数值正确 + 产物存在"，**看得见 / 摸得着的正确性（viewport 视觉、GUI 手感、拖拽体验）只能靠真机 dogfood**。这类残留统一收在本文件，避免散落在各 GAP 落地记录里被遗忘。

---

## 2026-05-31 session（headless import + glTF 内嵌贴图 + 编辑器内动画播放）

### 1. 编辑器内动画播放 —— Slime Doll 绿色呼吸脉动

- **commit**：`dd8f64b` feat(editor): 编辑器内动画播放可见 demo（后续 animator tick 路径重构为引擎层 `Animation::TickAnimators`，行为等价，本条 dogfood 顺带验证重构未破坏 Play 模式 tick）
- **怎么触发**：
  1. 启动 OrangeEditor（`build/bin/Debug/OrangeEditor.exe`）
  2. 默认加载 demo 场景后，点 **Play**（进 PlayState::Play）
  3. 看 viewport 里名为 **"Slime Doll"** 的球（位置约 `(0.8, 1.6, 0.5)`，贴 Glow Box 上方）
- **看什么 / 通过判据**：
  - 球做**绿色呼吸脉动**（绿色分量在亮暗间周期变化，约 2.2 rad/s，肉眼可见明显起伏）
  - 点 **Stop / 回 Edit** 后脉动停止（仅 Play 模式 tick）
  - 选中 Slime Doll → Inspector 的 Animator 段显示 backend = `procedural` + channel 列表含 `uBaseColor`
  - Save 场景 → 重启 → Load → 再 Play，呼吸行为一致（factory 重建）
- **背景**：之前 slimeDoll 是 Animator-only 空壳（无 target / 占位 channel / 无 Renderable）三重看不见；本次接上 sphere + pbr 专属材质 + uBaseColor 呼吸 channel。
- **关键确认点**：这同时验证 **ProceduralAnimator 对 pbr 材质实时可见**这条机制（ProceduralAnimator.h 旧注释"UBO 未接通看不到效果"已过时）——是首游"流体史莱姆 = 代码驱动动画"路线的地基，务必确认真能看到。

### 2. glTF `.glb` 内嵌贴图 / data: URI 导入

- **commit**：`bf10ea5` feat(editor): glTF .glb 内嵌贴图 / data: URI 导入
- **怎么触发**：
  1. 在 Blender 里建一个带 baseColor 贴图的简单模型，**导出 `.glb` 时勾选"内嵌贴图"**（Blender 默认行为：贴图打包进 .glb 的 buffer）
  2. 编辑器 File → Import（或拖拽）该 `.glb`
  3. 去 `assets/Models/<stem>/` 看产物
- **看什么 / 通过判据**：
  - `assets/Models/<stem>/` 下出现**提取出来的贴图文件**（`<imageName>.png` / `.jpg`，非 default 白贴图）
  - 同目录 `.material` 的 texture 槽 path 指向该提取贴图
  - 把该 `.material` 指给场景里一个 entity 的 Renderable → viewport 看到**真实 baseColor 贴图**（而非 default 白/棋盘）
- **背景**：此前 importer 只认外部 uri 贴图，Blender 默认导出的 .glb 内嵌贴图会全丢，落 default 白。headless 已验证 PNG 字节提取 + co-locate + .material 槽回填；**真实 Blender .glb + viewport 视觉待确认**。
- **推荐 fixture**：DamagedHelmet（有完整 PBR 贴图集）的 `.glb` 版本，或自己在 Blender 随手做一个带贴图的立方体导 .glb。

### 3. headless mesh import CLI

- **commit**：`08646d8` feat(editor): headless mesh import seam + CLI（GAP-2026-05-27 G1）
- **怎么触发**（命令行，不开 GUI）：
  ```
  build/bin/Debug/OrangeEditor.exe import-mesh path/to/model.obj
  build/bin/Debug/OrangeEditor.exe import-mesh path/to/model.glb
  ```
- **看什么 / 通过判据**：
  - 进程**不弹 GUI 窗口**、直接在控制台打印 import 结果后退出
  - 退出码：成功 `0` / 缺路径 `2` / registry 失败 `3` / 导入失败 `1`
  - `assets/Models/<stem>/` 下生成 `.mesh` + 源文件 copy + `.meta` sidecar
  - 与 GUI 导入同一文件的产物字节级一致（headless 已验证确定性，可手工比对）
- **背景**：headless 路径已有完整 ctest（程序化立方体 + Avocado fixture）；**真机批量调用手感 + 大模型表现待 dogfood**。
- **批量用法提示**：可写 shell/python 循环对一个目录的 `.glb` 批量 `import-mesh`，验证代码驱动内容管线（首游程序化批量生成场景道具的诉求）。

---

## 维护约定

- 新 feature 落地后，若有"headless 绿但视觉/手感待验"的残留，追加到本文件对应 session 段。
- dogfood 通过的条目：在标题前加 ✅，并可在条目内记一句实测结果（截图路径 / 发现的问题）。
- dogfood 发现 bug：在 `docs/engine-known-gaps.md` 登记新 GAP / BUG，本条目内 link 过去。
