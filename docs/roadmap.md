# OrangeEngine 长期路线图（Phase 6+）

- 基准日期：2026-05-05
- 基准状态：Phase 1 未开始；Phase 1–5 任务表见 [`design-plan.md`](./design-plan.md)
- 范围：Phase 6+ 表示"第一款游戏 fork 出去之后"的引擎演进；这些阶段不是 game-shipping critical path 的一部分，按"游戏仓库的反馈拉动 + 引擎自身能力补齐"双驱动决定优先级
- 任务字段沿用 `design-plan.md` 风格（描述 / 输入 / 输出 / 影响模块 / 前置 / 实现要点 / 验证 / 验收 / Critical Path）

> **关于 Phase 6+ 的实际节奏**：与 Phase 1–5 必须按依赖顺序串行不同，Phase 6+ 可以**乱序、跳跃、并行**。它们更像独立的能力包，而不是流水线。这份路线图给出的是一个建议优先级排序，而不是必须遵守的执行顺序。

---

## Phase 6 · 编辑器 v0.1

> **后续演进迁移至独立路线图**：编辑器 v0.2 之后的所有里程碑（Command System + Undo/Redo / Property Schema / Gizmo / Asset 浏览器 / 多 layer / Animation 子模式 / Log + Keybinding / Profiler / ...）记录在 [`editor-roadmap.md`](./editor-roadmap.md)。本节内的 Task 06-01 ~ 06-09 是 **v0.1 闭环的历史快照**，保留供回溯，不再扩展。

**目标**（v0.1 范围）：把 `samples/07_full_pipeline` 升级为独立 target `tools/OrangeEditor`，提供关卡 / 粒子 / 材质三个最小工具子集，足以让美术 / 关卡设计师不写代码完成日常工作。

**闭环后解锁**：游戏 content 生产从"程序员手写 JSON"过渡到"编辑器交互式编辑"。

### Task 06-01 · 提取 `tools/OrangeEditor` target ✅
- 描述：从 `samples/07_full_pipeline` 复制基础结构到 `tools/OrangeEditor`，独立 CMake target；`samples/` 仅保留作引擎 API 演示，编辑器走自己的演化路径
- 前置：Phase 5 完成
- 实现要点：编辑器通过 `find_package(OrangeEngine)` 依赖引擎，不直接吃源；这是引擎 API 自身可消费性的最强验证
- Critical Path：是
- **落地状态**（2026-05-09）：v0.0 scaffold 完成 —— 一个能开窗 + Esc 退出的最小可执行体。Sample 07 体量过大（737 行 + 内部 src/ 头依赖），不适合作为编辑器起点；改成参考 sample 01_minimal_window 形态，只接 `AppHost` + `EscQuitLayer`。
  - **Existing**：
    - `tools/OrangeEditor/CMakeLists.txt`（顶级独立 CMake 工程，`find_package(OrangeEngine 0.1 CONFIG REQUIRED)`，与 sample / 引擎同档 warning 级别）
    - `tools/OrangeEditor/main.cpp`（~80 行：AppConfig + AppHost + EscQuitLayer + Run loop；Esc raw key code 256 直接对比，不引入 InputContext 整套栈）
    - `tools/OrangeEditor/README.md`（两阶段 build 流程文档：先 `cmake --install build` 装引擎，再用 `-DCMAKE_PREFIX_PATH` 配编辑器；CI 验证路径）
    - `tests/install/editor_build_smoke.cmake`（与 `config_smoke.cmake` 同模式：install + configure + build editor 三步走，证明 `find_package` 消费链通；不 run，CI 一般无 GPU）
    - `tests/CMakeLists.txt` 注册 `editor_build_smoke` 为第 41 个 ctest case
  - **验证**：
    - `cmake --build build --config Debug` 引擎主仓 build 干净
    - `ctest -R editor_build_smoke` 通过，全套 41/41 无回归
    - 手动跑 `build/editor_build_smoke_consumer/Debug/OrangeEditor.exe` 能开窗 + 控制台显示 scaffold 启动文案 + Esc 触发"请求退出"日志后干净 quit
  - **Out-of-scope（明确推迟）**：
    - 渲染场景预览（viewport 内画 cube / 加载 builtin shader）→ Task 06-04 真要画 viewport 时再解决"内置 SPV 部署到 editor exe 旁边"的问题
    - ImGui 接入 → Task 06-02
    - 实体树 / 检视器 / 资源浏览器 → Task 06-03 起
    - 把 `samples/07_full_pipeline` 的物理 / 动画 / 粒子真实接入 → 编辑器自身演化路径上视需要再做，sample 07 仍保留作引擎 API demo

