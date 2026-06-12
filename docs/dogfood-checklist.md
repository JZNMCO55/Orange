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

### ✅ 8. Create 3D Object —— 一键创建 Cube / Sphere / Plane 基本体

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
  - [bug]Hierarchy没有置灰
  - Paste 到slime的Transform 后，entity位置发生变化，但不显示了
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
    - [bug] 拖入到 viewport 空白处，仍然时默认的灰色PBR材质
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
    - [bug] Add to Scene后，仍然是默认的灰色PBR材质
  - **Pick to Renderable.mesh** 一致性：选中一个实体 → 右键多材质 `.mesh` → "Pick to Renderable.mesh" → 该实体也正确挂上 SubMeshMaterials（各段材质），之前只换 mesh handle 不挂组件
- **背景**：给"把模型放进场景"一个菜单入口（拖放之外），对齐 Lumix/Unity instantiate。**菜单交互 + Pick 多材质同步 viewport 待真机确认**。

### ✅ 14. viewport 工具栏 Snap 开关

- **commit**：`7313ba5` feat(editor): viewport 工具栏 Snap 开关
- **怎么触发**：viewport 顶部工具栏（Gizmos / Grid / Sky / Debug Draw / Colliders 那排）勾选 **Snap**
- **看什么 / 通过判据**：
  - 勾上后用 translate / rotate / scale gizmo 拖动物体 → 按步进对齐（translate 默认步进 / rotate 角度步进 / scale 步进，hover Snap 看 tooltip 显示当前步进值）
  - 取消勾选 → 连续自由拖动（无吸附）
  - 步进值在 Settings 面板 Snap 段可调，工具栏开关与 Settings 双向同步（同一个 settings.snapEnabled）
- **背景**：snap 功能（3 个 gizmo 都已消费 snapEnabled）之前只埋在 Settings，加 viewport 快捷开关提升可发现性。对齐 Unity/Lumix snap toggle。**开关 + 吸附手感待真机确认**。

### ✅ 15. viewport 统计 overlay

- **commit**：`5b722b9` feat(editor): viewport 统计 overlay
- **怎么触发**：打开 OrangeEditor，看 viewport **左上角**
- **看什么 / 通过判据**：
  - 半透黑底的两行文字：`Entities N | Renderables M | Tris T` + `Selected: <名字>`
  - 数字随场景变化（建/删实体、Hide/Unhide、选中切换）实时更新；Tris = 所有可见 Renderable 的三角总数
  - 选中实体 → 第二行显示其名字；未选 → `(none)`
- **背景**：对齐 Lumix StudioApp / Unity scene stats。始终显示（紧凑半透不挡视野）。**overlay 视觉位置 + 计数准确性待真机确认**。

### ✅ 16. RMB+WASD 飞行相机导航

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
  - [bug] 按完后，viewport Snap 勾选框橙色高亮

### ✅ 18. pbr 材质 inspector 可调 emissive

- **commit**：`009a6e4` feat(editor): pbr 材质 inspector 暴露 uEmissive + emissive 贴图槽
- **怎么触发**：选中一个 `.material`（pbr 模板）进 Material 子模式 inspector（或新建 pbr 材质）
- **看什么 / 通过判据**：
  - inspector 出现 **Emissive R / G / B** 三个 slider（range 0~8，可 > 1）
  - 调高某通道（如 B 到 3.0）→ viewport 该材质物体**自发光**（配 bloom 发光晕）
  - 出现 emissive 贴图槽（binding 4 uEmissiveTex），可拖贴图进去
  - 导入带 emissive 的模型（如早上的 multimat_emissive_cube GlowMat）选其 .material → Emissive 值已是导入的（factor×strength）
- **背景**：早上加了 emissive 渲染通道（0d14e21）但漏了编辑器 meta（pbr.template.json）→ 之前调不了。本次补上，emissive 编辑闭环。**Material 子模式 emissive 控件 + viewport 实时性待真机确认**。

---

### ✅ 19. File → Open Recent（最近场景）

- **commit**：`58fcc34` feat(editor): File → Open Recent 最近场景列表
- **怎么触发**：File 菜单 → **Open Recent** 子菜单（打开 / 另存过几个场景后才有内容；空时灰禁）
- **看什么 / 通过判据**：
  - 子菜单列出最近打开 / 另存的场景（文件名短名，hover 看完整路径 tooltip），front = 最近
  - 点一项 → 直接打开该场景（跳过文件对话框；有未保存改动时先弹确认）
  - 重复打开同一场景 → 该项置顶不重复；最多 10 条
  - **持久化**：关编辑器重开，Open Recent 列表仍在（存进 editor_settings.json）
- **背景**：File 菜单之前没有最近场景，每个成熟编辑器都有。**菜单 + 持久化待 dogfood**。

### ✅ 20. 小补完一束（低 dogfood 风险，顺手扫一眼即可）

- `5a3a9a0` **viewport overlay 加 gizmo 状态**：左上角 overlay 第三行 `Gizmo: <Move/Rotate/Scale> [<World/Local>]` —— 切 W/E/R 模式 + X 键切 World/Local 时该行实时变。
- `b90a662` **相机聚焦菜单入口**：Entity Tree 节点右键有 **Focus**（聚焦该实体）；View 菜单有 **Frame Selected (F)** / **Frame All (Home)**。
- `8311bd3` **Ctrl+A 全选**：Entity Tree 焦点时 Ctrl+A → 全选所有实体（看 overlay Selected / 多选高亮）。

---

### ✅ 21. viewport 工具栏 gizmo 变换工具按钮

- **commit**：`e63d986` feat(editor): viewport 工具栏 gizmo 变换工具按钮（Move/Rotate/Scale + World/Local）
- **怎么触发**：看 viewport 顶部工具栏（Snap 开关右侧），有 **[Move][Rotate][Scale]** + **[World/Local]** 按钮
- **看什么 / 通过判据**：
  - 当前 gizmo 模式对应的按钮**高亮**（如默认 Move 高亮）；点 Rotate → Rotate 高亮 + 选中实体 gizmo 变旋转环
  - 键盘 W/E/R 切模式时，对应按钮的高亮**同步**变（双向一致）
  - 点 World/Local 按钮在两态间切，按钮文字随之变 World⇄Local；与 X 键同步
  - hover 各按钮有 tooltip 标快捷键（W/E/R/X）
- **背景**：gizmo 模式/坐标系之前只有隐蔽快捷键，无可点入口。对齐 Unity 左上变换工具栏 / Lumix scene toolbar。**按钮高亮 + 双向同步待真机确认**。

### ✅ 22. 飞行模式滚轮调速

- **commit**：`845c14c` feat(editor): 飞行模式滚轮调速（Unreal/Unity 标准）
- **怎么触发**：viewport 内**按住右键**进入飞行（见 item 16），飞行中**滚动滚轮**
- **看什么 / 通过判据**：
  - 飞行中（RMB 按住）滚轮**不再推近/拉远**，而是改飞行速度——向上滚 WASD 移动变快，向下滚变慢
  - 松开右键后滚轮恢复正常 zoom（缩放 orbit radius）
  - 速度有上下限（极慢仍能动、极快不失控）；Shift 加速（×3）在新速度基础上叠加
- **背景**：补完 item 16 飞行导航，对齐 Unreal/Unity scene 飞行——大场景调快、精修调慢。**纯手感，连同飞行方向/速度一起 dogfood**。

### ✅ 23. viewport 聚焦时 Delete / Ctrl+D

- **commit**：`7b20468` feat(editor): viewport 聚焦时也响应 Delete / Ctrl+D
- **怎么触发**：在 **viewport 里**点选一个实体（不切到 Hierarchy 面板），按 **Delete** 或 **Ctrl+D**
- **看什么 / 通过判据**：
  - Delete → 选中实体被删（可 Ctrl+Z 撤销）
  - Ctrl+D → 复制出一份选中子树（作 sibling），新副本被选中
  - 之前这俩**只在 Hierarchy 面板聚焦时**生效，viewport 里按没反应；现在 viewport 聚焦也行（对齐 Unity/Lumix）
  - **不误触**：在 Inspector 文本框输入时按 Delete 是删字符不删实体；RMB 飞行按住时按 D 是右移不复制
  - **不重复**：Hierarchy 和 viewport 不会同帧各删一次 / 各复制一次（幂等标志）
- **背景**：最高频的两个场景操作，此前被面板焦点限制。**删除/复制行为 + 不误触待真机确认**。

### ✅ 24. 文件快捷键 Ctrl+S / Ctrl+Shift+S / Ctrl+N / Ctrl+O

- **commit**：`48fe776` feat(editor): 接线 Ctrl+S / Ctrl+Shift+S / Ctrl+N / Ctrl+O 文件快捷键
- **怎么触发**：编辑器内（非文本输入焦点）按 **Ctrl+S**（保存）/ **Ctrl+Shift+S**（另存为）/ **Ctrl+N**（新场景）/ **Ctrl+O**（打开场景）
- **看什么 / 通过判据**：
  - Ctrl+S：场景有改动（标题/菜单 Save 亮）时存盘；无改动时无操作（不弹框）；当前无路径时自动转另存对话框
  - Ctrl+Shift+S → 弹另存对话框
  - Ctrl+N / Ctrl+O → 新建 / 打开；**有未保存改动时先弹"未保存确认"popup**（与菜单点击同一条路径）
  - 这些快捷键 File 菜单里**早就显示**了（label），之前按却**没反应**；现在真生效
  - **不误触**：Inspector 文本框输入时 Ctrl+S 不触发保存
- **背景**：菜单宣传了快捷键却没接线（注释自承"留后续"），用户按了会困惑。补接线，对齐所有成熟编辑器。**保存/新建/打开 + 未保存确认流程待真机确认**。

### ✅ 25. 拖 gizmo 时按住 Ctrl 临时吸附

- **commit**：`bb4ace6` feat(editor): 拖 gizmo 时按住 Ctrl 临时吸附（Unity 标准）
- **怎么触发**：**Snap 开关保持关闭**，选中实体用 translate / rotate / scale gizmo 拖动时**按住 Ctrl**
- **看什么 / 通过判据**：
  - 按住 Ctrl 拖 → 按步进吸附（同 Snap 开关开时的效果）；松开 Ctrl → 连续自由拖
  - Snap 开关已开时按 Ctrl 不影响（仍吸附）
  - 三个 gizmo（移/转/缩）都生效；步进值同 Settings 里的 snap 步进
- **背景**：之前吸附只能靠全局 Snap 开关；补 Unity 标准"拖动中按 Ctrl 临时吸附"。Snap tooltip 也加了这行提示。**吸附手感 + Ctrl 时机待真机确认**。

### ✅ 26. Asset 浏览器双击 .scene.json 打开场景

- **commit**：`f0e2df1` feat(editor): Asset 浏览器双击 .scene.json 打开场景
- **怎么触发**：Asset 浏览器文件列表里**双击**一个 `.scene.json`（[S] 图标 / 场景快照缩略图）
- **看什么 / 通过判据**：
  - 双击 → 直接打开该场景（等价 File→Open 选它）
  - **有未保存改动时先弹"未保存确认"popup**（与菜单 Open 同一条路径）
  - 单击仍只是选中（不打开）；双击其它类型文件（.mesh/.material）不触发打开
