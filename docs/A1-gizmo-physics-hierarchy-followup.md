# A1 收尾 follow-up：transform gizmo + physics 的层级（world↔local）支持

> 状态：**未实现**（A1 ~92% 已落地，剩此两项）。本文是基于真实代码读后的**精确实现 spec**，
> 供下一个**专门 session + dogfood** 执行。两项都是"写/apply"类（需 world→local 反向变换），
> 且 **GUI / 双向 sim 无法 headless 验证**，是编辑器主操作工具 / Play 模式核心——故从
> A1 自主 session 中剥离，避免盲改（参 `feedback_no_works_claim_from_codereading_interactive`
> 的教训：GUI 行为靠运行时，读代码判"能用"会被 dogfood 打脸）。

A1 已把所有**读**类 consumer（render mesh / picking / 3 类光源 / halo / postprocess /
6 个装饰 gizmo overlay）+ importer 灯光方向切到读 `WorldTransformComponent`。剩下两项是
**会写回 entity transform** 的路径，必须 world→local，复杂度与风险都更高。

---

## 共同前提：world→local 数学 + 零回归性质

- 引擎每帧 `Scene::PropagateWorldTransforms(World&)` 在 `WorldTransformComponent.world`
  写入累积世界矩阵。父的世界矩阵 `parentWorld` 可由 entity 的 `HierarchyComponent.parent`
  取其 `WorldTransformComponent.world`，或复用 `EditorHierarchy::ComputeWorldMatrix(world, parent)`
  （走父链，已有）。无父 / 父无 cache → `parentWorld = identity`。
- 把一个目标**世界**位置 `pWorld` 写成 entity 的 **local** position：
  `newLocal = vec3( inverse(parentWorld) * vec4(pWorld, 1) )`。
- 朝向同理：`newLocalRot = inverse(parentWorldRot) * targetWorldRot`（可复用
  `EditorHierarchy::SetLocalKeepingWorld` 的 decompose 思路）。
- **零回归性质（关键）**：root / 原点父实体 `parentWorld == identity` →
  `inverse(identity) = identity` → `newLocal == pWorld`，与现状逐字节相同。所有 committed
  场景的 group 父都在原点 → 本改动对既有内容**零回归**。只有 parent 到**非原点**实体的
  新建实体才走新路径（正是 dogfood 重点）。

---

## 一、Transform gizmo（translate / rotate / scale）

### 现状（读 `tools/OrangeEditor/EditorTranslateGizmo.cpp` 后确认）

gizmo **把 `pTC->position`（local）当成 world** 用——`EditorTranslateGizmo.cpp:140`
`const glm::vec3 entityWorldPos = pTC->position;`（line 246 注释明说"position 当 world"
的一致语义）。drag 在这个"伪 world"空间算 `newPos`，直接写回 `pTC->position`（local，
line 299 / apply lambda `MakeTransformPositionApply` line 77-88）。root 实体 local==world
故正确；parented 实体 gizmo 画在 local 偏移处（不在 mesh 上，mesh 渲在 world）+ 拖动按
local 轴。

### 改法（translate，逐点）

1. **origin（line 140）**：
   `entityWorldPos = wtc ? vec3(wtc->world[3]) : pTC->position;`（读 world，fallback local）。
   gizmo 即画在 mesh 上。
2. **主实体 apply（line 296-308）**：`newPos` 仍在世界空间算（含 line 289-295 的世界轴
   snap，无需改 snap）。写回前转 local：
   `const glm::vec3 newLocal = vec3(inverse(parentWorld) * vec4(newPos, 1));`
   `pTC->position = newLocal;` + 命令存 `oldLocal(=拖动起点的 local)` → `newLocal`。
   注意 `dragStartEntityPos`（line 242）要改存**世界**起点，命令的 oldVal 要单独存 local 起点。