### Task 06-02 · ImGui 集成与 dock space ✅
- 描述：默认窗口布局：场景视图 / 实体树 / 检视器 / 资源浏览器 / 控制台
- 前置：06-01
- 实现要点：ImGui 的 GLFW + Vulkan backend 已通过 OrangeRender 集成可复用；编辑器 UI 全走 ImGui
- Critical Path：是
- **落地状态**（2026-05-11）：
  - **Existing**：
    - `tools/OrangeEditor/CMakeLists.txt` 引入 `orange_editor_imgui` 静态库（ImGui core + impl_glfw + impl_vulkan，docking 分支 v1.91.5；FetchContent；编 `IMGUI_IMPL_VULKAN_NO_PROTOTYPES` + 通过 `Interop::GetVulkanGetInstanceProcAddr` 注入 loader callback，与 OrangeRender 共用同一 volk loader）
    - `tools/OrangeEditor/main.cpp`：
      - `ImGui_ImplGlfw_InitForVulkan` + `ImGui_ImplVulkan_Init`（启用 dynamic rendering）+ `ImGuiConfigFlags_DockingEnable | ViewportsEnable`
      - `EditorRenderLayer::OnUpdate` 内 `DockSpaceOverViewport` + 首帧 `DockBuilder*` 编程式建默认布局（左 20% Entity Tree / 右 25% Inspector / 下 30% Assets+Console tab / 中央 Scene）
      - 五个固定占位面板：Scene / Entity Tree / Inspector / Assets / Console（前四个 `TextDisabled` 占位，Console 放帧统计 + Esc 退出按钮）
      - swap-chain overlay callback 内调 `ImGui_ImplVulkan_RenderDrawData` 把主 viewport DrawData 录到引擎主窗口 swap-chain image；secondary viewport 走 ImGui 自带 `UpdatePlatformWindows` + `RenderPlatformWindowsDefault`
    - 消费 OrangeRender FEATURE-2026-05-09（Vulkan handle interop + swap-chain overlay hook）+ FEATURE-2026-05-10（vulkan loader export）
  - **关键 bug 修复**（提交 12ba32a）：multi-viewport 拖出子窗口奔溃 —— Vulkan 1.3 SDK loader 对 `vkGetInstanceProcAddr(inst, "vkCmdBeginRenderingKHR")` 总返回非 null trampoline，但 OrangeRender 启用的是 1.3 core `dynamicRendering` feature 而非 KHR 扩展，device dispatch 表里 KHR 槽位是 null，trampoline 一调即空跳。修复：在编辑器侧 ImGui loader callback 内拦截两个 KHR 名字（`vkCmdBeginRenderingKHR` / `vkCmdEndRenderingKHR`），强制走 `vkGetDeviceProcAddr` 解析到 core 名字（`vkCmdBeginRendering` / `vkCmdEndRendering`），拿驱动 ICD 直接函数指针绕开 trampoline。
  - **验证**：
    - `cmake --build build --config Debug --target OrangeEditor` 干净
    - 手动跑 `build/bin/Debug/OrangeEditor.exe`：默认 dock layout 正确显示五个面板；可拖动 tab 改 dock、可拖出主窗口形成独立 OS window（多次拖出 / 收回不奔溃）；Esc 干净退出
  - **Out-of-scope（移交后续 task）**：
    - 各面板的实际内容（实体树 / 检视器 / 资源浏览器 / 场景 viewport 渲染）→ 06-03 起
    - 编辑器侧日志系统（接 Core::Log，目前 Console 面板只显示帧统计）→ Phase 6 后续
    - 多窗口布局保存 / 加载多个 layout preset → Phase 6 后续

### Task 06-03 · 实体树视图 ✅
- 描述：可视化 ECS World，支持 select / 重命名 / 删除 / 拖拽改父子关系
- 前置：06-02
- 实现要点：通过 `World::GetNativeRegistry()` 直通访问 EnTT；不引入新公共 API
- Critical Path：是
- **落地状态**（2026-05-11）：
  - **Existing**（全部在 `tools/OrangeEditor/main.cpp` 内，不进引擎 include/）：
    - `EditorState` 跨面板共享结构：`pWorld` / `selectedEntity` / 内联重命名状态（`renamingEntity` + `renameBuffer` + `renameJustStarted`）/ 帧末 pending 结构性操作（`pendingDelete` + `pendingReparent`）。layer 持引用，main 拥有实例
    - `EditorHierarchy` namespace 工具集 —— 编辑器侧 hierarchy 维护，引擎 `HierarchyComponent` 是裸数据（parent + 双向 sibling chain），刻意不提供 reparent / link / unlink helper（task 描述明确"不引入新公共 API"），所以这一层放在编辑器本地：
      - `LinkAsLastChild` —— 挂到 parent 子链末尾
      - `Detach` —— 从父子链上摘下（保留自身子树），变 root
      - `IsAncestorOf` —— DnD 防环用
      - `ReparentTo` —— Detach + LinkAsLastChild 组合
      - `DestroySubtree` —— 收集 children 快照（不能边遍历边 destroy）+ 递归销毁
    - `SeedDemoWorld` 启动期种 7 个实体两棵根（Root → Camera/Light/Geometry → Floor/Wall + Misc Sibling），Task 06-07 接真实场景加载后退化为占位 fallback
    - `DrawEntityTreePanel` 真实实现：`reg.view<entt::entity>()` 找 root（EnTT 3.13 已删 `registry.each`，跟引擎序列化层一致用 view 形式遍历），递归 `DrawEntityNodeRecursive`；面板底空白区当 "drop here to unparent" drop target；帧末统一 apply pendingDelete / pendingReparent
    - `DrawEntityNodeRecursive` 单节点：`TreeNodeEx` + OpenOnArrow / OpenOnDoubleClick / SpanAvailWidth / DefaultOpen / AllowOverlap；选中点击 → 写 `selectedEntity`；双击 entry-body → `BeginRename`；自身既是 DnD source 也是 target，payload 类型 `"ORANGE_EDITOR_ENTITY"` 携带 `Entity` 值
    - 内联重命名：rename 期间 TreeNode label 给空串、SameLine 一个 `InputText` 接管 label 区域；首帧 `SetKeyboardFocusHere` 抢焦点；`EnterReturnsTrue` 区分 Enter（commit）/ 失焦（cancel via `IsItemDeactivated`）
    - 全局快捷键：Entity Tree panel focused 且未在 rename 中时，`F2` → BeginRename(selected)、`Del` → mark pendingDelete
    - DnD 防御：`src != dst` + `!IsAncestorOf(src, dst)`（自挂自 / 挂进自己子树 → 拒）
    - `DrawInspectorPanel` 顺手升级为读 `selectedEntity` —— 显示 `Entity #N` + name（Task 06-04 才真做组件检视，本 task 仅作"选中态在 UI 上可见"的反馈）
  - **验证**：
    - `cmake --build build --config Debug --target OrangeEditor` 干净
    - 手动跑 `build/bin/Debug/OrangeEditor.exe`：
      - 默认看到 Root（展开 Camera / Light / Geometry（展开 Floor / Wall））和 Misc Sibling 两棵根
      - 点击节点 → Inspector 显示对应 entity id + name
      - F2 / 双击 → 内联 InputText 抢焦，输入新名 Enter 提交；Esc 取消
      - Del → 整子树销毁，selected 清空
      - 拖一个节点到另一节点 → reparent；拖到面板底空白 → 提到 root；自挂自 / 挂进自己子树 → 静默拒绝
  - **Out-of-scope（移交后续 task）**：
    - 组件检视器真实组件编辑（Transform / Renderable / RigidBody 等）→ 06-04
    - 多选 / Shift / Ctrl 多选 → Phase 6 后续
    - Undo / Redo → Phase 7 才考虑
    - Entity 排序稳定性（EnTT view 遍历顺序非创建顺序，同根实体两帧间可能换位）→ 真需要时加 SortIndex 组件