- **背景**：之前双击场景文件无反应，只能走 File→Open 对话框 / Open Recent。补 Unity/Lumix 标准双击打开。**双击打开 + 未保存确认流程待真机确认**。

### ✅ 27. View → Standard Views（标准视角）

- **commit**：`ece850c` feat(editor): View 菜单加 Standard Views
- **怎么触发**：菜单 **View → Standard Views → Front / Back / Left / Right / Top / Bottom**
- **看什么 / 通过判据**：
  - 点 Front → 相机正对 -Z 看（场景从 +Z 正视）；Top → 俯视（从上往下看 XY 面）；Right/Left/Back 各沿对应世界轴
  - pivot 和距离（radius）保持不变，只换角度
  - Top/Bottom 不会翻转/抖动（钳到 ±89° 避 gimbal）
- **背景**：低 dogfood 风险（只设相机角度，对就是对）。对 2.5D 对齐 / 摆位有用。注：当前仍是透视投影（未切正交），后续可加 ortho 切换。

### ✅ 28. Hierarchy 右键 Reset Transform

- **commit**：`f1f25c1` feat(editor): Hierarchy 右键 Reset Transform（可 Undo）
- **怎么触发**：Entity Tree 里右键一个实体 → **Reset Transform**
- **看什么 / 通过判据**：
  - 把该实体的 position 归 0、rotation 归单位、scale 归 1（回到本地 identity）；viewport 里物体跳回父空间原点 / 无旋转 / 原始大小
  - **Ctrl+Z 可撤销**（恢复重置前的 transform）
  - Inspector 的 Position/Rotation/Scale 同步刷新成 0/0/1（rotation euler 也对）
  - 无 TransformComponent 的实体该项灰禁
- **背景**：导入模型 transform 异常 / 手滑挪偏后一键归位，对齐 Unity 的 Transform → Reset。**重置 + undo + Inspector 刷新待真机确认**。

### ✅29. Edit 菜单 Duplicate / Delete 入口（低风险）

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

### ✅ 31. Transform 层级传播（mesh 路径）—— 移动父节点带动子 mesh

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
    - **光源全 3 类 + halo + 后处理体积 + 装饰 overlay 已随父传播 ✅**：平行光方向 + 点光/聚光位置 + 聚光方向 + 点光 halo 球 + PostProcess local volume + 所有装饰 gizmo overlay（光源箭头/圆环/锥体 + 相机 frustum + emitter spawn box + volume box）都跟随父变换。root 实体零回归。
    - **仍未切：transform gizmo（移动/旋转/缩放手柄）+ 物理 collider（2D）**——这两个是"写/apply"类（需 world→local）：parent 到非原点父下，**transform gizmo 手柄画在 local 偏移处（不在 mesh 上）+ 拖动按 local 空间**（picking 选中仍对、Inspector 编辑仍对，只是手柄位置/拖动方向偏）；collider 不随父动（mesh+灯+overlay 都随）。这俩是编辑器主操作工具 + 双向 sim，需 world→local apply，留专门 session 做 + dogfood。glTF 导入的灯（通常 root 子）仍对；非 root 导入灯方向可能偏（rare）。
  - **reparent keep-world ✅**（A1.3 主 DnD 路径已落）：把 B 拖到**已移动过**（非原点）的 A 下，B **应保持原世界位置不跳**（keep-world 重算 local）；Ctrl+Z 撤回 B 也回原位。**dogfood 重点验**：拖 reparent 到移动过的父，物体不跳位 + undo 还原。（注：duplicate/Ctrl+D 的 reparent 不走 keep-world——clone 复制原 local，行为同原对象。）

> **A1 剩余 consumer 完成清单**（给后续 session）：gizmo 放置+拖动读 world / world→local apply（防 parented 拖偏）· A1.3 reparent keep-world（重算 local 防跳）· 光源方向读 world rotation（+ importer 去 world-dir 编码改 R 桥接）· 物理 collider 读 world（2D，subtler）。详见 `docs/maturity-roadmap.md` A1.1 step 2。

## 2026-06-02 session（成熟度地基 B2.1：动画 clip 数据模型）

### 32. 动画 clip 数据层（B2.1）—— **当前纯数据，全 headless 已验，暂无需 dogfood**

- 已落地的 `AnimationClip` / `SampleTrack` / `ProceduralAnimator::AddDataChannel` + keyframe
  CRUD 数据原语（Sort/Duration/Wrap/Add/Find/Remove/Move）**全是纯数据 + 纯函数**，
  AnimationClipTest 13 例 headless 覆盖采样/插值/边界/CRUD——**这层不需要人工 dogfood**。
- **dogfood 触发点在后续 increment 落地后**（预登记，届时打开对应项）：
  - ⏳ **ClipAnimator 写 Transform**：clip 驱动实体 pos/rot/scale 后，Play/scrub 看实体按曲线
    动；parent 到移动物体的实体动画**叠加父变换正确**（靠 A1 hierarchy 传播）。
  - ⏳ **Timeline / dopesheet UI**：拖 playhead scrub 预览实时跟随；dopesheet 上拖 key 改时间、
    点选/删除 key、打键（K）插入——**交互手感全靠真机**（参 transform gizmo 的"读代码判能用
    会翻车"教训）。
  - ⏳ **曲线编辑器**：Bezier handle 拖动改缓动，曲线视图与实际插值一致。
  - ⏳ **ProceduralAnimator 数据 channel 实时可见**：把一条 .anim track 喂给 slime 材质
    uniform，Play 看 shader 效果按曲线变化（pbr 材质 UBO 已接，应可见）。

> 其中第一项「ClipAnimator 写 Transform」已落地 → 见下方 item 33（视觉待 dogfood）。

## 2026-06-02 session（成熟度地基 B2.2：ClipAnimator + .anim 序列化）

### 33. ClipAnimator —— 数据曲线驱动实体 Transform（编辑器 Play 可见）

- **commit**：`0951a50`（ClipAnimator core）+ `a79cef3`（scene round-trip）+ `ddc2e3c`（SeedDemoWorld demo）；序列化 commit 同 session。
- **怎么触发**（⚠️ 新实体在 `SeedDemoWorld` 里，**现有 demo.scene.json 不含它**）：
  1. 把 `assets/scenes/demo.scene.json` **临时挪开 / 改名**（保留你的本地 dogfood 状态用副本），
     使启动时 `Scene::Load` 失败 → 回退 `SeedDemoWorld`（main.cpp:778-782 的回退路径）。
  2. 启动 OrangeEditor（`build/bin/Debug/OrangeEditor.exe`）→ 加载 seeded demo world。
  3. 看 viewport 里名为 **"Animated Cube (clip)"** 的 cube（位置约 `(-0.8, 1.4, 0.5)`，
     Slime Doll 左侧对称处）。
  4. 点 **Play**（进 PlayState::Play）。
- **看什么 / 通过判据**：
  - cube **上下浮动**（position.y 在 1.4↔1.9 之间，2s 一个来回）**同时绕 Y 自旋**（2s 转一圈），
    动作平滑连续、loop 无跳变（rotation 0°↔360° 接缝无视觉跳）。
  - 点 **Stop / 回 Edit** 后停在某一帧（仅 Play 模式 tick）。
  - 选中 cube → Inspector 的 Animator 段 backend = `clip`。
  - **Save 场景 → 重启 → Load → 再 Play，bob+spin 行为一致**（验证 "clip" backend 形态 B
    嵌入 clipJson + Load 重建 + target 自动接回 self Transform 的 round-trip；此路径
    headless 已由 `scene_serialization_test::TestClipAnimatorRoundTrip` 锁住，**人工再确认
    Play 快照/Stop 还原后不退化**）。
  - （可选叠加 A1）若把另一实体 reparent 到该 cube 下，Play 时子实体应随 cube 的
    bob+spin 一起动（ClipAnimator 写本地 + TransformSystem 累积父变换）。
- **背景 / 对照**：与 item 1 的 Slime Doll（ProceduralAnimator 写 material uniform）并列，
  这条是引擎"双后端"中**写 Transform** 的路径。clip 此处由 `SeedDemoWorld` 内联构造；实际
  工程里 clip 来自 timeline 编辑 + `.anim` 资产（序列化已就绪：`AnimationClipToJson` /
  `LoadAnimationClip`，schema `animation/Clip` v1.0）。
- **关键确认点**：这是首游"流体史莱姆 = 代码/数据驱动动画"路线里 **关键帧动画**地基；
  Transform 动画的视觉正确性（缓动手感、loop 接缝、与渲染同步）务必真机确认。

### 34. ClipAnimator 运行时特性（事件 / 过渡混合 / 速率）—— **headless 全测，手感待游戏消费时 dogfood**

- **commits**：`4e3afee`（动画事件）/ `0f3a86f`（SetSpeed）/ `aa66fab`（Progress）/ `12a3e5a`（CrossFadeTo）。
- 已 headless 全测（`clip_animator_test` 17 例 + 序列化），**这层不需要现在专门 dogfood**；
  **触发点在游戏/demo 真实消费后**（预登记，届时打开）：
  - ⏳ **动画事件**：boss 攻击 clip 第 N 秒触发"生成判定 / 脚步声"——游戏侧接 `SetEventCallback`
    后，**事件时刻与视觉帧对齐**（判定不早不晚）、loop clip 每圈触发一次、倒放/scrub 不误触发。
  - ⏳ **过渡混合 CrossFadeTo**（2026-06-02 升级为**真两-clip cross-fade**，commit `62cd25c`）：
    idle↔walk↔attack 切换时**视觉平滑无 pop**（起点不跳）、fade 时长手感合适、rotation slerp 走
    最短弧不翻转；**出场动画在 fade 期间仍在播放**（如从 run 淡入 jump 时，腿部摆动在淡出过程里
    继续动、非定格一帧）——这是真两-clip 相对旧冻结-pose MVP 的核心视觉差异，重点确认。
  - ⏳ **播放速率 SetSpeed**：slow-mo（boss 蓄力）/ 倒放调试视觉正确；速率 0 等价暂停。
- 这些是**首游 boss 战动作时序**的运行时地基（GAP-2026-06-01 列 boss 攻击为硬需求）；
  数据/序列化/运行时已就绪，缺的是游戏侧脚本消费（C# PIE 落地后）+ 编辑器创作 UI（B2.3/B2.6 spec）。

---

## 2026-06-02 session（动画运行时补完：Bezier 真时序缓动 + 真两-clip cross-fade）

### 35. Animated Cube 的 bob 改 Bezier ease-in-out —— 真时序缓动端到端可见

- **commits**：`15a991a`（InterpMode::Bezier 升级真 cubic-bezier 时序缓动）/ `62cd25c`（CrossFadeTo
  真两-clip）/ demo 接线（本 session 后续 commit，DemoWorld.cpp 的 "Animated Cube (clip)" 把 bob
  轨道从 Linear 改 Bezier ease-in-out `cubic-bezier(0.42,0,0.58,1)`）。
- **怎么触发**：与 item 33 同——把 `assets/scenes/demo.scene.json` 暂时挪开 → 启动 OrangeEditor
  触发 `SeedDemoWorld` → Enter Play → 看 "Animated Cube (clip)"。
