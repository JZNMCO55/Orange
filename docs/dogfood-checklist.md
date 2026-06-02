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
    - ⚠️ 2026-06-01 根因已修（`BUG-2026-06-01-slime-doll-invisible-and-mute-on-scene-load`）：之前"Slime Doll 是空的/看不见" = 启动加载的 `demo.scene.json`（commit 早于 `dd8f64b` 3 天）里该实体**无 Renderable** + 动画材质 `pAnimatedMaterial` 不可被 `materialInstanceId` 反查（独占实例不在 named 表）。已补 Renderable（sphere + 虚拟 id `editor/animated_slime.material`）+ 把动画材质纳入 `BuildNamedMaterialInstances`。**重新 dogfood：球应可见（贴 Glow Box 上方）且 Play 时绿色呼吸脉动；Stop 后停止。**
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

## 2026-06-01 session（多 material per mesh 导入 + drop 消费）

### ✅ 4. 多 material per mesh —— drop 多材质 .mesh 各段显示不同材质

> **2026-06-02 dogfood 通过**（用户实测，multimat_emissive_cube 各段显示不同材质）。同轮修了 Inspector 设 mesh 字段不挂组件的路径分叉 bug（`e64e5c5`，详见下方 item 5）。

- **commit**：`a795c10` feat(editor): 多 material per mesh 导入侧 + drop 消费 + headless 测试（承接引擎核心 `774956d`）
- **怎么触发**：
  1. 在 Blender 给**一个 mesh 分配 2+ 个 material slot**（如立方体不同面用不同材质），导出 `.glb` / `.gltf`
  2. 编辑器 import 该模型 → 看 `assets/Models/<stem>/`：应出现 `.mesh` + **多个 `.material`**（slot 0 = `<stem>.material`、slot≥1 = `<stem>_<matname>.material`）+ `.meta`（含 `subMeshMaterials` 段）
  3. 把该 `.mesh` 拖到场景一个 entity（或新建 Renderable 指向它）
- **看什么 / 通过判据**：
  - mesh 的不同 sub-mesh 段在 viewport 里显示**各自不同的材质**（slot 0 / 1 / … 各自 baseColor），而非整体单一材质
  - 选中 entity → 应挂上 `SubMeshMaterialsComponent`，slots 数 = material 数，逐项指向对应 `.material`
  - `Renderable.materialInstance` = slot 0（兜底语义，与单 material 一致）
  - **单 material 模型 drop 行为不变**：不挂 `SubMeshMaterialsComponent`，整 mesh 单材质（向后兼容回归）
- **背景**：headless `headless_mesh_import_test` 74/74 已验 importer 解析（SubMesh / materialSlot 连续 / indexOffset 紧接 / materialPaths 落盘）+ `.meta` `subMeshMaterials` 读写对称 + 消费侧逻辑；本 session 顺带修了一个 **use-after-free SEGFAULT**（`sanitizedMaterialName` 解引用 `cgltf_free` 后悬空的 `orderedMats[slot]->name`，slot≥1 必崩）。**drop 到 viewport 的多段材质渲染视觉 + GUI 手感待真机确认**。
- **推荐 fixture**：Blender 立方体不同面分 2 个材质导 `.glb`；或现成带多 primitive/material 的 glTF 资产。
- **2026-06-02 更新**：现成 fixture 已就绪，见下方第 5/6/7 项的 `multimat_emissive_cube`（一举覆盖多材质 + emissive），导入侧已在真实 Blender 导出上验证（2 个 `.material` + `.meta` 含 subMeshMaterials 两条路径）。

---

## 2026-06-02 session（多贴图后续：SubMeshMaterials Inspector + emissive 通道 + tangent fallback）

