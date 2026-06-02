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

### 9. Component Copy / Paste Values —— 跨实体复制组件值

- **commit**：`343444e` feat(editor): Component Copy / Paste Values
- **怎么触发**：
  1. 选中实体 A → Inspector 里**右键某个组件头**（如 Transform / Light / Renderable）→ `Copy Values`
  2. 选中实体 B → 右键**同类型**组件头 → `Paste Values`（不同类型时该项灰禁）
- **看什么 / 通过判据**：
  - Paste 后 B 的该组件各字段值 = A 的值（如 Copy A 的 Transform → Paste 到 B → B 的 position/rotation/scale 变成 A 的）
  - 跨类型禁用：Copy 了 Transform，去 Light 组件头右键 → Paste Values 灰掉不可点
  - **可 Undo**：Paste 后 Ctrl+Z 把 B 的组件值还原到 paste 前
  - AssetRef 字段（材质/mesh）也复制（粘贴后指向同一资源）
- **背景**：对齐 Lumix StudioApp / Unity "Copy Component / Paste Component Values"。复用 schema property get/set + remove-undo 的 CaptureComponentState 机制。右键菜单交互 + 各类型字段粘贴正确性待 dogfood。edge：粘贴 Renderable 的 mesh 不联动 SubMeshMaterials（少见）。

### 10. 单材质模型 drop 自动带材质

- **commit**：`ff53bfe` feat(editor): 单材质 mesh drop 自动应用导入材质
- **怎么触发**：
  1. 导入一个**单材质** glTF/glb 模型（如 KHR sample Avocado / Duck，或 Blender 单材质导出）
  2. 把导入的 `.mesh` 从资产浏览器拖到场景一个实体上（或拖到 viewport 实体上）
- **看什么 / 通过判据**：
  - drop 后该实体的 Renderable.Material **自动变成导入的 `<stem>.material`**（而非默认灰 pbr），viewport 显示模型自带的材质/贴图（之前是默认材质）
  - 不挂 SubMeshMaterialsComponent（单材质无需），Inspector 无 Sub-Mesh Materials 段
  - 多材质模型 drop 行为不变（仍挂 SubMeshMaterials + 各段材质）
- **背景**：之前单材质模型 drop 后是默认材质（导入的 .material 不自动应用，要手动指派）。对齐 Lumix/Unity 拖模型进场景自动带材质。headless section 7 已验导入侧 .meta 写 subMeshMaterials；**drop 后 viewport 材质视觉待真机确认**。推荐 fixture：KHR Avocado.glb。

### 11. 拖 mesh 到 viewport 空白处创建实体

- **commit**：`59f2401` feat(editor): 拖 mesh 到 viewport 空白处创建实体
- **怎么触发**：从资产浏览器拖一个 `.mesh`（如 cube.mesh / 导入的模型）到 viewport **没有物体的空白区域**松手
- **看什么 / 通过判据**：
  - 在松手处的**地面落点**（射线 ∩ y=0 平面）出现一个新实体，带该 mesh + 材质（单/多材质都按 Task C/10 自动应用），自动选中
  - 名字 = mesh 文件名（stem）
  - 朝天空拖（射线不交地面）→ 落到相机前方固定距离，不丢失
  - 拖到**已有物体上** → 仍是替换该物体的 mesh（原 ApplyMesh 行为，不创建新的）
  - 拖**非 mesh**（材质 / 音频）到空白 → 无反应（需既有实体承载）
  - Ctrl+Z 撤销 = 删掉新建的实体
- **背景**：对齐 Unity / Lumix 拖模型进空场景生成 GameObject。headless 无 GUI 拖放路径，**落点准确性 + 拖放手感 + 自动带材质 viewport 视觉待真机确认**。

### 12. OBJ .mtl 单材质导入