- **看什么**：
  - cube 上下浮动（bob）现在在**顶/底缓动、中段加速**（ease-in-out 时序），相比旧的匀速线性
    更**有机、不机械**——这是 `InterpMode::Bezier` 真时序缓动（切线时间方向 .x 真正参与）落到
    端到端路径 clip → ClipAnimator → 本地 Transform → 渲染的视觉确认点。
  - spin（绕 Y）仍是 Linear 匀速（旋转匀速才自然，未改）。
- **背景**：Bezier 缓动是动画 "juice" 的核心（首游 Ori-like 平台跳跃尤其依赖 squash-stretch /
  anticipation 的非线性时序）。headless 已全测（`animation_clip_test` 6 例时序覆盖：ease-in/out/
  in-out 对称 / 单调 / overshoot 回弹），此处仅 dogfood 端到端**视觉手感**。
- **关键确认点**：缓动方向对（不是反的：应顶/底慢、中段快）；loop 接缝处（t=2→0）无突跳。

---

## 2026-06-03 session（B2.6：AnimatorComponent 挂 ClipAnimator + .anim 引用 + 播放控制）

> 目标：把 B2 动画 clip 创作变成编辑器里**可挂、可引用、可播放控制**的功能（B2.3 timeline 的前置）。spec：`docs/b2.6-animator-clip-authoring-spec.md`。本段全是 ImGui 交互 / viewport 视觉，headless 测不到，逐条 dogfood。
>
> headless 已验：OrangeEditor.exe 编出 + 全量 ctest 85/85（含新增 `clip_animator_asset_reassign_test` 锁住 clipSet 数据序列 + `editor_build_smoke`）+ check_invariants OK（无新 hardcode / schema-first 合规）。

### 36. +Add Component → "Animator (Clip)" —— 挂空 ClipAnimator

- **改点**：spec 改点 4。schema `AddableWith`（Renderable c10 同款自定义 add 路径）建空 ClipAnimator（`AnimationClip{}` + target=self Transform）。displayName "Animator (Clip)"，typeName 仍 "Animator"。
- **怎么触发**：选中一个实体（确保有 TransformComponent）→ Inspector 底部 **+Add Component** → 菜单里点 **"Animator (Clip)"**。
- **看什么 / 通过判据**：
  - 实体多出 **Animator** 段，backend = `clip`（只读 Backend 字段显示 "clip"）。
  - 段内出现 **Clip**（AssetRef，初始 "(none)"）+ **Loop** checkbox（仅 clip backend 可见）。
  - 已挂 Animator 的实体，菜单里 "Animator (Clip)" 不再出现（schema.has 过滤）。
  - **Skeletal / Procedural 仍不在 +Add 菜单**（只开 clip）。
  - 段头右键 → **Remove Component** 可删（Removable）；删后 Ctrl+Z 还原（命令栈 CaptureComponentState 认 clip AssetRef + loop bool）。

### 37. 拖 .anim 进 Clip 字段 —— clip 引用 / 重指派

- **改点**：spec 改点 2（AssetKind::AnimationClip + 资产浏览器认 .anim）+ 改点 3（clip FieldAssetRef）。clipGet 读 `ClipAnimator::SourceAssetPath()`；clipSet 解析 path → `AssetRegistry::Load<AnimationClip>` → Get → `SetClip(*loaded)` + `SetSourceAssetPath`。
- **前置**：需要一个 `.anim` 文件在 `assets/` 下（当前无内置 .anim demo 资产——可用 headless 序列化造一个，或等 B2.3 timeline 能存 .anim 后再 dogfood）。
- **怎么触发**：
  1. 资产浏览器进有 `.anim` 的目录 → 应看到 **[Anim]** 图标的卡片（类型过滤下拉新增 **"Animation"** 项）。
  2. 把 `.anim` **拖到** Animator 段的 **Clip** 字段（或选中 .anim 后点字段的 **Pick** 按钮）。
- **看什么 / 通过判据**：
  - Clip 字段显示 `.anim` 短名（hover 看完整路径 tooltip）。
  - scrub slider 范围变成新 clip 的 duration（见 item 38）。
  - **Ctrl+Z** 撤回重指派（命令栈 SetFieldValueCommand<string>）；**×** 清除字段 → 回 "(none)" + 空 clip。
  - **数据序列已 headless 锁住**（`clip_animator_asset_reassign_test`：Load→Get→SetClip+SetSourceAssetPath→duration/source path/Seek pose/清空 全验）；**拖放 / Pick / Undo 的 GUI 手感待真机**。

### 38. Play / Pause + playhead scrub —— 编辑期预览 tick（**重点 dogfood**）

- **改点**：spec 改点 3 播放控制 + 核心新机制"编辑期预览 tick"。控件在 `AnimatorMiniPreviewPlugin::ParseEnd`（clip backend 时渲 Play/Pause 按钮 + scrub slider）；预览状态在新 `EditorAnimationPreviewState`（host.animPreview，**不序列化**）；tick 在 `EditorRenderLayer::OnUpdate` 的 Edit 分支只对单个 animator 推进。
- **怎么触发**：选中一个挂了 clip（引用了真 .anim、duration>0）的实体 → Inspector Animator 段底部。
- **看什么 / 通过判据**：
  - **scrub slider**：拖动 → 实体按 clip 曲线**实时变位姿**（直接 Seek，Edit 模式即可见，不依赖 Play 按钮）。
  - **Play 按钮**：点后实体在 **Edit 模式**自动按 clip 动（编辑期预览 tick，**不进 PlayState::Play**）；"(previewing)" 提示出现。
  - **Pause 按钮**：停在当前帧（pose 不归位）。
  - **切换选中到另一个实体** → 旧预览停止 + 旧 animator **归位 t0**（Seek(0)）。
  - **与 PlayState::Play 互斥（关键）**：进 Play（点工具栏 Play）前预览自动停 + 归位；Play 期由全量 TickAnimators 驱动，Inspector 的 Play/Pause/scrub **灰禁**（仅 Edit 模式可用）。Stop 回 Edit 后预览态已清空（不会残留双 tick）。
  - **Undo/Redo / 切场景**：删掉被预览的 animator（Undo）/ New / Open 场景 → 预览态自动清（不持野指针）。
- **背景**：编辑期预览 tick 是**新机制**（B2.3 timeline 也复用）。务必验证：① 预览真能在 Edit 模式动；② 切走 / Stop 归位；③ **绝不与 Play 模式全量 tick 并存双写 elapsed**（若发现进 Play 后动画"跳"或 Stop 后还在动，是互斥没做对，告诉我）。

> **dogfood 阻碍**：当前 `assets/` 无内置 `.anim` 资产（SeedDemoWorld 的 "Animated Cube (clip)" 是内联 clip，非 .anim 引用）。item 37/38 的完整 dogfood 需要先有一个 .anim 文件——可临时用 headless 序列化（`SaveAnimationClip`）造一个放进 `assets/`，或等 B2.3 timeline 落地后能在编辑器内存 .anim 再走完整闭环。item 36（+Add Component 挂空 ClipAnimator）+ scrub（空 clip duration=0 时禁用，有内联 clip 实体时可拖）可先验。

---

## 2026-06-03 session（B2.3：timeline / dopesheet 面板）

> 目标：把底部 Animation 面板从占位（v0.7 留的 2 行 TextDisabled）升级成**编辑选中实体 ClipAnimator clip 的时间轴**。建在 B2.6（`8e43084`，animator 可挂 + 引用 .anim + 编辑期预览 tick）之上。spec：`docs/b2.3-timeline-dopesheet-spec.md`。
>
> headless 已验：OrangeEditor.exe 编出（`/W4 /WX` 零警告新增 TU）+ 全量 ctest 86/86（含新增 `timeline_edit_primitives_test` 锁住命令 do/undo 对称 + clip CRUD 原语 + .anim 写回保真 + `editor_build_smoke` standalone 消费）+ check_invariants OK（无新 hardcode / 像素字面量，颜色全走 Theme token、尺寸全走 GetContentRegionAvail / CalcTextSize / 字号派生）。
>
> 架构关键：clip 编辑走 **copy-modify-SetClip 命令**（`command/SetAnimationClipCommand.{h,cpp}`，设计点 1b，不加 MutableClip）——每次编辑 = 拷当前 clip → 副本上调 AnimationClip.h 原语 → RecomputeDuration → 新建命令压栈，do/undo 都 SetClip。连续拖键 merge 成一条（参 MoveEntityCommand merge）。▶/⏸ **复用 B2.6 的 host.animPreview**（不新造 tick 路径）。所有 ImGui 像素 / 拖拽 / hit-test headless 测不到，逐条 dogfood（参"读代码判能用会翻车"教训）。
>
> **dogfood 阻碍同 B2.6 item 37/38**：`assets/` 无内置 `.anim` 资产 → 完整闭环（含资产化 clip 的 "Save to .anim" 写回）需先有一个 .anim 文件。可临时用 headless `SaveAnimationClip` 造一个放 `assets/`，**或直接用 B2.3 本身**——选 SeedDemoWorld 的 "Animated Cube (clip)"（内联 clip，无 source）即可验大部分 timeline 编辑（只 "Save to .anim" 按钮不出现，因内联 clip 随 scene 存 clipJson 不需写回）。

### 39. 空态 + 只读 timeline 绘制 —— 轨道行 / 关键帧点 / playhead / 标尺

- **改点**：`tools/OrangeEditor/panels/AnimationTimelinePanel.cpp` 的 `DrawAnimationPanel`（从 static 改成员函数，需访问 mHost）。
- **怎么触发**：
  1. 底部把 **Animation** tab 切到前台（与 Assets / Console 同 slot）。
  2. **空态**：未选实体 / 选中实体没有 clip backend → 看友好提示文案。
  3. **有 clip**：选中挂了 clip（内联或引用 .anim、duration>0）的实体（如 SeedDemoWorld 的 "Animated Cube (clip)"，需先把 demo.scene.json 挪开触发 seed，见 item 33）。
- **看什么 / 通过判据**：
  - 空态两种提示分别正确（无选中 vs 有选中但无 clip backend），文案友好不报错。
  - 有 clip 时画出：**左列轨道标签**（每 track 一行 targetName，如 `position.y` / `rotation`）+ **关键帧菱形**（按 time 横向定位，duration 映射到时间轴宽度）+ **顶部时间标尺**（0 / 中 / 末三档刻度 + 秒数）+ **playhead 竖线游标**（橙色，顶部三角）。
  - 行交替底色 + 标签列分隔线清晰；菱形位置与 key.time 成比例（首键贴左、末键贴右）。
  - transport 行：▶/⏸/⏹ + Loop checkbox + speed DragFloat + `t = X / Y` readout 都在。
- **背景**：只读层验"画得对"——headless 已验数据映射（time→x、duration），但**像素布局 / 菱形位置 / 标尺读数视觉只能真机**。若菱形错位 / 标签串行 / playhead 偏，告诉我（布局 helper 在 TU 顶 `RowHeight`/`KeyRadius`/`TimeToScreenX`）。

### 40. playhead scrub —— 拖游标实体实时跟随