> **就绪 dogfood fixture**：`multimat_emissive_cube`（Blender headless 生成的多材质 + 自发光立方体）。
> - 生成脚本（已提交，可复现）：`scripts/dogfood_make_multimat_emissive_cube.py`。重新生成：
>   ```
>   D:/Software/Blender/blender.exe --background --python scripts/dogfood_make_multimat_emissive_cube.py
>   # 输出 multimat_emissive_cube.glb 到脚本同目录（scripts/）；OUT_GLB 环境变量可指定输出路径
>   ```
> - 已 headless `import-mesh` 导入到 `assets/Models/multimat_emissive_cube/`（gitignore 但**存活 clean build** + 源 `.glb` 经 ADR-008 co-locate 一并落此目录，**编辑器资产浏览器直接可见**），产物：`.mesh` + slot 0 `multimat_emissive_cube.material`（OrangeMat 橙色不发光）+ slot 1 `multimat_emissive_cube_GlowMat.material`（GlowMat 蓝光自发光）+ `.meta`（subMeshMaterials 两条）。重新导入：`build/bin/Debug/OrangeEditor.exe import-mesh <glb>`（从仓库根 cwd 跑）
> - **导入侧已自动验证**（真实 Blender 导出，非合成 fixture）：slot 1 的 `uEmissive = (0.3, 1.8, 3.0, 0)` = emissiveFactor (0.1,0.6,1.0) × emissiveStrength 3.0；slot 0 无 uEmissive（不发光零回归）；`.meta` subMeshMaterials 两条路径对齐。**剩 viewport 视觉 + Inspector 交互待人工 dogfood**（无 headless viewport 渲染路径）。

### ✅ 5. SubMeshMaterials Inspector —— drop 多材质后 Inspector 可见/逐 slot 改材质

> **2026-06-02 dogfood 通过**（用户实测确认）。第一轮逮到 bug：**Inspector 设 Mesh 字段**（vs viewport/tree drop）不挂 SubMeshMaterialsComponent → 无 Sub-Mesh Materials 段。根因=两路径分叉（schema meshSet 只设 handle、拿不到 World/Entity 挂兄弟组件）。修复 `e64e5c5`：抽 `SyncSubMeshMaterialsForMesh` 让 drop 与 Inspector 设 mesh 复用同一份。修后 Inspector 正常弹 Sub-Mesh Materials 段 + slot 0/1 材质。

- **commit**：`ff13b41` feat(editor): SubMeshMaterialsComponent Inspector（AssetRef 数组字段类型）
- **怎么触发**：
  1. 启动 OrangeEditor → 资产浏览器进 `assets/Models/multimat_emissive_cube/`
  2. 把 `multimat_emissive_cube.mesh` 拖到场景一个 entity（或新建 Renderable 指向它）
  3. 选中该 entity → 看 Inspector
- **看什么 / 通过判据**：
  - Inspector 出现 **"Sub-Mesh Materials"** 段（紧接 Renderable 段下方），段顶 helper 说明 slot 语义
  - 段内逐 slot 一行 `Slot 0` / `Slot 1`，各显示当前材质短名（`multimat_emissive_cube.material` / `..._GlowMat.material`）
  - 从资产浏览器把另一个 `.material` **拖到某 slot 行** → 该 slot 材质替换，viewport 对应 sub-mesh 段材质实时变；Ctrl+Z 撤回
  - 点某 slot 行的 **×** 清除 → 该 slot 回退到 Renderable 的默认材质（viewport 该段变默认材质色）
  - **空 slot / 单材质 mesh**：单材质模型 drop 后不挂本组件、Inspector 无此段（向后兼容）
- **背景**：headless ctest 74/74 验 schema 注册 + AssetRefArray get/set + 命令栈 round-trip；**数组型 AssetRef 控件的 DnD / 清除 / Undo 手感 + 逐 slot 重指派的 viewport 实时性待真机确认**。

### ✅ 6. PBR emissive 自发光通道 —— GlowMat 半边发蓝光

> **2026-06-02 dogfood 通过**（用户实测确认 multimat_emissive_cube：一半橙色 PBR + 一半 GlowMat 蓝色自发光）。shader（pbr.vert 176B push + pbr.frag set 1 binding 4）+ 导入 uEmissive 预乘 GPU 端视觉正确。