### Task 06-04 · 组件检视器 ✅
- 描述：选中实体后展示其全部 component，提供 `Transform` / `Renderable` / `RigidBody` / `Collider` / `Animator` 等内置组件的可视化编辑控件
- 前置：06-03
- 实现要点：内置组件用 hard-coded inspector；将来用反射/编辑器扩展 API 让游戏组件也能显示（Phase 6 不实现）
- Critical Path：是
- **落地状态**（2026-05-11）：
  - **Existing**（全部在 `tools/OrangeEditor/main.cpp`）：
    - Inspector 主体 `DrawInspectorPanel` 拆成八个 `DrawInspectorXxx`：Name / Transform / Hierarchy / DirectionalLight / Renderable / RigidBody / Collider / Animator。统一模式：HasComponent → CollapsingHeader 段 → 字段控件直接读写 component 字段（修改下一帧反映到 ECS）。组件类型表硬编码（Phase 1–6 禁止 entt::meta / 反射，游戏侧自定义组件留给 Phase 6 后续的"编辑器扩展点 API"）
    - `ComponentHeader` 包装 `CollapsingHeader` + 右键 `Remove Component` 上下文菜单。Name / Hierarchy 不接 ComponentHeader —— Name 总在让 Entity Tree 有名字显示，Hierarchy 是 DnD 维护的结构性数据，手动 remove 会让自身脱离父链 + 子节点孤儿
    - `+ Add Component` 按钮在 Inspector 底，弹 popup 列实体**还没挂**的可添加内置组件：Transform / DirectionalLight / Renderable / RigidBody / Collider。Animator 跳过 —— 它持 `unique_ptr<IAnimator>`，空指针默认构造无意义，需要具体 backend 实例。Hierarchy 跳过 —— DnD 管理
    - `DragVec3Colored(label, v, speed)` 三色 X/Y/Z DragFloat 组合控件（Unity 风格）：X 红 (0.70, 0.18, 0.18) / Y 绿 (0.27, 0.55, 0.27) / Z 蓝 (0.18, 0.36, 0.70)。各分量前一个有色 Button 当 label，Button 装饰、点击无副作用。Transform 三段 + DirectionalLight Direction 都换成它
    - Transform rotation 编辑用 Euler 缓存 (`EditorState::transformEulerCache` + `transformEulerCacheEntity`)：换选中实体才从 quat 推 Euler；同实体连续 DragFloat3 编辑期间用 cache 保证 gimbal lock 附近不抖。Remove Transform 同步清缓存
    - DirectionalLight 段额外 "Normalize Direction" SmallButton —— UI 允许拖中间态非单位向量，按钮归一化
    - RigidBody 段：BodyType (Combo: Static / Kinematic / Dynamic) + initial position / angle / velocity / damping / fixedRotation / gravityScale；`handle` 字段只读显示（是 `PhysicsWorld::AddBody` 反写的运行时引用）
    - Collider 段：std::variant<CircleDesc, BoxDesc, PolygonDesc, EdgeChainDesc> 显示当前 shape 类型 + 各自字段（Circle radius / center；Box halfExtents / center；Polygon、EdgeChain 仅显示顶点数 + 占位）。**不**提供切换 shape 类型控件 —— 切换 = 重新 assign variant alternative，会丢字段；留给 06-05 / 后续 collider 专用 UI
    - 实体创建：Entity Tree 面板背景右键 → `Create Entity (root)`；节点右键 → `Create Child` + Rename(F2) + Delete(Del) 菜单。`EditorState::pendingCreate` 帧末统一 apply（CreateEntity 会修改 entity storage，不能在 EnTT view 迭代中即时 mutate）。新建实体默认带 NameComponent + TransformComponent，自动选中 + 自动进 BeginRename，省一次 F2
    - 节点右键菜单弹出会顺手 select 当前节点
    - rename 进行中**屏蔽**节点右键菜单 —— 避免 InputText 与 ContextItem 输入冲突
    - SeedDemoWorld 扩：给 Light 挂 DirectionalLight；Floor 挂 Renderable + RigidBody(Static) + Collider(Box halfExtents 5.0 × 0.5)；Wall 挂 Renderable —— 让 Inspector 在不同选中下能展示不同组件区块
  - **验证**：
    - `cmake --build build --config Debug --target OrangeEditor` 干净
    - 手动跑：
      - 选 Light → Inspector 显示 Name / Transform / Hierarchy / Directional Light 四段
      - 选 Floor → 再多 Renderable / RigidBody / Collider 三段
      - Transform position / rotation / scale 三组 DragFloat 显示红绿蓝 X/Y/Z 标签
      - Rotation 拖动稳定（gimbal lock 附近不抖），换选中实体后值刷新
      - 右键 component header → Remove Component（Name / Hierarchy 没有此菜单）
      - `+ Add Component` 弹菜单只列实体还没挂的组件；点 "Directional Light" → 立即出现 header；可继续编辑字段
      - 空白右键 → Create Entity 创建 root 实体；节点右键 → Create Child 创建子实体，两种情况都自动选中并进 rename
  - **Out-of-scope（移交后续 task）**：
    - 游戏侧自定义组件 inspector → Phase 6 后续 / 反射方案出来后
    - Collider shape 类型切换 + 多边形顶点编辑 → 06-05 或 collider 专用子模式
    - Animator backend 切换 + 动画状态机编辑 → animation 子模式（Phase 6 后续）
    - mesh handle / materialInstance 编辑（需 Asset 浏览器先做）→ Phase 6 后续
    - Inspector 多选编辑 → Phase 7+