- **commit**：`36f2651` feat(editor): OBJ .mtl 单材质导入 + 修 mtl_basedir 缺尾斜杠
- **怎么触发**：导入一个带 `.mtl`（单材质）的 `.obj` 模型（Blender/其他 DCC 导出 OBJ 勾选材质，或手写 .obj + .mtl，确保 .mtl 与 .obj 同目录）
- **看什么 / 通过判据**：
  - `assets/Models/<stem>/` 下生成 `<stem>.material`（pbr 模板），其 uBaseColor = .mtl 的 Kd、uMRA.y = Ns 推导的 roughness、有 Ke 时含 uEmissive
  - 把该 `.mesh` 拖到场景（或拖空白处建实体）→ viewport 显示模型自带的 OBJ 材质颜色（橙色/对应 Kd），有 Ke 的发光（配 bloom）
  - 之前同样的 OBJ 导入后只有几何（默认灰材质）
- **背景**：之前 OBJ importer 解析 .mtl 但不消费。单材质 .mtl → pbr 材质（scalar：Kd/Ns/Ke，贴图 map_Kd 等留后续）。顺手修了 tinyobj mtl_basedir 缺尾斜杠的潜在 bug。
- **2026-06-02 补**：**多材质 OBJ 也已支持**（`6739df2`，与 gltf 对等）—— usemtl 分组的 OBJ 导入后拆 sub-mesh + 生成多个 .material（slot 0=`<stem>.material` / slot≥1=`<stem>_<matname>.material`），drop 后各段显示各自材质。dogfood 多材质 OBJ：cube 用 Blender/手写分 2+ usemtl 组导出 .obj。
- **2026-06-02 再补**：**OBJ 贴图也已导入**（`fcbe5b8` + `aa7d534`，OBJ 与 gltf 完全对等）—— map_Kd→baseColor / norm(或 map_bump)→normal / map_Ke→emissive 经 ImportTextureToRegistry co-locate 到模型目录 + 写进 .material texture 槽。dogfood：带 map_Kd 贴图的 .obj（贴图文件与 .obj/.mtl 同目录）导入后 viewport 应显示贴图（而非纯 Kd 色）。

### 13. Asset 浏览器右键 mesh "Add to Scene"

- **commit**：`73e1807` feat(editor): Asset 浏览器右键 mesh "Add to Scene" + Pick-to-mesh 同步 SubMeshMaterials
- **怎么触发**：资产浏览器里**右键**一个 `.mesh` / `.obj` → `Add to Scene`（不需先选中场景实体）
- **看什么 / 通过判据**：
  - 在相机焦点（pivot ≈ 视野中心）处出现一个带该 mesh + 导入材质的新实体并选中（与拖到空白处 item 11 同款落地）
  - **Pick to Renderable.mesh** 一致性：选中一个实体 → 右键多材质 `.mesh` → "Pick to Renderable.mesh" → 该实体也正确挂上 SubMeshMaterials（各段材质），之前只换 mesh handle 不挂组件
- **背景**：给"把模型放进场景"一个菜单入口（拖放之外），对齐 Lumix/Unity instantiate。**菜单交互 + Pick 多材质同步 viewport 待真机确认**。

### 14. viewport 工具栏 Snap 开关

- **commit**：`7313ba5` feat(editor): viewport 工具栏 Snap 开关
- **怎么触发**：viewport 顶部工具栏（Gizmos / Grid / Sky / Debug Draw / Colliders 那排）勾选 **Snap**
- **看什么 / 通过判据**：
  - 勾上后用 translate / rotate / scale gizmo 拖动物体 → 按步进对齐（translate 默认步进 / rotate 角度步进 / scale 步进，hover Snap 看 tooltip 显示当前步进值）
  - 取消勾选 → 连续自由拖动（无吸附）
  - 步进值在 Settings 面板 Snap 段可调，工具栏开关与 Settings 双向同步（同一个 settings.snapEnabled）
- **背景**：snap 功能（3 个 gizmo 都已消费 snapEnabled）之前只埋在 Settings，加 viewport 快捷开关提升可发现性。对齐 Unity/Lumix snap toggle。**开关 + 吸附手感待真机确认**。

### 15. viewport 统计 overlay

- **commit**：`5b722b9` feat(editor): viewport 统计 overlay
- **怎么触发**：打开 OrangeEditor，看 viewport **左上角**
- **看什么 / 通过判据**：
  - 半透黑底的两行文字：`Entities N | Renderables M | Tris T` + `Selected: <名字>`
  - 数字随场景变化（建/删实体、Hide/Unhide、选中切换）实时更新；Tris = 所有可见 Renderable 的三角总数
  - 选中实体 → 第二行显示其名字；未选 → `(none)`