3. **多选 group followers（line 318-335）**：`groupDelta = newPos - dragStartEntityPos`
   是**世界**位移。每个 follower：`otherNewWorld = followerStartWorld + groupDelta`，再
   `otherNewLocal = inverse(followerParentWorld) * otherNewWorld`。当前 `snap.position`
   存的是 follower 的 local（line 253）——需改存 follower 的**世界**起点，且各 follower
   各自的 `parentWorld`。
4. **rotate gizmo**（`EditorRotateGizmo.cpp`）：类比——手柄圆画在 world，拖出的
   `targetWorldRot` 写回前 `newLocalRot = inverse(parentWorldRot) * targetWorldRot`。
5. **scale gizmo**（`EditorScaleGizmo.cpp`）：scale 通常只作用 local，受父**旋转**影响时
   non-uniform 会 shear（glm decompose 会失真）——保守只切 origin 画在 world，scale 值
   仍写 local（父无旋转时正确；父带旋转的 non-uniform scale 是公认难点，可暂不支持 +
   文档标注）。

### 验证 / dogfood（GUI，必须人工）

- root 实体：gizmo 行为与现状**完全一致**（零回归）——回归项。
- 新建实体 parent 到一个**移动到非原点**的父：gizmo 画在子的 mesh 上（而非原点附近）；
  拖 X/Y/Z 子沿世界轴动且不跳；松手后 Inspector 的 local position 是换算后的值；父再移动
  子跟随。
- 父带**旋转**时：Local/World space 切换（gizmo 工具栏）轴向正确；拖动方向跟轴。
- 多选群组：parented + 非 parented 混选，整体世界刚体平移，Undo 一次回退全部。

---

## 二、Physics collider（2D，双向 sim）

### 现状（读 `tools/OrangeEditor/EditorRenderLayer.cpp` 后确认）

Play 模式（`EditorRenderLayer.cpp` ~line 1526-1532）进入时，遍历 `RigidBody + Collider +
Transform` 把 entity transform 的 pos/angle 注册进 `PhysicsWorld`（读 entity **local**
position）。每帧 `Step` 后（~line 224-244）把 dynamic body 的新位姿**写回** entity
（`tc->position.x/y = xf.position.x/y`，写 **local**）。

A1 后 render 读 world，physics 读/写 local → parented 物理实体渲染与模拟**不一致**。

### 改法（双向都要 world→local，故复杂）

- **Play 进入读**（一次性）：`RigidBody+Collider` 实体注册进 body 时，先 `PropagateWorld
  Transforms` 再读**世界** pos/angle（`world[3].xy` + 从 world 矩阵抽 2D 旋转角）→ body
  初始位姿对齐世界。
- **每帧写回**（dynamic）：body 给的是**世界** pos/angle，写回 entity 前要
  `inverse(parentWorld)` 转 local 再写 `tc->position`。否则父会二次变换 → 实体跳。
- **static / kinematic**（2.5D 移动平台常见）：只有"读"无"写回"，单切读侧即可让 parented
  静态 collider 对齐世界——比 dynamic 简单，可先只做 static/kinematic 读侧。
- **2.5D 现实**：player 通常**不** parented（直接世界实体），故此项实际拉动度中等；建议
  优先级低于 transform gizmo。先做 static/kinematic 读侧（移动平台）即覆盖主用例。

### 验证 / dogfood（Play 模式，必须人工）

- 非 parented 物理实体：行为与现状一致（零回归）。
- parented static collider（如挂在移动 group 下的平台）：Play 时碰撞体在世界正确位置。
- parented dynamic body：Play 后 body 移动，entity 不跳、跟随父正确（双向 world↔local）。

---

## 实施顺序建议

1. transform gizmo translate 主实体（origin + world→local apply）——最高频、收益最大。
2. gizmo group followers + rotate。
3. gizmo scale（父无旋转优先；带旋转 non-uniform 标注暂不支持）。
4. physics static/kinematic 读侧（移动平台）。
5. physics dynamic 双向写回（最后，niche）。

每步：改完先确认 root 实体零回归（ctest 76/76 仍绿——headless 覆盖既有内容），再人工
dogfood parented 用例并在 `docs/dogfood-checklist.md` 登记/打勾。