### Task 06-05 · 粒子编辑器子模式 ✅（参数编辑部分；实时预览跟 06-08 绑定）
- 描述：可视化调整粒子发射器参数（emission rate / lifetime / velocity / curve），实时预览
- 前置：06-04、Phase 5 VFX
- Critical Path：否
- **落地状态**（2026-05-11）：
  - **In-scope（参数编辑）已完成**：
    - 新增 `DrawInspectorParticleEmitter` 段，与其它 Inspector 段同 ComponentHeader 模式：emitting 开关 / emissionRate / lifetime 范围 / spawn offset 范围 / initial velocity 范围 / gravity / 颜色 start-end（RGB ColorEdit3 + Alpha 单独 DragFloat 因为 a>1 允许 bloom 拾取超出 [0,1]）/ size start-end / maxParticles（DragInt 转 uint32 + std::max clamp 0）
    - "+ Add Component" 菜单加上 "Particle Emitter"（之前漏了，现在补）
    - Inspector 段尾显式 TextDisabled "(real-time preview pending Task 06-08 viewport)" 说明 preview 状态
  - **Out-of-scope（移交 06-08）**：
    - 真实时预览 —— 需要 Scene 视口渲染（06-08）+ VfxSystem tick + 粒子 pass 接通；编辑器目前完全不渲染 3D，连静态 mesh 都画不出
    - dedicated "粒子编辑器子模式"独立窗口 / 时间线 —— 当前参数编辑借 Inspector 段做覆盖了 task 描述里的 emission rate / lifetime / velocity / curve 四项，独立窗口算"美化"，留给 Phase 6 后续微调

### Task 06-06 · 材质编辑器子模式
- 描述：MaterialTemplate 选择 + uniform 调参 + 纹理槽指派；保存为 `.material` 资源
- 前置：06-04
- Critical Path：否

### Task 06-07 · 场景保存 / 加载流程 ✅
- 描述：从工具栏触发 `Scene::Serialize` / `Scene::Deserialize`；undo/redo 在 Phase 7 再加
- 前置：06-04、Phase 5 序列化
- Critical Path：是
- **落地状态**（2026-05-11）：
  - **Existing**：
    - 顶部 `BeginMainMenuBar` 加 `File` 菜单：New Scene / Open Scene... / Save / Save Scene As... / Exit。Save 在 `currentScenePath` 为空时灰掉，强制走 SaveAs 流程；menu bar 右侧显示当前 scene 路径或 `[Untitled]` 作为状态 indicator
    - `ShowSceneFileDialog`：Windows native IFileDialog 包装（IFileOpenDialog / IFileSaveDialog）—— COM 初始化用 STA + DISABLE_OLE1DDE；filter 固定 `.scene.json`；路径 wide→UTF-8 写回；CoUninitialize 仅在本次实际 init 时调，避免破坏调用方更上层的 COM 上下文。`GLFW_EXPOSE_NATIVE_WIN32` + `glfwGetWin32Window` 取 HWND 做 dialog parent，dialog 表现为 modal child
    - `EditorState` 重构：`pWorld` 从裸指针升级为 `std::unique_ptr<Orange::Engine::World>`（编辑器拥有），新增 `currentScenePath` + `pendingSceneOp { None / New / Open / Save / SaveAs }`。改动让 `OnUpdate` 内整体 swap world 成为可能
    - `SceneOp` 帧末统一 apply：`ApplyPendingSceneOp` 在 `ImGui::Render` 之前执行，dialog 模态阻塞与 ImGui frame 不冲突。Open 路径用临时 `unique_ptr<World>` 装载，`Scene::Load` 失败时保留原 world（与引擎"失败不部分写入"约定对齐）
    - `ResetEntityLocalState` 在 Open / New 切 world 之后清掉 selectedEntity / renamingEntity / pendingDelete / pendingReparent / pendingCreate / transformEulerCacheEntity —— 这些状态全是 per-world entity 身份，新 world 上不再有效
    - `NOMINMAX` / `WIN32_LEAN_AND_MEAN` 移到文件最顶端：GLFW native header 会拉 windows.h，min/max 宏会污染 std::min / std::numeric_limits::max（实测 C2589 / C2737）
    - main() 简化：world 所有权从 main 局部 unique_ptr 搬到 EditorState 字段；启动期种 demo world 一行 `SeedDemoWorld(*editorState.pWorld)`
  - **错误处理**：Save / Load 失败仅 stderr 记录 + 返回原状态，不弹 modal —— 与项目"日志走 stderr，等 Core::Log 接入再改"的过渡期惯例一致
  - **验证**：
    - `cmake --build build --config Debug --target OrangeEditor` 干净
    - 手动跑：File → Save Scene As → 选路径 → 落盘 .scene.json（schemaVersion scene/world v1.0 + 7 demo entities）；改实体（rename / 加组件 / delete）→ File → Open Scene → 选同一文件 → world 回到保存时状态；File → New Scene → 重新种子；快捷 Save 直接覆写当前路径
  - **当前限制**（编辑器尚未驱动 runtime，序列化层 graceful 退化）：
    - 没注入 AssetRegistry → RenderableComponent.mesh handle 落空 + warning
    - 没注入 PhysicsWorld → RigidBody.handle 落 Invalid + warning（数据字段如 type / damping / 等正常 round-trip）
    - 没注入 AnimatorRegistry → AnimatorComponent.animator 空 + warning
  - **Out-of-scope（移交后续 task）**：
    - undo / redo → Phase 7+
    - 快捷键 Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S 实际响应（菜单 label 只是显示文本）→ 后续微调
    - "dirty" 状态指示 + 关闭确认对话框 → Phase 6 后续
    - 多 scene 标签 → Phase 6 后续