- **commit**：`0d14e21` feat(render): PBR emissive 自发光通道（shader + 导入消费）
- **怎么触发**：
  1. 同上把 `multimat_emissive_cube.mesh` 拖入场景（slot 1 = GlowMat 自发光）
  2. 确保场景启用 bloom（PostProcess）以看 HDR glow
- **看什么 / 通过判据**：
  - cube 一半面（slot 0 OrangeMat）显示**橙色普通 PBR**，另一半（slot 1 GlowMat）显示**蓝色自发光**（即便无直接光照也亮），且 emissive > 1 经 **bloom 发光晕**
  - 给任意 entity 的 pbr `.material` 手动加 `uEmissive` override（Inspector / 编辑 .material）→ viewport 即时自发光（验证 emissive 不依赖导入路径）
  - **零回归**：历史无 emissive 的 pbr 材质（不写 uEmissive override）渲染与之前完全一致（不发光）
- **背景**：headless ctest 不跑 Vulkan → emissive 的 **GPU 渲染视觉只能靠真机**。shader（pbr.vert 176B push + pbr.frag set 1 binding 4）+ 导入消费已编译通过 + 导入侧数值已验证（见上 fixture 说明）。

### 7. tangent fallback —— 无 UV 模型导入不崩 + 渲染正常

- **commit**：`dbac111` test(editor): tangent fallback 端到端验证（无 UV .obj → Lengyel 兜底）
- **背景 / 现状**：headless `headless_mesh_import_test` 第 6 段已**端到端自动验证**（无 UV .obj → importer 不崩 → Load 端 Lengyel 兜底产出单位长 + 与法线正交 + w=±1 的有效 TBN）。属"已自动覆盖"，**视觉残留极小**——仅需顺手确认：导入一个缺 UV 的真实模型（如某些只导 position+normal 的 `.obj`）后 viewport 渲染无黑斑 / 无 NaN 闪烁（法线贴图退化路径走几何法线）。无专用 fixture，撞到再验即可。

---

## 2026-06-02 autonomous session（贴近 Lumix 成熟度：编辑器能力补完）

> 目标：参考 gap 文档，自主找任务让引擎更贴近 Lumix 成熟度。本段登记该 session 落地能力里"headless 绿但视觉/交互待人工 dogfood"的残留。

### 8. Create 3D Object —— 一键创建 Cube / Sphere / Plane 基本体

- **commit**：`c5242b1` feat(editor): Create 3D Object 基本体（Cube / Sphere / Plane）
- **怎么触发**：
  1. Entity Tree 空白处**右键** → `Create 3D Object (root)` → 选 Cube / Sphere / Plane
  2. 或在某个实体节点上**右键** → `Create 3D Object (Child)` → 选基本体（挂为该节点末子）
- **看什么 / 通过判据**：
  - viewport 立刻出现对应几何：**Cube**（立方体）/ **Sphere**（光滑球，lat-lon UV 球）/ **Plane**（平面，地面/墙面）—— 都带 pbr 材质（非隐形）
  - 新建实体自动选中 + 进入重命名（名字默认 Cube/Sphere/Plane）
  - 选中后 Inspector 有 Renderable（mesh = 对应内置 mesh，material = pbr，castsShadow 勾上）
  - child 形式创建的挂在父节点下（Hierarchy 缩进体现父子）
  - Ctrl+Z 撤销 = 删掉刚建的实体（走 CreateEntityCommand 命令栈）
- **背景**：之前只能 Create Entity（空壳）+ Create Light Object，得"建空实体 → Add Renderable → 选 mesh"三步才有可见几何。本项对齐 Unity GameObject→3D Object / Godot 节点创建。headless 无 GUI 菜单测试路径，**菜单交互 + 三种几何 viewport 视觉待真机确认**。

---

## 维护约定

- 新 feature 落地后，若有"headless 绿但视觉/手感待验"的残留，追加到本文件对应 session 段。
- dogfood 通过的条目：在标题前加 ✅，并可在条目内记一句实测结果（截图路径 / 发现的问题）。
- dogfood 发现 bug：在 `docs/engine-known-gaps.md` 登记新 GAP / BUG，本条目内 link 过去。