- **怎么触发**：有 clip 的实体选中（Edit 模式），在 timeline 时间轴区（标签列右侧）**点击 / 拖动空白处**（非关键帧 / 非事件）。
- **看什么 / 通过判据**：
  - 点击 / 拖动 → playhead 跳到点击时刻 + **实体按 clip 曲线实时变位姿**（直接 `Seek(t)`，Edit 模式写 Transform 即时可见，不依赖 ▶）。
  - 拖动连续 scrub → 实体平滑跟随（如 "Animated Cube" 上下浮 + 自旋随 playhead 走）。
  - **仅 Edit 模式可用**：进 Play（工具栏 Play）后 timeline 交互全禁（与 B2.6 预览互斥纪律一致）。
- **背景**：scrub 是 timeline 最高频交互。**拖拽手感 + 实体跟随实时性靠真机**（hit-test 区分点中 key vs 空白的容差是否合适，告诉我调 `HitRadius`）。

### 41. 打键 / 删键 —— K 插入 + Del 删除（命令栈）

- **怎么触发**（面板需聚焦）：
  - **打键**：移 playhead 到某时刻（scrub）→ 点某轨道行选中它（点中该轨的某 key，或默认第一条轨）→ 按 **K** 或底部 **Key (K)** 按钮。
  - **删键**：点中一个关键帧菱形（变橙高亮）→ 按 **Del**。
- **看什么 / 通过判据**：
  - 打键 → 在 playhead 时刻该轨出现新菱形；新键值 = 当前曲线在该时刻的采样值（**打键不跳变**，曲线视觉连续）。同时刻重打覆盖。
  - 删键 → 选中菱形消失；曲线在该段重新插值。
  - **Ctrl+Z 一步回退**（打键撤回 = 删该键；删键撤回 = 恢复该键），Redo 恢复。每次打 / 删各占一条撤销步（离散编辑唯一 merge key，不互相合并）。
- **背景**：走 `SetAnimationClipCommand`（整 clip 快照命令）。**命令 do/undo 数据语义 headless 已锁**（`timeline_edit_primitives_test` 段 1/3）；**K/Del 键捕获时机 + 选中高亮 + Undo 逐步真机验**。注意 K/Del 仅在 Animation 面板聚焦时响应（避免与 viewport Del 删实体冲突）。

### 42. 拖关键帧改时间 —— 水平拖 + 连续拖 merge 成一条

- **怎么触发**：点中一个关键帧菱形 → **水平拖动**到新时刻松手。
- **看什么 / 通过判据**：
  - 拖动中菱形跟随鼠标横移，松手后 key.time = 落点时刻；曲线随之重定时；duration 若末键被拖远则**实时重算**（标尺末档跟着变）。
  - **连续拖动整段在撤销栈只留一条**（merge：拖动期每帧 push 同 merge key 命令合并），Ctrl+Z 一步回到拖动起点（参 MoveEntityCommand merge 心智）。
  - 拖过相邻 key 时维持升序（MoveKeyframeTime 重排）；拖动期选中跟踪正确（不丢选）。
- **背景**：**拖拽 + merge headless 测不到**（数据层 MoveKeyframeTime + duration 重算已由 `timeline_edit_primitives_test` 段 2 锁）。重点验：① 拖动跟手；② **整段拖动 Undo 一步回退**（不是每帧一条）；③ 拖动后选中不丢。若拖动卡顿 / Undo 要按多次 / 拖完选丢，告诉我。

### 43. ▶ 播放预览 —— 复用 B2.6 host.animPreview（不新造 tick）

- **怎么触发**：有 clip 的实体选中（Edit 模式）→ timeline transport 行点 **▶**（播放）/ 再点变 **⏸**（暂停）/ **⏹**（停止归位）。
- **看什么 / 通过判据**：
  - ▶ → 实体在 **Edit 模式**自动按 clip 动（编辑期预览 tick，**不进 PlayState::Play**）；playhead 随之推进；按钮变 ⏸。
  - ⏸ → 停在当前帧（pose 不归位）；⏹ → 归位 t0 + 停。
  - **与 B2.6 Inspector 的 Play/Pause 同一套 host.animPreview**：在 Inspector 点 Play 和在 timeline 点 ▶ 驱动同一预览态（切到另一实体自动接管）。
  - **与 PlayState::Play 互斥**：进 Play 前预览自动停 + 归位；Play 期 timeline 交互灰禁。
- **背景**：**复用 B2.6 已落地的预览 tick**（`EditorRenderLayer::OnUpdate` Edit 分支单 animator 推进），本面板只置 `host.animPreview.previewPlaying`。务必验证与 B2.6 item 38 行为一致（不双 tick / 切走归位 / 与 Play 互斥）。

### 44. 轨道增删 + .anim 写回 —— Add/Remove Track + Save to .anim

- **怎么触发**（底部工具行，Edit 模式）：
  - **加轨道**：选 targetName 下拉（position / position.x / rotation / scale / scale.uniform 等约定名）→ **Add Track**。
  - **删轨道**：点中某轨道的一个 key 选中该轨 → **Remove Track**。
  - **写回**（仅引用 .anim 的 clip）：编辑后点 transport 行的 **Save to .anim** 按钮。
- **看什么 / 通过判据**：
  - Add Track → timeline 多一行（空轨，无 key），可在其上打键。Remove Track → 该行消失。都可 Ctrl+Z。
  - **资产化 clip（引用 .anim，SourceAssetPath 非空）**：transport 行出现 **Save to .anim** 按钮 + "(资产化 clip：编辑后须写回 .anim)" 提示；点它把 in-memory clip 写回 .anim 文件（Console 打 `已写回 <path>`）。重开该 .anim（或重 Load 引用它的 scene）→ 编辑仍在。
  - **内联 clip（无 source，随 scene 存 clipJson）**：**不出现** Save to .anim 按钮（编辑随 scene 存即可，无需单独写回）。
- **背景**：UpsertTrack/RemoveTrack do/undo + SaveAnimationClip round-trip **headless 已锁**（`timeline_edit_primitives_test` 段 4/6）；**Add/Remove Track 工具行交互 + .anim 写回后重开保真 + 内联 vs 资产化 clip 分支的按钮可见性真机验**。注意：内联 clip 改动若不保存 scene 会随关闭丢（与所有 scene 编辑一致）。

### 45. 事件 marker 行 —— 加 / 删 / 拖 / 改名事件

- **怎么触发**（Edit 模式）：
  - **加事件**：移 playhead 到某时刻 → 底部 **Add Event**（在 playhead 时刻加一个名 "event" 的事件）。
  - **选 / 拖**：点中标尺行内的事件三角 marker（变橙高亮）→ 水平拖改 time。
  - **改名**：选中事件 → 工具行下方出现 **Event Name** InputText，输入新名回车。
  - **删**：选中事件 → 按 **Del**。
- **看什么 / 通过判据**：
  - 事件以**小三角 marker** 显示在顶部标尺行（按 time 横向定位 + 旁边显示 name）。
  - 加 → marker 出现在 playhead 处；拖 → marker 横移改 time（维持升序）；改名 → marker 旁文字变；删 → marker 消失。全部 Ctrl+Z 可撤。
  - **Del 优先删选中事件**（若有事件选中），否则删选中 key——两者不冲突。
- **背景**：用已落地的 `AddClipEvent`/`RemoveClipEvent` 事件原语（B2.2）。**事件 do/undo headless 已锁**（`timeline_edit_primitives_test` 段 5）；**marker 绘制位置 + 点选/拖动 hit-test + 改名 InputText 手感真机验**。事件最终供游戏侧 `SetEventCallback`（boss 攻击判定时序），timeline 是其创作壳。

> **B2.3 不含**（后续单独做）：曲线编辑器（B2.4，Bezier handle 拖动改 inTangent/outTangent.y 缓动）；状态机图编辑器（B2.5）。本面板是 dopesheet（key 时间编辑），不是 curve editor（key 值/缓动编辑）。

---

## 2026-06-03 session（B2.4：曲线编辑器）

> 目标：B2.3 timeline 的收尾件——在 Animation 面板加**曲线编辑器视图**，让用户可视化编辑关键帧的 Bezier 缓动手柄。建在 B2.3（`98d543c`）之上。spec：`docs/b2.3-timeline-dopesheet-spec.md` 实施顺序第 6 步。
>
> headless 已验：OrangeEditor.exe 编出（`/W4 /WX` 零警告）+ 全量 ctest 87/87（含新增 `curve_editor_primitives_test` 锁住手柄屏幕落点↔切线逆/正运算 round-trip + 改 inTangent/outTangent → SampleTrack 值按预期变 + do/undo 对称 + 段隔离 + `editor_build_smoke`）+ check_invariants OK（无新 hardcode / 像素字面量，颜色全走 Theme token、尺寸全派生 GetContentRegionAvail / CalcTextSize / 字号）。
>
> 架构关键：① 曲线**用 SampleTrack 密集采样画折线**（display == playback 的正确性核心，不自己重算插值）；② 切线手柄屏幕位置按 CubicBezierEase 控制柄约定从 outTangent/inTangent 推算（单位方框 (0,0)=k0、(1,1)=k1，out 控制柄 c1=outTangent、in 控制柄 c2=(1,1)+inTangent），拖手柄走与之一致的逆运算反推切线；③ 编辑全走 **`SetAnimationClipCommand`**（复用 B2.3，copy-modify-SetClip 整快照命令；连续拖同一手柄按 merge key `anim_curve_handle:<track>:<key>:<handle>` 合并一条），改完曲线实时重画（SampleTrack 读新切线）；④ ▶/⏸/⏹ + scrub + 模式切换复用 B2.3 transport 行 + host.animPreview。所有 ImGui 像素 / 拖拽 / hit-test headless 测不到，逐条 dogfood（参"读代码判能用会翻车"教训）。
>
> **dogfood 阻碍同 B2.3**：完整闭环（资产化 clip "Save to .anim"）需先有 `.anim` 文件，但曲线编辑本身可直接用 SeedDemoWorld 的 "Animated Cube (clip)"（内联 clip，bob 轨道已是 Bezier ease-in-out，见 item 35）验——选它 → Animation 面板 → 切 Curve 模式即可拖手柄。

### 46. Dopesheet ↔ Curve 模式切换 + 曲线绘制（用 SampleTrack）

- **改点**：`tools/OrangeEditor/panels/AnimationTimelinePanel.cpp` 的 `DrawTransportRow`（加模式切换按钮）+ 新 `DrawCurveEditor` 成员函数（curve 模式整块绘制 + 交互）。
- **怎么触发**：
  1. 选中挂了 clip（内联或引用 .anim、duration>0、有 Bezier key）的实体（如 SeedDemoWorld 的 "Animated Cube (clip)"，需先把 demo.scene.json 挪开触发 seed，见 item 33）。
  2. Animation 面板 transport 行点 **"Curve >"** 按钮 → 切到曲线视图；再点 **"< Dopesheet"** 切回。
- **看什么 / 通过判据**：
  - 切到 Curve 模式后画出：**横轴 time、纵轴 value 的曲线**（左侧值标签 min/mid/max + 网格线）+ 每个 key 的菱形点 + playhead 竖线。
  - **曲线形状与实际播放插值完全一致**（关键正确性）：曲线用 `SampleTrack` 密集采样画——bob 轨道的 Bezier ease-in-out 在曲线视图里应是**顶/底缓、中段陡**的 S 形（与 item 35 的实体运动手感对应）；Linear 段是直线、Step 段是阶梯。**若曲线形状与 Play 时实体运动不符，告诉我**（display==playback 是本任务核心，不符说明绘制没用 SampleTrack 或采样有 bug）。
  - track 选择下拉（左上）可切看哪条轨道的曲线（沿用选中 track）。