### Task 06-08 · 场景视口渲染 ✅
- 描述：Scene 面板内显示 ECS World 的 3D 渲染结果（不是 Entity Tree 的层级，是真实着色的几何），并提供编辑器相机控制（WASD / orbit）。当前 Scene 面板只有占位文案，"看不到场景"是 Phase 6 闭环最后一道缺口
- 前置：06-07
- 实现要点：
  * **架构跳变**：编辑器现在完全不用 engine Pipeline（06-01 落地状态明示"编辑器只渲染 ImGui"）。本 task 反过来要把 engine Pipeline 接入：让 Pipeline 渲到一张 off-screen RT（不是 swap-chain），编辑器侧拿到这张 RT 的 VkImageView，包成 ImGui 用的 descriptor set 喂给 `ImGui::Image` 显示在 Scene 面板里
  * **种子 demo 实体要有真 mesh / material**：现在 SeedDemoWorld 给 Floor / Wall 的 RenderableComponent.mesh 都是 Invalid handle，啥都画不出。本 task 启动时通过 AssetRegistry 加载至少一个内置 mesh（cube / plane），种到 demo 实体上
  * **编辑器相机**：独立的 viewport-local Camera，不污染 World 里可能存在的 game camera。WASD + 鼠标右键拖 = look around；中键平移；滚轮 zoom。相机参数（位置 / 朝向 / FOV）存在 EditorState
  * **viewport 大小变化处理**：Scene 面板 resize → off-screen RT 重建 + descriptor set 重绑；不能每帧 alloc
  * **内置 SPV 部署**：编辑器 exe 旁要能找到内置 shader SPV（已在 06-01 占位讨论里挂账）
- Critical Path：是 —— Phase 6 目标"足以让美术 / 关卡设计师不写代码完成日常工作"必须有视口

### Task 06-09 · 编辑器 Play Mode ✅
- 描述：编辑器引入 Edit / Play / Paused 三态状态机；Play 状态下物理（Box2D PhysicsWorld）+ 粒子（VfxSystem）+ 动画（IAnimator 子类）每帧 tick，让 RigidBody / Collider / ParticleEmitter / Animator 等组件挂上后能在视口看到运行时行为；Stop 还原 World 快照，组件值回 Play 开始前的状态
- 前置：06-08
- 实现要点：
  * **状态机**：EditorState 加 `enum PlayState { Edit, Play, Paused }`；主菜单（或独立 toolbar）加 Play / Pause / Stop 按钮。状态切换走"统一帧末 apply"路径，与 SceneOp 一致风格
  * **World 快照**：Edit → Play 时把 World 序列化到内存 buffer（复用 `Scene::Save` 但走 stringstream 落点）；Stop → Edit 时 `Scene::Load` 反序列化回去。selectedEntity / renamingEntity 等 entity-local UI 状态在 Stop 时清空
  * **PhysicsWorld 接入**：Edit → Play 时实例化 `Phys::PhysicsWorld`，遍历挂了 RigidBody / Collider 的 entity 调 AddBody / AddFixture；每帧 `Step(dt)` 后把 b2Body 位姿写回 TransformComponent；Stop 时销毁
  * **VfxSystem 接入**：Edit → Play 时实例化 `Render::VfxSystem` + 调 Pipeline 的 SetVfxSystem；每帧 Tick；Stop 时先 SetVfxSystem(nullptr) 后销毁
  * **Animator 接入**：每帧遍历 AnimatorComponent 调 `animator->Tick(dt)`（运行时已经支持）
  * **Edit 期 vs Play 期编辑约束**：Play 期间禁用结构性编辑（CreateEntity / Delete / DnD reparent / Rename）—— 防止 simulation invariant 被打破；Inspector 字段编辑也禁（component 是 simulation 输入，运行时改有非确定后果）。Paused 期 = Play 帧暂停 + 编辑同样禁；用户可以选 Inspector 查看运行时数值但不能改