- **背景**：对齐 Lumix StudioApp / Unity scene stats。始终显示（紧凑半透不挡视野）。**overlay 视觉位置 + 计数准确性待真机确认**。

### 16. RMB+WASD 飞行相机导航

- **commit**：`f151fa6` feat(editor): RMB+WASD 飞行相机导航
- **怎么触发**：在 viewport 内**按住鼠标右键（RMB）**，同时按 **WASD / Q / E**
- **看什么 / 通过判据**：
  - RMB 拖动 → 视角原地转动（look-in-place，相机不绕物体转，是"原地环顾"）
  - RMB 按住 + **W/S** 前进/后退、**A/D** 左移/右移、**Q/E** 下降/上升（沿当前视向飞行）
  - 按住 **Shift** 飞行加速 ×3
  - **不冲突**：RMB 按住时按 W 不会切到 translate gizmo（只飞行）；松开 RMB 后 W/E/R 恢复切 gizmo mode
  - LMB 轨道 / MMB 平移 / 滚轮缩放 行为不变
- **背景**：之前相机纯轨道（orbit），大场景穿行不便。补 Unreal/Unity 标准 RMB+WASD 自由飞行。**纯交互功能 headless 测不了** —— RMB-look 方向是否顺手 / WASD 方向对不对 / 飞行速度合不合适，全靠真机 dogfood（若方向反了/速度不对，告诉我调 lookSensitivity 符号 / flySpeed）。

### 17. Frame All（Home 键聚焦全场景）

- **commit**：`931023d` feat(editor): Frame All（Home 键 frame 全场景）
- **怎么触发**：viewport 内按 **Home** 键（无文本输入焦点时）
- **看什么 / 通过判据**：相机拉到能看**全场景所有几何**的距离（合并所有 entity 世界 bounds）；对比 **F** 键（只聚焦当前选中）。空场景按 Home 无反应。
- **背景**：F 聚焦选中（已有）+ Home 聚焦全场景（新增），对齐 Unity/Unreal。**视觉待真机确认**。

### 18. pbr 材质 inspector 可调 emissive

- **commit**：`009a6e4` feat(editor): pbr 材质 inspector 暴露 uEmissive + emissive 贴图槽
- **怎么触发**：选中一个 `.material`（pbr 模板）进 Material 子模式 inspector（或新建 pbr 材质）
- **看什么 / 通过判据**：
  - inspector 出现 **Emissive R / G / B** 三个 slider（range 0~8，可 > 1）
  - 调高某通道（如 B 到 3.0）→ viewport 该材质物体**自发光**（配 bloom 发光晕）
  - 出现 emissive 贴图槽（binding 4 uEmissiveTex），可拖贴图进去
  - 导入带 emissive 的模型（如早上的 multimat_emissive_cube GlowMat）选其 .material → Emissive 值已是导入的（factor×strength）
- **背景**：早上加了 emissive 渲染通道（0d14e21）但漏了编辑器 meta（pbr.template.json）→ 之前调不了。本次补上，emissive 编辑闭环。**Material 子模式 emissive 控件 + viewport 实时性待真机确认**。

---

### 19. File → Open Recent（最近场景）

- **commit**：`58fcc34` feat(editor): File → Open Recent 最近场景列表
- **怎么触发**：File 菜单 → **Open Recent** 子菜单（打开 / 另存过几个场景后才有内容；空时灰禁）
- **看什么 / 通过判据**：
  - 子菜单列出最近打开 / 另存的场景（文件名短名，hover 看完整路径 tooltip），front = 最近
  - 点一项 → 直接打开该场景（跳过文件对话框；有未保存改动时先弹确认）
  - 重复打开同一场景 → 该项置顶不重复；最多 10 条
  - **持久化**：关编辑器重开，Open Recent 列表仍在（存进 editor_settings.json）
- **背景**：File 菜单之前没有最近场景，每个成熟编辑器都有。**菜单 + 持久化待 dogfood**。

### 20. 小补完一束（低 dogfood 风险，顺手扫一眼即可）