- **背景**：曲线视图用 SampleTrack 画是 display==playback 一致性的核心。headless 无 GUI 绘制路径，**曲线形状视觉 / 模式切换按钮 / track 下拉手感待真机**。

### 47. Bezier 切线手柄拖动改缓动（**重点 dogfood**）

- **怎么触发**（Curve 模式，Edit 模式）：找一个 **InterpMode::Bezier** 的 key（如 bob 轨道的 key），它会显示**橙色切线手柄**（一条线 + 末端小圆点）——out 手柄（从本 key 出发控制后一段）/ in 手柄（落到本 key 控制前一段）。**拖动手柄圆点**。
- **看什么 / 通过判据**：
  - 拖 **out 手柄往上** → 该段缓动**值方向抬升**（outTangent.y 增大），曲线在该段隆起、SampleTrack 中点变高 → 实体在该段运动幅度变化；拖 **左右** → 改时间方向缓动（outTangent.x，ease-in/out 时序），曲线时序变（顶/底缓的程度变）。
  - 拖 **in 手柄** → 同理改 inTangent（落到本 key 的那段的收尾缓动）。
  - **手柄拖动即时重画曲线**（因 SampleTrack 读新切线）+ 实体在 scrub/预览时按新缓动动。
  - **连续拖同一手柄 Ctrl+Z 一步回退**（merge：拖动期每帧 push 同 merge key 命令合并），不是每帧一条。
  - 拖 out 手柄时间方向夹在 [0,1]（不越过段）、in 手柄夹在 [-1,0]（指回前帧）；值方向不夹（可拖出 overshoot 回弹）。
  - **平直段（相邻两 key 值相同，如 bob 顶点附近）**：值方向 .y 无法从屏幕落点反推（dv≈0），拖手柄的值方向不变——这是预期（除零保护），时间方向仍可拖。
- **背景**：手柄屏幕↔切线值的正逆映射是本任务摩擦点。**逆运算数据正确性 headless 已锁**（`curve_editor_primitives_test`：手柄落点→切线→落点 round-trip + 改 outTangent.y → SampleTrack 中点抬高 + ease-out 时序 + overshoot do/undo）；**手柄 hit-test 容差 / 拖动跟手 / 整段拖动 Undo 一步 / 改完曲线实时重画的视觉待真机**。若手柄拖不动（hit-test 太小）/ 拖动方向反了 / Undo 要按多次，告诉我（调 `HitRadius` / 检查 Solve*Tangent 符号 / merge key）。

### 48. 右键 key 切 InterpMode（Step / Linear / Bezier）

- **怎么触发**（Curve 模式，Edit 模式）：**右键**曲线视图里的一个 key 菱形 → 弹 "Interpolation" 菜单 → 选 Step / Linear / Bezier。
- **看什么 / 通过判据**：
  - 切 **Bezier** → 该 key 出现可拖的橙色切线手柄（若原切线全零，自动给个**平滑默认** `cubic-bezier(0.42,0,0.58,1)` 同款，曲线立刻可见缓动而非退化线性）。
  - 切 **Linear** → 手柄消失，该段变直线；切 **Step** → 该段变阶梯（保持 k0 值到 k1）。
  - 曲线形状随之实时变（SampleTrack 读新 interp）；当前 interp 在菜单里打勾。
  - **Ctrl+Z 撤回**模式切换（走 SetAnimationClipCommand）。
  - 非 Bezier 段（Step/Linear）**无切线手柄**（只读，底部提示 "右键 key 切 Bezier"）。
- **背景**：右键菜单 + 默认平滑切线是 spec 第 6 步的"可选"项，已实现。**右键 hit-test + 菜单交互 + 默认切线视觉待真机**。

> **B2.4 实现细节**（dogfood 时参考）：曲线模式与 dopesheet 共享 `sTimelineSel`（选中 track/key）+ transport 行 + host.animPreview 预览 tick。曲线视图**只编辑 key 的值/缓动维度**（切线 + interp），**key 的时间编辑仍留 dopesheet**（点 key 只选中不拖时间）——curve 编辑 value/缓动、dopesheet 编辑 time，职责分明。多分量 track（Vec3 如 position）当前画首个驱动分量的曲线 + 编辑它的缓动（多维共享同一标量时序缓动，spec 现状）。

---

## 维护约定

- 新 feature 落地后，若有"headless 绿但视觉/手感待验"的残留，追加到本文件对应 session 段。
- dogfood 通过的条目：在标题前加 ✅，并可在条目内记一句实测结果（截图路径 / 发现的问题）。
- dogfood 发现 bug：在 `docs/engine-known-gaps.md` 登记新 GAP / BUG，本条目内 link 过去。

## Bug

> **2026-06-10 修复批次**（结合早上的全仓 code-review `Orange-Ecosystem/.claude/reviews/code-review-2026-06-10.md`）：
> 见下方"## 2026-06-10 bug 修复批次"段。全量 ctest 92/92 绿，待重新 dogfood 确认。

- 1. Open Recent 会出现相同路径的场景文件 —— **✅ 已修**（`AddRecentScene` 改路径归一化去重：相对/绝对/斜杠不同的同一场景不再重复。重新 dogfood：多次以不同形式打开同一场景，列表应只一条置顶）
- 2. [31] Transform 层级传播（mesh 路径）—— 移动父节点带动子 mesh，但 —— ⚠️ 描述被截断，**待补充**：父节点带动子 mesh 后具体看到什么异常？（位置偏移 / 不跟随 / 跳变？）补全后单独排查。
- 3. [33] 打开后崩溃 —— **✅ 已定位并修复（真根因，非动画）**。用户提供崩溃栈确诊：`main → ImFontAtlas::AddFontFromFileTTF → ImGui::ErrorLog → BeginErrorTooltip → ImGui::Begin → IM_ASSERT(g.WithinFrameScope)`。根因链：**dogfood 指示"把 demo.scene.json 改名/挪开" → `main.cpp::ChdirToRepoRoot` 用单文件 `assets/scenes/demo.scene.json` 作仓库根标记，改名后找不到 → 不 chdir → cwd 停在 build/bin/Debug（VS 默认）→ 相对路径字体 `codicon.ttf` 加载失败 → ImGui 新版字体缺失不返回 null 而走 ErrorLog→Begin，初始化期（无 frame）触发 assert 崩溃**。修复两层（详见下方批次"动画崩溃"段）：① ChdirToRepoRoot 改用稳健标记（`assets/`+`src/` 目录同时存在，不受场景改名影响）；② 所有 AddFontFromFileTTF 前先查文件存在（缺失降级为 '?' 占位，绝不进 assert 路径）。已模拟用户场景（build/bin/Debug 作 cwd + demo.scene.json 缺失）验证不再崩。

---

## 2026-06-10 bug 修复批次

本批次 = dogfood-checklist 登记 bug + 早上全仓 code-review（`.claude/reviews/code-review-2026-06-10.md`）OrangeEngine 侧 20 条。全量 ctest **92/92** 绿、增量 build 全过。**所有改动 headless 已验，GUI/视觉残留待重新 dogfood**。OrangeRender 侧 6 条按 per-session 单子仓纪律仅登记到 `../OrangeRender/docs/incoming_bugs.md`（留独立 session 修）。

### 动画崩溃（item 33 "打开后崩溃"）—— ✅ 真根因已修
- **真根因（用户崩溃栈确诊，非动画代码）**：栈 = `main:601 → AddFontFromFileTTF → ImGui::ErrorLog → BeginErrorTooltip → ImGui::Begin → IM_ASSERT(g.WithinFrameScope)`。链条：item 33 dogfood 指示"把 demo.scene.json 改名/挪开" → `main.cpp::ChdirToRepoRoot` 用**单文件 `assets/scenes/demo.scene.json`** 作仓库根标记，改名后失配 → 不 chdir → cwd 停在 `build/bin/Debug`（VS 默认 cwd）→ **相对路径字体 `tools/OrangeEditor/theme/codicons/codicon.ttf` 加载失败** → ImGui 新版字体缺失不返回 null 而走 ErrorLog→Begin，初始化期（无 frame）触发 assert 崩溃（604 行的 null 检查根本到不了）。
- **修复两层**（`main.cpp`）：
  1. `ChdirToRepoRoot` 标记改 `is_directory(assets) && is_directory(src)`——`src/` 只在仓库根、绝不在 build 产物、与任何可改名场景文件无关，**改名/挪开任意场景都不影响 cwd 定位**。
  2. 所有 `AddFontFromFileTTF`（msyh.ttc / segoeui.ttf / codicon.ttf）前先 `std::filesystem::exists` 检查，缺失降级为返回 null / 跳过（icon '?' 占位，不致命），**绝不进 ImGui 缺失字体的 assert 路径**。
- **已验证**：模拟用户场景（`build/bin/Debug` 作 cwd + demo.scene.json 缺失）启动，SeedDemoWorld 正常加载、字体加载、跑满 25s 不崩。
- **教训**：① 用户可改名/删除的内容文件不适合做"仓库根/资产根"探测标记，应用结构性目录（src/）；② 相对路径资源加载在非仓库根 cwd 下会静默失败，且 ImGui 新版字体缺失走 assert 而非 null，必须前置 exists 检查。
- 另注：H1（Inspector 折叠 + 绘制 table 空指针崩）是**另一个独立真 bug**（见下 P0），也已修——它会在"折叠 Inspector + 选中 .material/实体"时崩，与本字体崩独立。
- 顺带修（bug-hunt agent 发现，关系到 timeline dogfood items 41/42/46/47）：timeline 拖 key 的 merge key 只含 track index（同轨连续拖两个不同 key 被错误合并成一条 Undo）→ 改用稳定拖动会话 id；curve 编辑器 rotation 轨恒显示 component 0（恒 0 平直线）→ 改为显示值跨度最大的分量。

### dogfood-checklist 登记 bug
- **item 9（Copy/Paste Values）**：
  - (a) "Hierarchy 没置灰" —— Inspector 组件头 Paste Values 的置灰门控（`canPaste` 按 `componentClipboard.typeName == schema.typeName` 比较）**当前代码正确**：跨类型 / 剪贴板空时 Paste 项灰禁。若 6月5 旧 exe 没置灰，rebuild 即修。**重测确认**；若指的是别处（如 Entity Tree 右键）请指明。
  - (b) "Paste 到 slime Transform 后位置变了但不显示" —— Paste 忠实复制源的 position/rotation/scale 整套；**若源实体在相机视野外 / scale 退化（接近 0）**，slime 会随之移出视野 / 缩到不可见（属"复制了什么就得到什么"，非 paste 本身的 bug）。**真实修复**：补了 Paste 后 invalidate Inspector 的 Euler 缓存（否则 rotation 字段下一帧仍显示 paste 前旧 Euler）。**重测**：若 paste 一个正常 scale≈1、视野内位置的 Transform 后 slime 仍消失，请告诉我源实体是谁。