- Critical Path：是 —— 06-08 让"看到场景"，06-09 让"组件挂上能动"。Phase 6 闭环目标是"美术 / 关卡设计师不写代码完成日常工作"，物理 / 粒子可视化是日常工作必备
- **落地状态**（2026-05-12）：
  - **S1**（2026-05-11 已落）：PlayState 枚举 + PlayOp 枚举 + EditorState 字段 + Play/Pause/Stop 按钮 + ApplyPendingPlayOp 骨架
  - **S2**：World 快照落盘（`%TEMP%/OrangeEditor_play_snapshot.scene.json`，带 SaveOptions.assetRegistry 保证 mesh handle round-trip）；Stop 时 Load 还原 + 删 temp 文件 + ResetEntityLocalState
  - **S3**：PhysicsWorld 实例化 + 遍历 `view<RigidBodyComponent, ColliderComponent>` 注册 body（从 TransformComponent 填 initialPosition/initialAngle，handle 反写 ECS）；每帧 Step + dynamic body transform 写回 ECS（Z-axis quat）；Stop 时 reset
  - **S4**：VfxSystem 实例化 + Initialize(renderDevice, 2) + Pipeline::SetVfxSystem；每帧 Tick；Stop 时 SetVfxSystem(nullptr) + Shutdown + reset。Animator 每帧 Tick。Play/Paused 期编辑约束：Entity Tree（快捷键/DnD/右键菜单）+ Inspector（BeginDisabled 包裹全部 DrawInspectorXxx + Add Component）
  - **快照实现说明**：原实现要点写"stringstream 落点"，最终走 temp 文件（`std::filesystem::temp_directory_path()`），语义等价；v0.2 改为 Command Stack capture/restore primitive 时替换（已在 editor-roadmap v0.2 中显式记录）
  - **build**：2026-05-12 `cmake --build build --config Debug --target OrangeEditor` 5 TU 全绿
  - **验证**（待手跑）：Play 点击 → stdout "[play] Edit → Play"；"Dynamic Box"（y=4 悬空）受重力下落，Transform 在 Inspector 实时刷新；粒子 emitter emitting=true 时粒子在 Scene 视口出现（已确认）；Stop 后 World 回到 Play 前状态（entity 位置 / 值与保存时一致）；Play/Paused 期 F2/Del/DnD/Inspector 字段全部 disable
  - **注**：DemoWorld 原只有 Static 刚体，无法演示下落；2026-05-12 补 "Dynamic Box" 实体（toon + Dynamic RigidBody + BoxDesc，y=4）

---

## Phase 6.5 · 渲染真实感基线（PBR + IBL）

**详细 milestone 设计** —— [`pbr-ibl-milestone.md`](./pbr-ibl-milestone.md)。本节是 outline 入口，task 字段（描述 / 前置 / 输出 / 实现要点 / 验证 / Critical Path）以及决议记录 / 风险登记全部在 companion 文件，节奏与 `editor-roadmap.md` 之于 Phase 6 同款。

**目标**：默认 viewport 观感跃迁——从"棋盘 × 阴影"塑料感升级到 PBR + IBL 真实感，对标 Cocos `standard.effect` / Godot `StandardMaterial3D`。Phase 10 渲染深化（SSAO / SSR / 软阴影 / 大气散射）所有 task 建立在本 phase baseline 之上。

**定位**：对位 `Phase 5.5 · Save Game` 先例 —— 独立小 phase 承接基础设施，**不**塞 Phase 10（Phase 10 是按游戏需求拉动的可选渲染深化集合，PBR baseline 是其前置条件而非可选项）。

**拆分**：方案 B 两步走（详细决议见 companion 文件 §决议记录）：

- **B.1 · PBR direct lighting**（3-5 天，不依赖跨仓）：monolithic PBR shader（Cook-Torrance + GGX + Schlick + Smith correlated）+ MaterialInstance 五通道（baseColor / metallic / roughness / normal / AO）+ sample `13_pbr_direct`；IBL 槽位绑 dummy 1×1 黑纹理退化为 direct-only，B.2 阶段替换为真实纹理**零 shader 重构**
- **B.2 · IBL 完整接入**（1.5-2 周，依赖跨仓 R1/R2/R3 audit pass）：BRDF LUT + irradiance + prefiltered specular 三种卷积烘焙合并单 task（启动期烘焙，对照 Lumix `data/shaders/ibl_filter.hlsl` 122 行单文件多 entry 风格）+ EnvironmentComponent + PolyHaven CC0 default IBL + sample `14_pbr_ibl`

**前置**：Phase 6 编辑器 v0.1 ✅

**跨仓依赖**（OrangeRender 侧 audit）：R1 cubemap 完整 binding 链 / R2 mipmap level-by-level upload / R3 R16G16F format 创建。Lumix（Vulkan + DX12 双后端）跑通这套，audit pass 概率高但仍需独立 session 验证；audit session **B.1 commit-1 当天启动**，与 B.1 实施并行不阻塞。

**编辑器伴随**：
- B.1 Task 06.5-02 同 commit 序列内补 Material schema 同步（hours 级，不另立 milestone）
- B.2 Task 06.5-06 同 commit 序列内补 Environment schema 同步
- 完整编辑器扩展（Environment 浏览 / material thumbnail / sky placeholder）作为**独立 v0.8 编辑器 milestone**在 B.2 完工后启动，见 `editor-roadmap.md`

### Task 06.5-01 · monolithic PBR shader 落地（IBL 槽位 dummy） ✅

详细字段：companion §Task PBR-01。一份 shader 同时含 direct + IBL 全路径，IBL 三槽位 B.1 期间绑 dummy 1×1 黑纹理自然退化，**对标 Lumix `data/shaders/standard.hlsl` monolithic 风格**。`textured_mesh.{vert,frag}` 不删保留作 dev fallback。Critical Path。

### Task 06.5-02 · MaterialInstance 五通道 + texture binding（含 Inspector schema 同步） ✅

详细字段：companion §Task PBR-02。同 commit 序列内完成编辑器 Inspector schema 同步，避免"PBR ✅ 但 Inspector 看不到 metallic / roughness 字段"断层态。Critical Path。

### Task 06.5-03 · sample `13_pbr_direct` + B.1 验收 ✅

详细字段：companion §Task PBR-03。9 球阵（3 metallic × 3 roughness）+ 1 个 directional light，**不接 IBL**。Critical Path。

> **B.1 完工 ritual**：跑两份 lint，标 06.5-01 / 02 / 03 ✅，确认 OrangeRender audit 收尾状态（R1/R2/R3 全 pass 或 OrangeRender 侧已 land + bump 完）后才进 B.2。

### Task 06.5-04 · IBL 三种卷积烘焙（合并 BRDF LUT + irradiance + prefiltered specular）