- `5a3a9a0` **viewport overlay 加 gizmo 状态**：左上角 overlay 第三行 `Gizmo: <Move/Rotate/Scale> [<World/Local>]` —— 切 W/E/R 模式 + X 键切 World/Local 时该行实时变。
- `b90a662` **相机聚焦菜单入口**：Entity Tree 节点右键有 **Focus**（聚焦该实体）；View 菜单有 **Frame Selected (F)** / **Frame All (Home)**。
- `8311bd3` **Ctrl+A 全选**：Entity Tree 焦点时 Ctrl+A → 全选所有实体（看 overlay Selected / 多选高亮）。

---

### 21. viewport 工具栏 gizmo 变换工具按钮

- **commit**：`e63d986` feat(editor): viewport 工具栏 gizmo 变换工具按钮（Move/Rotate/Scale + World/Local）
- **怎么触发**：看 viewport 顶部工具栏（Snap 开关右侧），有 **[Move][Rotate][Scale]** + **[World/Local]** 按钮
- **看什么 / 通过判据**：
  - 当前 gizmo 模式对应的按钮**高亮**（如默认 Move 高亮）；点 Rotate → Rotate 高亮 + 选中实体 gizmo 变旋转环
  - 键盘 W/E/R 切模式时，对应按钮的高亮**同步**变（双向一致）
  - 点 World/Local 按钮在两态间切，按钮文字随之变 World⇄Local；与 X 键同步
  - hover 各按钮有 tooltip 标快捷键（W/E/R/X）
- **背景**：gizmo 模式/坐标系之前只有隐蔽快捷键，无可点入口。对齐 Unity 左上变换工具栏 / Lumix scene toolbar。**按钮高亮 + 双向同步待真机确认**。

### 22. 飞行模式滚轮调速

- **commit**：`845c14c` feat(editor): 飞行模式滚轮调速（Unreal/Unity 标准）
- **怎么触发**：viewport 内**按住右键**进入飞行（见 item 16），飞行中**滚动滚轮**
- **看什么 / 通过判据**：
  - 飞行中（RMB 按住）滚轮**不再推近/拉远**，而是改飞行速度——向上滚 WASD 移动变快，向下滚变慢
  - 松开右键后滚轮恢复正常 zoom（缩放 orbit radius）
  - 速度有上下限（极慢仍能动、极快不失控）；Shift 加速（×3）在新速度基础上叠加
- **背景**：补完 item 16 飞行导航，对齐 Unreal/Unity scene 飞行——大场景调快、精修调慢。**纯手感，连同飞行方向/速度一起 dogfood**。

### 23. viewport 聚焦时 Delete / Ctrl+D

- **commit**：`7b20468` feat(editor): viewport 聚焦时也响应 Delete / Ctrl+D
- **怎么触发**：在 **viewport 里**点选一个实体（不切到 Hierarchy 面板），按 **Delete** 或 **Ctrl+D**
- **看什么 / 通过判据**：
  - Delete → 选中实体被删（可 Ctrl+Z 撤销）
  - Ctrl+D → 复制出一份选中子树（作 sibling），新副本被选中
  - 之前这俩**只在 Hierarchy 面板聚焦时**生效，viewport 里按没反应；现在 viewport 聚焦也行（对齐 Unity/Lumix）
  - **不误触**：在 Inspector 文本框输入时按 Delete 是删字符不删实体；RMB 飞行按住时按 D 是右移不复制
  - **不重复**：Hierarchy 和 viewport 不会同帧各删一次 / 各复制一次（幂等标志）
- **背景**：最高频的两个场景操作，此前被面板焦点限制。**删除/复制行为 + 不误触待真机确认**。

### 24. 文件快捷键 Ctrl+S / Ctrl+Shift+S / Ctrl+N / Ctrl+O