- **item 10 / 13（单材质模型 drop 到 viewport 空白 / Add to Scene 后仍默认灰 PBR）**：**✅ 已修**。根因 = `CreateEntityFromMeshAsset` 只读 `.meta` 的 `subMeshMaterials`，而用户本地 Avocado 等**旧 .meta 无该字段**（导入早于该特性 + hash-skip 未重导）→ 解析空 → 回退默认灰。修复：`.meta` 无材质时**回退到 ADR-008 约定的同目录 `<stem>.material`**（存在才用）。Avocado.material 已 co-located，重测应带材质。（注：新导入的模型 .meta 已写 subMeshMaterials，无需 fallback；fallback 专治旧产物。）
- **item 17（按 Home 后 viewport Snap 勾选框橙色高亮）**：⏳ **未修（cosmetic，已定位）**。根因 = 编辑器启用了 `ImGuiConfigFlags_NavEnableKeyboard`，Home 是 ImGui 键盘导航键，在 NewFrame 的 NavUpdate 阶段就把 nav 焦点移到了 toolbar 首个可导航控件（Snap 勾选框）并画橙色 focus 环；我们在 Draw 里处理 Home（Frame All）已晚于 nav。**纯视觉**（Snap 状态不变、不影响功能）。干净修复需给 toolbar 控件加 `ImGuiItemFlags_NoNav`（imgui_internal）或调整全局 nav 配置，风险偏高，留后续。

### code-review OrangeEngine 侧（20 条，按严重度）
- **P0**：H1 Inspector 折叠空表崩（`InspectorPanel.cpp` Begin 守卫 + `MaterialAssetInspectorPlugin` 两处 `BeginPropertyTable` 返回值守卫）· H2 AnimFsm 链式 rename 丢失（Merge 内吸收新值前先 Undo）· H3 `AssetRegistry::WaitForErased` UAF（删持久 slot 引用、重索引）· M4 Remove Component / Copy-Paste undo 丢 String/Polygon/EdgeChain 顶点（`CaptureComponentState` 补三 case）。
- **P1**：H4/M12 AudioEngine move-assign 泄漏（Impl 析构 RAII 化）+ PlayOneShot 删死代码 · M5 AnimFsm 删 state 后清选中 transition · M1/M2 scale gizmo 用按下帧旋转基准 + follower 在 primary local 系缩放 · M6 scene load 回滚补 RemoveBody · M3/L3 LinkAsLastChild 空解引用兜底 + 环上界 · M8 Box2DBridge EdgeChain count 上界 clamp。
- **P2**：L4 CommandStack EndGroup merge 前截断 redo · L5 Pipeline RTT 无相机返回 InvalidArgument · L6 PrefabOverride float key 改 bit-pattern 防碰撞 · L7 AnimationStateMachine Tick 拷 to/condition 防回调再入 UAF · L2 EditorCameraControl near/far 改每帧按 radius 推导（不持久化）。
- **未修留待**：L1 EditorRotateGizmo 圆环侧视（视线近平行旋转平面）hover 高亮但点击 RayPlaneIntersect 失败无 fallback → 点不动（low，需屏幕空间切向 fallback 设计，留后续）。

### 顺带发现（非本次修复，登记待办）
- **~~toon 材质 Location 3 顶点属性缺失~~ → ✅ 2026-06-11 已修（真根因是 pbr 模板，非 toon）**：运行时 Vulkan validation error `pVertexAttributeDescriptions does not have a Location 3 but vertex shader has an input variable at that Location`。**原误归因到 Animated Cube 的 toon 材质——错**：解析编译产物 `toon.vert.spv` 实测输入 location = `[0,1,2]`，toon 顶点 shader **不**消费 location 3。真根因 = 数据驱动模板路径：编辑器经 `RegisterTemplatesFromDirectory` 加载 `pbr.template.json`，而该 JSON **没有 `usesTangentVertex` 字段**（`ShaderTemplateDesc` 当时也没有这个字段、解析与 `RegisterTemplate` 都不传播它）→ 编辑器里的 **pbr** 模板 `usesTangentVertex=false`，但 `pbr.vert.spv` 消费 location 3 → 失配 validation error（validation 消息不带实体名，旁边的 pbr drawable 才是源头，被误当成 toon）。不只是噪声：pbr normal mapping 还读到未定义的 tangent 输入。**修复**：`ShaderTemplateDesc` 加 `usesTangentVertex` 字段 + `LoadTemplateDescFromFile` 解析（可选，schema minor 1.1→1.2）+ `RegisterTemplate` 传播到 `Material` + `pbr.template.json` 声明 `"usesTangentVertex": true`。新增 `MaterialSystemTest` parity 测试锁住"内置路径 vs JSON 路径 usesTangentVertex 一致"（防同类 schema 漂移）。
  - **需要你做的 dogfood 验证**：① 启动 OrangeEditor，在 viewport 放一个用 **pbr 材质** + 带法线贴图的模型（或既有 demo 的 pbr drawable）。② 看控制台/输出：之前刷屏的 `Location 3` validation error 应**消失**。③ 看带法线贴图的 pbr 表面：法线细节应正确（修复前 tangent 输入未定义，法线贴图方向可能错乱/闪烁）。若仍有 Location 3 报错，记下是哪个材质模板的 drawable。

---

## 2026-06-11 session（Lumix 成熟度推进：FBX 相机 + pbr tangent + PIE 字段 + gizmo 层级）

> 本 session headless 全绿（ctest 93/93），下列为视觉/交互待人工 dogfood 项。FBX 相机导入（`746592a`）是 **headless 真验**（投影/朝向/位置断言），无需 dogfood，故不在此列。

### 49. A1 transform gizmo —— parented 实体的 translate world→local（`746592a` 后续 commit）

- **commit**：translate gizmo 改 origin 读世界 + apply 转 local（rotate / scale / physics 仍**未**改，见下"剩余"）。
- **怎么触发**：
  1. 建实体 A（如 Cube），用 gizmo 把它挪到非原点（如 `(3, 1, 0)`）。
  2. 建实体 B（如 Sphere），在 Hierarchy 里把 B 拖到 A 下成为**子节点**。
  3. 选中 B，用 **W**（translate）gizmo 拖动 X / Y / Z。
- **看什么 / 通过判据**：
  - gizmo 画在 B 的 **mesh 世界位置**上（修复前画在 B 的 local 偏移处，即原点附近，不在 mesh 上）。
  - 拖动方向跟世界轴（World space 模式），B 沿该世界轴平滑移动**不跳变**。
  - 松手后 Inspector 里 B 的 **local** position 是换算后的值（= 世界位移经父逆变换）。
  - 之后再移动父 A，B 跟随父一起动（层级保持）。
  - **零回归（务必确认）**：选一个**无父 / 父在原点**的实体（如既有 demo 的 root 实体）拖 gizmo —— 行为应与之前**完全一致**（gizmo 在 mesh 上、拖动手感不变、Undo 一步回退）。
  - 多选群组：parented + 非 parented 混选一起拖，整体世界刚体平移；Ctrl+Z 一次回退全部。
- **失败上报**：若 root 实体拖动行为变了（回归！）、或 parented 实体拖动时飞走/抖动，请说明父实体的 position 数值。
- **剩余（已部分推进）**：~~rotate gizmo~~ → ✅ 2026-06-12 已落地（item 51）；~~scale gizmo~~ → ✅ 2026-06-12 已落地（保守做法，item 52）。仍剩 physics collider 双向 world↔local（见 `docs/A1-gizmo-physics-hierarchy-followup.md` §二）。

### 50. PIE C# 脚本 ScriptComponent fieldOverrides（`f72429e`，引擎层 headless 已验）

- **状态说明**：fieldOverrides 的**引擎层闭环已 headless 真验**（`script_system_test`：override Speed=2.5 → 4 帧后 position.x=10）。但 **Inspector 里编辑 fieldOverrides 的 GUI + EnterPlay 驱动 ScriptComponent 都尚未接线**（见下"前置缺口"），故当前**无法在编辑器内 dogfood**——本条登记为"等编辑器接线后再 dogfood"。
- **前置缺口（下次 session 任务）**：① 编辑器 EnterPlay 接 ScriptSystem（Play 时实例化 + tick + 应用 fieldOverrides）；② ScriptComponent 的 Inspector schema 注册（填 assemblyPath/typeName + 编辑 fieldOverrides 列表）。② 依赖编辑器侧暴露 `ORANGE_ENGINE_WITH_DOTNET` 编译定义（当前只 PRIVATE 给 orange_engine）。
- **接线后的 dogfood 步骤（草稿，待前置缺口落地后启用）**：给实体加 Script 组件 → assemblyPath 指向 `ScriptFixtures.dll`、typeName 填 `OrangeFixtures.Tweakable, ScriptFixtures` → 在 Inspector 加一条 fieldOverride `Speed = 2.5` → Play → 实体每帧沿 +X 移动 2.5（不加 override 是 1.0）；改 override 值重 Play 速度随之变。

---

## 2026-06-12 session（A1 收尾：rotate + scale gizmo 层级 world→local）

> 承接 item 49（translate gizmo）。本 session 把剩余两件 transform gizmo 切到 world→local。headless 全绿（ctest 94/94 零回归 + identity-parent epsilon 等价性独立验证），下列为视觉/交互**必须人工 dogfood** 项（gizmo 拖拽手感是真机的事，读代码不能判"能用"，参 `feedback_no_works_claim_from_codereading_interactive`）。

### 51. A1 rotate gizmo —— parented 实体的 world→local 旋转

- **改动**：`EditorRotateGizmo.cpp` 圆环 origin + 朝向改读 `WorldTransformComponent.world`（位置 + 抽旋转）；drag 在世界空间累乘 deltaQ → targetWorldRot，写回前 `inverse(parentWorldRot) * targetWorldRot` 转 local；命令 oldVal 锁拖动起点 local rotation。群组 follower 绕世界 pivot 公转 → 各自转 local 写回。
- **怎么触发**：
  1. 建实体 A（如 Cube），用 translate gizmo 挪到非原点（如 `(3, 1, 0)`）。
  2. 建实体 B（如 Sphere），Hierarchy 里把 B 拖到 A 下成为**子节点**。
  3. 选中 B，按 **E**（rotate）gizmo，拖动 X / Y / Z 圆环。
- **看什么 / 通过判据**：
  - rotate 圆环画在 B 的 **mesh 世界位置 + 世界朝向**上（修复前画在 B 的 local 偏移处，即原点附近）。
  - 拖动圆环 B 绕该轴平滑旋转**不跳变**；松手后 Inspector 里 B 的 **local** rotation 是换算后的值。
  - **Local / World 模式切换（X 键）**：World 模式圆环固定世界轴；Local 模式圆环跟 B 的 mesh 世界朝向（含父链旋转）旋转——切换时圆环朝向都应贴合 mesh。
  - 父 A 旋转后，再选 B 拖 rotate：圆环朝向 + 旋转结果都正确（B 的世界朝向 = 父旋转 ∘ B local 旋转）。
  - **零回归（务必确认）**：选**无父 / 父在原点**的实体（如既有 demo root 实体）拖 rotate —— 行为应与之前**完全一致**（圆环在 mesh 上、手感不变、Undo 一步回退）。World 模式尤其要逐项核对（World 模式不读世界旋转走快路径）。
  - 多选群组：parented + 非 parented 混选一起拖 rotate，整体绕 primary 世界位置刚体公转；Ctrl+Z 一次回退全部。
