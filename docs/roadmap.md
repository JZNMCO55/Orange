# OrangeEngine 长期路线图（Phase 6+）

- 基准日期：2026-05-05
- 基准状态：Phase 1 未开始；Phase 1–5 任务表见 [`design-plan.md`](./design-plan.md)
- 范围：Phase 6+ 表示"第一款游戏 fork 出去之后"的引擎演进；这些阶段不是 game-shipping critical path 的一部分，按"游戏仓库的反馈拉动 + 引擎自身能力补齐"双驱动决定优先级
- 任务字段沿用 `design-plan.md` 风格（描述 / 输入 / 输出 / 影响模块 / 前置 / 实现要点 / 验证 / 验收 / Critical Path）

> **关于 Phase 6+ 的实际节奏**：与 Phase 1–5 必须按依赖顺序串行不同，Phase 6+ 可以**乱序、跳跃、并行**。它们更像独立的能力包，而不是流水线。这份路线图给出的是一个建议优先级排序，而不是必须遵守的执行顺序。

---

## Phase 6 · 编辑器 v0.1

**目标**：把 `samples/07_full_pipeline` 升级为独立 target `tools/OrangeEditor`，提供关卡 / 粒子 / 材质三个最小工具子集，足以让美术 / 关卡设计师不写代码完成日常工作。

**闭环后解锁**：游戏 content 生产从"程序员手写 JSON"过渡到"编辑器交互式编辑"。

### Task 06-01 · 提取 `tools/OrangeEditor` target
- 描述：从 `samples/07_full_pipeline` 复制基础结构到 `tools/OrangeEditor`，独立 CMake target；`samples/` 仅保留作引擎 API 演示，编辑器走自己的演化路径
- 前置：Phase 5 完成
- 实现要点：编辑器通过 `find_package(OrangeEngine)` 依赖引擎，不直接吃源；这是引擎 API 自身可消费性的最强验证
- Critical Path：是

### Task 06-02 · ImGui 集成与 dock space
- 描述：默认窗口布局：场景视图 / 实体树 / 检视器 / 资源浏览器 / 控制台
- 前置：06-01
- 实现要点：ImGui 的 GLFW + Vulkan backend 已通过 OrangeRender 集成可复用；编辑器 UI 全走 ImGui
- Critical Path：是

### Task 06-03 · 实体树视图
- 描述：可视化 ECS World，支持 select / 重命名 / 删除 / 拖拽改父子关系
- 前置：06-02
- 实现要点：通过 `World::GetNativeRegistry()` 直通访问 EnTT；不引入新公共 API
- Critical Path：是

### Task 06-04 · 组件检视器
- 描述：选中实体后展示其全部 component，提供 `Transform` / `Renderable` / `RigidBody` / `Collider` / `Animator` 等内置组件的可视化编辑控件
- 前置：06-03
- 实现要点：内置组件用 hard-coded inspector；将来用反射/编辑器扩展 API 让游戏组件也能显示（Phase 6 不实现）
- Critical Path：是

### Task 06-05 · 粒子编辑器子模式
- 描述：可视化调整粒子发射器参数（emission rate / lifetime / velocity / curve），实时预览
- 前置：06-04、Phase 5 VFX
- Critical Path：否

### Task 06-06 · 材质编辑器子模式
- 描述：MaterialTemplate 选择 + uniform 调参 + 纹理槽指派；保存为 `.material` 资源
- 前置：06-04
- Critical Path：否

### Task 06-07 · 场景保存 / 加载流程
- 描述：从工具栏触发 `Scene::Serialize` / `Scene::Deserialize`；undo/redo 在 Phase 7 再加
- 前置：06-04、Phase 5 序列化
- Critical Path：是

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