- **commit**：`48fe776` feat(editor): 接线 Ctrl+S / Ctrl+Shift+S / Ctrl+N / Ctrl+O 文件快捷键
- **怎么触发**：编辑器内（非文本输入焦点）按 **Ctrl+S**（保存）/ **Ctrl+Shift+S**（另存为）/ **Ctrl+N**（新场景）/ **Ctrl+O**（打开场景）
- **看什么 / 通过判据**：
  - Ctrl+S：场景有改动（标题/菜单 Save 亮）时存盘；无改动时无操作（不弹框）；当前无路径时自动转另存对话框
  - Ctrl+Shift+S → 弹另存对话框
  - Ctrl+N / Ctrl+O → 新建 / 打开；**有未保存改动时先弹"未保存确认"popup**（与菜单点击同一条路径）
  - 这些快捷键 File 菜单里**早就显示**了（label），之前按却**没反应**；现在真生效
  - **不误触**：Inspector 文本框输入时 Ctrl+S 不触发保存
- **背景**：菜单宣传了快捷键却没接线（注释自承"留后续"），用户按了会困惑。补接线，对齐所有成熟编辑器。**保存/新建/打开 + 未保存确认流程待真机确认**。

### 25. 拖 gizmo 时按住 Ctrl 临时吸附

- **commit**：`bb4ace6` feat(editor): 拖 gizmo 时按住 Ctrl 临时吸附（Unity 标准）
- **怎么触发**：**Snap 开关保持关闭**，选中实体用 translate / rotate / scale gizmo 拖动时**按住 Ctrl**
- **看什么 / 通过判据**：
  - 按住 Ctrl 拖 → 按步进吸附（同 Snap 开关开时的效果）；松开 Ctrl → 连续自由拖
  - Snap 开关已开时按 Ctrl 不影响（仍吸附）
  - 三个 gizmo（移/转/缩）都生效；步进值同 Settings 里的 snap 步进
- **背景**：之前吸附只能靠全局 Snap 开关；补 Unity 标准"拖动中按 Ctrl 临时吸附"。Snap tooltip 也加了这行提示。**吸附手感 + Ctrl 时机待真机确认**。

### 26. Asset 浏览器双击 .scene.json 打开场景

- **commit**：`f0e2df1` feat(editor): Asset 浏览器双击 .scene.json 打开场景
- **怎么触发**：Asset 浏览器文件列表里**双击**一个 `.scene.json`（[S] 图标 / 场景快照缩略图）
- **看什么 / 通过判据**：
  - 双击 → 直接打开该场景（等价 File→Open 选它）
  - **有未保存改动时先弹"未保存确认"popup**（与菜单 Open 同一条路径）
  - 单击仍只是选中（不打开）；双击其它类型文件（.mesh/.material）不触发打开
- **背景**：之前双击场景文件无反应，只能走 File→Open 对话框 / Open Recent。补 Unity/Lumix 标准双击打开。**双击打开 + 未保存确认流程待真机确认**。

### 27. View → Standard Views（标准视角）

- **commit**：`ece850c` feat(editor): View 菜单加 Standard Views
- **怎么触发**：菜单 **View → Standard Views → Front / Back / Left / Right / Top / Bottom**
- **看什么 / 通过判据**：
  - 点 Front → 相机正对 -Z 看（场景从 +Z 正视）；Top → 俯视（从上往下看 XY 面）；Right/Left/Back 各沿对应世界轴
  - pivot 和距离（radius）保持不变，只换角度
  - Top/Bottom 不会翻转/抖动（钳到 ±89° 避 gimbal）
- **背景**：低 dogfood 风险（只设相机角度，对就是对）。对 2.5D 对齐 / 摆位有用。注：当前仍是透视投影（未切正交），后续可加 ortho 切换。

### 28. Hierarchy 右键 Reset Transform

- **commit**：`f1f25c1` feat(editor): Hierarchy 右键 Reset Transform（可 Undo）
- **怎么触发**：Entity Tree 里右键一个实体 → **Reset Transform**
- **看什么 / 通过判据**：
  - 把该实体的 position 归 0、rotation 归单位、scale 归 1（回到本地 identity）；viewport 里物体跳回父空间原点 / 无旋转 / 原始大小
  - **Ctrl+Z 可撤销**（恢复重置前的 transform）
  - Inspector 的 Position/Rotation/Scale 同步刷新成 0/0/1（rotation euler 也对）
  - 无 TransformComponent 的实体该项灰禁
- **背景**：导入模型 transform 异常 / 手滑挪偏后一键归位，对齐 Unity 的 Transform → Reset。**重置 + undo + Inspector 刷新待真机确认**。