详细字段：companion §Task PBR-04。单一烘焙路径产出 IBL 三件套，**启动期一次性烘焙**（不走编译期 codegen——理由：IBL prefilter 必须启动期，多一条编译期路径属工程复杂度净增）。Critical Path（依赖 R2 audit pass）。

### Task 06.5-05 · IBL 接入 PBR shader（替换 dummy 槽位）

详细字段：companion §Task PBR-05。把 B.1 阶段 dummy 1×1 黑 IBL 纹理替换成 06.5-04 烘焙产物，**shader 一行不改**（B.1 shader 已预留 IBL 段）。Critical Path。

### Task 06.5-06 · EnvironmentComponent + 资产管线

详细字段：companion §Task PBR-06。World 全局 `EnvironmentComponent`（cubemap asset + intensity + tint），**对标 Lumix `render_module.h:267 EnvProbeInfo`**。default IBL 从 **PolyHaven CC0** HDRI 站选 1K outdoor scene equirect 入库 `assets/environments/`。Critical Path。

### Task 06.5-07 · sample `14_pbr_ibl` + B.2 验收

详细字段：companion §Task PBR-07。9 球阵 + IBL 环境 + 1 directional light + furnace test（验能量守恒）。Critical Path。

> **Phase 6.5 完工 ritual**：跑两份 lint，标 Phase 6.5 ✅，拉 v0.8 编辑器伴随 milestone 立项。

---

## Phase 7 · C# Scripting (CoreCLR Hosting)

**目标**：让游戏侧能用 C# 写 component / system，引擎用 CoreCLR hosting 嵌入 .NET runtime；C++ 与 C# 通过反向 P/Invoke + 受控 marshalling 通讯。**这是大工程，预算 3–6 个月**，且只有在游戏侧明确反馈 C++ 编译循环慢到不可接受时才启动。

**闭环后解锁**：游戏迭代速度大幅提升；非引擎程序员（gameplay programmer）可以不接触 C++ 完成大部分工作。

### Task 07-01 · CoreCLR hosting 基础
- 描述：嵌入 hostfxr / nethost，加载 .NET 8+ runtime，启动 default AppDomain
- 前置：无（独立工程实验）
- 实现要点：仅在 `BUILD_SHARED_LIBS=OFF` 时通过 `ORANGE_ENGINE_WITH_DOTNET=ON` 启用
- Critical Path：是

### Task 07-02 · C# ↔ C++ marshalling 设计
- 描述：定义 marshalling 边界规则——值类型走 P/Invoke 直传，引用类型走 handle，禁止跨边界 GC
- 前置：07-01
- 实现要点：参考 Unity ECS Burst-compiled jobs 与 Unreal CoreUObject 的 reflection；目标是 hot path 调用 < 100ns overhead
- Critical Path：是

### Task 07-03 · 反射元数据生成器
- 描述：编译期工具扫描 C++ 头文件，生成 C# binding 代码（component 字段、system 入口、引擎 API 子集）
- 前置：07-02
- 实现要点：基于 libclang AST 而非手写宏；输出 C# 文件 + C 风格 ABI shim
- Critical Path：是

### Task 07-04 · ECS 组件 / 系统的 C# 表达
- 描述：C# `[Component]` / `[System]` attribute；C# 定义的 component 自动注册到 EnTT
- 前置：07-03
- Critical Path：是

### Task 07-05 · 调试器接通
- 描述：C# 端可挂 Visual Studio C# debugger；C++ 与 C# 联调
- 前置：07-04
- Critical Path：否

---

## Phase 8 · Hot Reload

**目标**：加快迭代循环——shader 改了不重启、scene 改了不重启、游戏 DLL 换了不重启。优先级：shader > scene > DLL。

### Task 08-01 · Shader hot reload
- 描述：监视 `.glsl` / `.hlsl` 改动，重新编译为 SPIR-V，热替换 Material 引用
- 前置：Phase 3
- 实现要点：失败时不崩溃，回退上一版本 + 控制台报错
- Critical Path：是

### Task 08-02 · Scene hot reload
- 描述：编辑器外部修改 `.scene` JSON，引擎检测到 mtime 变化后 reload，保留运行时实体的差量 diff
- 前置：Phase 5 序列化
- Critical Path：否

### Task 08-03 · Game DLL plugin reload
- 描述：游戏代码作为可热插拔的 plugin DLL，引擎运行中卸载/重载
- 前置：07-04（如果走 C#，DLL reload 由 .NET 提供）；纯 C++ 路径需要 plugin ABI 设计
- 实现要点：相当大的工程；考虑只支持"system 函数指针重定向"而不是任意状态迁移
- Critical Path：否

---

## Phase 9 · Asset Cook Pipeline

**目标**：建立离线资产工具链，把 source asset（FBX、PNG、WAV）烤成引擎运行时格式（自定义 mesh binary、KTX2 纹理、压缩音频）。在第一款游戏 ship 前可以不做（直接用 source asset 运行也能玩），但发布版必须有。

### Task 09-01 · CLI cook 工具
- 描述：`orange-cook --input assets/ --output cooked/ --target windows`
- 前置：无
- 实现要点：每种资源类型一个 cooker；输出包含 schema 版本头
- Critical Path：是

### Task 09-02 · Mesh cook（assimp + 自定义 binary）
- 描述：FBX/glTF → 引擎 mesh binary，包含顶点 / 索引 / 子网格 / 包围盒
- 前置：09-01
- Critical Path：是

### Task 09-03 · Texture cook（KTX2 + Basis Universal）
- 描述：PNG / TGA → KTX2，BC7 / ASTC 压缩，mipmap 链生成
- 前置：09-01
- Critical Path：是

### Task 09-04 · Shader cook
- 描述：`.glsl` → SPIR-V + 反射元数据；离线避免运行时编译开销
- 前置：09-01
- Critical Path：否