- **失败上报**：若 root 实体拖动行为变了（回归！）、parented 实体旋转后飞走/抖动、或圆环朝向与 mesh 脱钩，请说明父实体的 position + rotation 数值。

### 52. A1 scale gizmo —— parented 实体的 origin + handle 朝向读世界（保守做法）

- **改动**：`EditorScaleGizmo.cpp` origin + handle 轴朝向（`entityRot`）改读 `WorldTransformComponent.world`（位置 + 抽旋转），handle 画在 mesh 世界位置 + 世界朝向；**scale 值仍写 local**。drag 基准用世界旋转。群组 follower 位置绕世界 pivot 缩放 → 转 local；follower scale 仍乘 factorVec 写 local。
- **怎么触发**：同 item 51，但选中 B 后按 **R**（scale）gizmo，拖动轴 handle 或中心 uniform handle。
- **看什么 / 通过判据**：
  - scale handle 画在 B 的 **mesh 世界位置 + 世界朝向**上（修复前画在 local 偏移处）。
  - 拖单轴 handle，B 沿该轴缩放；拖中心 handle，B uniform 缩放——视觉上 handle 跟 mesh 走。
  - **零回归（务必确认）**：选**无父 / 父在原点 + 无旋转**的实体拖 scale —— 行为与之前**完全一致**（handle 在 mesh 上、factor 手感不变、Undo 一步回退）。
  - 多选群组：parented + 非 parented 混选拖 scale，follower 绕 primary 世界位置缩放；Ctrl+Z 一次回退全部。
- **⚠️ 已知限制（明确不支持，非 bug）**：**父链带旋转 + 对子做 non-uniform scale**（如父绕 Y 旋 45°，子只拖 X 轴 handle）。此时沿世界朝向轴拖出的 factor 直接乘到子 local scale 分量，是个近似——父旋转会让"沿世界轴的非均匀缩放"在子 local 空间 shear，无法用纯对角 scale 表示。**dogfood 时此场景视觉可能偏斜/不直观，属预期限制，不要当 bug 上报**。父无旋转的 non-uniform scale、以及任意父下的 uniform / center scale 都应正确。
- **失败上报**：若 root 实体（无父无旋转）拖 scale 行为变了（回归！）、或父**无旋转**的 parented 实体 scale 异常，请说明父实体 transform 数值。父带旋转 + non-uniform 的偏斜属已知限制，不计。

## 2026-06-12 session（C1.1 prefab override 编辑器 UI）

> 引擎层地基（CS1 diff / CS2 refresh / overriddenPaths 持久化 / C1.3 apply·revert）已 commit 并 headless 验证；本 session 落地编辑器消费层（蓝条 / 右键 revert / Prefab Instance banner 的 Apply·Revert All·Refresh）。**所有交互均 GUI 行为，headless 测不到（仅编译 + ctest 94/94 + invariant lint 绿），全部 dogfood-pending**。新增 TU `tools/OrangeEditor/PrefabOverrideUI.{h,cpp}`，钩入 `schema/SchemaInspector.cpp`（蓝条 + 字段右键 + 帧首 BeginFrame / 帧末 SyncRecordedOverrides）、`panels/InspectorPanel.cpp`（Prefab Instance banner）、`EditorWidgets.cpp`（PropertyLabel overridden 参数）、`theme/EditorTheme.cpp`（`Color::GetPrefabOverride()` 蓝 token）。

### 53. prefab override 蓝条 + 持久化记录

- **怎么触发**：
  1. 启动 OrangeEditor，在 Asset Browser 选一个 `.prefab.json`（或右键某子树 Create Prefab... 先造一个），拖进 viewport 实例化。
  2. 选中实例（或其子节点），在 Inspector 改某个字段（如 Transform.Position 拖一下、Renderable.Visible 取消勾选、某 Light 的 Intensity 改值）。
- **看什么 / 通过判据**：
  - 被改的字段行**左缘出现一道蓝色竖条**，且字段名 label **变蓝**（Unity prefab override 同款）。未改的字段无蓝条。
  - 改 Vec3（Position/Scale/Rotation）任一分量 → 整字段行蓝条（因为底层是 `position/0..2` 叶子，prop 前缀匹配命中）。
  - Save 场景 → 重启 → Load → 选中该实例 → **蓝条仍在**（overriddenPaths 已持久化进 scene，schema 1.18）。
- **失败上报**：普通（非 prefab 实例）实体的字段**绝不应**出现蓝条（回归）；蓝条出现在没改过的字段；改了字段却没蓝条。

### 54. 字段右键 Revert to Prefab

- **怎么触发**：在一个带蓝条的 override 字段 label 上**右键** → "Revert to Prefab"。
- **看什么 / 通过判据**：
  - 该字段值**立即回到模板值**，蓝条消失（同 component 内其它 override 字段不受影响）。
  - Vec3 字段 revert → 三个分量都回模板值。
- **⚠️ 已知限制（非 bug）**：**revert 不可 Undo**（Ctrl+Z 不恢复）。原因：引擎层无 typed by-path 标量逆写原语，type-erased 的精确 Undo 做不出来（见 PrefabOverrideUI.cpp 头注释）。误点了只能手动改回或 Refresh。dogfood 时确认"revert 生效"即可，**不要把"Ctrl+Z 不回退"当 bug 上报**。

### 55. Prefab Instance banner —— Apply to Prefab / Revert All / Refresh

- **怎么触发**：选中一个 prefab 实例（建议选**实例根**），Inspector 顶部 `Entity #N` 下出现蓝色 "Prefab Instance" 标题 + 三个按钮。
- **看什么 / 通过判据**：
  - **Apply to Prefab**：把实例当前态推回模板 `.prefab.json`（重写文件）。改一个字段 → Apply → 再拖一个**新**实例进来，新实例应带上刚 apply 的改动。**关键**：Apply 后其它已存在实例的 templateEntityGuid 锚不应断（A2.2，引擎层 ApplyInstanceToTemplate 保证 guid 映回模板）。⚠️ **不可 Undo**（改磁盘文件）。
  - **Revert All**：丢弃该实例所有 override，全部字段回模板值，所有蓝条消失。⚠️ 不可 Undo。
  - **Refresh**：用持久化 override 集从模板重拉非 override 字段（模板演进后，未手改字段更新成新模板值、手改字段保留）。验证需要"先改模板再 Refresh 实例"的多步场景。⚠️ 不可 Undo。
  - 三个按钮在 **Play / Paused** 期 disabled（灰显，hover 有 tooltip）。
- **失败上报**：Apply 后其它实例与模板的关联断裂（蓝条全亮 / Refresh 失效）；banner 出现在非 prefab 实例上。
- **需主循环 / 用户拍板的设计点**：Apply / Revert All / Refresh **均不进命令栈（不可 Undo）**——apply/revert all 是磁盘/批量动作，refresh 缺 typed in-place 逆写原语。若 dogfood 觉得 Refresh / Revert All 需要 Undo，需引擎层补 typed by-path 写原语（下个 session 跨能力，登记到 engine-known-gaps）。当前以"显式用户动作 + tooltip 标注不可撤销"落地。

---

## 2026-06-12 通宵 session（B1 PIE 编辑器侧：ScriptComponent Inspector）

### 56. ScriptComponent Add-Component + Inspector 编辑（assemblyPath/typeName + fieldOverrides 列表）

- **commit**：本 session（RegisterScriptComponentSchema + ScriptFieldOverridesInspectorPlugin）。引擎层 ScriptComponent / ScriptSystem / fieldOverrides 已 headless 真验（B1.2 `e558861` + B1.3 `f72429e`），本项是**编辑器侧数据编辑接线**（纯数据，不需 dotnet runtime）。
- **怎么触发**：
  1. 选中一个实体 → Inspector 底部 **+Add Component** → 菜单点 **"Script"**。
  2. 实体多出 **Script** 段，含 **Assembly Path** / **Type Name** 两个 string 字段 + 段末 **Field Overrides** 列表。
- **看什么 / 通过判据**：
  - Assembly Path 填 game assembly 相对路径（如 `ScriptFixtures.dll`）、Type Name 填 assembly-qualified 全名（如 `OrangeFixtures.Mover, ScriptFixtures`）；可编辑、随场景 Save/Load round-trip。
  - **Field Overrides 列表**：点 **+ Add Override** 加一行 → 每行 `[name 输入] [type 下拉 Float/Int/Bool/String] [value 输入] [x 删除]`；增/删/编辑即时改 `ScriptComponent.fieldOverrides`。
  - 段头右键 **Remove Component** 可删；已挂 Script 的实体 +Add 菜单不再出现 "Script"。
- **重要限制（dogfood 注意，非 bug）**：**EnterPlay 尚未接 ScriptSystem**（需 dotnet runtime + 开 `ORANGE_ENGINE_WITH_DOTNET`，本 session 为隔离风险未碰 build 配置）。所以编辑了 assemblyPath/typeName/fieldOverrides 后 **Play 时脚本不会真运行**——当前仅作**数据编辑 + 序列化**。Play 驱动脚本留专门 dotnet session（见 item 50）。
- **背景**：B1 PIE 重点 epic 编辑器侧地基。运行时全齐（headless 验过），缺 ① 本项（数据编辑 UI，已落地）② EnterPlay 接 ScriptSystem（dotnet-gated 留待）③ workspace 外部项目模型。

---

## 2026-06-12 session（OE-MCP M0 · spike 通路）

### 57. OE-MCP M0 —— `--mcp-port` 命令端端到端通路（ping / get_scene_info）

- **commit**：本 session（mcp/McpBridge.h + McpServer.cpp + McpCommandHandler.cpp + EditorHost/EditorRenderLayer/main 接线 + tools/orange-mcp/ Python server）。架构见 ADR-020 + `docs/mcp-realtime-coediting-design.md` §13 M0。
- **已自动真验（非 dogfood-pending）**：裸 TCP NDJSON 端到端已用 `tools/orange-mcp/smoke_test.py` 跑通——`OrangeEditor.exe --mcp-port 8765` 启动监听、`ping` 返回 `{editorVersion, protocolVersion, sceneName}`、`get_scene_info` 返回 20 实体树（guid/name/parentGuid/components 全对，与 demo 场景一致）、未知 op graceful 报错、断连重连不崩、优雅关闭线程 join 干净（log 见 `[mcp] MCP 命令端已停止` + `clean shutdown`）。
- **仍需 dogfood（Python MCP 层 + 真实客户端）**：
  1. `cd tools/orange-mcp && pip install -r requirements.txt`（装 `mcp` 包）。
  2. 把 orange-mcp 注册给 Claude Code（`.mcp.json`，见 `tools/orange-mcp/README.md`），`ORANGE_MCP_PORT=8765`。
  3. `OrangeEditor.exe --mcp-port 8765` 启动编辑器。
  4. 在 Claude 里调 `ping` → 返回编辑器版本；调 `get_scene_info` → 返回的实体树与编辑器 **Hierarchy 面板逐项一致**（用户对照）。