### 29. Edit 菜单 Duplicate / Delete 入口（低风险）

- **commit**：`fc6d2ff` feat(editor): Edit 菜单加 Duplicate / Delete 入口
- **怎么触发**：选中实体 → 菜单 **Edit → Duplicate (Ctrl+D) / Delete (Del)**
- **看什么 / 通过判据**：点 Duplicate 复制选中、Delete 删除选中（与快捷键 / Hierarchy 右键同效）；无选中时灰禁。
- **背景**：低风险（只是给既有 host 标志加菜单可发现入口）。Edit 菜单之前只有 Undo/Redo。

---

## 2026-06-02 autonomous session（贴近 Lumix：glTF scene-level 导入）

> 目标续上一段（贴近 Lumix 成熟度）。本段登记 GAP-2026-05-28 G1 落地里"headless 绿但视觉/层级待人工 dogfood"的残留。

### 30. glTF scene-level 导入 —— 保留 transform 层级 + 每 mesh 单独不塌平

- **commit**：见本 session `feat(editor): glTF scene-level 导入`（GAP-2026-05-28 G1）。
- **背景**：此前 glTF importer 只有"asset import"维度（multi-mesh / multi-primitive **塌平合并成单个 MeshAsset**，丢失 transform 层级 / per-mesh 划分）。本次补"scene import"维度：遍历 `scenes[0].nodes` 的 transform 树，**每个 cgltf mesh 单独写一个 `.mesh`（不塌平）**，产出与 DCC 摆位同构的 `.scene.json`。对齐 Unity model prefab / Unreal scene import / Godot ".glb as scene" / Lumix per-mesh import。
- **怎么触发**（两种入口）：
  - **CLI**（headless，不开 GUI）：`build/bin/Debug/OrangeEditor.exe import-scene path/to/scene.glb`
  - **GUI 菜单**（⚠️ 编译验证过、**运行时待 dogfood**——我无法启动 GUI 验证）：编辑器内 **File → Import glTF Scene...** → 选 `.gltf/.glb` → 导入后 ORANGE_LOG 打印产出路径。**注意 GUI 入口当前不自动打开导入的场景**（避免与未保存场景冲突），需再手动 **File → Open** 或资产浏览器双击该 `.scene.json`。
  - 产物：`assets/scenes/<basename>.scene.json` + `assets/Models/<basename>/<basename>_<meshname>.mesh`（每 cgltf mesh 一个）+ 各 `.meta`。
  - **GUI 入口专项 dogfood**：File→Import glTF Scene... 菜单点了是否真弹文件框、选 .glb 后是否真产出 scene.json + 控制台 log、有无崩溃 —— 这部分我没法运行验证，**请重点确认**；若菜单无反应 / 崩溃，告诉我（接线在 `EditorRenderLayer::ApplyPendingImports` 的 `mPendingImportSceneDialog` 分支）。
- **看什么 / 通过判据**：
  - viewport 里每个 prop 出现在 **Blender/DCC 摆好的世界位置 / 旋转 / 缩放** 上（不再全部叠在原点）；
  - **Hierarchy panel 显示与 DCC 同构的 transform tree**（父子关系保留；group 空节点也在，作为分组父节点）；
  - 每个 mesh 是**独立实体 + 独立 `.mesh`**（可单独选中 / 各自的 Renderable 指向不同 mesh），而非整包一个 mesh；
  - 同一 mesh 被多 node 引用时只生成一个 `.mesh` 文件（多实体共享同一 handle）。