### Task 09-05 · Audio cook
- 描述：WAV → OGG/Opus
- 前置：09-01
- Critical Path：否

### Task 09-06 · Scene cook
- 描述：JSON scene → binary scene，缩短加载时间
- 前置：Phase 5 序列化
- Critical Path：否

---

## Phase 10 · 渲染深化（按游戏需求拉动）

仅在第一款游戏需要时启用。每条都是独立 feature。

- Task 10-01 · 多视口 / 分屏（OrangeRender 已有 `multi_view` sample，封装到引擎 API）
- Task 10-02 · 屏幕空间反射（如果需要水面）
- Task 10-03 · 屏幕空间环境光遮蔽（SSAO，强化室内场景）
- Task 10-04 · GPU 粒子（迁移 Phase 5 的 CPU 粒子到 compute shader）
- Task 10-05 · 高质量软阴影（PCSS / VSM）
- Task 10-06 · 大气散射（户外地图）
- Task 10-07 · PointLight + 多 light 支持（详见 `docs/engine-known-gaps.md` GAP-2026-05-11-point-light-and-visible-halo：PointLightComponent 公共接口 + Pipeline 多 light 收集 / forward shading + billboard 可见光晕近似；omnidirectional shadow map 留给更后的任务）

---

## Phase 11 · 网络与多人（极远）

**触发条件**：第一款游戏明确加入多人功能。在那之前不写一行。

- Task 11-01 · ENet / yojimbo 集成（client/server）
- Task 11-02 · ECS 状态同步框架
- Task 11-03 · 延迟补偿
- Task 11-04 · 房间管理 / 匹配
- Task 11-05 · 反作弊基础

---

## Phase 12 · 跨平台（条件触发）

**触发条件**：游戏要发 macOS / Linux / 主机 / 移动端。在那之前不投资跨平台抽象。

- Task 12-01 · OrangeRender DX12 后端启用（依赖 OrangeRender 本身完成 DX12 后端）
- Task 12-02 · OrangeRender Metal 后端启用（同上）
- Task 12-03 · Linux 平台层（GLFW 已支持，主要是 file watcher / clock 等细节）
- Task 12-04 · Console SDK 集成（PS5 / Xbox / Switch，每个都是单独的法务和工程包）

---

## Phase 13 · 自然场景与地形（按编辑器需求拉动）

**触发条件**：游戏关卡需要"自然环境"内容——地形 / 水体 / 山体 / 植被等程序化或半程序化场景元素。第一款 2.5D 平台跳跃 demo 不一定需要；若游戏向"开放区域 / 自然场景"扩展则启用。

**当前状态**（2026-05-09 记录）：引擎完全不具备地形 / 水体 / 程序化自然场景能力。Phase 1-5.5 全部聚焦在通用 ECS / 渲染 / 物理 / 动画 / 存档 / 输入等基础设施；`Phase 10 · 渲染深化` 中的"屏幕空间反射 (10-02)"和"大气散射 (10-06)"是渲染效果而非内容创作；`extension-points.md` 中的 `WaterPass` 仅是"如何写自定义 IRenderPass"的示例代码，不是引擎 feature。

**预期内容**（按需在游戏侧反馈拉动时展开为完整 Task Breakdown）：
- Task 13-01 · `TerrainComponent` + heightmap asset 格式（与 `MeshAsset` 解耦，按 patch / chunk 切分以支持 LOD）
- Task 13-02 · 编辑器 terrain sculpting 工具（笔刷 raise / lower / smooth / flatten / stamp）
- Task 13-03 · `WaterSurfaceComponent` + 着色器（顶点波浪 + 折射 / 反射；可与 Task 10-02 SSR 联动）
- Task 13-04 · 植被 / 散物 instancing（`InstancedRenderableComponent`，支持 GPU 数万实例 + frustum cull）
- Task 13-05 · 编辑器植被 painting + 分布工具（密度笔刷 / mesh scatter / 风格化噪声）
- Task 13-06 · 程序化辅助库（noise / curl / Voronoi 等基础 utility，给 game 端做地形 / 水流 procedural authoring 用）

**前置**：Phase 6 编辑器（13-02 / 13-05 是编辑器交互工具）；与 Phase 10 渲染深化可并行；可与 Phase 13 自身各 Task 乱序推进。
**Critical Path**：否（仅在游戏明确需求时启动；Phase 13 整体可与 Phase 11 / 12 并行排布）

---

## 不做的事（明确排除）

下列功能在引擎层**不**实现，除非有明确的游戏需求拉动：

- 大世界 streaming（第一款游戏关卡可一次性加载）
- 3D 骨骼蒙皮（第一款游戏是 2.5D，不需要 3D skeleton）
- 全局光照 / 路径追踪（视觉风格用不到）
- 游戏内 GUI 富文本编辑器（用 ImGui 足以）
- VR / AR 支持
- 录像与回放（除非游戏需要 replay）
- 本地化框架（直接用查表 JSON 即可）
- 反作弊
- 商店 / IAP / 玩家账号系统

如果上述需求未来突然变成 critical，再写"破例 Phase"，不混入主线 roadmap。

---

## Self-Check

- Phase 6+ 的所有任务都不是 Phase 1–5 任意 task 的前置依赖
- 不存在"必须做完整个 Phase 6 才能进 Phase 7"的强约束——这些 Phase 是**能力包**，按游戏侧反馈选择性启用
- 所有"为假想未来而做"的工作都列在"不做的事"清单
- 工业级体量的工作（C# scripting / DX12 后端 / 跨平台）显式标注了体量与启动门槛
- 任何一个 Phase 6+ 的工作都不会被"必须先做"卡住，只会被"暂时不值得做"延后