- **看什么 / 通过判据**：
  - 不带 `--mcp-port` 启动：行为与现状完全一致，无新监听端口（`netstat -ano | findstr 8765` 为空）。
  - 带 flag：Console / 日志出现 `[mcp] MCP 命令端监听 127.0.0.1:8765`；FastMCP 层 tool 调用返回结果与 smoke_test.py 一致。
  - Python server 断开 / 重连，编辑器不崩、不卡帧。
- **失败上报**：编辑器带 flag 启动卡死 / 崩；`get_scene_info` 实体数或层级与 Hierarchy 面板不符；关闭编辑器时挂起（MCP 线程 join 不返回）。
- **背景 / 下一步**：M0 是 spike 通路（技术最高风险点：winsock 后台线程 + 帧末 marshal + Python 桥），已打通。M1 加 `get_entity` / `list_component_types` / `capture_viewport`（读闭环，含引擎层 `Pipeline::CaptureViewportToCpu`）；M2 加写闭环（create/set_field/add_component/delete/select/save，全走命令栈）。tool 全景见 `docs/mcp-requirements.md` §4。

### 58. OE-MCP M1 —— 读闭环（get_entity / list_component_types / capture_viewport）

- **commit**：本 session（McpCommandHandler 加三 op + 引擎层 `Pipeline::CaptureViewportToCpu` + Python tool）。
- **已自动真验（非 dogfood-pending）**：
  - `get_entity`：smoke_test 验返回结构化全字段（Static EdgeChain 的 Transform position/rotation/scale 全对）、失效 guid graceful 报错。
  - `list_component_types`：smoke_test 验列出 18 组件含字段元数据（含游戏侧 HealthComponent → 证 schema-first NF-10）。
  - `capture_viewport`：**已亲眼验证**——截图经 PIL 解码成 PNG，看到 demo 场景（绿球 Slime Doll + 发光 Glow Box 带 bloom + 火焰/闪光粒子 + 楼层网格 + 绿色 gizmo 线框），与 get_scene_info 实体一致。回读的是 viewportColor（含后处理，所见即用户所见）。ctest 95/95 零回归。
- **仍需 dogfood（真实 Claude 客户端经 FastMCP）**：
  1. `pip install -r requirements.txt`（含 pillow）。
  2. 编辑器 `--mcp-port 8765` + Claude 注册 orange-mcp。
  3. 调 `get_entity(guid)` → 字段值与 Inspector 显示一致（含 Enum 名 / AssetRef 路径）。
  4. 调 `capture_viewport` → Claude 能**正确描述屏幕上看到的场景内容**（用户对照——「AI 看见了」的直接证据）。
  5. 新挂一个此前没读过的组件类型，`get_entity` 无需改 MCP 代码即正确返回其字段（NF-10）。
- **看什么 / 通过判据**：`capture_viewport` 返回 PNG 与编辑器 viewport 画面一致（含 bloom/tonemap 后处理）；尺寸 = viewport 当前尺寸，过大时 Python 侧按最长边 1280 缩小。`capture_viewport` 含 WaitIdle，单次数百 ms、让编辑器卡一帧——非高频操作（tool 描述已注明）。
- **失败上报**：`capture_viewport` 返回全黑 / 尺寸异常 / 与屏幕不符；`get_entity` 字段值与 Inspector 不一致；高频 capture 拖垮帧率。
- **下一步**：M2 写闭环（create_entity / set_field / add_component / delete_entity / select_entity / save_scene，全走命令栈，AI 操作可被用户 Ctrl+Z）。

### 59. OE-MCP M2 —— 写闭环（create/set_field/add_component/delete/select/save，**首版完整**）

- **commit**：本 session（McpCommandHandler 加 6 写 op + Python tool）。**P0 共 11 tool 全落地**=首版读写协同闭环完整。
- **已自动真验（非 dogfood-pending）**：smoke_test 写往返全过——
  - `create_entity`（undoable:true）→ 返回新 guid；`set_field`(Transform.position=[1,2,3], undoable:true) → get_entity 确认写入生效 + name 正确；错误组件名 graceful 报错；`add_component`(Renderable, undoable:false) → get_entity 确认挂上；`select_entity` ok；`delete_entity`(undoable:false) → get_scene_info 确认实体从场景消失；`save_scene`(显式路径) → 下一帧真写盘 30KB scene 文件。
- **仍需 dogfood（真实 Claude 客户端，尤其 GUI 联动 + Ctrl+Z）**：
  1. AI `create_entity` + `add_component`(Renderable)：用户**实时**看到 viewport 出现白色 cube、Hierarchy 出现新节点。
  2. AI `set_field` 改 Transform.position：viewport 实体移动；用户 **Ctrl+Z 撤销 AI 的改动**成功、Ctrl+Y 重做成功（set_field 走命令栈的关键验证）。
  3. AI 连续微调同一字段 N 次：undo 栈合并为一条（coalesce 生效）。
  4. AI `select_entity`：Inspector 联动显示该实体；AI 改动后场景标题出现 dirty 标记。
  5. 走一遍 UC-1（设计 §3）：人口述布局 → AI 搭灰盒（批量 create+set_field）→ `capture_viewport` → 人微调 → save，全程无需人替 AI 执行编辑操作。
- **重要限制（dogfood 注意，非 bug）**：
  - `add_component` **不可 Undo**（镜像编辑器现状：Add Component 清空 undo 历史）——加错不能 Ctrl+Z，且会清掉之前 MCP set_field 的 undo。tool 描述已注明。（`delete_entity` 自 2026-06-12 起**已可 Undo**，见 item 60-P1。）
  - Play 模式下写操作默认被拒绝（"in play mode" error），需 `allowInPlay=True` 逃生门（改动会被 Stop 还原）。
  - `set_field` 仅支持 11 种标量/向量/字符串/Enum/AssetRef 类型；EntityRef / AssetRefArray / 顶点表首版只读（返回 "not writable in first version"）。
  - `save_scene` 无 path 且当前无场景路径时会弹文件对话框（GUI），自动化勿用空 path。
- **失败上报**：set_field 后 viewport 实体不动 / Ctrl+Z 撤不掉 AI 操作 / coalesce 失效（N 次微调留 N 条 undo）；create 的实体不可见 / Hierarchy 不出现；delete 后实体残留。

## 2026-06-12 session（OE-MCP M4 · P1 全 17 tool）

> 设计见 `docs/mcp-realtime-coediting-design.md` §13 M4 + 需求 §4.M P1 集。C++ 全编译链接通过 + invariant lint 绿 + `tools/orange-mcp/smoke_test.py` 加 P1 往返覆盖（headless 可跑的 NDJSON 端到端）。下列为**真实 Claude 客户端 + GUI 联动**待人工 dogfood 项。

### 60. OE-MCP P1 —— E1 协同效率包（get_editor_state / find_entities / camera / frame / duplicate / reparent / remove_component / undo group）

- **commit**：本 session（McpCommandHandler 加 P1 handlers + 基础设施导出：`SchemaInspector::CaptureComponentValues` / `FrameEntityCamera` / undo-group 护栏 `TickMcpUndoGroupGuard` + McpBridge 连接信号；Python server 加 9 tool）。
- **已自动真验（smoke_test，非 dogfood-pending）**：find_entities（name/component/underGuid + 失效组件报错）/ get_camera→set_camera round-trip + 越界 clamp（fov→179、radius→0.01）/ frame_entity(All) / get_editor_state.selectedGuid 反映 select / duplicate→reparent→remove_component 全套 + 环检测拒绝 + reparent 后 child 在子树内 + remove 后组件消失 / begin·end_undo_group + 嵌套报错 + 无组 end 报错。
- **仍需 dogfood（GUI 联动 + Ctrl+Z）**：
  1. `begin_undo_group` 摆 N 个实体 `end_undo_group` → 用户**一次 Ctrl+Z 全部回退**（组合并为一条）；组打开 30s 不关 → 编辑器日志出现 `[mcp] undo group 自动闭合（30s 超时）`；MCP 断连 → 自动闭合（`客户端断开`）。
  2. `reparent_entity(keepWorld=True)` 后实体**世界位姿不跳变**（viewport 无跳动）；Ctrl+Z 精确复位原层级。
  3. `set_camera` / `frame_entity` 后 viewport 视角实时变化；`get_editor_state` 各字段与编辑器状态栏 / Inspector / gizmo 工具栏一致。
  4. `duplicate_entity` → Hierarchy 出现克隆体（新名/选中）；Ctrl+Z 删掉克隆。
  5. `remove_component` → Inspector 该组件段消失；可 Undo 的组件 Ctrl+Z 恢复且**字段值还原**。
- **失败上报**：undo group 没合并成一条 / 护栏没自动闭合（栈卡死）；reparent 跳位或 undo 不复位；duplicate 克隆体与源共享 guid；remove 后组件残留 / undo 丢字段值。

### 61. OE-MCP P1 —— E2 资产管线（list_assets / import_asset）

- **已自动真验（smoke_test）**：list_assets(all) 返回资产列表 + kind 过滤（Material 只回 .material）；import_asset / open_scene 失效路径 graceful 报错。
- **仍需 dogfood（真导入 + Blender↔Orange 双 MCP，UC-4）**：
  1. Blender MCP 导出 .glb/.fbx → orange-mcp `import_asset(srcPath, scale)` → 返回 destPath + materialPaths → `create_entity` + `set_field`(Renderable.mesh = destPath) 挂上 → `capture_viewport` 对照 Blender 渲染图（贴图 / 多材质 sub-mesh / 轴向正确）。
  2. `list_assets(kind="Mesh")` 返回的 path 能直接喂 `set_field` 的 AssetRef 字段。
  3. FBX `scale=0.01`（真 cm 文件）导入后尺寸正确。
- **失败上报**：导入产物路径错 / 挂上后不可见 / 贴图丢失 / 多材质 sub-mesh 错位；list_assets 漏资产或路径不能直接喂 AssetRef。

### 62. OE-MCP P1 —— E3 Play 调试（play / pause / resume / stop）

- **已自动真验（smoke_test）**：play → get_editor_state playState=Play → Play 期 set_field 被拒 → stop → 回 Edit；状态机校验（pause 需 Play / resume 需 Paused / 重复 play 报错）。
- **仍需 dogfood（GUI + 快照往返）**：
  1. AI `play` → 用户看到 toolbar 进 Play、物理 / 粒子 / 动画开始 tick；间隔 `capture_viewport` + `get_entity` 观察运行时行为（如物理驱动的 Transform 变化）。
  2. AI `pause` → tick 停在当前帧，可细看；`resume` 继续。
  3. AI `stop` → 场景**还原到 Play 前**（快照往返）；AI 之前持有的实体 guid **仍有效**（A2.3 性质：guid 跨快照稳定）。
  4. Play 期 AI `set_field` 默认收到 "in play mode" error（除非 allowInPlay）。
- **重要限制（非 bug）**：**EnterPlay 尚未接 ScriptSystem**（需 dotnet runtime + `ORANGE_ENGINE_WITH_DOTNET`，见 item 50）——Play 期 C# 脚本不真跑，UC-5「调玩法手感」的脚本侧待 B1 接线 session。当前 Play 仅物理 / VFX / 动画 tick。
- **失败上报**：play 后物理 / 动画没 tick；stop 后场景没还原 / guid 失效；快照往返崩溃。