- **G1 范围限制（dogfood 时注意，不是 bug）**：
  - **材质全是默认材质**（灰 pbr）—— per-mesh PBR material 划分是 **G2**（未做），G1 只保几何 + 层级。所以即便 DCC 里有材质，导入后也是默认材质，这是预期。
  - **灯光已导入（G3）**：glTF 灯光（directional/point/spot，KHR_lights_punctual）导入成对应引擎光源，**方向 / 颜色 / 位置 / range / 锥角正确**；intensity 经 **÷683 luminous efficacy** 把 glTF 光度单位映射到引擎尺度（实测 Blender sun 3W/m²→glTF 2049 lux→3.0，落在引擎 directional 1.2~2.5 尺度，**不再过曝纯白**）。**相机不导入**（G3 cameras 未做）；skinning / morph / 非 triangle primitive 全 skip。
  - **灯光 dogfood 看什么**：导入带灯光的 .glb 后，viewport 里光的**方向**（directional 太阳角度 / spot 锥指向）和**颜色**应与 DCC 一致；若方向反了 / 偏 90°，告诉我（可能是 -Z→-Y 转换或 has_matrix 分解问题）。亮度应在可用范围（÷683 后），若仍明显偏亮/偏暗，告诉我标定 point/spot 的近似系数（directional 已实测对齐）。
  - **现成 dogfood fixture**：`scripts/dogfood_make_scene_hierarchy_lights.py`（Blender headless 生成父 Empty + 2 parented props + Sun + Point light 的层级场景）。生成 + 导入：
    ```
    D:/Software/Blender/blender.exe --background --python scripts/dogfood_make_scene_hierarchy_lights.py
    build/bin/Debug/OrangeEditor.exe import-scene scripts/scene_hierarchy_lights.glb
    # 然后 GUI File→Open assets/scenes/scene_hierarchy_lights.scene.json
    ```
    **已用 HEAD 二进制端到端验证**（CLI 路径）：import 日志 `entities=5 meshes=2 lights=2`；scene.json = 5 entity + PropGroup→{CrateProp,BarrelProp} 层级 + DirectionalLight/PointLight + 各独立 .mesh + Z-up→Y-up 正确；**intensity ÷683 实测落引擎尺度**（SunLight=3.0 / LampLight=7.96，非 raw 2049/5435）；二次导入 hash-skip 生效。**剩待人工 dogfood**：① **GUI File→Import glTF Scene... 菜单运行时**（我无显示无法验）；② **viewport 视觉**（层级摆位 / 灯光方向颜色 / 整体亮度是否合适）。
  - **transform 是 world-baked**：因引擎渲染不累积 hierarchy 父变换（见 `GAP-2026-06-02-hierarchy-transform-not-propagated`），导入时把每个 node 的 world 变换 flatten 进各自 TransformComponent。**所以摆位视觉是对的**（每个 prop 在 DCC 世界位置），但**导入后在编辑器移动父节点不会带动子节点**（沿用引擎现有限制，非本导入的 bug）。dogfood 看"初始摆位对不对"即可，别期望父子联动。
- **推荐 fixture**：在 Blender 摆 3~5 个 prop（各自不同 transform，组织成 1~2 层父子，比如一个 Empty 父节点下挂几个 mesh），导出 `.glb`（**勾选 +Y up，glTF 默认**）。或现成带 node 层级的多 mesh glTF 资产（如 KHR sample 里的 `BoxAnimated` / 任意场景型 .glb）。
- **若发现问题**：摆位错位（可能是 has_matrix 分解 / 坐标轴问题）/ 层级反了 / mesh 被错误合并 → 在 `docs/engine-known-gaps.md` 登记，link 回 GAP-2026-05-28。

---

## 2026-06-02 session（成熟度地基 A1：Transform 层级传播）

> 目标：贴近 Lumix 成熟度，开始排期 + 开发。A1 = Transform 层级传播（`maturity-roadmap.md` 地基首位 / ADR-016）。本段登记 A1.1 step 2（首消费者 mesh 切换）的视觉 dogfood —— 这是有**真实视觉变化**的改动，必须真机验证。

### 31. Transform 层级传播（mesh 路径）—— 移动父节点带动子 mesh

- **commit**：本 session `feat(render): RenderScene::Collect 读 WorldTransformComponent`（A1.1 step 2 首消费者）+ `feat(editor): glTF scene import 回退 world-bake 为 local TRS`（A1.2）。引擎层 ADR-016 / `TransformSystem::PropagateWorldTransforms`。
- **背景**：此前引擎渲染**不沿 hierarchy 累积父变换**（`GAP-2026-06-02-hierarchy-transform-not-propagated`）——parenting 对世界位置无效，移动父节点子节点不动。本次让 **mesh drawable** 沿 hierarchy 累积 world matrix（Unity/Godot/Lumix table-stakes）。
- **① 新行为：parenting 现在传播（看什么 / 通过判据）**：
  1. 编辑器里建两个有 mesh 的实体 A、B（如两个 Cube）。
  2. 把 B **parent 到 A 下**（Entity Tree 拖 B 到 A，或右键）。
  3. 选中 A，用 gizmo **移动 / 旋转 / 缩放 A** → **B 应跟着动**（保持相对位姿）——这是新行为（之前 B 纹丝不动）。
  4. 嵌套多层（A→B→C）→ 移动 A，B、C 都跟随；移动 B，只 C 跟随。
- **② 回归检查：现有场景渲染不变（重要）**：
  - 打开 **pbr_showcase** 场景（File→Open 或 Reset to Demo）→ 18 个球阵 + demo 几何的**摆位应与之前完全一致**（因 group 父节点 root/warmGroup/whiteGroup/geometry 都在原点，累积==local，零变化）。
  - **若有任何物体跳位 / 消失 / 整体偏移 → 报告我**（说明累积或 cache 有 bug）。
- **③ glTF scene import 现在写 local（需重导旧产物）**：
  - 之前 import 的 glTF 场景是 world-bake 的旧产物——**重新跑** `import-scene <glb>`（或 File→Import glTF Scene），新产物 importer 写 local TRS。
  - 打开新 scene.json → viewport 里层级摆位应正确（父在某位置，子在"父+local"的世界位置），且**移动父带动子**。用 `scripts/dogfood_make_scene_hierarchy_lights.py` 那个层级 fixture 验。
- **④ picking（选中）已跟上 ✅**：parented mesh 在它的**世界位置**可被点选（之前 picking 测 local，非原点父的 mesh 选不中）。选中后 Inspector 可编辑、Del/Ctrl+D 可用。
- **范围限制（dogfood 时注意，不是 bug）**：
  - **已切：mesh 渲染 + picking**。**未切：gizmo 放置/拖动、光源方向、物理 collider**（后续 increment）——具体表现：
    - **gizmo 仍画在 local 位置**：选中一个**非原点父**下的 mesh，gizmo 会画在它的 local 偏移处（不在 mesh 上）；拖 gizmo 仍写 local。可发现性受影响但能编辑（Inspector / 拖动仍改 local）。**这是预期**，A1.3 + gizmo 切换后修。
    - **光源全 3 类 + halo + 后处理体积已随父传播 ✅**：平行光方向 + 点光/聚光位置 + 聚光方向 + 点光 halo 球 + PostProcess local volume box 都跟随父变换。root 灯零回归。**仍未切：gizmo 放置/拖动、物理 collider（2D）**——这两个是"写/apply"类（需 world→local），parent 到非原点父下：gizmo 画在 local 偏移处 + 拖动按 local；collider 不随父动（mesh+灯+halo 都随）。glTF 导入的灯（通常 root 子）仍对；非 root 导入灯方向可能偏（rare）。
  - **reparent keep-world ✅**（A1.3 主 DnD 路径已落）：把 B 拖到**已移动过**（非原点）的 A 下，B **应保持原世界位置不跳**（keep-world 重算 local）；Ctrl+Z 撤回 B 也回原位。**dogfood 重点验**：拖 reparent 到移动过的父，物体不跳位 + undo 还原。（注：duplicate/Ctrl+D 的 reparent 不走 keep-world——clone 复制原 local，行为同原对象。）

> **A1 剩余 consumer 完成清单**（给后续 session）：gizmo 放置+拖动读 world / world→local apply（防 parented 拖偏）· A1.3 reparent keep-world（重算 local 防跳）· 光源方向读 world rotation（+ importer 去 world-dir 编码改 R 桥接）· 物理 collider 读 world（2D，subtler）。详见 `docs/maturity-roadmap.md` A1.1 step 2。

---

## 维护约定

- 新 feature 落地后，若有"headless 绿但视觉/手感待验"的残留，追加到本文件对应 session 段。
- dogfood 通过的条目：在标题前加 ✅，并可在条目内记一句实测结果（截图路径 / 发现的问题）。
- dogfood 发现 bug：在 `docs/engine-known-gaps.md` 登记新 GAP / BUG，本条目内 link 过去。
