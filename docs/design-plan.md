# OrangeEngine 设计与实施规划

## 目录

- [Current State and Assumptions](#current-state-and-assumptions)
  - [当前仓库状态](#当前仓库状态)
  - [明确假设](#明确假设)
  - [关键风险](#关键风险)
- [Design Doc](#design-doc)
  - [架构目标](#架构目标)
  - [与 OrangeRender 的边界](#与-orangerender-的边界)
  - [分层结构](#分层结构)
  - [分层职责](#分层职责)
  - [数据流](#数据流)
  - [反射与序列化策略](#反射与序列化策略)
  - [核心 API 边界](#核心-api-边界)
  - [最小 API 草案](#最小-api-草案)
  - [模块划分](#模块划分)
  - [建议目录结构](#建议目录结构)
  - [文件级起步建议](#文件级起步建议)
- [Roadmap](#roadmap)
  - [Phase 1：引擎骨架与最小可运行路径](#phase-1引擎骨架与最小可运行路径)
  - [Phase 2：数据流贯通](#phase-2数据流贯通)
  - [Phase 3：Ori 视觉基线 + 自定义 shader 接入](#phase-3ori-视觉基线--自定义-shader-接入)
  - [Phase 4：可玩性](#phase-4可玩性)
  - [Phase 5：生产化与游戏 fork 时机](#phase-5生产化与游戏-fork-时机)
  - [Phase 5.5：Save Game 系统](#phase-55save-game-系统)
  - [Phase 6+：长期演进](#phase-6长期演进)
- [Task Breakdown](#task-breakdown)
  - [Phase 1：引擎骨架与最小可运行路径](#phase-1引擎骨架与最小可运行路径-1)
    - [Task 01：清理旧骨架并建立新目录结构 ✅](#task-01清理旧骨架并建立新目录结构-)
    - [Task 02：建立顶层 CMake 与 `ORANGE_ENGINE_API` 宏机制 ✅](#task-02建立顶层-cmake-与-orange_engine_api-宏机制-)
    - [Task 03：第三方依赖接入与 vendor submodule 化 ✅](#task-03第三方依赖接入与-vendor-submodule-化-)
    - [Task 04：定义 Core 基础类型 ✅](#task-04定义-core-基础类型-)
    - [Task 05：实现 Core 序列化原语与启动配置 ✅](#task-05实现-core-序列化原语与启动配置-)
    - [Task 06：实现 Platform::Window ✅](#task-06实现-platformwindow-)
    - [Task 07：定义 App 层（Layer / LayerStack / FrameContext） ✅](#task-07定义-app-层layer--layerstack--framecontext-)
    - [Task 08：实现 AppHost 主循环 ✅](#task-08实现-apphost-主循环-)
    - [Task 09：完成 `samples/01_minimal_window` ✅](#task-09完成-samples01_minimal_window-)
    - [Task 10：建立测试入口 ✅](#task-10建立测试入口-)
    - [Task 11：发布 install 与 `OrangeEngineConfig.cmake` ✅](#task-11发布-install-与-orangeengineconfigcmake-)
  - [Phase 2：数据流贯通](#phase-2数据流贯通-1)
    - [Task 01：定义 `Asset` 公共接口 ✅](#task-01定义-asset-公共接口-)
    - [Task 02：实现 Mesh / Texture / Shader 三个内置 loader ✅](#task-02实现-mesh--texture--shader-三个内置-loader-)
    - [Task 03：定义 Scene 公共接口（World / Entity / ISystem / Transform / Hierarchy） ✅](#task-03定义-scene-公共接口world--entity--isystem--transform--hierarchy-)
    - [Task 04：实现 EnTT 包装（World 的后端） ✅](#task-04实现-entt-包装world-的后端-)
    - [Task 05：定义 Render 模块公共接口（Camera / RenderableComponent / Pipeline） ✅](#task-05定义-render-模块公共接口camera--renderablecomponent--pipeline-)
    - [Task 06：实现 RenderScene 收集（World → drawable list） ✅](#task-06实现-renderscene-收集world--drawable-list-)
    - [Task 07：实现最小 Pipeline（接通 OrangeRender RenderGraph，绘制单个 mesh） ✅](#task-07实现最小-pipeline接通-orangerender-rendergraph绘制单个-mesh-)
    - [Task 08：完成 `samples/02_ecs_basics`（实体/组件 CRUD） ✅](#task-08完成-samples02_ecs_basics实体组件-crud-)
    - [Task 09：完成 `samples/03_textured_quad`（带贴图四边形） ✅](#task-09完成-samples03_textured_quad带贴图四边形-)
    - [Task 10：完成 `samples/04_3d_mesh`（旋转 3D mesh，无 bloom） ✅](#task-10完成-samples04_3d_mesh旋转-3d-mesh无-bloom-)
  - [Phase 3：Ori 视觉基线](#phase-3ori-视觉基线)
    - [Task 01：定义 Material / MaterialInstance 公共接口 ✅](#task-01定义-material--materialinstance-公共接口-)
    - [Task 02：MaterialTemplate 内置（卡通 + rim light） ✅](#task-02materialtemplate-内置卡通--rim-light-)
    - [Task 03：PostProcessChain 接口与默认链（HDR → Bloom → Tonemap → LUT） ✅](#task-03postprocesschain-接口与默认链hdr--bloom--tonemap--lut-)
    - [Task 04：MaterialSystem 自定义 shader 注入接口 ✅](#task-04materialsystem-自定义-shader-注入接口-)
    - [Task 05：软阴影 LightComponent + ShadowConfig + 着色侧产物 ✅](#task-05软阴影-lightcomponent--shadowconfig--着色侧产物-)
    - [Task 06：升级 `samples/04_3d_mesh_with_bloom` ✅](#task-06升级-samples04_3d_mesh_with_bloompipeline-接通-materialinstance--postprocesschain-真路径-)
    - [Task 07：`samples/07_full_pipeline` 雏形 ✅](#task-07samples07_full_pipeline-雏形pipeline-接通-shadow-pass--multi-entity-综合演示-)
    - [Task 08：自定义 shader sample ✅](#task-08自定义-shader-sample验证-materialsystemregistertemplate-扩展点-)
  - [Phase 4：可玩性](#phase-4可玩性-1)
    - [Task 01：Animation 公共接口 ✅](#task-01animation-公共接口ianimator--animationstatemachine--animatorregistry-)
    - [Task 02：DragonBones C++ runtime 集成 ✅](#task-02dragonbones-c-runtime-集成srcanimationdragonbones-)
    - [Task 03：SkeletalAnimator 实现 ✅](#task-03skeletalanimator-实现dragonbones-后端--matrix-palette-输出-)
    - [Task 04：ProceduralAnimator 实现 ✅](#task-04proceduralanimator-实现shader-uniform-驱动-)
    - [Task 05：Physics 公共接口 ✅](#task-05physics-公共接口physicsworld--rigidbody--collider-组件-)
    - [Task 06：Box2D 3.x 集成 ✅](#task-06box2d-3x-集成srcphysicsbox2d-)
    - [Task 07：Fixture 替换 API ✅](#task-07fixture-替换-api运行时碰撞器变形支持-)
    - [Task 08：Input 模块 ✅](#task-08input-模块action--actionmap--inputcontext--json-加载-)
    - [Task 09：Audio 模块 ✅](#task-09audio-模块miniaudio-集成-)
    - [Task 10：`samples/05_skeletal_animation` + `samples/06_physics_platformer` ✅](#task-10samples05_skeletal_animation--samples06_physics_platformer-)
    - [Task 11：`samples/07_full_pipeline` 升级为可玩 demo ✅](#task-11samples07_full_pipeline-升级为可玩-demoanimation--physics--input-综合-)
  - [Phase 5：生产化](#phase-5生产化)
  - [Phase 5.5：Save Game 系统](#phase-55save-game-系统-1)
- [Self-Check](#self-check)
- [后续篇章](#后续篇章)

---

## Current State and Assumptions

### 当前仓库状态

#### 已存在
- `README.md`（占位，仅"Orange Engine"两行）
- `LICENSE`
- `CMakeLists.txt`（顶层占位，引用 `Src/OrangeEngine` 与 `Src/OrangeEditor` 两个空骨架）
- `3rdparty.json`（依赖清单：`spdlog` / `tracy` / `glm` / `fmt` / `glfw` / `VulkanSDK` / `VMA`）
- `Src/OrangeEngine/{Layer1,Layer2,Orange.h,orgpch.h}`（旧 GEA 四层骨架，已弃用）
- `Src/OrangeEditor/Entry`（空骨架）
- `docs/Technical Documentation/GameEngineStructure.md`（历史架构文档，按 *Game Engine Architecture* 第三版分层；当前已弃用，仅作历史记录）
- `vendor/OrangeRender/`（本地仓库；非 submodule；提供 `OrangeRender::orange_render` 静态库，覆盖 RHI + Vulkan 后端 + Render Graph，对外通过 `find_package(OrangeRender CONFIG)` 暴露）
- `vendor/Orange-Wiki/`（本地仓库；非 submodule；当前固定在 `Orange-Render-Wiki` 分支；包含 Game Engine Architecture / Real-Time Rendering 的 ingest 知识库，作为 LLM 消费端）

#### 当前缺失
- 无 `include/`（公共 API 头）
- 无重构后的 `src/`（只剩历史空骨架 `Src/OrangeEngine/Layer1`、`Layer2` 和 `OrangeEditor`）
- 无可运行的引擎代码（`Orange.h` 为空）
- 无 `find_package(OrangeRender)` 接入
- 无 ECS、资源、场景、动画、物理、音频、输入等任何子系统的实现
- 无 `samples/` 与 `tests/` 目录
- 无 `cmake/` 工具链与 Config 模板
- 无 `CHANGELOG.md`
- 无 `CLAUDE.md`（消费端 AI 入口）
- 无对 `Orange-Wiki` 与 `OrangeRender` 的 git submodule 声明

#### 对起步阶段的含义
- 项目应**整体推倒** `Src/` 下旧骨架，按 OrangeRender 的交付范式重建；`docs/Technical Documentation/` 保留为历史档案，不再作为施工依据。
- 优先解决目录结构、构建方式、与 OrangeRender 的接入方式、`OrangeEngine::orange_engine` 的 Public API 边界、最小可运行路径。
- 不能在 Phase 1 就引入 ECS / 动画 / 物理这类大模块；它们必须排到后续阶段，先把交付物的基础形态建立起来。

### 明确假设

#### 工具链假设
- 目标平台：Windows 11
- 编译器：MSVC 2022
- 构建系统：CMake 3.28+
- C++ 标准：C++20，`CMAKE_CXX_EXTENSIONS OFF`
- 渲染底座：`OrangeRender 0.1.x`（间接依赖 Vulkan 1.3）
- 库交付形式：静态库优先（`OrangeEngine::orange_engine`），`BUILD_SHARED_LIBS=ON` 时切换到 dll，使用 `ORANGE_ENGINE_API` 宏机制
- 示例程序：`samples/` 下若干独立可执行
- 单元测试：`tests/` 下接入 ctest，Phase 1 只建立测试入口

#### 依赖策略假设
- 渲染：通过 `find_package(OrangeRender 0.1 CONFIG REQUIRED)` 接入；OrangeRender 自身已 `find_dependency` 了 `volk` / `glfw3` / `VulkanMemoryAllocator`，本仓不需重复声明
- 数学：`glm`（PUBLIC，沿 OrangeRender 公共头出现）
- 日志：`spdlog`（默认开启）
- ECS：`EnTT`（PUBLIC，组件/系统类型在公共头中出现）
- 物理：`Box2D 3.x`（PRIVATE，只出现在 `src/physics/box2d/`）
- 2D 骨骼动画：`DragonBones C++ runtime`（PRIVATE，只出现在 `src/animation/dragonbones/`）
- 音频：`miniaudio`（PRIVATE）
- 图像加载：`stb_image`（PRIVATE）
- 字体：`stb_truetype`（PRIVATE，UI 与游戏内文本共用）
- 序列化：`nlohmann/json`（PUBLIC 用于公共 schema 字段）
- UI（编辑器与运行时调试覆盖）：`Dear ImGui`（PRIVATE，集成 ImGui 的 GLFW + Vulkan backend）
- 性能分析：`tracy`（默认 OFF，`ORANGE_ENGINE_WITH_TRACY=ON` 时打开）

#### 关键未知项
- 是否要求 dll ABI 在 0.x 阶段保持稳定（默认：与 OrangeRender 一致，不保证）
- 是否要求支持非 Windows 平台（默认：暂不）
- 是否要求 Hot reload / DLL plugin（默认：Phase 6+ 再讨论）
- 脚本层最终选型（C# / Lua / 纯 C++）（默认：Phase 1–5 不引入脚本层）
- 第一款游戏的资源体量与关卡数（不影响引擎设计，但影响 Phase 5 的 streaming 优先级）

### 关键风险
- 若引擎公共头泄露 OrangeRender / Box2D / DragonBones / miniaudio 的私有类型，"runtime 可替换"承诺立即作废。**这是和 OrangeRender 的 RHI 不许漏 Vulkan 同构的项目级 invariant**。
- 若 Phase 1 把 ECS / Asset / Render 三件套同时拉起，会陷入"三个未完成模块互相 stub"的死循环。必须严格按依赖顺序串行。
- 若 Animation 模块在最初就只服务于 Skeletal 后端，后期接 Procedural 后端会被迫返工。设计必须从一开始就承认两类后端并列。
- 若 Material 系统硬编码若干内置 shader 槽位，将无法满足"游戏侧自定义 shader"这一已知需求。Material 必须从 Phase 3 起就支持 shader 注入。
- 若引擎仓库为单一游戏定制功能（例如内置"史莱姆角色形态系统"），引擎扩展性破产。**game-specific 机制必须留在游戏仓库**，引擎只提供 ECS + 多动画后端 + Fixture 替换 + dissolve VFX 这些原子能力。

---

## Design Doc

### 架构目标

`OrangeEngine` 是一个面向 2D / 2.5D 游戏的通用 C++ 游戏框架，作为 `OrangeEngine::orange_engine` 静态库（可切 dll）通过 `find_package(OrangeEngine CONFIG)` 暴露给独立的游戏仓库。它持有：场景表达、资源管理、动画运行时、物理仿真、音频、输入、应用主循环；它不持有：具体游戏的玩法、剧情、关卡数据、角色形态系统、UI 美术。

引擎在垂直方向构筑于 `OrangeRender` 之上，由 `Render` 模块作为唯一桥接点；在水平方向通过 ECS（EnTT）+ 资源工厂 + 动画后端注册 + 自定义 shader 注入提供扩展面，让游戏侧不修改引擎源码即可加入新的 component / system / 资源类型 / 渲染效果。

### 与 OrangeRender 的边界

```text
┌────────────────────────────────────────────────────────┐
│  Game Repository (find_package(OrangeEngine))          │
│   - GameComponent / GameSystem (EnTT)                  │
│   - 自定义 shader / 自定义 RenderPass                   │
│   - 自定义 AssetType                                    │
└────────────────────────────────────────────────────────┘
                           │ 仅依赖 OrangeEngine 公共头
                           ▼
┌────────────────────────────────────────────────────────┐
│  OrangeEngine (本仓库)                                  │
│   include/orange/engine/...     公共 API                │
│   src/...                       私有实现                │
│   src/render/                   ★ 唯一允许 #include      │
│                                 <orange/...> 的目录      │
└────────────────────────────────────────────────────────┘
                           │ find_package(OrangeRender)
                           ▼
┌────────────────────────────────────────────────────────┐
│  OrangeRender (vendor 仓库)                             │
│   Application / RenderFramework / RenderGraph / RHI    │
│   Vulkan Backend                                        │
└────────────────────────────────────────────────────────┘
```

边界 invariant：

- 引擎公共头永远不出现 OrangeRender 类型（`Orange::Rhi::*`、`Orange::*Pipeline*` 等）
- 引擎公共头永远不出现 Box2D / DragonBones / miniaudio / Vulkan 类型
- `<orange/...>` 仅允许出现在 `src/render/` 内
- `<box2d/...>` 仅允许出现在 `src/physics/box2d/` 内
- `<dragonBones/...>` 仅允许出现在 `src/animation/dragonbones/` 内
- `<miniaudio.h>` 仅允许出现在 `src/audio/miniaudio/` 内
- 游戏仓库与编辑器仓库永远只 `#include <orange/engine/...>`，不直接依赖 OrangeRender 与上述任何 runtime

### 分层结构

水平模块化（不分层金字塔）：

```text
                ┌──────────┐
                │   App    │  ← 主循环 / Layer / FrameContext
                └────┬─────┘
                     │
         ┌───────────┼───────────┐
         ▼           ▼           ▼
      ┌──────┐  ┌────────┐  ┌────────┐
      │ Input│  │  Scene │  │ Render │  ← 三个核心横切模块
      └──┬───┘  └───┬────┘  └───┬────┘
         │          │            │
         │     ┌────┴────┐  ┌────┴────┐  ┌──────────┐
         │     │Animation│  │  Asset  │  │Postprocess│
         │     │ Physics │  │         │  │   VFX    │
         │     │  Audio  │  │         │  └──────────┘
         │     └─────────┘  └─────────┘
         │          │            │
         └──────────┴────────────┘
                    │
              ┌─────┴─────┐
              │Platform   │  ← Window / Input 设备 / 文件系统 / 时钟
              └─────┬─────┘
                    │
              ┌─────┴─────┐
              │   Core    │  ← 类型 / 日志 / Handle / Result / 数学
              └───────────┘
```

注意三件事：

1. `Render` 是唯一向下依赖 OrangeRender 的模块；其他模块**不感知**渲染后端
2. `Animation` / `Physics` / `Audio` 都依赖 `Scene`（通过 ECS component 表达运行时状态）但彼此正交
3. `Platform` 与 `Core` 是所有上层模块的基础底座，不允许反向依赖

### 分层职责

#### Core
- 类型别名：`Vec2/3/4`、`Mat3/4`、`Quat`（透传 glm，便于将来替换）
- `Result<T, E>` / `ResultCode` 错误码系统
- `TypedHandle<Tag>` 强类型句柄
- `Log`（spdlog 包装；编译期可选）
- `Time`（DeltaSeconds / 帧序号 / 累计时长 / 固定步长辅助）
- `Hash` / `Id` / `StringId`（编译期 FNV-1a 哈希）
- `Serialization`：`JsonReader` / `JsonWriter`（封装 nlohmann，统一错误格式与定位信息）；`BinaryReader` / `BinaryWriter`（little-endian 固定，blob I/O）；`SchemaVersion` 约定（每个 schema 一个 namespace + version 号 + 可选 migrator hook）。**所有 read/write 函数手写**，不引入运行时反射库
- `Config`：键值 in-memory 存储 + TOML / JSON 启动配置加载（分辨率 / vsync / log level / 模块开关），通过 `Platform::FileSystem` 读取

#### Platform
- `Window`（GLFW 包装；不暴露 `GLFWwindow*`）
- `InputDevice` 原始设备事件（键盘 / 鼠标 / 手柄）
- `FileSystem`（路径处理 / 二进制读写 / 监视）
- `Clock`（高精度时钟，给 `Core::Time` 提供后端）

#### App
- `AppHost`：拥有 `Window`、`World`、`Renderer` 的桥接、主循环
- `Layer` 接口：`OnAttach` / `OnDetach` / `OnUpdate(FrameContext&)` / `OnEvent(Event&)`
- `LayerStack`：覆盖 + 弹出，用于游戏层与调试 UI 共存
- `FrameContext`：单帧只读快照（`deltaTime` / 帧号 / 输入快照 / 视口尺寸）

#### Asset
- `AssetHandle<T>`：弱句柄，过期检测
- `AssetRegistry`：类型 → 加载器映射；`Register<T>(extension, ILoader*)`
- `IAssetLoader`：`Load(path)` / `Reload(handle)` / `Free(handle)`
- 内置类型：`MeshAsset` / `TextureAsset` / `ShaderAsset` / `SkeletonAsset` / `SoundAsset` / `FontAsset` / `MaterialTemplate`
- 异步加载在 Phase 5+ 再加，Phase 1–4 同步即可

#### Scene
- `World`：包装 EnTT registry，所有组件/实体生命周期入口
- `Entity`：值类型 wrapper（id + world*）
- 内置组件：`TransformComponent` / `HierarchyComponent` / `NameComponent`
- `ISystem` 接口：`OnAttach(World&)` / `Update(World&, FrameContext&)`
- `SystemScheduler`：注册顺序即执行顺序，Phase 5+ 再考虑并行调度

#### Render
- ★ 唯一 OrangeRender 桥接层
- `Camera`：抽象基类；`OrthoCamera` / `PerspectiveCamera`
- `RenderableComponent`：mesh handle + material instance + 可见性 flag
- `LightComponent`：方向光 / 点光 / 环境（2D 平台跳跃光照模型简化）
- `Material` / `MaterialInstance`：shader + uniform 块 + 纹理槽
- `MaterialSystem`：shader 注册（含游戏侧自定义 shader 注入）
- `Pipeline`：默认前向渲染管线（Shadow → Opaque → Transparent → PostProcess → UI）
- `PassRegistry`：自定义 RenderPass 注入接口（Phase 5+ 启用）
- `PostProcessChain`：HDR / Bloom / Tonemap / LUT / Vignette / GodRays
- `VfxSystem`：粒子（CPU 发射 + 实例化绘制；Phase 5 内迁到 GPU sim）
- `RenderScene`：从 `World` 收集到 `RenderItem` 列表的中间表示，跨入 OrangeRender RenderGraph

#### Animation
- `IAnimator`：抽象动画后端，`Update(deltaTime)` / `BindToEntity(Entity)`
- `SkeletalAnimator`：DragonBones 后端
- `ProceduralAnimator`：shader uniform 驱动；时间相位、velocity-driven squash、noise 振幅参数
- `AnimationStateMachine`：状态 / 过渡 / 条件
- `AnimatorRegistry`：注册新后端（游戏侧可加 Spine、Live2D 等）

#### Physics
- `PhysicsWorld`：`Step(deltaTime)` 固定步长内部插值
- `RigidBodyComponent`：质量 / 阻尼 / kinematic / dynamic
- `ColliderComponent`：shape（box / circle / polygon）+ category bits + mask
- `CollisionEvent`：begin / end / 持续接触
- ★ `PhysicsWorld::ReplaceFixture(Entity, ColliderDesc)`：运行时替换碰撞体（角色形变期间用）
- `RaycastQuery` / `OverlapQuery`

#### Audio
- `AudioEngine`：miniaudio 包装；`PlayOneShot` / `PlayLooped` / `StopAll`
- `Sound`（asset） / `SoundInstance`（运行时实例）
- `AudioListenerComponent` / `AudioSourceComponent`（2D 距离衰减）

#### Input
- `Action`：命名输入，绑定一组 raw key/button
- `ActionMap`：Action 的集合，可从 JSON 加载
- `InputContext`：当前激活的 ActionMap 集合（栈结构，UI 打开时压入新 context）
- 不暴露原始 GLFW 事件给上层；上层只问"jumpAction.IsPressed()"

### 数据流

```text
[Window]                      ← Platform 输入事件
   │
   ▼
[InputContext]                ← Input 模块：raw → Action
   │
   ▼
[Layer::OnEvent]              ← App 层：游戏 Layer 与 ImGui Layer 串行接收
   │
   ▼
[FrameContext.deltaTime]      ← Core::Time 给定本帧 dt
   │
   ▼
[Layer::OnUpdate]
   │
   ▼
[World::Step]
   ├─→ Animation Systems     ← 更新骨骼姿态 / shader uniform
   ├─→ Physics System        ← Step Box2D world，写回 transform
   ├─→ Gameplay Systems      ← 游戏侧定义
   └─→ Audio System          ← 触发声音、更新 listener
   │
   ▼
[Render::CollectScene]
   │   World → RenderScene（drawables / lights / camera）
   │
   ▼
[Render::Pipeline::Render]
   │   RenderScene → OrangeRender RenderGraph
   │
   ▼
[OrangeRender::FrameLifecycle]
   │   RenderGraph → RHI command list → Vulkan submit
   │
   ▼
[Window present]
```

### 反射与序列化策略

引擎在 0.x 阶段**不引入任何形式的运行时反射或编译期代码生成**。所有可序列化类型（内置组件、Asset payload、Scene 节点、配置文件、Save Game 字段）必须**手写 `Read(JsonReader&)` / `Write(JsonWriter&) const` 成对函数**。

#### 决策依据
- 第一款游戏的 component 体量预估在 30–50 个量级，手写完全可承受
- 反射库（EnTT meta / RTTR）会污染所有 component 的写法，且运行时反射调用有 cache miss 开销
- AST codegen（Unreal Header Tool 风格）工程量极大，构建系统复杂度跳一个量级
- 手写策略可控性最强：每个字段的 schema 演进、版本兼容、错误信息都可独立编排

#### 阶段策略
- **Phase 1–6**：完全手写。每个 component / asset / scene 节点提供 `Read` / `Write` 函数对，编辑器 inspector 也手挂控件
- **Phase 6 编辑器启动后**：评估"手挂 inspector 控件"的实际烦扰程度。如果可承受，保持手写；如果发散到无法维护，再考虑升级到 EnTT meta（局部反射，仅用于编辑器，不进运行时热路径）
- **Phase 7 C# Scripting 启动后**：AST codegen 工具反正要写（C# binding 必须），反射元数据是它的副产物——但即使到那时，C++ 端运行时反射仍不开放，仅供 binding 生成器内部使用

#### 序列化抽象层级
- **Layer 1（Phase 1 落地）：`Core::Serialization` 原语** —— `JsonReader` / `JsonWriter` / `BinaryReader` / `BinaryWriter` / `SchemaVersion` 的工具集，每个模块的 read/write 都构筑在这一层之上
- **Layer 2（Phase 2 起按模块自带）：每模块的 schema 定义** —— 每个引入新持久化格式的模块在自己的目录内提供该格式的 schema 描述与 read/write 实现
- **Layer 3（Phase 5.5 落地）：`SaveGameSystem`** —— 不是一种"通用反射"，而是一个明确职责的子系统：注册可参与存档的 component 类型 + 它们的手写 read/write，原子写入磁盘，跨版本 migrator hook

#### 项目级 invariant
- **任何序列化代码必须基于 `Core::Serialization` 原语构筑**，不允许在模块内直接 `nlohmann::json` 裸用（避免 schema 错误处理风格分裂）
- **任何可序列化类型必须显式声明 schema_version**（即使 0），且 read 路径必须先校验版本
- **不允许 component 实现 `operator<<` / `operator>>` 或类似全局重载**，序列化入口必须是命名清楚的 `Read` / `Write` 自由函数或成员函数
- **手写策略写入 `extension-points.md` 越界禁令**

### 核心 API 边界

#### Core
- `Orange::Engine::ResultCode` / `Result<T>`
- `Orange::Engine::TypedHandle<Tag>`
- `Orange::Engine::Time` / `FrameClock`
- `Orange::Engine::JsonReader` / `JsonWriter`
- `Orange::Engine::BinaryReader` / `BinaryWriter`
- `Orange::Engine::SchemaVersion`
- `Orange::Engine::Config` / `ConfigLoader`

#### App / 主循环
- `Orange::Engine::AppHost`
- `Orange::Engine::AppConfig`
- `Orange::Engine::Layer`
- `Orange::Engine::LayerStack`
- `Orange::Engine::FrameContext`

#### Scene / ECS
- `Orange::Engine::World`
- `Orange::Engine::Entity`
- `Orange::Engine::ISystem`
- `Orange::Engine::TransformComponent`
- `Orange::Engine::HierarchyComponent`
- `Orange::Engine::NameComponent`

#### Asset
- `Orange::Engine::AssetHandle<T>`
- `Orange::Engine::AssetRegistry`
- `Orange::Engine::IAssetLoader`
- `Orange::Engine::MeshAsset` / `TextureAsset` / `ShaderAsset` / `SkeletonAsset` / `SoundAsset`

#### Render
- `Orange::Engine::Render::Camera` / `OrthoCamera` / `PerspectiveCamera`
- `Orange::Engine::Render::RenderableComponent`
- `Orange::Engine::Render::LightComponent`
- `Orange::Engine::Render::Material` / `MaterialInstance` / `MaterialTemplate`
- `Orange::Engine::Render::Pipeline`
- `Orange::Engine::Render::PostProcessChain`
- `Orange::Engine::Render::VfxSystem`

#### Animation
- `Orange::Engine::Animation::IAnimator`
- `Orange::Engine::Animation::SkeletalAnimator`
- `Orange::Engine::Animation::ProceduralAnimator`
- `Orange::Engine::Animation::AnimationStateMachine`

#### Physics
- `Orange::Engine::Physics::PhysicsWorld`
- `Orange::Engine::Physics::RigidBodyComponent`
- `Orange::Engine::Physics::ColliderComponent`
- `Orange::Engine::Physics::ColliderDesc`

#### Audio / Input / Platform
- `Orange::Engine::Audio::AudioEngine`
- `Orange::Engine::Input::Action` / `ActionMap` / `InputContext`
- `Orange::Engine::Platform::Window`

### 最小 API 草案

```cpp
// include/orange/engine/app/AppHost.h
#ifndef ORANGE_ENGINE_APP_APP_HOST_H
#define ORANGE_ENGINE_APP_APP_HOST_H

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/LayerStack.h>

#include <memory>

namespace Orange::Engine
{

class World;
class Window;

class ORANGE_ENGINE_API AppHost
{
public:
    explicit AppHost(const AppConfig& config);
    ~AppHost();

    int Run();
    void RequestExit();

    World&       GetWorld();
    LayerStack&  GetLayerStack();
    Window&      GetWindow();

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_APP_APP_HOST_H
```

```cpp
// include/orange/engine/scene/World.h
#ifndef ORANGE_ENGINE_SCENE_WORLD_H
#define ORANGE_ENGINE_SCENE_WORLD_H

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/ISystem.h>

#include <memory>

namespace Orange::Engine
{

class ORANGE_ENGINE_API World
{
public:
    World();
    ~World();

    Entity CreateEntity();
    void   DestroyEntity(Entity entity);

    void   RegisterSystem(std::unique_ptr<ISystem> pSystem);
    void   Step(float deltaTime);

    // EnTT registry 直通访问，供 Render / Animation / Physics 模块拉取组件视图
    void* GetNativeRegistry();

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_SCENE_WORLD_H
```

```cpp
// include/orange/engine/render/Pipeline.h
#ifndef ORANGE_ENGINE_RENDER_PIPELINE_H
#define ORANGE_ENGINE_RENDER_PIPELINE_H

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/render/RenderScene.h>

namespace Orange::Engine::Render
{

enum class PipelineStage
{
    Shadow,
    Opaque,
    Transparent,
    PostProcess,
    UI
};

class ORANGE_ENGINE_API Pipeline
{
public:
    virtual ~Pipeline() = default;

    virtual void Render(const RenderScene& scene) = 0;

    // ★ 自定义 RenderPass 注入（Phase 5+ 启用，Phase 3–4 接口预留 not-implemented）
    virtual void InsertPass(PipelineStage stage, class IRenderPass* pPass) = 0;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_PIPELINE_H
```

更多 API 设计细节见 `docs/extension-points.md`。

### 模块划分

#### 核心模块（Phase 1 必交付）
- `Core`：基础类型、日志、Handle、Result、Math
- `Platform`：Window、InputDevice、FileSystem、Clock
- `App`：AppHost、Layer、LayerStack、FrameContext

#### 数据流模块（Phase 2 必交付）
- `Asset`：AssetHandle、AssetRegistry、Loader 接口、内置 Mesh/Texture/Shader 加载
- `Scene`：EnTT World 包装、Transform、Hierarchy、ISystem
- `Render`（最小）：Camera、RenderableComponent、Pipeline 最小实现

#### 视觉模块（Phase 3 必交付）
- `Render`（完整）：Material 系统、PostProcessChain、自定义 shader 注入
- `Vfx`（最小）：粒子系统骨架

#### 玩法模块（Phase 4 必交付）
- `Animation`：双后端（Skeletal + Procedural）
- `Physics`：Box2D 包装 + Fixture 替换
- `Input`：Action 系统
- `Audio`：miniaudio 包装

#### 生产化模块（Phase 5 必交付）
- `Render`：完整 VFX、软阴影、体积光
- `Asset`：场景序列化（JSON）、异步加载
- `Render`：自定义 RenderPass 注入正式启用

#### 长期模块（Phase 6+，见 `docs/roadmap.md`）
- 编辑器（独立 target）
- C# Scripting（CoreCLR hosting）
- Hot reload
- 资产 cook 工具链

### 建议目录结构

#### 拟新增（Proposed）

```text
Proposed: CMakeLists.txt
Proposed: cmake/
Proposed: cmake/CompilerOptions.cmake
Proposed: cmake/OrangeEngineConfig.cmake.in
Proposed: include/orange/engine/
Proposed: include/orange/engine/OrangeEngine.h           (umbrella header)
Proposed: include/orange/engine/OrangeEngineExport.h    (ORANGE_ENGINE_API)
Proposed: include/orange/engine/OrangeEngineVersion.h.in
Proposed: include/orange/engine/core/
Proposed: include/orange/engine/platform/
Proposed: include/orange/engine/app/
Proposed: include/orange/engine/asset/
Proposed: include/orange/engine/scene/
Proposed: include/orange/engine/render/
Proposed: include/orange/engine/animation/
Proposed: include/orange/engine/physics/
Proposed: include/orange/engine/audio/
Proposed: include/orange/engine/input/
Proposed: include/orange/engine/save/                  (Phase 5.5 引入)
Proposed: src/
Proposed: src/core/
Proposed: src/platform/
Proposed: src/app/
Proposed: src/asset/
Proposed: src/scene/
Proposed: src/render/                  ★ 唯一 #include <orange/...> 区域
Proposed: src/animation/
Proposed: src/animation/dragonbones/   ★ DragonBones 私有适配
Proposed: src/physics/
Proposed: src/physics/box2d/           ★ Box2D 私有适配
Proposed: src/audio/
Proposed: src/audio/miniaudio/         ★ miniaudio 私有适配
Proposed: src/input/
Proposed: src/save/                    (Phase 5.5 引入)
Proposed: samples/
Proposed: samples/01_minimal_window/
Proposed: samples/02_ecs_basics/
Proposed: samples/03_textured_quad/
Proposed: samples/04_3d_mesh_with_bloom/
Proposed: samples/05_skeletal_animation/
Proposed: samples/06_physics_platformer/
Proposed: samples/07_full_pipeline/
Proposed: tests/
Proposed: docs/case-studies/
Proposed: docs/extension-points.md
Proposed: docs/roadmap.md
Proposed: docs/coding-standards.md
Proposed: CHANGELOG.md
Proposed: CLAUDE.md                    (engine 仓库根，AI 入口)
Proposed: .gitmodules                  (vendor/OrangeRender, vendor/Orange-Wiki)
```

#### 拟删除
```text
Removed: Src/OrangeEngine/Layer1/
Removed: Src/OrangeEngine/Layer2/
Removed: Src/OrangeEngine/Orange.h         (空文件，并入 include/orange/engine/OrangeEngine.h)
Removed: Src/OrangeEngine/orgpch.h         (PCH 重新组织到 src/orgpch.h)
Removed: Src/OrangeEditor/                 (改为 samples/07_full_pipeline，将来再 promote 为 tools/OrangeEditor/)
Removed: Src/                              (整目录，src/ 替换)
```

`docs/Technical Documentation/` 保留为历史文档不动。

### 文件级起步建议

#### Proposed: `cmake/CompilerOptions.cmake`
- MSVC `/utf-8` / `/W4` / `/permissive-` / `/Zc:preprocessor`
- C++20，extensions OFF

#### Proposed: `include/orange/engine/OrangeEngineExport.h`
- `ORANGE_ENGINE_API` 宏：`BUILD_SHARED_LIBS=ON` 时切换 dllexport/dllimport

#### Proposed: `include/orange/engine/core/Result.h`
- 通用 `ResultCode` 与 `Result<T>`

#### Proposed: `include/orange/engine/core/Handle.h`
- 强类型句柄

#### Proposed: `include/orange/engine/core/Time.h`
- `DeltaSeconds` / `FrameIndex` / 固定步长辅助

#### Proposed: `include/orange/engine/core/Serialization.h`
- `JsonReader` / `JsonWriter` / `BinaryReader` / `BinaryWriter` / `SchemaVersion`

#### Proposed: `include/orange/engine/core/Config.h`
- 启动配置键值存储 + 类型化 getter

#### Proposed: `src/core/Config.cpp`
- TOML / JSON 加载实现，依赖 `Platform::FileSystem`

#### Proposed: `include/orange/engine/platform/Window.h`
- 窗口创建、事件回调注册（不暴露 GLFW 类型）

#### Proposed: `include/orange/engine/app/AppHost.h`
- 主循环入口

#### Proposed: `include/orange/engine/app/Layer.h`
- Layer 接口

#### Proposed: `src/platform/glfw/Window.cpp`
- GLFW 包装实现

#### Proposed: `src/app/AppHost.cpp`
- 主循环实现：Window 事件 → InputContext → Layer::OnEvent → World::Step → Render::Render → present

#### Proposed: `samples/01_minimal_window/main.cpp`
- 创建 AppHost，挂一个空 Layer，运行直到关闭

---

## Roadmap

### Phase 1：引擎骨架与最小可运行路径

#### 目标
- 建立可编译可安装的 `OrangeEngine::orange_engine` 静态库目标
- 接通 `find_package(OrangeRender 0.1 CONFIG REQUIRED)`，让引擎能链上 OrangeRender
- 建立 Core / Platform / App 三层最小实现
- 跑通"窗口 + 主循环 + 空 Layer"的 sample

#### 核心任务
- 顶层 CMake、子目录划分、`ORANGE_ENGINE_API` 宏机制
- 3rdparty 依赖清单更新（加入 EnTT / Box2D / miniaudio / stb / nlohmann_json / Dear ImGui / DragonBones runtime）
- vendor 提升为 git submodule（`vendor/OrangeRender` / `vendor/Orange-Wiki`，Wiki 跟踪 `Orange-Render-Wiki` 分支）
- 实现 Core 基础类型、Core 序列化原语与启动配置、Platform Window、App 主循环
- 通过 `samples/01_minimal_window` 验证

#### 本阶段引入的持久化 schema
- `engine.config`（启动配置：分辨率 / vsync / log level / 模块开关）

#### 依赖
- 无前置阶段（OrangeRender 已可用）

#### 里程碑
- `samples/01_minimal_window` 可在 Windows 11 上启动一个空窗口，60fps 稳定运行

#### 完成标准
- `cmake -S . -B build -DCMAKE_PREFIX_PATH="<prefix>"` 成功
- `cmake --build build --config Debug -j` 成功
- 安装到 prefix 后，外部工程可通过 `find_package(OrangeEngine 0.1 CONFIG REQUIRED)` 链接
- `samples/01_minimal_window.exe` 能稳定运行 60 帧以上，窗口可正常关闭

### Phase 2：数据流贯通

#### 目标
- 在 OrangeRender 之上建立 Render 模块的最小桥接
- 接入 EnTT，建立 Scene 模块
- 接入 Asset 模块，能加载 mesh / texture / shader
- 跑通"屏幕画一个 3D mesh"的 sample

#### 核心任务
- Asset：AssetHandle、AssetRegistry、内置 Mesh/Texture/Shader 加载
- Scene：World / Entity / Transform / Hierarchy / ISystem
- Render（最小）：OrthoCamera、RenderableComponent、Pipeline 最小实现，把 World 中的可见实体翻译为 OrangeRender RenderGraph 调用
- 样例：`02_ecs_basics`、`03_textured_quad`、`04_3d_mesh_with_bloom`（最后这个先不带 bloom，留到 Phase 3）

#### 本阶段引入的持久化 schema
- `*.asset.json` 资源 sidecar（声明类型 / loader 参数 / 依赖资源）
- 内置 component 的手写 `Read` / `Write`（`TransformComponent` / `HierarchyComponent` / `NameComponent` / `RenderableComponent`），全部基于 Phase 1 的 `JsonReader` / `JsonWriter` 原语

#### 依赖
- 依赖 Phase 1

#### 里程碑
- 屏幕中央显示一个旋转的带贴图 3D mesh，由 ECS 实体驱动

#### 完成标准
- ECS 创建/销毁实体 + 组件 add/remove 流程稳定
- Asset 能从磁盘加载并缓存 mesh/texture/shader 三类资源
- Render 模块的 Pipeline 完成 RenderScene → OrangeRender RenderGraph 翻译
- 至少一个 sample 完整跑通这条数据流

### Phase 3：Ori 视觉基线 + 自定义 shader 接入

#### 目标
- 把 Render 模块从"能画"提升到"像 Ori"
- 引入 Material 系统与 PostProcessChain
- 开放游戏侧自定义 shader 注入接口（这是史莱姆 / dissolve / fresnel 等效果的前提）

#### 核心任务
- Material / MaterialInstance / MaterialTemplate
- 卡通照明 + rim light shader 模板（引擎内置）
- PostProcessChain：HDR → Bloom → Tonemap → LUT
- 软阴影（PCF / VSM 二选一）
- MaterialSystem 自定义 shader 注册接口
- 样例：`04_3d_mesh_with_bloom`（升级带 bloom）、`07_full_pipeline` 雏形

#### 本阶段引入的持久化 schema
- `*.material.json` 材质实例（template 引用 / uniform 值 / 纹理槽指派）
- `*.posteffect.json` 后处理链配置（pass 顺序 + 每个 pass 的参数）

#### 依赖
- 依赖 Phase 2

#### 里程碑
- 一个 3D mesh 在屏幕上呈现 Ori 风格质感（rim light + bloom + tonemap）
- 游戏侧能不修改引擎源码，注册一个自定义 fresnel shader 并应用到实体

#### 完成标准
- Material 系统支持 uniform 块 + 纹理槽 + 自定义 shader
- PostProcessChain 至少串通 Bloom + Tonemap 两个 pass
- 自定义 shader 注入 API 通过 sample 验证

### Phase 4：可玩性

#### 目标
- 引入 Animation / Physics / Input / Audio 四个模块
- 把"在屏幕上动起来"提升到"可控制角色跳跃"
- Animation 必须在第一版就承认 Skeletal + Procedural 双后端并列

#### 核心任务
- Animation：IAnimator 抽象 + SkeletalAnimator (DragonBones) + ProceduralAnimator + AnimationStateMachine
- Physics：PhysicsWorld (Box2D 3.x 包装) + RigidBody/Collider 组件 + Fixture 替换 API
- Input：Action / ActionMap / InputContext，JSON 配置加载
- Audio：AudioEngine (miniaudio) + Sound asset
- 样例：`05_skeletal_animation`、`06_physics_platformer`、`07_full_pipeline`（综合 demo）

#### 本阶段引入的持久化 schema
- `*.actions.json` 输入动作映射（Action 定义 + binding）
- `*.skeleton.meta.json` DragonBones 骨架 sidecar（DragonBones 自己的 `.json` / `.dbbin` 二进制由 runtime 直接读，sidecar 只描述资源关联与初始动画名）
- `*.statemachine.json` 动画状态机定义（状态 / 过渡 / 条件）
- 内置组件 read/write 扩展：`RigidBodyComponent` / `ColliderComponent` / `AnimatorComponent` / `AudioSourceComponent`

#### 依赖
- 依赖 Phase 3

#### 里程碑
- 一个由骨骼动画驱动的角色，能在带物理的关卡里跳跃移动，触发音效
- 一个 Procedural 动画的 mesh（粗略史莱姆 demo），shader uniform 时间驱动 noise 振幅

#### 完成标准
- DragonBones runtime 在 `src/animation/dragonbones/` 内集成成功，能加载并播放官方测试资源
- ProceduralAnimator 能驱动一个自定义 shader 的若干 uniform
- Box2D 世界稳定步进，与 ECS Transform 同步
- Action 系统能从 JSON 加载并响应输入
- `samples/07_full_pipeline` 综合演示上述能力

### Phase 5：生产化与游戏 fork 时机

#### 目标
- 把引擎从"能玩 demo"提升到"能开发完整游戏"
- 关卡序列化、VFX 系统、自定义 RenderPass 注入正式可用
- 此阶段完成后，可以 fork 出游戏仓库正式开始游戏开发

#### 核心任务
- 场景序列化（JSON schema 化，版本字段）
- VFX 完整化：粒子系统 + dissolve shader + emission
- 体积光（screen-space god rays）
- Render：自定义 RenderPass 注入接口正式启用
- 软阴影质量提升（PCSS / VSM 二选一）
- Asset：异步加载（Phase 1 同步加载的升级）

#### 本阶段引入的持久化 schema
- `*.scene.json` 场景文件（实体列表 + 各组件 + 引用关系，schema_version 字段必备）
- `*.prefab.json` 预制体（可选，Phase 5 末或 Phase 6 决定）
- `*.particle.json` 粒子发射器配置
- 注：Save Game 单独抽到 Phase 5.5，不在本阶段

#### 依赖
- 依赖 Phase 4

#### 里程碑
- 一个 30 秒长度的可玩关卡，包含背景、角色控制、敌人、特效、音效、UI 提示
- 游戏仓库雏形可独立 build

#### 完成标准
- 关卡可被序列化 / 反序列化，schema 版本可演化
- VFX 系统能实现 dissolve in/out 效果（dissolve shader 是史莱姆变形机制的引擎侧依赖，不是游戏代码）
- 自定义 RenderPass 注入通过 sample 验证
- 游戏仓库可通过 `find_package(OrangeEngine 0.5 CONFIG REQUIRED)` 链接并跑通最小 demo

### Phase 5.5：Save Game 系统

#### 目标
- 把"玩家进度"作为独立子系统从 Scene 序列化中切分出来
- 提供原子写入 + 跨引擎版本兼容 + 平台路径解析的存档框架
- 严格遵循"完全手写"反射策略：可参与存档的 component 由游戏侧手写 read/write 注册到 `SaveGameRegistry`

> Phase 5.5 与 Phase 5 并列，但**强依赖** Phase 5 的 Scene 序列化基础设施。物理上可与 Phase 5 末尾并行推进，逻辑上独立成章是为了承认它解决的是和"关卡数据"完全不同的问题：关卡是 **content**（设计师 / 美术产出，跟随版本走），存档是 **player state**（玩家产出，必须穿越引擎升级）。

#### 核心任务
- `SaveGameSystem` 子系统（不是 ECS system，是 service）
- `SaveGameRegistry`：注册可参与存档的 component 类型 + 它们的手写 read/write
- 原子写入：先写到 `<save>.tmp` → fsync → rename，避免半写损坏
- 平台路径解析：Windows `%APPDATA%/OrangeEngine/<game>/saves/`；Linux/Mac 对应路径
- 跨版本 migrator hook：每个 schema_version bump 提供 `(oldJson) -> newJson` 转换函数
- Slot 管理：多存档槽 + 元信息（时间 / 截图 / 玩家进度摘要）
- 校验：CRC32 / xxhash 检查文件完整性
- 加密（可选，弱保护级别）：XOR 或 AES，仅作为"防小白手改"门槛，不当真正反作弊

#### 依赖
- 依赖 Phase 5（Scene 序列化基础设施 + 异步加载）
- 间接依赖 Phase 1 Task 05（`Core::Serialization` 原语）

#### 里程碑
- 玩家在 sample 关卡内拾取若干 token、推进到 checkpoint，调用 `SaveGameSystem::Save("slot1")`，关闭程序，重新启动后 `SaveGameSystem::Load("slot1")` 回到相同状态
- 故意把 `.save` 文件破坏（截断 / 修改 CRC），Load 时返回明确错误码，不崩溃
- 把 schema_version 从 1 改到 2，提供 migrator，旧存档加载后自动 migrate

#### 完成标准
- 存档格式带 schema_version 字段，read 路径强制校验
- 至少 3 个槽位 + 自动存档槽（autosave）
- 损坏存档不导致进程崩溃，仅返回错误
- 平台路径解析在 Windows 上工作；其他平台留接口
- 至少一个 sample / 测试覆盖：保存 → 重启 → 加载 → 状态匹配

#### Task Breakdown（要点级）
- Task 01：`SaveGameRegistry` 公共接口与 component 注册流程
- Task 02：`SaveGameSystem::Save` / `Load` 主流程（原子写入 + CRC + schema 校验）
- Task 03：平台路径解析（Windows `SHGetKnownFolderPath` 包装）
- Task 04：Slot 元信息 + autosave 调度
- Task 05：Migrator hook 接口 + 至少一个版本迁移示例
- Task 06：损坏与错误路径测试覆盖
- Task 07：Sample 关卡集成存档 / 加载流程

### Phase 6+：长期演进

详见 `docs/roadmap.md`。包括：
- 编辑器（关卡 / 粒子 / 材质三件套）
- C# Scripting (CoreCLR hosting)
- Hot reload (DLL plugin / shader hot reload)
- 资产 cook 工具链（离线流水线）
- 多视口、分屏、自定义 RHI 后端（如需 DX12）

---

## Task Breakdown

### Phase 1：引擎骨架与最小可运行路径

#### Task 01：清理旧骨架并建立新目录结构 ✅
- 描述：删除 `Src/OrangeEngine/Layer1`、`Layer2`、`OrangeEditor`、`Orange.h`、`orgpch.h` 等历史文件，建立 `include/orange/engine/`、`src/`、`samples/`、`tests/`、`cmake/` 顶层目录。
- 输入：当前仓库（仅含旧 Src/ 骨架）
- 输出：
  - `Proposed: include/orange/engine/`（多个子目录）
  - `Proposed: src/`（多个子目录）
  - `Proposed: samples/01_minimal_window/`
  - `Proposed: tests/`
  - `Proposed: cmake/`
  - `Removed: Src/`
- 影响路径/模块：根目录、所有模块
- 前置依赖：无
- 实现要点：
  - 旧目录整体移除而非渐进式迁移，避免新旧并存时构建系统冲突
  - `docs/Technical Documentation/` 保留不动（历史档案）
- 验证方式：仓库结构与 `Proposed` 对应；`git status` 反映了删除与新增
- 验收标准：旧 `Src/` 完全移除；新顶层目录就位
- Critical Path：是

#### Task 02：建立顶层 CMake 与 `ORANGE_ENGINE_API` 宏机制 ✅
- 描述：编写顶层 `CMakeLists.txt`，定义 `OrangeEngine::orange_engine` 静态库目标，接入 `find_package(OrangeRender 0.1 CONFIG REQUIRED)`，提供 `ORANGE_ENGINE_API` dllexport/dllimport 切换。
- 输入：Task 01 输出的目录结构
- 输出：
  - `Proposed: CMakeLists.txt`
  - `Proposed: cmake/CompilerOptions.cmake`
  - `Proposed: include/orange/engine/OrangeEngineExport.h`
  - `Proposed: include/orange/engine/OrangeEngineVersion.h.in`
- 影响路径/模块：构建系统、所有模块
- 前置依赖：Task 01
- 实现要点：
  - 默认 `BUILD_SHARED_LIBS=OFF`（静态）
  - `ORANGE_ENGINE_VERSION` 0.1.0
  - `ORANGE_ENGINE_INSTALL` 默认随 `PROJECT_IS_TOP_LEVEL`
  - C++20，extensions OFF，MSVC `/utf-8` `/W4` `/permissive-`
- 验证方式：`cmake -S . -B build` 成功；`cmake --build build` 编译通过空目标
- 验收标准：可生成 VS2022 解决方案；`orange_engine` 目标存在但暂无源
- Critical Path：是

#### Task 03：第三方依赖接入与 vendor submodule 化 ✅
- 描述：把 `vendor/OrangeRender` 与 `vendor/Orange-Wiki` 注册为 git submodule（Wiki 跟踪 `Orange-Render-Wiki` 分支）；更新 `3rdparty.json` 增加 EnTT / Box2D / miniaudio / stb_image / stb_truetype / nlohmann_json / Dear ImGui / DragonBones C++ runtime；提供 `cmake/Dependencies.cmake` 走 `find_package` 解析。
- 输入：Task 02 输出
- 输出：
  - `Proposed: .gitmodules`
  - `Proposed: cmake/Dependencies.cmake`
  - `Modified: 3rdparty.json`
- 影响路径/模块：构建系统、第三方
- 前置依赖：Task 02
- 实现要点：
  - 所有依赖默认从单一 prefix（与 OrangeRender 共用 `D:\3rdparty` 或自定）解析
  - `OrangeRender` 走 PUBLIC（公共头要传递 OrangeRender 类型，但**仅在 Render 模块的私有源**中真实使用），`Box2D / DragonBones / miniaudio` 走 PRIVATE
  - `EnTT` 走 PUBLIC（component 类型必须在公共头出现，因为游戏侧要 add component）
- 验证方式：`cmake -S . -B build` 能 find 全部依赖
- 验收标准：依赖图正确；PRIVATE 依赖不出现在 `OrangeEngineConfig.cmake.in`
- Critical Path：是

#### Task 04：定义 Core 基础类型 ✅
- 描述：实现 `ResultCode` / `Result<T>` / `TypedHandle<Tag>` / `Time` / `Hash` / `Id` 等基础设施。
- 输入：CMake 与依赖就绪
- 输出：
  - `Proposed: include/orange/engine/core/Result.h`
  - `Proposed: include/orange/engine/core/Handle.h`
  - `Proposed: include/orange/engine/core/Time.h`
  - `Proposed: include/orange/engine/core/Hash.h`
  - `Proposed: include/orange/engine/core/Log.h`
  - `Proposed: src/core/Log.cpp`（spdlog 包装）
- 影响路径/模块：Core
- 前置依赖：Task 02、Task 03
- 实现要点：
  - 公共头不引入 spdlog（`ORANGE_LOG_INFO` 等宏走 fmt 风格 + 内部 vararg）
  - `TypedHandle<Tag>` 强类型，避免不同资源句柄混用
- 验证方式：头文件可被空 cpp 单独编译；`samples/01_minimal_window` 能 `ORANGE_LOG_INFO("hello")`
- 验收标准：Core 公共头自包含
- Critical Path：是

#### Task 05：实现 Core 序列化原语与启动配置 ✅
- 描述：实现 `JsonReader` / `JsonWriter` / `BinaryReader` / `BinaryWriter` / `SchemaVersion` 工具集，以及 `Config` / `ConfigLoader`。所有后续 Phase 的 read/write 必须基于这套原语，禁止裸 `nlohmann::json` 调用。
- 输入：Core 基础类型（Result / Handle / Log）
- 输出：
  - `Proposed: include/orange/engine/core/Serialization.h`
  - `Proposed: include/orange/engine/core/SchemaVersion.h`
  - `Proposed: include/orange/engine/core/Config.h`
  - `Proposed: src/core/Serialization.cpp`
  - `Proposed: src/core/Config.cpp`
- 影响路径/模块：Core
- 前置依赖：Task 03（nlohmann/json 已就绪）、Task 04
- 实现要点：
  - `JsonReader` / `JsonWriter` 包装 `nlohmann::json`，不暴露其类型到接口；错误带字段路径与行号信息
  - `BinaryReader` / `BinaryWriter` 强制 little-endian，`uint8_t` 为单位 I/O，提供 `Read<T>` / `Write<T>` 模板（仅限 trivially copyable 算术类型与定长 POD）
  - `SchemaVersion` 是 `{namespace, major, minor}` 三元组；read 路径首先校验 namespace 匹配，再走 minor 兼容（minor 向下兼容，major 强制匹配）
  - `Config` 是只读键值快照（`GetInt` / `GetFloat` / `GetBool` / `GetString` / `GetVec2` 等）；`ConfigLoader` 从 JSON 文件加载，缺键时使用编译期默认值
  - 公共头不出现 `nlohmann::json` 类型；所有公共 API 用本仓自有类型表达
- 验证方式：单元测试覆盖 schema 版本不匹配错误、JSON ↔ Binary round-trip、Config 缺键 fallback
- 验收标准：序列化原语自洽；后续 Phase 任意模块读 / 写 JSON 都走这一层
- Critical Path：是

#### Task 06：实现 Platform::Window ✅
- 描述：建立 `Window` 抽象，GLFW 实现细节藏在 `src/platform/glfw/`，公共头不暴露 `GLFWwindow*`。
- 输入：Core 就绪
- 输出：
  - `Proposed: include/orange/engine/platform/Window.h`
  - `Proposed: include/orange/engine/platform/WindowEvent.h`
  - `Proposed: src/platform/glfw/Window.cpp`
- 影响路径/模块：Platform
- 前置依赖：Task 04
- 实现要点：
  - `Window::Create(WindowDesc)` / `PollEvents` / `ShouldClose` / `SwapBuffers`（其实 swap 由 OrangeRender 接管，这里只 PollEvents）
  - 事件回调通过 `std::function` 注册，不暴露 GLFW 回调签名
  - GLFW 初始化/销毁的全局状态封装在匿名 namespace 的 `sInitialized` 计数
- 验证方式：构造 Window，poll 事件循环，10 秒后自动关闭
- 验收标准：Window 能创建、销毁、响应关闭事件
- Critical Path：是

#### Task 07：定义 App 层（Layer / LayerStack / FrameContext） ✅
- 描述：建立主循环框架；Layer 接口允许游戏侧注入逻辑；LayerStack 管理多 Layer 顺序。
- 输入：Window 就绪
- 输出：
  - `Proposed: include/orange/engine/app/Layer.h`
  - `Proposed: include/orange/engine/app/LayerStack.h`
  - `Proposed: include/orange/engine/app/FrameContext.h`
  - `Proposed: include/orange/engine/app/AppConfig.h`
- 影响路径/模块：App
- 前置依赖：Task 06
- 实现要点：
  - `Layer::OnAttach` / `OnDetach` / `OnUpdate(FrameContext&)` / `OnEvent(WindowEvent&)`
  - `LayerStack` 区分 layer 与 overlay（overlay 始终在 layer 之上接收事件）
  - `FrameContext` 是只读快照，单帧不变
- 验证方式：头文件可独立编译
- 验收标准：App 接口声明完整
- Critical Path：是

#### Task 08：实现 AppHost 主循环 ✅
- 描述：`AppHost::Run` 串起 Window poll → LayerStack OnEvent → LayerStack OnUpdate → Render（占位）→ present。
- 输入：Layer / Window 就绪
- 输出：
  - `Proposed: include/orange/engine/app/AppHost.h`
  - `Proposed: src/app/AppHost.cpp`
- 影响路径/模块：App
- 前置依赖：Task 07
- 实现要点：
  - 帧节拍：默认 vsync，无 fixed timestep（fixed timestep Phase 4 物理时再加）
  - 异常路径：Window 关闭事件 → 退出循环
  - Render 阶段当前是空（Phase 2 接通 OrangeRender）
- 验证方式：`samples/01_minimal_window` 能 Run 并稳定 60fps
- 验收标准：主循环跑得起来，能正常退出
- Critical Path：是

#### Task 09：完成 `samples/01_minimal_window` ✅
- 描述：最小 sample：构造 AppConfig、构造 AppHost、Run。
- 输入：AppHost 就绪
- 输出：
  - `Proposed: samples/01_minimal_window/CMakeLists.txt`
  - `Proposed: samples/01_minimal_window/main.cpp`
- 影响路径/模块：Samples
- 前置依赖：Task 08
- 实现要点：
  - `main` 不超过 30 行
  - 无 layer 也能跑（空 LayerStack 合法）
- 验证方式：build 后 `samples/01_minimal_window.exe` 启动空窗口、可关闭
- 验收标准：Phase 1 首个里程碑
- Critical Path：是

#### Task 10：建立测试入口 ✅
- 描述：`tests/CMakeLists.txt` + 最小 ctest case（Result / Handle / Serialization 单元测试）。
- 输入：Core 已就绪
- 输出：
  - `Proposed: tests/CMakeLists.txt`
  - `Proposed: tests/core/ResultTest.cpp`
  - `Proposed: tests/core/HandleTest.cpp`
  - `Proposed: tests/core/SerializationTest.cpp`
- 影响路径/模块：Tests
- 前置依赖：Task 04、Task 05
- 实现要点：
  - 不引入完整测试框架（Catch2 / GoogleTest 看 OrangeRender 用的什么对齐），最小可用即可
  - 测试受 `ORANGE_ENGINE_BUILD_TESTS=OFF` 默认关闭
  - SerializationTest 覆盖 schema_version 校验、版本不匹配错误路径、JSON ↔ Binary 双向往返一致性
- 验证方式：`ctest --test-dir build -C Debug` 通过 3 个 case
- 验收标准：测试入口建立
- Critical Path：否

#### Task 11：发布 install 与 `OrangeEngineConfig.cmake` ✅
- 描述：让外部工程可通过 `find_package(OrangeEngine 0.1 CONFIG REQUIRED)` 链接。
- 输入：所有 Phase 1 task 完成
- 输出：
  - `Proposed: cmake/OrangeEngineConfig.cmake.in`
  - `Modified: CMakeLists.txt`（install 规则）
  - `Proposed: tests/install_smoke/`
- 影响路径/模块：构建系统
- 前置依赖：Task 02、Task 04–09
- 实现要点：
  - `find_dependency(OrangeRender 0.1 CONFIG REQUIRED)` / `find_dependency(EnTT)` / `find_dependency(glm)` 等所有 PUBLIC 依赖
  - `SameMinorVersion` 兼容
  - `install_smoke` ctest 验证从一个空 CMake 项目 `find_package(OrangeEngine)` 能链通
- 验证方式：install 到 prefix 后，外部 ctest 验证链通
- 验收标准：Phase 1 完成；引擎可被外部消费
- Critical Path：是

### Phase 2：数据流贯通

> 任务模板沿用 Phase 1 风格，详细字段在落地各 Task 时填充。Phase 2 起的 Task 列表先列要点，详细 Task Breakdown 在 Phase 1 收尾后产出 increment patch。

- Task 01：定义 `Asset` 公共接口（AssetHandle / AssetRegistry / IAssetLoader）
- Task 02：实现 Mesh / Texture / Shader 三个内置 loader（同步加载 + 简单缓存）
- Task 03：定义 Scene 公共接口（World / Entity / ISystem / Transform / Hierarchy）
- Task 04：实现 EnTT 包装（World 的后端）
- Task 05：定义 Render 模块公共接口（Camera / RenderableComponent / Pipeline）
- Task 06：实现 RenderScene 收集（World → drawable list）
- Task 07：实现最小 Pipeline（接通 OrangeRender RenderGraph，绘制单个 mesh）
- Task 08：完成 `samples/02_ecs_basics`（实体/组件 CRUD）
- Task 09：完成 `samples/03_textured_quad`（带贴图四边形）
- Task 10：完成 `samples/04_3d_mesh`（旋转 3D mesh，无 bloom）

#### Task 01：定义 `Asset` 公共接口 ✅
- 描述：把 Asset 模块的"用户面"先定义出来——AssetHandle 标识、IAssetLoader 抽象、AssetRegistry 公共声明。Phase 2 Task 02 起把 loader 与 registry 内部存储真正实现。
- 输入：Phase 1 收尾（Core::Result / Core::Handle / Core::Serialization 已就绪）
- 输出：
  - `Proposed: include/orange/engine/asset/AssetHandle.h`
  - `Proposed: include/orange/engine/asset/IAssetLoader.h`
  - `Proposed: include/orange/engine/asset/AssetRegistry.h`
  - `Proposed: src/asset/AssetRegistry.cpp`（PIMPL 骨架；Load / Get / Unload 体留给 Task 02）
  - `Proposed: src/asset/AssetHeaderCheck.cpp`
- 影响路径/模块：Asset
- 前置依赖：Phase 1 全部任务
- 实现要点：
  - `AssetHandle<T>` 直接 `using` 重用 Core 的 `TypedHandle<T>`——assets 只需身份 + invalid sentinel + std::hash 已经具备的能力，没必要复制一份。
  - `IAssetLoader<T>` 是 template-virtual：`virtual Result<std::unique_ptr<T>, ResultCode> Load(std::string_view) = 0`。每种 asset type 一个 loader 实现。
  - `AssetRegistry` 走 PIMPL，公共 API 是 template 方法（Load / Get / Unload / RegisterLoader）。Task 01 只把它们 declare 出来；template 实现体由 Task 02 在 header 内填充，借助 `std::type_index` 路由到 Impl 的非模板存储。
  - 命名空间统一 `Orange::Engine::Asset`，公共类用 `ORANGE_ENGINE_API`。
- 验证方式：`AssetHeaderCheck.cpp` 在隔离环境下能 include 三个公共头并通过编译；`orange_engine` 静态库继续 link 通；现有 5 个 ctest case 不受影响。
- 验收标准：Asset 模块的公共表面就绪，使 Task 02 可以纯实现路径推进。
- Critical Path：是

#### Task 02：实现 Mesh / Texture / Shader 三个内置 loader ✅
- 描述：把 Asset 模块的 dispatch 与 dedup 缓存接通，并交付三个内置 loader——同步路径、简单 LRU-style cache，未来 Phase 5 异步流式上线再分裂出 streaming 子系统。
- 输入：Phase 2 / Task 01（Asset 公共接口）
- 输出：
  - `Proposed: include/orange/engine/asset/MeshAsset.h`
  - `Proposed: include/orange/engine/asset/TextureAsset.h`
  - `Proposed: include/orange/engine/asset/ShaderAsset.h`
  - `Proposed: include/orange/engine/asset/MeshLoader.h`
  - `Proposed: include/orange/engine/asset/TextureLoader.h`
  - `Proposed: include/orange/engine/asset/ShaderLoader.h`
  - `Proposed: src/asset/MeshLoader.cpp`
  - `Proposed: src/asset/TextureLoader.cpp`
  - `Proposed: src/asset/ShaderLoader.cpp`
  - `Modified: include/orange/engine/asset/AssetRegistry.h`（template 方法的 inline body + 4 个非模板 erased 入口）
  - `Modified: src/asset/AssetRegistry.cpp`（Impl 加 typeid → loader 表 + typeid → AssetTable）
  - `Proposed: tests/asset/AssetRegistryTest.cpp`
- 影响路径/模块：Asset、tests
- 前置依赖：Task 01
- 实现要点：
  - **Mesh** 走自有 binary 格式（magic `ORME` + version + vertexCount + indexCount + positions + indices），`Core::BinaryReader` 解析，避免 Task 02 上线时同时引入 OBJ/glTF parser。
  - **Texture** 走自有 binary 格式（magic `ORTX` + version + w/h/format + RGBA8 像素），同样规避 stb_image 在 3rdparty 上的 vendoring 子任务。
  - **Shader** 直接读 .spv 字节为 SPIR-V word 流；`ShaderStage` 由文件名后缀（.vert.spv / .frag.spv / .comp.spv）映射，不在 Asset 层解析 OpEntryPoint——那是 Render 模块创建 pipeline 时本来就要走的反射步骤。
  - **AssetRegistry** 走 type-erasure：公共 `Load<T>` / `Get<T>` / `Unload<T>` / `RegisterLoader<T>` 是 template 方法，body 在 header 内 forward 到 4 个非模板私有入口；Impl 用 `std::unordered_map<std::type_index, ...>` 持有 LoaderEntry 与 AssetTable。AssetTable 用 vector<Slot> + freelist，handle = slot_index + 1。
  - **dedup** 通过 `unordered_map<path, handle>` 在每个 AssetTable 内部维护：同 path Load → 直接返回缓存 handle，loader 不重入。
  - 内置 loader 注册不在 AssetRegistry 构造时自动完成；调用方按需 `RegisterLoader<...>`，保留 hot-swap 弹性。
- 验证方式：`tests/asset/AssetRegistryTest.cpp` 通过 ctest，覆盖 6 条路径：mesh load/get/unload、texture load、shader load+stage 推断、dedup + unload 后重新触发 loader、错误路径（Unsupported / IoError / SchemaMismatch / InvalidArgument）、跨类型多 loader 共存。
- 验收标准：6/6 ctest 全过；Asset 模块可被 Phase 2 / Task 07 的 Pipeline 当作"路径 → 数据"的 single source of truth 使用。
- Critical Path：是

#### Task 03：定义 Scene 公共接口（World / Entity / ISystem / Transform / Hierarchy） ✅
- 描述：把 Scene 模块的"用户面"先定义出来——Entity 标识、TransformComponent / HierarchyComponent 两个内置组件、ISystem 抽象、World 公共声明。Phase 2 / Task 04 起把 World 真正接到 EnTT 后端。
- 输入：Phase 2 / Task 02 收尾（Asset 模块独立可用；Core::Time 提供 FrameContext）
- 输出：
  - `Proposed: include/orange/engine/scene/Entity.h`（顶层 `Orange::Engine::Entity`）
  - `Proposed: include/orange/engine/scene/TransformComponent.h`（`glm::vec3` + `glm::quat` TRS）
  - `Proposed: include/orange/engine/scene/HierarchyComponent.h`（parent / firstChild / next/prevSibling 双向兄弟链）
  - `Proposed: include/orange/engine/scene/ISystem.h`（`Orange::Engine::Scene::ISystem`，OnAttach/OnDetach/OnUpdate(World&, FrameContext&)）
  - `Proposed: include/orange/engine/scene/World.h`（顶层 `Orange::Engine::World`，PIMPL；CRUD 模板仅声明）
  - `Proposed: src/scene/World.cpp`（PIMPL 骨架 + 自研 sparse-set 实体生命周期）
  - `Proposed: src/scene/SceneHeaderCheck.cpp`
- 影响路径/模块：Scene
- 前置依赖：Task 02
- 实现要点：
  - **Entity** 是 nominal class（非 alias）：opaque 64-bit，调用方不解读高低位拆分；专属 `std::hash` 特化便于直接做 unordered_map key。
  - **HierarchyComponent** 用 `parent + firstChild + next/prevSibling` 而非 `children` 数组——保持定长字段、不破 archetype SoA；双向兄弟链支持 O(1) 摘除。
  - **TransformComponent** 只存本地 TRS；世界矩阵不在组件上缓存，由 Render 模块在收集 drawable list 时按需合成。
  - **World** 公共面分两层：非模板生命周期（CreateEntity / DestroyEntity / IsValid / Size / Empty）现在就实现，让 sample 可以构 World 并 round-trip 实体；模板组件 CRUD 只声明，body 留给 Task 04 接 EnTT 时连同 erased 入口一起写。
  - 公共头**不**暴露任何 EnTT 类型——保留后端切换余地，也避免在 EnTT soft dep 期把整个引擎绑死。
  - `World` 当前内部用最小 sparse-set（vector<generation> + vector<bool> alive + freeIndices）实现实体生命周期；Task 04 引入 EnTT 时整体替换 Impl。
- 验证方式：`SceneHeaderCheck.cpp` 在隔离环境下能 include 五个公共头并通过编译；`orange_engine` 静态库链通；现有 6 个 ctest case 不受影响。
- 验收标准：Scene 模块的公共表面就绪；Render 模块（Task 05+）可以把 World 当作 drawable 来源；EnTT 接通可纯实现路径推进。
- Critical Path：是

#### Task 04：实现 EnTT 包装（World 的后端） ✅
- 描述：把 EnTT 从 soft dep 升 REQUIRED；用 entt::registry 替换 Task 03 的临时 sparse-set；接通 World 的组件 CRUD 模板路径；提供 entt::registry 的逃生舱口供 Render / Physics / 序列化层使用。
- 输入：Phase 2 / Task 03（Scene 公共接口骨架）
- 输出：
  - `Modified: 3rdparty.json`（EnTT 加 version `v3.13.2` 与 `cmake_extra: ["-DENTT_INSTALL=ON"]`）
  - `Proposed: scripts/fetch_and_build_3rdparty.py`（JSON 驱动的依赖拉取 + 构建 + 安装脚本，与 OrangeRender 同一思路但表从 `3rdparty.json` 读）
  - `Modified: cmake/Dependencies.cmake`（EnTT 升 REQUIRED）
  - `Modified: CMakeLists.txt`（EnTT 无条件 PUBLIC 链接；移除 Task 03 时的 `if(TARGET EnTT::EnTT)` 条件块）
  - `Modified: cmake/OrangeEngineConfig.cmake.in`（`find_dependency(EnTT 3.13 CONFIG)` 无条件加入；删除 `_ORANGE_ENGINE_NEEDS_ENTT` gate）
  - `Modified: include/orange/engine/scene/World.h`（公共 API 暴露 `entt::registry&`；模板 CRUD inline body 走 entt::registry 接口；`ToEntt` / `FromEntt` 静态转换）
  - `Modified: src/scene/World.cpp`（替换为 entt::registry 后端；保留 `mLiveCount` 手动计数维持 `Size()` 精确）
  - `Modified: tests/CMakeLists.txt` + `tests/install/config_smoke.cmake`（forward `EnTT_DIR` 给消费者）
  - `Proposed: tests/scene/SceneWorldTest.cpp`（生命周期 + 组件 CRUD + Hierarchy 链 + entt::registry view）
- 影响路径/模块：Scene、构建系统、tests
- 前置依赖：Task 03
- 实现要点：
  - **EnTT 升 REQUIRED**：`3rdparty.json` 加 `version` / `cmake_extra` 字段；新增 `scripts/fetch_and_build_3rdparty.py`，可被 `python scripts/fetch_and_build_3rdparty.py --only EnTT` 一键安装到 `D:/3rdparty/install`。脚本走 git clone + cmake configure / build / install，不引入 vcpkg。
  - **entt::registry 暴露公共**：与 3rdparty.json 中"EnTT linkage = PUBLIC"对齐——游戏侧要能 emplace 自己的 component 类型，必须能 #include `<entt/entt.hpp>`。原本计划的 PIMPL 在保持等同 EnTT 零开销的前提下做不到，且 0.x 阶段不承诺 ABI 稳定，PIMPL 的成本不值得。
  - **`ToEntt` / `FromEntt`**：Entity 内部用 64-bit 数值，把 entt::entity 的位模式直接放低 32 位（高 32 位留 0），不解读 entt 内部的 index/version 拆分。
  - **`AddComponent` 用 `emplace_or_replace`**：让"对同一实体重复 add"成为幂等覆盖语义，而不是 EnTT 默认 `emplace` 的 "已存在则 abort"——更符合 Phase 1 工程化中的容错风格。
  - **`Size()` 手动计数**：v3.13 的 `storage<entt::entity>` 迭代会把 tombstone 也吐出来，`free_list()` 又是返回实体而非计数；不如在 World 自己的 Create / Destroy 路径上 ±1 一个 `mLiveCount`。代价：通过 `Registry()` 逃生舱口直接 create/destroy 会绕开本计数——这点在 World.h 的注释里写明白。
  - **config_smoke 转发 `EnTT_DIR`**：消费者侧的 `find_dependency(EnTT)` 需要被告知 cmake config 路径；和 OrangeRender_DIR / glm_DIR / nlohmann_json_DIR / glfw3_DIR 同模式追加一行。
- 验证方式：`tests/scene/SceneWorldTest.cpp` 通过 ctest，覆盖 5 条路径：实体 CRUD、组件 CRUD、Destroy 联带卸载组件、Hierarchy 双向链常见操作（O(1) 摘除中间节点）、`Registry()` 逃生舱口跑 EnTT view。`config_smoke` 复测确认从零消费者侧 find_package(OrangeEngine) → find_dependency(EnTT) 链路完整。
- 验收标准：7/7 ctest 全过；World 可被任何模块当作组件存储 + view 入口使用；EnTT 在 Dependencies.cmake / Config.cmake.in / 顶层 CMakeLists.txt 三处一致表达为 REQUIRED PUBLIC。
- Critical Path：是

#### Task 05：定义 Render 模块公共接口（Camera / RenderableComponent / Pipeline） ✅
- 描述：交付 Render 模块的"用户面"——Camera 数据壳、RenderableComponent 关联组件、Pipeline 公共声明。Pipeline 内部接 OrangeRender RenderGraph 由 Task 06 / 07 落地。
- 输入：Phase 2 / Task 04（World 接通 EnTT）
- 输出：
  - `Proposed: include/orange/engine/render/Camera.h`（header-only struct + inline 工厂；Vulkan NDC y-down + z 0..1，矩阵手写不依赖 GLM_FORCE_*）
  - `Proposed: include/orange/engine/render/RenderableComponent.h`（mesh handle + texture handle + visible 开关）
  - `Proposed: include/orange/engine/render/Pipeline.h`（PIMPL 类；`Render(World&)` 单一驱动方法；公共头**不**包含 `<orange/...>`）
  - `Proposed: src/render/Pipeline.cpp`（PIMPL 骨架 + Render() 占位 no-op；OrangeRender 接通延后到 Task 06/07）
  - `Proposed: src/render/RenderHeaderCheck.cpp`
  - `Proposed: tests/render/RenderInterfaceTest.cpp`（Camera 矩阵关键元素 + RenderableComponent CRUD + Pipeline 占位调用）
- 影响路径/模块：Render、tests
- 前置依赖：Task 04
- 实现要点：
  - **Camera 是 value-type，不是 component-of-camera**：与 RenderableComponent 同类，可直接 AddComponent 到 entity 上；Pipeline::Render 在每帧从 World 中取出第一个挂 Camera 组件的实体作为本帧视图来源。
  - **Camera 工厂内部手写矩阵**：Vulkan NDC y-down + z ∈ [0,1] 由 OrangeRender 同样的约定决定；不靠 `glm::perspective` / `glm::ortho` 的 GLM_FORCE_* 配置——保持调用点的数学行为可见、可移植。和 OrangeRender particle_field sample 的 MakeOrthoVulkan 同一思路。
  - **RenderableComponent 持有 AssetHandle 而非裸指针**：资源生命周期由 AssetRegistry 管；handle 可在序列化路径上原样写入；跨实体复用同一 mesh 时 registry 的 dedup 缓存自动生效。
  - **Pipeline 走 PIMPL**：依据 CLAUDE.md "Header isolation" 不变量，`<orange/...>` 只允许出现在 `src/render/**`；公共 Pipeline.h 通过 `unique_ptr<Impl>` 把 OrangeRender RHI / RenderGraph 类型完全藏在 .cpp。
  - **Render() 当前空 body**：让 sample / 测试 / AppHost 现在就能把 Pipeline 串进主循环——不报错、不假装在做实际渲染。Task 06 加 RenderScene 收集逻辑、Task 07 接 OrangeRender 真正下发。
  - **`InsertPass` 不提前 stub**：CLAUDE.md 写明 InsertPass 在 Phase 3 起出现、Phase 5 才接通；当前 task 不放占位接口污染公共面。
- 验证方式：`tests/render/RenderInterfaceTest.cpp` 通过 ctest，覆盖 4 条路径：Camera::Orthographic 矩阵元素正确（Y-flip + z 0..1）、Camera::Perspective 关键元素（f / aspect、Y-flip、w 输出）、RenderableComponent 在 World 里 CRUD 一遍 + 默认 visible=true、Pipeline::Render(world) 在空 World / 多组件 World / 重复调用三种情况下不崩。
- 验收标准：8/8 ctest 全过；Render 模块的公共表面就绪——Pipeline 可被 sample / Layer / 任何调用方持有并 Render；具体绘制逻辑由 Task 06 / 07 在保持公共面不变的前提下填充。
- Critical Path：是

#### Task 06：实现 RenderScene 收集（World → drawable list） ✅
- 描述：把 ECS 的"主相机 + 可见 Renderable"翻成扁平 drawable 列表；让 Task 07 的真实下发只关心 mesh / texture / world matrix，不再触碰 World / Component。
- 输入：Phase 2 / Task 05（Render 公共接口骨架）
- 输出：
  - `Proposed: include/orange/engine/render/RenderScene.h`（Drawable POD + RenderScene 类；公共面无 entt 类型）
  - `Proposed: src/render/RenderScene.cpp`（用 entt::view 扫两类组件、TRS 合成 world matrix）
  - `Modified: src/render/Pipeline.cpp`（Impl 持有 RenderScene；Render(world) 走 Clear + Collect）
  - `Modified: src/render/RenderHeaderCheck.cpp`（追加 RenderScene.h）
  - `Proposed: tests/render/RenderSceneTest.cpp`
- 影响路径/模块：Render、tests
- 前置依赖：Task 05
- 实现要点：
  - **Camera 取首个**：用 `entt::view::front()` 显式拿首个挂 Camera 组件的实体；多相机 / 主相机选择留待引入 `ActiveCameraTag` marker component（不在本 task 范围）。
  - **Drawable 收集**：`view<TransformComponent, RenderableComponent>()`；`visible=false` 在循环内 continue 过滤；只有 Transform 没 Renderable / 只有 Renderable 没 Transform 的实体不会出现在 drawable list（view 只命中两者皆有）。
  - **TRS world matrix 合成**：`T * mat4_cast(R) * S`，列向量惯例对应 `worldVec = M * localVec`。Hierarchy 父子链的复合矩阵**不在本 task 范围**——Phase 2 sample 都是单层实体，flat=local 即可；引入复合 matrix 路径会同时拉进"depth-first 还是按拓扑序"、"是否缓存计算结果"等设计问题，留给 Phase 4 关卡复杂化时一并解决。
  - **`Collect` 接受 `const World&` 但内部 const_cast 拿 mutable registry**：entt::view 的迭代需要 mutable registry 引用，但本实现保证只读；语义上对调用方仍是"World 不被修改"。
  - **MSVC C4702 规避**：`for + break` 在 MSVC `/W4 /WX` 下偶发被误判为 unreachable；改用 `if (auto e = view.front(); e != entt::null)` 的早退分支更稳。
- 验证方式：`tests/render/RenderSceneTest.cpp` 通过 ctest，覆盖 5 条路径：空 World 无相机无 drawable；仅 Camera 的 World（HasCamera + 0 drawable）；Camera+多种 Renderable（visible=false 与缺 Transform 都被滤掉）；TRS world matrix 关键元素（平移列 [3] + 缩放对角）；Clear+Recollect 不残留状态、不重复叠加 drawable。
- 验收标准：9/9 ctest 全过；RenderScene 可被 Pipeline 在每帧顶部 Clear+Collect 用作 working buffer；Task 07 的实际下发逻辑只需读 RenderScene::MainCamera 与 RenderScene::Drawables。
- Critical Path：是

#### Task 07：实现最小 Pipeline（接通 OrangeRender RenderGraph，绘制单个 mesh） ✅
- 描述：把 Pipeline 真正接到 OrangeRender——RenderDevice / Renderer / 内置 GraphicsPipeline / shader 模块 / 帧生命周期全部串起来；用 gl_VertexIndex 走的硬编码 triangle 作为"绘制单个 mesh"的最小可运行实现。drawable list 驱动的 mesh upload 在 Task 09 的 sample 真正需要可视化时一并接通。
- 输入：Phase 2 / Task 06（RenderScene 收集就绪）
- 输出：
  - `Proposed: src/render/builtin_shaders/minimal_mesh.vert.glsl`（gl_VertexIndex 写出三角形）
  - `Proposed: src/render/builtin_shaders/minimal_mesh.frag.glsl`（固定 OrangeEngine 主色输出）
  - `Modified: include/orange/engine/render/Pipeline.h`（加 Initialize / Shutdown / IsInitialized；Render 改为可在未 initialize 时 no-op）
  - `Modified: src/render/Pipeline.cpp`（Impl 持有 RenderDevice + Renderer + ShaderModule + GraphicsPipeline；Initialize 串完整链路；Render 走 BeginFrame + 硬编码 triangle SubmitItem + EndFrame；Shutdown 走 WaitIdle + reverse-order reset；析构调用 Shutdown 兜底）
  - `Modified: CMakeLists.txt`（find_program glslangValidator + add_custom_command 编 GLSL → SPIR-V，落到 ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/shaders/orange_engine/；orange_engine 依赖 orange_engine_builtin_shaders 自定义 target）
- 影响路径/模块：Render、构建系统
- 前置依赖：Task 06
- 实现要点：
  - **范围决策**：spec 是"绘制单个 mesh"，但完整实现（mesh upload + push constant + 真正按 drawable list 驱动绘制）一次性 commit 风险高且没 sample 时无法视觉验证。本 task 折中——把 OrangeRender 完整链路 **真正接通**（RenderDevice + Renderer + GraphicsPipeline + frame lifecycle 都跑起来），用硬编码 triangle 走 gl_VertexIndex 作为"single mesh"的最小代表。drawable list 仍然 Clear+Collect 一遍但 pipeline 内部不读其几何——这一段在 Task 09（textured_quad sample）真正需要可视化时切到 mesh-asset-driven，配合 sample 实跑做视觉验证。
  - **Shader 编译走 glslangValidator**：CMake `find_program` 锁 VULKAN_SDK 下的可执行文件，找不到 FATAL_ERROR——Render 模块强依赖它。`add_custom_command(OUTPUT ...)` 用 `$<CONFIG>` generator expression 让 .spv 落到与 .exe 同一 multi-config 子目录（`build/bin/Debug/shaders/orange_engine/`），运行时按 "shaders/orange_engine/<name>.spv" 相对工作目录解析；orange_engine target 通过 add_dependencies 强制 .spv 先于 link 阶段就绪。
  - **Pipeline 析构兜底 Shutdown**：保证调用方忘记显式 Shutdown 时 OrangeRender 资源仍能按"WaitIdle → graphicsPipeline → shaders → renderer → renderDevice"的反向顺序释放，避免资源在 swap-chain 还活着时被销毁。
  - **未 Initialize 时 Render 是 no-op**：让"在主循环顶层无脑调一发 Render"成为受支持的退化状态，与现行 minimal_window sample 共存。
  - **Renderer 接 native window handle 用 Window::GetNativeWindowHandle**（HWND）：当前 OrangeRender 的 RendererDesc::mpNativeWindowHandle 是 `void*`，与 Platform::Window 暴露出的 HWND 兼容。两侧若在 OrangeRender 后续 phase 调整接口，会在 .cpp 内集中改。
- 验证方式：
  1. cmake build 通过——证明 SPIR-V 编译、orange_engine 链路、消费者 `find_package(OrangeEngine)` (config_smoke) 三层都没回归。
  2. ctest 9/9（同 Task 06）——Pipeline 改动不破任何已有测试（Render 在未 init 时仍是 no-op 与 RenderInterfaceTest 的占位假设一致）。
  3. light runtime smoke：`samples/01_minimal_window.exe` 跑起来 + `CloseMainWindow` 触发干净退出——证明 Pipeline 的额外 link 依赖（Renderer / RHI 符号、SPIR-V 加载路径）在启动 / 析构时都不挂。
  4. **真正的视觉正确性验证延后到 Task 09 sample**——届时把 Pipeline.Initialize / Render / Shutdown 串进 sample，目视确认三角形上屏。
- 验收标准：9/9 ctest 通过 + minimal_window 启动收尾干净 + 内置 SPIR-V 落到正确目录；Pipeline 公共表面已具备所有"启动 / 渲染 / 关闭"操作所需方法。视觉正确性的责任明确转移给 Task 09。
- Critical Path：是

#### Task 08：完成 `samples/02_ecs_basics`（实体/组件 CRUD） ✅
- 描述：控制台 sample——纯 ECS 演示，不开窗口、不接 Pipeline。让"实体-组件-查询"这套心智模型独立呈现，给对引擎不熟的读者最小学习材料。
- 输入：Phase 2 / Task 04（World 接通 EnTT）
- 输出：
  - `Proposed: samples/02_ecs_basics/main.cpp`（控制台程序，stdout 打印每步结果）
  - `Proposed: samples/02_ecs_basics/CMakeLists.txt`
  - `Modified: samples/CMakeLists.txt`（add_subdirectory 02_ecs_basics）
- 影响路径/模块：Samples
- 前置依赖：Task 04
- 实现要点：
  - **控制台模式而非窗口模式**：本 sample 强调 ECS 与 Pipeline / 渲染的解耦——World 可独立存在并被操作。窗口模式 sample 在 Task 09 / 10 出场。
  - **演示路径覆盖**：World 创建 / 实体生命周期（Create / Destroy / IsValid） / 组件 CRUD（Add / Has / Get / Remove） / Hierarchy 双向链构造（parent + firstChild + prev/nextSibling）/ EnTT 视图遍历（`world.Registry().view<T>()`）/ DestroyEntity 联带卸载组件。
  - **每步 stdout**：每个操作打印前后状态，让读者能 `cmake --build && build/bin/Debug/02_ecs_basics.exe` 直接观察 API 行为；新加的 `DumpEntity` / `DumpHierarchy` 局部辅助函数把"打印"和"操作"分离，主流程读起来像教程。
- 验证方式：build + 跑 `02_ecs_basics.exe`，目视确认 7 个段落（World 初始化 / 创建 + AddComponent / Has 查询 / Hierarchy 链 / view 遍历 / RemoveComponent / DestroyEntity）输出与预期一致；World::Size 在 Add / Destroy 之后变化正确；DestroyEntity(shield) 之后 IsValid(shield) 返回 false。
- 验收标准：sample 可被 build + 运行；输出符合上述断点；现有 9/9 ctest 不受影响。
- Critical Path：否（本 sample 是教学材料，Phase 2 milestone 不依赖它本身可视化）

#### Task 09：完成 `samples/03_textured_quad`（带贴图四边形） ✅
- 描述：Phase 2 milestone 的视觉验证节点——把 ECS → AssetRegistry → Pipeline → OrangeRender 的完整数据流串到位，让一个 quad mesh 真上屏。
- 输入：Phase 2 / Task 07（Pipeline 已通 OrangeRender 帧生命周期）+ Task 08（前置 sample 验证 ECS API）
- 输出：
  - `Modified: include/orange/engine/asset/MeshAsset.h`（加可选 UV 字段，三参数构造重载，HasUVs() 查询；不破 MeshLoader v1 binary 格式）
  - `Modified: include/orange/engine/asset/AssetRegistry.h` + `src/asset/AssetRegistry.cpp`（加 `Insert<T>` / `InsertErased`，允许把已构造好的 unique_ptr<T> 直接注册——sample 程序式生成 mesh 时省掉"先写 .orme 再 Load"的中间步）
  - `Modified: include/orange/engine/platform/Window.h` + `src/platform/glfw/Window.cpp`（加 `GetGlfwWindowHandle()` 返回 GLFWwindow* 透成 void*；公共头继续不依赖 GLFW 类型）
  - `Proposed: src/render/builtin_shaders/textured_mesh.vert.glsl`（pos+uv vertex layout + push-constant MVP）
  - `Proposed: src/render/builtin_shaders/textured_mesh.frag.glsl`（uv 程序式合成 8×8 棋盘 + uv 渐变 tint，作为"贴图存在"的视觉证据）
  - `Removed: src/render/builtin_shaders/minimal_mesh.{vert,frag}.glsl`（被 textured_mesh 取代）
  - `Modified: include/orange/engine/render/Pipeline.h`（Initialize 接 `(Window&, AssetRegistry&)`）
  - `Modified: src/render/Pipeline.cpp`（重写：UploadContext + mesh GPU 缓存 + push-constant MVP + 真正按 RenderScene::Drawables() 驱动 SubmitItem；shader 路径改为 .exe-相对解析，避免 CWD ≠ exe 目录时 LoadSpirv 失败）
  - `Modified: CMakeLists.txt`（编译 textured_mesh shader 取代 minimal_mesh）
  - `Proposed: samples/03_textured_quad/main.cpp` + `CMakeLists.txt`
  - `Modified: samples/CMakeLists.txt`
- 影响路径/模块：Asset、Platform、Render、Samples、构建系统
- 前置依赖：Task 07、Task 08
- 实现要点：
  - **范围决策**：当前 OrangeRender 的 RHI 还没暴露 sampler / descriptor-set 路径——它自己的 `textured_mesh` sample 也走"fragment shader 从 uv 程序式合成 checker 当贴图"的同思路。本 task 沿用这个思路：vertex layout、UV、push-constant MVP、drawable-driven 渲染全部走真实路径；只把"采样真 TextureAsset"那一段延后到 OrangeRender RHI 升级。
  - **MeshAsset UV 字段是内存路径专属**：MeshLoader v1 binary 格式不变（pos + indices）；UV 仅存在于"程序式 / `Insert` 路径"上。后续要把 UV 写入 .orme 时升 schema_version + 加 attribute mask 即可，不破公共 API。
  - **`AssetRegistry::Insert` 不依赖 RegisterLoader**：deleter 由 Insert 自己传入，让 sample / 工具流不必为程序式生成的资源再走"注册一个 dummy loader"的圈。同 path 重复 Insert 等于幂等覆盖（释放旧 asset、handle 沿用）。
  - **`Window::GetGlfwWindowHandle()`**：OrangeRender 的 `RendererDesc::mpNativeWindowHandle` 期望 `GLFWwindow*`（注释写明），不是 HWND——之前 Task 07 的实现误传 HWND 导致 03_textured_quad 第一次跑直接段错误。修：加一个新方法专门透 GLFW handle，原 `GetNativeWindowHandle()` 仍返回 HWND 给将来需要 D3D 路径的消费者。
  - **shader 路径用 .exe-相对解析**：用 `GetModuleFileNameW` 拿到 .exe 自身目录、再拼 `shaders/orange_engine/...spv`。跑 sample 的 CWD 不一定与 .exe 目录重合（从 repo 根直接 `build/bin/Debug/...exe` 跑就不重合），exe-相对路径把 SPIR-V 加载稳稳锚定。
  - **mesh GPU cache**：`unordered_map<u64 handleValue, MeshGpu>`，首次见到某 mesh handle 时通过 `Resource::UploadContext::UploadBuffer` 把 interleaved 顶点 + uint32 索引推 GPU；后续帧命中缓存直接重用。MeshGpu 持有 `unique_ptr<RHIBuffer>` 两个，Pipeline::Shutdown 在 WaitIdle 之后 clear 整张表完成回收。
  - **Sample 用 `AssetRegistry::Insert` 程序式造 quad**：4 顶点 (pos+uv) + 6 索引，覆盖 ortho [-1,1]² 中央。RenderLayer 在 OnUpdate 里调 Pipeline.Render；Pipeline.Shutdown() 必须早于 host 析构（Window 还活时释放渲染资源）。
- 验证方式：
  1. cmake build 通过——shader 编出 SPIR-V、orange_engine 链通、消费者 `find_package` (config_smoke) 不退化。
  2. ctest 9/9 通过——核心 + asset + scene + render + install/config smoke 全绿。
  3. 跑 03_textured_quad.exe，截屏：窗口正中显示橙 + 深蓝 8×8 棋盘 + 一点 uv 渐变 tint，背景为 swap-chain 清色（黑）；这是 ECS（quad entity + camera）→ RenderScene → Pipeline → OrangeRender RenderGraph → swap-chain 整条数据流的真实视觉证据。
  4. 关闭窗口 → exit code 0：Pipeline.Shutdown 与 OrangeRender 资源反向释放路径正确。
- 验收标准：上述 4 条全部成立。Phase 2 milestone（"屏幕显示一个由 ECS 实体驱动的带贴图 mesh"）的"由 ECS 驱动 + mesh 上屏"两半已经成立；剩"真贴图 sampler 化"等 OrangeRender RHI 路径打通后再补一刀。
- Critical Path：是

#### Task 10：完成 `samples/04_3d_mesh`（旋转 3D mesh，无 bloom） ✅
- 描述：Phase 2 收尾的 3D 视觉验证节点——把 perspective camera + 立方体 mesh + 每帧 TransformComponent 旋转串到位，确认引擎已具备"3D 实体在 3D 投影下旋转上屏"的全链路能力。无 bloom / 无后处理（这些由 Phase 3 引入）。
- 输入：Phase 2 / Task 09（textured-mesh pipeline + AssetRegistry::Insert + drawable-driven Render）
- 输出：
  - `Modified: src/render/Pipeline.cpp`（rasterizer state 切到 `CullMode::Back` + `FrontFace::CounterClockwise` 默认值——3D 实体没背面剔除会有严重 overdraw）
  - `Modified: samples/03_textured_quad/main.cpp`（quad 索引从 `(0,1,2,0,2,3)` 翻成 `(0,2,1,0,3,2)`，让 world-CCW 输入经 Y-flip projection 后在 framebuffer space 中变成 CCW = front-facing，与 OrangeRender procedural_scene 的约定对齐）
  - `Proposed: samples/04_3d_mesh/main.cpp`（24 顶点立方体 + per-face UV [0,1]² + perspective camera + Layer::OnUpdate 改 TransformComponent.rotation）
  - `Proposed: samples/04_3d_mesh/CMakeLists.txt`
  - `Modified: samples/CMakeLists.txt`（add_subdirectory 04_3d_mesh）
- 影响路径/模块：Render（一行 rasterizer state）、Samples、构建系统
- 前置依赖：Task 09
- 实现要点：
  - **Cube 是 24 顶点而非 8 顶点**：8 顶点共享角点会导致每个面没法独立 UV [0,1]²，checker shader 会扭曲。24 顶点（每面 4 个）让 textured_mesh shader 复用即可——每面看起来都是干净的 8×8 棋盘，转动时三个可见面之间的边界清晰可读，是"3D 实体真的在转"的直观证据。
  - **背面剔除约定**：OrangeRender 默认 rasterizer 是 `FrontFace::CounterClockwise + CullMode::Back`，"front face = CCW in framebuffer space (Vulkan Y-down NDC)"。Camera 工厂的 projection 内置 `p[1][1] = -f` Y-flip，会把"输入 world-CCW"翻成"NDC-CW"——直接 CullMode::Back 就把所有外向面都剔了。修：本 task 起规定 sample / engine 内置 mesh 的索引按 `(0, 2, 1, 0, 3, 2)` 等"world-CW = NDC-CCW after Y-flip" 编排，与 OrangeRender procedural_scene 的注释里写的同一约定。03_textured_quad 的 quad 索引随 pipeline 切到 Back-cull 一并翻一刀，回归测试同时确认 03 仍正确上屏。
  - **没 depth buffer 也不写 depth**：OrangeRender 的 BeginFrame/SubmitItem/EndFrame 路径目前只挂 swapchain color attachment，VkRenderingInfo 没 depth attachment。所以 Pipeline 仍 `mDepthTestEnable=false`/`mDepthWriteEnable=false`，3D 凸体（cube）只靠背面剔除做"显隐"——任意旋转下，相机能看到的只有 1–3 个外向面，互不重叠，视觉上等价于深度正确的 cube。深度路径（depth attachment 接通 + Pipeline 升 `DepthTestEnable=true`）等 OrangeRender 暴露 depth 接口或 Phase 3 自定义 RenderTarget 路径打通后再补，本 task 不在 Render 模块重新铺设这条线。
  - **Camera 用 `Camera::Perspective` + 手动 view matrix**：Camera struct 当前只对 `projection` 提供工厂；view 留给调用方填。Sample 用 `glm::lookAt(eye, center, up)` 从 (2.5, 1.7, 3.0) 看向原点——拿出三个面（+X、+Y、+Z）让 checker 可读。aspect 用窗口尺寸算，fovY=45°，near=0.1，far=100。本 task 不给 Camera 加 LookAt 工厂——单 sample 用 `glm::lookAt` 一行写完，提抽象层为时尚早；多个 sample 都要时再统一设计。
  - **旋转动画走 OnUpdate 改 TransformComponent.rotation**：`RenderLayer` 持有 cube entity，每帧从 `FrameContext::time.totalSeconds` 算角度，更新 `TransformComponent::rotation`（quat），再调 `Pipeline::Render(world)`。RenderScene::Collect 已经按 `T * R * S` 合成 worldMatrix，旋转自动随 quat 走。轴向选 `normalize(0.4, 1.0, 0.2)`——非主轴让三个面交替露出来，看着比单纯绕 Y 轴更"3D"。
  - **AssetRegistry::Insert 复用 03 路径**：cube 是程序式构造，沿用 Task 09 加的 `Insert<MeshAsset>`；不引入 .orme 资源文件、不动 MeshLoader v1 binary 格式。
- 验证方式：
  1. cmake build 通过——orange_engine 链通、shader 不变、消费者 `find_package` (config_smoke) 不退化。
  2. ctest 9/9 通过——pipeline rasterizer 调整对 RenderInterfaceTest / RenderSceneTest 的占位调用是透明的（这些测试不验证视觉、只验证 API 形状）。
  3. 跑 03_textured_quad.exe 回归确认（索引翻转 + Cull::Back 后仍正确上屏，棋盘居中、颜色不变、关闭窗口干净退出）。
  4. 跑 04_3d_mesh.exe，目视确认：屏幕中央有立方体绕斜轴匀速旋转；同时可见 1–3 个面（每面 8×8 橙/深蓝棋盘）；面与面之间的接缝随旋转自然推移；无明显 z-fighting / 内面穿透 / 闪烁；窗口可关闭，exit code 0。
- 验收标准：上述 4 条全部成立。Phase 2 整阶段（"ECS → Asset → Render → OrangeRender 全链路 + 2D 与 3D 两类视觉证据"）收口完成；可向 Phase 3 推进材质 / 后处理 / 自定义 shader 注入。
- Critical Path：是

### Phase 3：Ori 视觉基线

> 任务模板沿用 Phase 2 风格，详细字段在落地各 Task 时填充。Phase 3 起的 Task 列表先列要点，详细 Task Breakdown 在 Phase 2 收尾后产出 increment patch。

- Task 01：Material / MaterialInstance 公共接口
- Task 02：MaterialTemplate 内置（卡通 + rim light）
- Task 03：PostProcessChain 接口与默认链（HDR → Bloom → Tonemap → LUT）
- Task 04：自定义 shader 注入接口（MaterialSystem::RegisterTemplate）
- Task 05：软阴影实现（PCF）
- Task 06：升级 `samples/04_3d_mesh_with_bloom` 显示 Ori 风格
- Task 07：`samples/07_full_pipeline` 雏形（带后处理的综合演示）
- Task 08：自定义 shader sample（验证游戏侧能不改引擎注册新 shader）

#### Task 01：定义 Material / MaterialInstance 公共接口 ✅
- 描述：交付 Material 模块的"用户面"——`MaterialUniformType` 枚举与描述符、`Material`（值类型 template，描述 shader + uniform 布局 + texture 槽）、`MaterialInstance`（PIMPL，每实例 uniform / texture 覆盖）。Phase 3 / Task 02 把内置卡通 + rim-light template 真正铺出来；Phase 3 / Task 04 引入 `MaterialSystem::RegisterTemplate` 走自定义 shader 注入路径。Task 01 不接 RHI、不动 RenderableComponent、不引入 MaterialSystem——纯类型表面。
- 输入：Phase 2 收尾（Asset / Render 公共面 + Pipeline 已接通 OrangeRender）
- 输出：
  - `Proposed: include/orange/engine/render/MaterialTypes.h`（`MaterialUniformType` 枚举：Float/Vec2/Vec3/Vec4/Int/Mat4；`MaterialUniformDesc { name, type }`；`MaterialTextureSlotDesc { binding, name }`）
  - `Proposed: include/orange/engine/render/Material.h`（`struct Material`：name + 顶点/片段 ShaderAsset handle + uniforms 描述符列表 + texture 槽描述符列表；纯数据，无 PIMPL）
  - `Proposed: include/orange/engine/render/MaterialInstance.h`（`class MaterialInstance`：构造时绑定 `const Material*`；`SetUniform(name, value)` / `SetTexture(binding, handle)` / `HasUniformOverride` / `HasTextureOverride`；PIMPL 隐藏 override 存储）
  - `Proposed: src/render/MaterialInstance.cpp`（PIMPL 实现：unordered_map<string, UniformValue> + unordered_map<uint32_t, AssetHandle<TextureAsset>>；UniformValue = type tag + 64-byte 对齐 blob；SetUniform 在 Material 描述符里查 name + 比对 type，不匹配 → 直接 no-op）
  - `Modified: src/render/RenderHeaderCheck.cpp`（追加新三个公共头）
  - `Modified: CMakeLists.txt`（orange_engine 源列表加 MaterialInstance.cpp）
  - `Proposed: tests/render/MaterialInterfaceTest.cpp`
  - `Modified: tests/CMakeLists.txt`（注册 material_interface_test）
- 影响路径/模块：Render、tests、构建系统
- 前置依赖：Phase 2 全部任务（Asset 模块的 ShaderAsset / TextureAsset handle 已稳定，Render 公共面已接通 OrangeRender 真实下发路径）
- 实现要点：
  - **Material 是 plain struct，不走 PIMPL**：它只是 "shader + uniform 布局 + texture 槽" 的描述记录——把这层 PIMPL 化没收益、还把字段 visibility 弄复杂。MaterialInstance 才有动态状态（per-instance uniform value blob），那一边走 PIMPL 把 type-erased 存储藏到 .cpp。
  - **MaterialInstance 绑 `const Material*` 而非 shared_ptr**：Task 01 不规定 Material 的所有权策略（Asset 风格 / MaterialSystem 风格 / 局部值都可能）——把 ownership 决策推迟到 Phase 3 / Task 02 / 04 真正铺存储时。当前调用方负责让 Material 活到所有引用它的 instance 都析构完。这是 0.x 阶段可以接受的契约，等 Task 04 引入 MaterialSystem 后会被替换为 system-managed 引用。
  - **uniform type 检查在 SetUniform 内做**：在 Material.uniforms 里查 name；找不到 → no-op；type 不匹配 → no-op。**no-op 而不是 abort**——uniform schema 演化（template 加新字段、调用方还在传旧值）是工程现实，silently 忽略比让游戏崩溃更稳。Phase 3 / Task 02 引入第一个内置 template 时，如果发现 silent-ignore 隐藏了真 bug，再加可选 ORANGE_LOG_WARN。
  - **uniform value 存储用 64-byte 对齐 blob + type tag**：Mat4 是 64 字节，是当前所有支持类型里最大的；用 std::array<byte, 64> + MaterialUniformType tag 做存储，避免在公共头暴露 std::variant<glm 多类型>。Task 02 起把这个 blob 翻成 push-constant / UBO bytes 时复用同一布局，无 schema churn。
  - **不在 Task 01 加 SetUniformXxx getter**：Pipeline / MaterialSystem 真正消费 override 时（Task 02 起）会按需要决定是公开 read-back API 还是 friend 一个 internal accessor。Task 01 提交的 `HasUniformOverride` / `HasTextureOverride` 仅作为单元测试 visible 的 query，业务路径不依赖它。
  - **不动 RenderableComponent**：Phase 2 Task 05 的 RenderableComponent 注释里已经预告 "Phase 3 接入 Material 系统后 texture 槽会被 AssetHandle<MaterialInstance> 取代"——但那一刀属于 Task 02（第一个真用 MaterialInstance 的 sample 出现时）。Task 01 不引入 ECS schema 变更，避免给 EnTT 的 archetype 制造无用迁移。
  - **公共头继续不漏 OrangeRender / Vulkan 类型**：Material 里只有 std::string + AssetHandle + std::vector + 自定义 POD 描述符；MaterialInstance 通过 PIMPL 把 unordered_map 也藏到 .cpp。RenderHeaderCheck.cpp 在 ctest 路径上守卫 isolation 不变量。
- 验证方式：`tests/render/MaterialInterfaceTest.cpp` 通过 ctest，覆盖 6 条路径：
  1. Material 默认构造的 uniforms / textureSlots 为空、HasUVs 等查询行为合理；
  2. Material 显式构造（含两个 uniform + 一个 texture 槽）后字段读回正确；
  3. MaterialInstance 绑定 Material 后 GetMaterial 返回原指针、HasUniformOverride / HasTextureOverride 全为 false；
  4. SetUniform 对已声明且 type 匹配的 name 生效（HasUniformOverride 转 true）；
  5. SetUniform 对未声明 name 是 no-op；type 不匹配也是 no-op；
  6. SetTexture 对任意 binding 生效；MaterialInstance 移动构造后 override 状态保留。
- 验收标准：cmake build 通过 + 10/10 ctest 全过（之前 9 个 + material_interface_test 一个新增）+ install_smoke / config_smoke 不退化。Material 模块的公共表面就绪——Task 02 起可以把 "build 内置 toon + rim light template" 当作纯实现路径推进。
- Critical Path：是

#### Task 02：MaterialTemplate 内置（卡通 + rim light） ✅
- 描述：交付两套内置 MaterialTemplate（卡通 cel-shading + rim/fresnel light）的全部"模板侧"产物——GLSL 源 + 编译产出的 SPIR-V + `BuiltinMaterials::LoadXxx` 工厂返回完整可用的 `Material`（含已注册 ShaderAsset handle + uniform 布局 + texture 槽布局）。**Pipeline 真正按 MaterialInstance 路由 push-constant / 描述符走绘制是 Phase 3 / Task 04 起的工作**——Task 02 不动 Pipeline.cpp、不改 RenderableComponent、不接 Material 到 RenderScene。这样把 "模板可消费" 与 "渲染管线消费模板" 这两件相互正交的事拆成各自可验证的增量。
- 输入：Phase 3 / Task 01（Material / MaterialInstance 公共面）
- 输出：
  - `Proposed: src/render/builtin_shaders/toon.vert.glsl` + `toon.frag.glsl`（顶点 layout = pos+uv 与 textured_mesh 同；单 push-constant block 打包所有 uniform，≤128 字节，dFdx/dFdy 推 face normal、避免引入 vertex normal attribute）
  - `Proposed: src/render/builtin_shaders/rim_light.vert.glsl` + `rim_light.frag.glsl`（同 layout 同 push-constant 风格；rim 用 fresnel = pow(1 - dot(N, V), uRimPower) * uRimColor * uRimIntensity）
  - `Modified: CMakeLists.txt`（新增 4 个 `orange_engine_compile_builtin_shader` 调用 + `orange_engine_builtin_shaders` 自定义 target 依赖列表追加；orange_engine 源列表加 `src/render/BuiltinMaterials.cpp`）
  - `Proposed: include/orange/engine/render/BuiltinMaterials.h`（namespace `Orange::Engine::Render::BuiltinMaterials`：`Material LoadToon(Asset::AssetRegistry&)` / `Material LoadRimLight(Asset::AssetRegistry&)`）
  - `Proposed: src/render/BuiltinMaterials.cpp`（.exe-相对路径解析复用 Pipeline 的 GetExecutableDir 套路；按 path 走 `registry.Load<ShaderAsset>` 的标准 dedup 缓存——同一 path 重复调用幂等返回同一 handle）
  - `Modified: src/render/RenderHeaderCheck.cpp`（追加 BuiltinMaterials.h）
  - `Proposed: tests/render/BuiltinMaterialsTest.cpp`
  - `Modified: tests/CMakeLists.txt`（注册 builtin_materials_test）
- 影响路径/模块：Render（仅新增模板侧）、tests、构建系统
- 前置依赖：Task 01
- 实现要点：
  - **Push-constant only，全打 ≤128 字节**：Vulkan 最低保证 128 字节 push constant；toon = uMVP(64) + uColorWarm(16) + uColorCool(16) + uLightDir(16) + uShadowThreshold(4) = 116 B；rim_light = uMVP(64) + uViewPos(16) + uRimColor(16) + uRimPower(4) + uRimIntensity(4) = 104 B。两者都不需要 UBO / 描述符集，与 Phase 2 Pipeline 走的纯 push-constant 路径一致。Task 04 把 MaterialInstance 覆盖打包成 push-constant bytes 时，按 Material.uniforms 列表的顺序 + std430 对齐就行——schema 已经提前对齐好。
  - **dFdx / dFdy 推 face normal**：MeshAsset 当前没 vertex normal 字段（PHASE 2 范围只有 pos+uv+indices）；toon / rim_light 都用 `cross(dFdx(vWorldPos), dFdy(vWorldPos))` 在 fragment 阶段推 flat face normal——视觉上够用，且不需要在 Task 02 顺手扩 MeshAsset schema。Phase 3 后续 task 引入 vertex normal 时把 shader 切到 attribute-driven normal 即可，uniform 布局不变。
  - **shader 走 AssetRegistry，不 fopen**：与 extension-points "shader 文件路径走 Asset 系统加载" 约定一致。BuiltinMaterials::LoadXxx 假定调用方已 `RegisterLoader<ShaderAsset>`——这与"内置 loader 注册不在 AssetRegistry 构造时自动完成"（Phase 2 / Task 02 写明）的契约对齐；test 在 setup 阶段显式 RegisterLoader。**没在 BuiltinMaterials 里 RegisterLoader-if-missing**：那会给 AssetRegistry 加新查询面（HasLoader<T>），Task 02 不做这层 API 扩展。
  - **path resolution 复用 .exe-相对方案**：与 Pipeline.cpp 的 GetExecutableDir 同思路（GetModuleFileNameW），让 .spv 不论 CWD 如何都从 .exe 同目录的 `shaders/orange_engine/<name>.spv` 起解析。当前直接复制一份 helper 到 BuiltinMaterials.cpp——单独提到 Platform 层是后续 task 的事，未到"3+ 处复用"的阈值不动。
  - **Material 字段填全**：`name`（"toon" / "rim_light"）、`vertexShader` / `fragmentShader` 两个 ShaderAsset handle、`uniforms` 列表（按 push-constant 出现顺序）、`textureSlots` 留空（两个内置 shader 当前都是程序式着色，不采样贴图——等 Phase 3 后续 task 把贴图真接上时再填）。
  - **不动 RenderableComponent / Pipeline / RenderScene**：Task 02 仅在 Render 模块里增项；不破任何已有 ECS / 渲染路径，所有现存 ctest 与 sample 行为不变。
- 验证方式：`tests/render/BuiltinMaterialsTest.cpp` 通过 ctest，覆盖 4 条路径：
  1. LoadToon 返回 Material：name="toon"、两个 shader handle 有效、uniforms 非空且类型布局符合预期、textureSlots 为空；
  2. LoadRimLight 同上，uniforms 集合不同（验证两个模板 schema 不串）；
  3. 重复调用 LoadToon dedup 命中（同一 path → 同一 handle，registry 不重新跑 loader）；
  4. MaterialInstance 绑 toon 模板时只能 SetUniform 在 toon 的 uniform 名上（rim_light 的 uniform 名 `uRimColor` 在 toon instance 上是 no-op）——验证 Task 01 的 silent-ignore 与 Task 02 的 schema 真实分离。
- 验收标准：cmake build 通过（4 个新 SPIR-V 编出）+ 11/11 ctest 全过（之前 10 + builtin_materials_test 新增）+ install_smoke / config_smoke 不退化 + 现有 03/04 sample 行为不变。Material 模块的"内置模板"侧就绪——Phase 3 / Task 03（PostProcessChain）、Task 04（自定义 shader 注入）可以并行/顺序推进；Phase 3 / Task 06 升级 sample 时直接消费 BuiltinMaterials::LoadToon。
- Critical Path：是

#### Task 03：PostProcessChain 接口与默认链（HDR → Bloom → Tonemap → LUT） ✅
- 描述：交付 PostProcessChain 模块的"用户面"——`IPostProcessPass` 抽象基类（plugin 风格，与 extension-points 写明的 IRenderPass 路径对齐）、`PostProcessChain` 容器（CRUD + 顺序遍历）、`BuiltinPostProcessChain::CreateDefault()` 工厂返回 4-pass 链（HDR → Bloom → Tonemap → LUT）。**Pipeline 真跑链 / GLSL shader / HDR off-screen target / descriptor-set 接通全部延后**——Task 03 与 Task 01/02 同节奏，仅交付"接口 + 描述符"，Pipeline 整合走 Task 04 起的渐进路径，HDR / 实际 pass shader 在 Task 06 sample 升级时再补。
- 输入：Phase 3 / Task 02（BuiltinMaterials 模板侧已稳定）
- 输出：
  - `Proposed: include/orange/engine/render/IPostProcessPass.h`（`IPostProcessPass` 抽象类：`Name()` / `Setup(PostProcessSetupContext&)` / `Execute(PostProcessExecuteContext&)`；两个 context 当前是空 struct，Phase 5 RenderGraph 接通时补字段，签名不破）
  - `Proposed: include/orange/engine/render/PostProcessChain.h`（`PostProcessChain`：`AddPass` / `RemoveAt` / `Clear` / `PassCount` / `PassAt` / `FindByName`；走 `std::vector<std::unique_ptr<IPostProcessPass>>` 顺序存储——后处理链是顺序敏感的，map 不是合适的存储）
  - `Proposed: include/orange/engine/render/PostProcessPasses.h`（4 个内置 pass 具体类：`HdrPass`（无参）/ `BloomPass`（threshold + intensity）/ `TonemapPass`（exposure）/ `LutPass`（lut handle + strength）。每类继承 IPostProcessPass，方法体在 .cpp 给空 stub）
  - `Proposed: include/orange/engine/render/BuiltinPostProcessChain.h`（namespace `BuiltinPostProcessChain::CreateDefault()` 返回按 HDR→Bloom→Tonemap→LUT 顺序填好的 PostProcessChain）
  - `Proposed: src/render/PostProcessChain.cpp`（容器 CRUD 实现）
  - `Proposed: src/render/PostProcessPasses.cpp`（4 个 pass 的 Name() / Setup / Execute，Setup/Execute 当前是空 stub）
  - `Proposed: src/render/BuiltinPostProcessChain.cpp`（4 行 `chain.AddPass(make_unique<XxxPass>())`）
  - `Modified: src/render/RenderHeaderCheck.cpp`（追加新 4 个公共头）
  - `Modified: CMakeLists.txt`（orange_engine 源列表加 3 个 .cpp）
  - `Proposed: tests/render/PostProcessChainTest.cpp`
  - `Modified: tests/CMakeLists.txt`（注册 postprocess_chain_test）
- 影响路径/模块：Render（仅新增 PostProcess 子面）、tests、构建系统
- 前置依赖：Task 02
- 实现要点：
  - **D1 = 接口 + 默认链描述符（不接 Pipeline、不写 GLSL、不接 OrangeRender 新路径）**：用户决策。理由——OrangeRender 的 BeginFrame/SubmitItem 路径目前只挂 swapchain color attachment，要让 PostProcessChain 真跑得引擎层先接全套 render-graph 边带（HDR off-screen target + ping-pong + descriptor set），一个 task 装不下。把"接口可用 + 默认链描述就位"先抽出来，下游 Pipeline / Sample 集成 task 各自有清晰边界。
  - **D2 = plugin 风格（IPostProcessPass 抽象基类 + Setup/Execute）**：用户决策。与 docs/extension-points.md "RenderPass 注入" 的 IRenderPass 抽象一致——后处理本质是渲染 pass 的子集，复用同一个心智模型。data-driven 描述符风格虽然序列化友好，但默认链所有 pass 都是引擎自家提供的具体类，不需要先经过 desc → impl 的 dispatch。等到 Phase 5 序列化场景配置时再考虑加 desc 层。
  - **D3 = CreateDefault 给可改的链（不锁死开关）**：用户决策。`BuiltinPostProcessChain::CreateDefault()` 返回完整 4-pass chain，调用方可以 `chain.RemoveAt` / `chain.AddPass` / `chain.PassAt(i)->As<BloomPass>()` 自由调整——不在 chain / pass 上加 enable bool flag，"是否跑某 pass" 由是否在 chain 里决定。这条与 ECS 把 component-storage 当 single source of truth 的风格一致，避免 `enable=false` 与 "remove 掉" 两种禁用方式并存导致语义模糊。
  - **Setup / Execute 空 context 占位**：当前两个 context 是空 struct，签名却已经定下。Phase 5 RenderGraph 接通时，给 SetupContext 加 `RenderGraphBuilder& builder`、给 ExecuteContext 加 `Orange::Renderer::IRenderer& renderer`、当前帧 uniform / 输入输出 texture view 等字段。子类实现签名不变，新字段只是给具体 pass impl 补可读取的输入——这层稳定性是 Task 03 选 plugin 风格的核心收益。
  - **PassAt 返回 IPostProcessPass\***：用户取得后用 dynamic_cast 落到具体类（`PassAt(1) -> dynamic_cast<BloomPass*>`）；不在 IPostProcessPass 上加 As<T> 模板 helper（那会绑死 RTTI 风格 + 增加 surface area）。0.x 阶段 dynamic_cast 是可接受的成本。
  - **HdrPass 当前是空 marker**：HDR 真正生效需要 Pipeline 在 SubmitItem 路径上额外开一张 RGBA16F off-screen color target；那是后续 task 的事。本 task 把 HdrPass 留作 chain 头节点的 marker——Phase 3 / Task 06 真接 Pipeline 时识别 chain[0] 是 HdrPass 就切到 HDR 渲染目标。
  - **LutPass 持 `AssetHandle<TextureAsset>`**：与 RenderableComponent 的 texture handle 同模式，未来切到真正的 3D LUT texture（17×17×17 unrolled to 2D atlas，Phase 3 / Task 06 与 sample 一并接入）时不破公共面。
  - **不动 Pipeline / RenderableComponent / sample**：所有现存 ctest 与 sample 行为不变。
- 验证方式：`tests/render/PostProcessChainTest.cpp` 通过 ctest，覆盖 5 条路径：
  1. `PostProcessChain` 默认空 + AddPass / PassCount 行为；
  2. `RemoveAt` / `Clear` 后 PassCount 归零、原指针失效；
  3. `FindByName` 命中 + 不命中 → nullptr；
  4. `BuiltinPostProcessChain::CreateDefault()` 返回 4-pass chain，顺序为 hdr / bloom / tonemap / lut；每个 pass 的 Name() 与具体类型（dynamic_cast）匹配；
  5. 取出 `BloomPass*` / `TonemapPass*` 后修改其 public 参数（threshold / intensity / exposure）能持久——验证 D3 的"可改链"承诺。
- 验收标准：cmake build 通过 + 12/12 ctest 全过（之前 11 + postprocess_chain_test 新增）+ install_smoke / config_smoke 不退化 + 现有 03/04 sample 行为不变。后处理模块的公共表面就绪——Task 04 起的自定义 shader 注入、Task 05 起的软阴影、Task 06 起的 sample 升级都可以引用这个 chain。
- Critical Path：是

#### Task 04：MaterialSystem 自定义 shader 注入接口 ✅
- 描述：交付 `MaterialSystem`——把 Material 模板的注册 / 查询 / 实例化集中到一个值构造的容器类。`ShaderTemplateDesc` 描述（name + 顶点/片段 SPIR-V 路径 + uniform 列表 + 纹理槽列表）；`RegisterTemplate` 通过 AssetRegistry 加载 SPIR-V 落到 ShaderAsset handle 并把 Material 存进系统内表；`FindTemplate` 按名取 `const Material*`；`CreateInstance` 返回 `unique_ptr<MaterialInstance>` 绑到对应模板。**Pipeline 真按 MaterialInstance 路由 push-constant / 描述符是 Task 06 sample 升级的工作**——Task 04 与 Task 01/02/03 同节奏，仅交付"接口可用 + 注册路径打通"。
- 输入：Phase 3 / Task 02（BuiltinMaterials 已稳定，可作为内置 template 注册路径示例）+ Task 03（PostProcessChain 公共面）
- 输出：
  - `Proposed: include/orange/engine/render/MaterialSystem.h`（`struct ShaderTemplateDesc { name, vertexSpirvPath, fragmentSpirvPath, uniforms, textureSlots }`；`class MaterialSystem`：`explicit MaterialSystem(Asset::AssetRegistry&)` / `Result<void, ResultCode> RegisterTemplate(const ShaderTemplateDesc&)` / `const Material* FindTemplate(std::string_view) const` / `std::unique_ptr<MaterialInstance> CreateInstance(std::string_view)` / `std::size_t TemplateCount() const` / `Result<void, ResultCode> RegisterBuiltins()`）
  - `Proposed: src/render/MaterialSystem.cpp`（PIMPL：`std::unordered_map<std::string, Material>` + `Asset::AssetRegistry&` 引用；RegisterTemplate 走 `registry.Load<ShaderAsset>` 加载 SPIR-V → 构造 Material → 存入 map）
  - `Modified: src/render/RenderHeaderCheck.cpp`（追加 MaterialSystem.h）
  - `Modified: CMakeLists.txt`（orange_engine 源列表加 src/render/MaterialSystem.cpp）
  - `Modified: docs/extension-points.md`（更新示例：去掉 `app.GetMaterialSystem()` 形式，改为 `MaterialSystem matSys(registry); matSys.RegisterTemplate(desc);`）
  - `Proposed: tests/render/MaterialSystemTest.cpp`
  - `Modified: tests/CMakeLists.txt`（注册 material_system_test）
- 影响路径/模块：Render（仅新增 MaterialSystem 子面）、tests、构建系统、docs/extension-points
- 前置依赖：Task 03
- 实现要点：
  - **D1 = ownership 走实例化模式（值类型，非 AppHost-managed、非 singleton）**：用户决策。MaterialSystem 由调用方自己持有（典型场景：sample / 游戏代码在 main 里建一个、给 Pipeline 借引用），与 PostProcessChain 同节奏；理由——0.x 阶段不强制 AppHost 拥抱所有子系统，需要时升级到 `app.GetMaterialSystem()` 不破公共面，且实例化模式对单元测试更友好。docs/extension-points.md 同步去掉旧的 `app.GetMaterialSystem()` 示例。
  - **D2 = shader 输入走 SPIR-V 路径，不接运行时 GLSL 编译**：用户决策。`ShaderTemplateDesc` 的 `vertexSpirvPath` / `fragmentSpirvPath` 是 .spv 文件路径，与内置 toon/rim_light 同走 `AssetRegistry::Load<ShaderAsset>`；游戏侧自己用 glslangValidator 编 .spv 喂给 RegisterTemplate。运行时编译 / hot-reload 留给 Phase 6。
  - **D3 = 不接 Pipeline，与 Task 03 同节奏**：用户决策。Task 04 仅"建表"，Pipeline 仍走 Phase 2 / Task 07 的硬编码 push-constant 路径不动；Task 06 sample 升级时把 Pipeline 接通 MaterialInstance 路由——RenderableComponent 的当前 texture 槽与硬编码 shader 在 Task 06 一并替换为 MaterialInstance 引用。Task 04 不动 RenderableComponent / Pipeline / RenderScene，所有现存 ctest / sample 行为不变。
  - **持 `AssetRegistry&` 引用而非每次传参**：MaterialSystem 构造时绑定 registry 引用并存为成员；调用方负责让 registry 活到 system 析构。这与 Pipeline::Initialize 持有的 registry 引用同模式——子系统需要长期访问 AssetRegistry 时就在构造时绑定，避免每个 RegisterTemplate 调用都重新传参。
  - **RegisterTemplate 失败语义**：返回 `Result<void, ResultCode>`；name 重复 → AlreadyExists；SPIR-V 加载失败 → 仍把 template 落地（Material 字段填全、shader handle 用无效 handle，与 BuiltinMaterials::LoadToon 同语义）但返回错误码，调用方可以 ignore。这与 BuiltinMaterials 的"加载失败也填 schema"策略对齐——0.x 阶段允许"半残 template"作为可观测中间态。
  - **`RegisterBuiltins()` helper**：单独一个方法内部调用 `BuiltinMaterials::LoadToon` / `LoadRimLight` 把两个内置 template 注册进 system，避免每个 sample / test 重复"建 system + 注册内置"样板。Task 02 的 BuiltinMaterials 函数仍独立可用，RegisterBuiltins 只是 system 上的便利包装。
  - **CreateInstance 返回 `std::unique_ptr<MaterialInstance>`**：MaterialInstance 已 Task 01 走 PIMPL move-only；用 unique_ptr 让调用方持有所有权，绑定的 `const Material*` 由 system 表保活——只要 system 活着，所有由它创建的 instance 都安全。Material 在 unordered_map 里地址稳定（节点存储、不像 vector 因 rehash 失效），新 RegisterTemplate 不会让旧引用失效。
  - **MaterialSystem 走 PIMPL**：`unordered_map<string, Material>` 在公共头里会传染 std::unordered_map / std::string 的复合类型；与 MaterialInstance 同思路把存储藏到 .cpp。公共头继续不漏 OrangeRender / Vulkan 类型。
- 验证方式：`tests/render/MaterialSystemTest.cpp` 通过 ctest，覆盖 6 条路径：
  1. 默认 MaterialSystem 构造后 TemplateCount == 0、FindTemplate("toon") == nullptr；
  2. RegisterBuiltins() 后 TemplateCount == 2、FindTemplate("toon") / FindTemplate("rim_light") 都命中且 name 字段对；
  3. 自定义 ShaderTemplateDesc 注册（用内置 toon.spv 路径作为占位 shader、自取一个新 name "test_template" + 若干 uniform）→ FindTemplate 命中、uniforms 数量与 desc 一致；
  4. 重复 RegisterTemplate 同名返回 AlreadyExists、表内 template 不被覆盖；
  5. CreateInstance("toon") 返回非 nullptr，绑定的 `Material->name == "toon"`；CreateInstance("nonexistent") 返回 nullptr；
  6. 通过 CreateInstance 拿到的 MaterialInstance 上 SetUniform 命中 toon 的 uniform 名（验证 Task 01 silent-ignore 与 Task 04 system-managed Material 引用真正贯通）。
- 验收标准：cmake build 通过 + 13/13 ctest 全过（之前 12 + material_system_test 新增）+ install_smoke / config_smoke 不退化 + 现有 03/04 sample 行为不变 + docs/extension-points.md 示例已同步至实例化模式。Material 模块的"注入扩展点"侧就绪——Task 06 升级 sample 时直接消费 MaterialSystem 走 MaterialInstance 路由。
- Critical Path：是

#### Task 05：软阴影 LightComponent + ShadowConfig + 着色侧产物 ✅
- 描述：交付软阴影的"公共面 + 着色侧"——`DirectionalLight` ECS component（方向 + 颜色 + 强度 + 是否投影）+ `ShadowConfig`（map 分辨率 / PCF kernel / depth bias / normal bias）+ depth-only `shadow_caster` shader pair + 通用 PCF GLSL include 头（可被 toon/rim_light/自定义 shader 共用）+ `BuiltinShadowShaders::LoadShadowCaster` 工厂。**Pipeline 真接通 shadow pass / shadow map 资源管理 / 主 pass 采样 shadow map 推到 Task 07**（与 multi-entity demo 一并接）——Task 05 与 Task 04 同节奏，仅交付"数据 / 着色侧 + 单元测试"。
- 输入：Phase 3 / Task 04（MaterialSystem 已可注册 template）
- 输出：
  - `Proposed: include/orange/engine/render/LightComponent.h`（`struct DirectionalLight { glm::vec3 direction; glm::vec3 color; float intensity; bool castsShadow; }` —— ECS component；运行时 light view / proj 由 Pipeline 在 ShadowPass 实时计算，不存进 component）
  - `Proposed: include/orange/engine/render/ShadowConfig.h`（`struct ShadowConfig { uint32_t mapResolution = 1024; uint32_t pcfKernelRadius = 1; float depthBias = 0.005f; float normalBias = 0.01f; }` —— per-pipeline 全局配置，Task 07 接 Pipeline 时由 `Pipeline::SetShadowConfig` 消费）
  - `Proposed: src/render/builtin_shaders/shadow_caster.vert.glsl`（顶点 layout = pos；push-constant block = uLightViewProj(64) + uModel(64)；输出 gl_Position）
  - `Proposed: src/render/builtin_shaders/shadow_caster.frag.glsl`（空 main —— 只写 depth attachment，Vulkan 默认 GL_LESS 接管）
  - `Proposed: src/render/builtin_shaders/include/shadow_pcf.glsl.inc`（`float SamplePcfShadow(sampler2D shadowMap, vec3 worldPos, mat4 lightViewProj, int kernelRadius, float depthBias)` —— 3x3 / 5x5 box filter PCF；调用方用 `#include "include/shadow_pcf.glsl.inc"` 拉进自己 fragment shader）
  - `Proposed: include/orange/engine/render/BuiltinShadowShaders.h`（`namespace BuiltinShadowShaders`：`struct ShaderPair { Asset::AssetHandle<Asset::ShaderAsset> vertex; Asset::AssetHandle<Asset::ShaderAsset> fragment; };` + `ShaderPair LoadShadowCaster(Asset::AssetRegistry&)`）
  - `Proposed: src/render/BuiltinShadowShaders.cpp`（与 BuiltinMaterials.cpp 同模式：.exe-相对路径 + AssetRegistry dedup）
  - `Modified: CMakeLists.txt`（追加 2 个 `orange_engine_compile_builtin_shader` 调用：shadow_caster.vert / shadow_caster.frag；shadow_pcf.glsl.inc 不直接编 SPIR-V，由 glslangValidator 通过 `-I src/render/builtin_shaders` include path 拉进来）
  - `Modified: src/render/RenderHeaderCheck.cpp`（追加 LightComponent.h / ShadowConfig.h / BuiltinShadowShaders.h）
  - `Modified: CMakeLists.txt`（orange_engine 源列表加 BuiltinShadowShaders.cpp）
  - `Proposed: tests/render/LightAndShadowTest.cpp`
  - `Modified: tests/CMakeLists.txt`（注册 light_and_shadow_test）
- 影响路径/模块：Render（新增 light/shadow 子面）、tests、构建系统
- 前置依赖：Task 04
- 实现要点：
  - **D4 = 不接 Pipeline，与 Task 04 同节奏**：用户决策。Pipeline 真跑 shadow pass = 第一遍 depth-only 用 light view/proj 渲场景到 shadow map → 主 pass 把 shadow map 作描述符喂给 fragment shader；这两件事放到 Task 07 一起接通（届时 Pipeline 还会顺带加 shadow render target / sampler / descriptor-set 一整套）。Task 05 不动 Pipeline.cpp / RenderScene.cpp。
  - **算法 = PCF（Phase 3 核心任务列表既定）**：选 PCF 而非 VSM——PCF 实现简单 / 与 Vulkan depth sampler 自然对齐 / 不需要单独的 moment-computation pass / 与卡通照明的"硬边阴影 + 微羽化"美术目标契合。VSM 留给 Phase 6 视觉调优时再评估。
  - **DirectionalLight 不存 light view / light proj 矩阵**：light view 由 light direction + scene bounding box 在 Pipeline shadow pass 实时算，不是 ECS data；component 只存方向 + 颜色 + 强度 + 是否投影——ECS schema 紧凑、避免 player 修改 component 字段时把 view 矩阵设错。
  - **不修改 toon / rim_light frag shader**：让 sample 03/04 行为完全不变是 Task 05 的硬约束——一旦 frag 引入 light UBO / shadow map 描述符，Pipeline 不绑就是 undefined behavior（validation layer 会爆）。toon / rim_light 接 light + shadow 的 shader 改动放到 Task 07，与 Pipeline 接通描述符同 patch。
  - **shadow_pcf.glsl.inc 是 #include 头，不是独立 ShaderAsset**：glslangValidator 在编译 .frag.glsl 时通过 `-I src/render/builtin_shaders` include path 拉进来；与"shader 走 AssetRegistry 加载，不裸 fopen"约束不冲突——所有展开都在编译期、运行时仍然只加载完整 SPIR-V。
  - **shadow_caster push-constant 仅放 uLightViewProj + uModel**：Vulkan 最低 128 B 容得下两个 mat4 = 128 B 整好，不需要 UBO。Pipeline 在 Task 07 接通 shadow pass 时按 per-draw 设置 uModel、按 per-frame 设置 uLightViewProj——shadow_caster 不需要描述符集，最朴素的 depth-only pass。
  - **shadow_caster.frag 是空 main**：Vulkan 允许 fragment shader 完全省略，但留一个空 main 让 SPIR-V pipeline 默认行为可预测（不依赖 driver fallback）；深度写由 depth attachment 默认接管。
  - **ShadowConfig 是值结构体**：与 PostProcessChain 默认参数同思路，开箱即用 default 合理（1024 阴影图 + 3x3 PCF + 0.005 depth bias 是卡通/PCG 类游戏的常见基线）。Task 07 `Pipeline::SetShadowConfig` 消费时直接 by-value 拷贝。
  - **BuiltinShadowShaders 与 BuiltinMaterials 平行**：单独一个 namespace 而不是塞进 BuiltinMaterials——shadow_caster 不是 Material（没 uniform 描述符 / 没 texture 槽，是 Pipeline-managed shader pair），强行套进 Material 会让 Material 的语义模糊。Task 07 Pipeline 接 shadow pass 时直接拿 ShaderPair 的 handle 不经过 MaterialSystem。
- 验证方式：`tests/render/LightAndShadowTest.cpp` 通过 ctest，覆盖 4 条路径：
  1. DirectionalLight 默认构造字段合理（方向是单位向量 / 颜色白 / 强度 1 / castsShadow false）；
  2. ShadowConfig 默认值合理（1024 / 1 / 0.005 / 0.01）+ 字段独立可改且不串；
  3. World 里挂一个带 DirectionalLight 的 entity → 通过 `world.View<DirectionalLight>()`（或等价 EnTT 查询）能查询出来，验证 component 类型可用作 ECS component；
  4. `BuiltinShadowShaders::LoadShadowCaster` 返回的 vertex / fragment handle 都有效；重复调用同一 registry 上 dedup 命中（与 BuiltinMaterials 同语义）。
- 验收标准：cmake build 通过（toon/rim_light/shadow_caster 共 6 个 SPIR-V 编出，shadow_pcf.glsl.inc 通过 include path 解析）+ 14/14 ctest 全过（之前 13 + light_and_shadow_test 新增）+ install_smoke / config_smoke 不退化 + 现有 03/04 sample 行为完全不变（toon/rim_light frag 没碰）。Light + Shadow 模块的着色侧就绪——Task 07 Pipeline 接通时引用这套 component / shader / config 即可。
- Critical Path：是

#### Task 06：升级 `samples/04_3d_mesh_with_bloom`（Pipeline 接通 MaterialInstance + PostProcessChain 真路径） ✅

> **阻塞 trail（已全部解除）**：
> - **2026-05-07 第一次阻塞登记**：触达"渲染到 off-screen RGBA16F → 在后续 pass 里采样它"全链路，依赖 OrangeRender 公共 RHI 缺失的 B2/B3/B4。登记 `FEATURE-2026-05-07-rhi-render-pass-and-bindings`，✅ 落地（B4 + B5 部分扩枚举 + `RHITexture::GetDefaultView()`；B2/B3 经评审确认 Phase 6 早已闭环）。
> - **2026-05-07 第二次阻塞登记**：第一条解除后复盘发现 tonemap → swap-chain 还差一脚——`RenderItem` 不带 descriptor-set、`RecordRenderItems` 不发 `SetDescriptorSet`。登记 `FEATURE-2026-05-07-renderitem-descriptor-sets`，✅ 落地（`RenderItem.mpDescriptorSets[4] + mDescriptorSetCount` + `RecordRenderItems` 自动发 + dedup + 单测 + sample `--via-renderer` 双栈 + `api_guide.md` §6.6 追加）。
> - **当前**：B2 + B3 + B4 + RenderItem.descriptorSets 四件套齐全；OrangeEngine 走 windowed `IRenderer` 路径有 `mpNativeWindowHandle` 可用，T3 风险确认里"headless 走不通 SubmitItem"的限制对引擎不构成阻塞。
>
> **拆解决策**（2026-05-07）：本 task 工作量大（HDR target / 6+6 bloom mip / 8 个新 SPIR-V / MaterialInstance 路由 / per-template Pipeline 缓存 / `RenderableComponent` schema 迁移 / sample 03/04 迁移 / 新 sample），按 5 子任务串行交付——见下方"子任务拆解"段。每条独立 commit / PR、各自有验收门槛。

- 描述：把 Task 03（PostProcessChain 接口）+ Task 04（MaterialSystem 注册）+ Task 05（Light/Shadow 公共面 + BuiltinShadowShaders）的 stub 真正跑起来——Pipeline 加 HDR off-screen color target（RGBA16F）+ bloom mip-chain（downsample 6 levels + upsample 6 levels + composite）+ tonemap fullscreen pass + LUT pass（handle 无效时 no-op bypass）；Pipeline 按 RenderableComponent 上的 MaterialInstance 反查 Material → 编 RHI Pipeline state → 按 instance 覆盖打 push-constant；RenderableComponent 的 `materialInstance`（替代当前的 hardcoded texture handle）作为新 ECS schema。新增 sample `04_3d_mesh_with_bloom` 用 `BuiltinMaterials::LoadToon` + `BuiltinPostProcessChain::CreateDefault`，演示一个旋转立方体在 HDR 卡通照明 + bloom + tonemap 下的视觉。**Shadow pass 不在本 task 范围**——见 Task 07。
- 前置依赖：Task 03 / 04 / 05；vendor/OrangeRender 已 bump 到含 `FEATURE-2026-05-07-rhi-render-pass-and-bindings` + `FEATURE-2026-05-07-renderitem-descriptor-sets` 的 commit。
- 影响路径/模块：Render（重写绘制路径，跨 5 子任务 + 双段 frame 流程）、samples、构建系统。
- 设计决策（跨子任务通用）：
  - **MaterialInstance 路由 = per-template Pipeline 缓存**：`unordered_map<const Material*, RhiPipeline>`，第一次见到某 Material 时按其 `vertexShader / fragmentShader / uniforms` 编 `GraphicsPipelineDesc`、之后切 instance 不重编。与 extension-points "每个 MaterialTemplate 编译为一个 RHI Pipeline" 约定一致。
  - **RenderableComponent 持非拥有 `MaterialInstance*`**：instance 由 sample / 游戏代码持有（典型：`std::vector<std::unique_ptr<MaterialInstance>>` 在 main 里管），component 仍是 trivially-copyable 的小数据；理由 = MaterialInstance PIMPL move-only，嵌 EnTT archetype 触发整行迁移。生命周期约束：析构 instance 前先析构 World，加文档不强制。
  - **HDR target 选 RGBA16F**：主 pass 写 off-screen RGBA16F；bloom chain mip 也 RGBA16F；tonemap 读 RGBA16F → ACES → 写 swap-chain LDR。
  - **bloom 资源 = 6 张独立 `RHITexture`，不用 sub-resource view**：Karis 风格 mip-chain 每跳要求 "写 mip[N] 时绑 mip[N] 为 RT、读 mip[N-1] 时把它喂为 sampled view"。当前 `RHITexture` 公共面只有 `GetDefaultView()`，没 mip view 工厂；本 task 用 6 张独立 RHITexture（mip0=1/2 … mip5=1/64）绕过——比 sub-resource view 浪费 ~33% 内存，但避免向 OrangeRender 提第三轮需求；Phase 6 性能调优时再换。
  - **bloom 算法 = COD AW 风格 mip-chain**：6 levels downsample + 6 levels upsample，Karis 平均防 fireflies、tent filter 上采，`BloomPass.threshold / intensity` 按 push-constant 喂。
  - **tonemap 算子 = ACES Narkowicz fit**：单步 ALU 算子（5 行 GLSL），不需要 LUT。`LutPass` 真正色彩分级（3D LUT 17×17×17 unrolled）等到 Phase 6 资产管线就绪再接，本 task 仅做 "handle 无效 → bypass" 的 stub。
  - **双段 frame 流程**：Pipeline.Render 改为 ① **离屏段**（自管 cmd list）：`device.CreateCommandList(Graphics)` → transition + `BeginRendering(HDR/bloom mips)` + drawables/fullscreen quad + `EndRendering` + transition `ShaderReadOnly` → `device.SubmitCommandList` → `device.WaitIdle()` 兜底（Phase 6 切 timeline semaphore）；② **swap-chain 段**（`IRenderer`）：`renderer.BeginFrame` → `SubmitItem(fullscreen tonemap quad with mpDescriptorSets[0]=HDR+bloom CIS set)` → `renderer.EndFrame`。
  - **`Pipeline::SetPostProcessChain / SetMaterialSystem` 是非拥有指针**：Pipeline 不接管生命周期；空 chain / 空 system 走 fallback 路径（passthrough / hardcoded），保证 sample 03/04 在每个子任务节点都能跑。
  - **Shadow pass 不在本 task 范围**：D4 决策推给 Task 07，本 task 不接。

##### 子任务拆解

每条以 `Task 06.0X` 命名，commit / PR title 引用此编号；按 01 → 02 → 03 → 04 → 05 严格串行（每条改了下条要用的状态）。每条都有独立验证手段、blast radius 受控；中间状态保证 sample 03/04 不退化，是跨子任务的硬约束。

###### Task 06.01 · 数据 schema：MaterialInstance 在 ECS 上贯通 ✅
- 输出：
  - `Modified: include/orange/engine/render/BuiltinMaterials.h` + `src/render/BuiltinMaterials.cpp`：新增 `BuiltinMaterials::LoadTextured`，复用现 `textured_mesh.vert/frag`，`uniforms` = uMVP push-constant，`textureSlots` = binding 0 SampledImage + 可选 binding 1 Sampler。
  - `Modified: include/orange/engine/render/MaterialSystem.h` + `src/render/MaterialSystem.cpp`：`RegisterBuiltins` 同步注册 `textured` template，与 toon / rim_light 并列。
  - `Modified: include/orange/engine/render/MaterialInstance.h` + `src/render/MaterialInstance.cpp`：增读回 API（`std::optional<glm::vec4> GetUniformVec4(name) const` / `AssetHandle<TextureAsset> GetTextureBinding(binding) const noexcept` 等），只暴露 Pipeline 路由要消费的类型，不把 `std::variant` 传染到公共面。
  - `Modified: include/orange/engine/render/RenderableComponent.h`：`texture` 字段移除；新增 `Render::MaterialInstance* materialInstance{nullptr}`（非拥有）；保留 `mesh` + `visible`。
  - `Modified: include/orange/engine/render/RenderScene.h` + `src/render/RenderScene.cpp`：`Drawable` 加 `Render::MaterialInstance* materialInstance{nullptr}`；`Collect` 同步带过去。
  - `Modified: samples/03_textured_quad/main.cpp` + `samples/04_3d_mesh/main.cpp`：迁移到 `MaterialSystem(registry).RegisterBuiltins() + matSys.CreateInstance("textured")`；程序式生成 checker `TextureAsset` → `instance->SetTexture(0, handle)`；`world.AddComponent(e, RenderableComponent{mesh, instance.get()})`。
  - `Modified: src/render/Pipeline.cpp`：路径接通——`drawable.materialInstance == nullptr` 时仍走原 hardcoded textured pipeline；非 null 时 Pipeline 内部本子任务仍走 hardcoded textured pipeline、不切 per-template 路径，保证视觉等价（per-template 路径下条 task 切）。
- 验证方式：
  1. cmake build 通过；ctest 不退化；install_smoke / config_smoke 不退化；
  2. `03_textured_quad.exe` / `04_3d_mesh.exe` 视觉与本 task 之前等价；
  3. 新增最小单测：`tests/render/MaterialSchemaTest.cpp` 覆盖 `BuiltinMaterials::LoadTextured` + `MaterialInstance::GetUniformVec4 / GetTextureBinding` 读回正确性。
- Critical Path：是

###### Task 06.02 · Pipeline 路由：per-template `RhiPipeline` 缓存 ✅
- 输出：
  - `Modified: src/render/Pipeline.cpp`：核心改动——`std::unordered_map<const Material*, std::unique_ptr<Rhi::RHIPipeline>> mTemplatePipelines`；helper `GetOrCompilePipeline(const Material&)` 按 Material shader handle + uniform 布局组装 `GraphicsPipelineDesc`；drawable 提交按 `drawable.materialInstance->GetMaterial()` 路由；删除原 hardcoded textured pipeline + 内联 SPIR-V 加载——textured template 编出的 RhiPipeline 直接接管 sample 03/04。
  - `Modified: src/render/Pipeline.cpp`：fallback——`drawable.materialInstance == nullptr` 时按 "无 instance 视为 textured 默认 instance" 走，从 MaterialSystem 拿 textured template 编出的 pipeline；MaterialSystem 未挂时 Pipeline 自己懒加载内置 textured，避免 sample 必须挂 MaterialSystem。
- 验证方式：
  1. cmake build 通过；ctest 不退化；
  2. `03_textured_quad.exe` / `04_3d_mesh.exe` 视觉与 06.01 完成态等价（textured template pipeline 替代 hardcoded pipeline 后字节级一致）；
  3. 新增 ctest：`tests/render/PipelineTemplateCacheTest.cpp` 覆盖 "同 Material 多 instance 只编一次 pipeline" + "不同 Material 编出独立 pipeline"。
- Critical Path：是

###### Task 06.03 · 双段 frame 流程：HDR off-screen + passthrough 收尾 ✅
- 输出：
  - `Proposed: src/render/builtin_shaders/fullscreen.vert.glsl` + `passthrough.frag.glsl`：big-triangle 全屏覆盖 + 采样 HDR view 写 swap-chain（无 tonemap）。
  - `Modified: include/orange/engine/render/Pipeline.h`：追加 `void SetPostProcessChain(PostProcessChain*)` + `void SetMaterialSystem(MaterialSystem*)`（非拥有指针）。
  - `Modified: src/render/Pipeline.cpp`：创建 / 销毁 / `OnResize` 重建 RGBA16F HDR target；`Render` 改双段——自管 cmd list 跑离屏主 pass + `SubmitCommandList` + `WaitIdle`；`renderer.BeginFrame` + `SubmitItem(passthrough fullscreen quad, mpDescriptorSets[0]=HDR-CIS-set)` + `renderer.EndFrame`。
  - `Modified: include/orange/engine/render/IPostProcessPass.h`：`PostProcessSetupContext` 升级字段（`Renderer::IRenderer& renderer / Rhi::RHIDevice& device / Rhi::RHITextureView* hdrColor / Rhi::RHITextureView* swapchainColor`）；`PostProcessExecuteContext` 升级（同上 + `Rhi::RHICommandList& offscreenCmd / uint32_t frameIndex / Rhi::RHIDescriptorPool& sharedPool`）。子类 `Setup/Execute` 签名不破。
  - `Modified: src/render/PostProcessPasses.cpp`：HdrPass 仍空 marker；其余 3 pass 仍空 stub——Bloom / Tonemap / LUT 真填内容由 06.04 / 06.05 上线。
- 验证方式：
  1. cmake build 通过（新增 2 个 SPIR-V：fullscreen.vert + passthrough.frag）；ctest 不退化；
  2. `03_textured_quad.exe` / `04_3d_mesh.exe` 视觉与 06.02 完成态等价（HDR 中转无损 passthrough，clamp 在 [0,1] 时字节级一致）；
  3. 新增 ctest：`tests/render/PipelineHdrTargetTest.cpp` 覆盖 HDR target 生命周期 + OnResize 重建；validation layer 静默。
- Critical Path：是

###### Task 06.04 · Bloom mip-chain（downsample + upsample） ✅
- 输出：
  - `Proposed: src/render/builtin_shaders/bloom_downsample.vert.glsl` + `bloom_downsample.frag.glsl`：fullscreen + 13-tap 采样 + Karis 平均；mip0 跳应用 `BloomPass.threshold` 做 bright-pass。
  - `Proposed: src/render/builtin_shaders/bloom_upsample.vert.glsl` + `bloom_upsample.frag.glsl`：fullscreen + 9-tap tent filter + add 上层结果。
  - `Modified: src/render/Pipeline.cpp`：持 6 张独立 RGBA16F bloom mip texture + view；`OnResize` 重建；离屏段在主 pass 后追加 6 round downsample + 6 round upsample，每跳一对 BeginRendering/EndRendering。
  - `Modified: src/render/PostProcessPasses.cpp`：`BloomPass::Setup` 声明 chain；本子任务决策 = Pipeline 直接拥有 bloom 资源 + 直接驱动 mip-chain 调度，pass 仅承载 `threshold / intensity` 给 push-constant，不在 Execute 里独立组装资源（避免 chain[1] 内部嵌一个子 chain 状态机）。
  - `Modified: src/render/Pipeline.cpp`：passthrough 收尾改读 `HDR + bloom 末态` 两个 sampler 做 add；当 chain 不含 BloomPass 时仍走 06.03 的纯 HDR passthrough。
- 验证方式：
  1. cmake build 通过（新增 4 个 SPIR-V）；ctest 不退化；
  2. 临时启用 BloomPass（在 sample 内 chain 注入）显示亮区光晕；sample 03/04 默认 chain 空时视觉等价；
  3. 新增 ctest：`tests/render/BloomChainTest.cpp` 覆盖 bloom 6 张 mip 资源生命周期 + OnResize 同步重建。
- Critical Path：是

###### Task 06.05 · Tonemap + LUT bypass + 新 sample `04_3d_mesh_with_bloom` ✅
- 输出：
  - `Proposed: src/render/builtin_shaders/tonemap.vert.glsl` + `tonemap.frag.glsl`：fullscreen + ACES Narkowicz fit，按 push-constant 喂 exposure。
  - `Modified: src/render/PostProcessPasses.cpp`：`TonemapPass::Execute` 走 `IRenderer::SubmitItem` + `mpDescriptorSets[0]=HDR+bloom combined`；`LutPass::Execute` 当 `lut.IsValid() == false` → 直接早退；handle 有效路径仍 stub，Phase 6 真接 3D LUT 时再做。
  - `Modified: src/render/Pipeline.cpp`：移除 06.03 临时 passthrough 收尾——chain 满载 Bloom + Tonemap 时由 TonemapPass 完成 swap-chain 写出；chain 为空 / 仅 HdrPass / 含 LutPass 但 lut handle 无效时 fallback 到 06.03 的 passthrough，保证调试 / 测试场景不退化。
  - `Proposed: samples/04_3d_mesh_with_bloom/CMakeLists.txt` + `samples/04_3d_mesh_with_bloom/main.cpp`：与 04_3d_mesh 同骨架（World + Pipeline + MaterialSystem + PostProcessChain）；旋转立方体挂 toon `MaterialInstance`；chain = `BuiltinPostProcessChain::CreateDefault()` = HDR + Bloom + Tonemap + LUT(no-op)。
  - `Modified: samples/CMakeLists.txt`：注册 `04_3d_mesh_with_bloom`。
- 验证方式：
  1. cmake build 通过（新增 2 个 SPIR-V：tonemap.vert + tonemap.frag；本 task 总新增 SPIR-V = 06.03 的 2 + 06.04 的 4 + 06.05 的 2 = 8 个）；
  2. `04_3d_mesh_with_bloom.exe` 启动后窗口内显示一个 toon 着色的旋转立方体，bloom + tonemap 视觉与 reference 截图（PR review 时附）一致；
  3. 现有 `03_textured_quad` / `04_3d_mesh` 行为不退化（chain 空 = passthrough）；
  4. 现有 ctest 不退化 + install_smoke / config_smoke 不退化。
- Critical Path：是

##### 整体验收

跨 5 子任务全部 ✅ 后视为 Task 06 完成：
1. cmake build 通过（总新增 SPIR-V 8 个：fullscreen.vert + passthrough.frag + bloom_down/up × vert/frag = 4 + tonemap × vert/frag = 2，加 06.01 不新增 = 总 8）；
2. `04_3d_mesh_with_bloom.exe` 视觉与 reference 截图一致；
3. `03_textured_quad` / `04_3d_mesh` 在每个子任务节点视觉等价（中间状态硬约束）；
4. ctest 总数增量 ≥ 4（schema / Pipeline cache / HDR target / bloom chain）；install_smoke / config_smoke 不退化；
5. PR review 时按 5 子任务分别附运行截图。

Pipeline 真消费 PostProcessChain / MaterialSystem 的承诺兑现，Phase 3 视觉基线达到 "单 mesh + 后处理" 水平；Task 07 起在此基础上加 shadow + multi-entity demo。

- Critical Path：是（伞节点；5 子任务全部 critical path）。

#### Task 07：`samples/07_full_pipeline` 雏形（Pipeline 接通 shadow pass + multi-entity 综合演示） ✅
- 描述：把 Task 05 的 Light/Shadow 公共面接通 Pipeline——shadow render target（depth attachment）+ shadow pass（用 `BuiltinShadowShaders::LoadShadowCaster` 渲场景到 shadow map）+ 主 pass 加 light UBO + shadow sampler 描述符；toon / rim_light fragment shader 改为 `#include "include/shadow_pcf.glsl.inc"` 后乘 PCF 阴影系数。新增 sample `07_full_pipeline`：多 entity（≥3 个 mesh，分别挂 toon / rim_light / 一个 textured material）+ 一个 DirectionalLight + 完整 PostProcessChain，演示 Phase 3 视觉基线综合。
- 输入：Phase 3 / Task 06（Pipeline 已接 PostProcessChain / MaterialInstance 真路径）
- 输出：
  - `Modified: include/orange/engine/render/Pipeline.h`（追加 `void SetShadowConfig(const ShadowConfig&)`；可选 `void SetDirectionalLight(...)` 或者由 Pipeline 自己 `world.View<DirectionalLight>()` 取——见下面实现要点）
  - `Modified: src/render/Pipeline.cpp`（shadow pass 实现：depth-only target / per-frame light UBO / 第一遍 shadow_caster 绘 scene / 主 pass 绑 shadow sampler 给 fragment shader）
  - `Modified: src/render/builtin_shaders/toon.frag.glsl`（push-constant 仍 ≤ 128 B；新增 descriptor set 0 binding 0 = shadow sampler、binding 1 = light UBO（uLightViewProj + uLightDir + uLightColor + uLightIntensity）；`#include "include/shadow_pcf.glsl.inc"` + 乘 PCF 阴影系数）
  - `Modified: src/render/builtin_shaders/rim_light.frag.glsl`（同 toon 改动）
  - `Modified: include/orange/engine/render/BuiltinMaterials.h`（如果 Material.uniforms 需要因 push-constant 重排而调整 desc，则同步）
  - `Modified: src/render/BuiltinMaterials.cpp`（同上）
  - `Proposed: samples/07_full_pipeline/CMakeLists.txt` + `samples/07_full_pipeline/main.cpp`
  - `Modified: samples/CMakeLists.txt`（注册新 sample）
- 影响路径/模块：Render（Pipeline + 内置 shader 双侧）、samples、构建系统
- 前置依赖：Task 06
- 实现要点：
  - **D5 = 07_full_pipeline 当多 entity + 多 material 拼 demo 关卡**：用户决策。Task 06 已经是"单 mesh + 后处理"的视觉基线；Task 07 的差异化产出是 ≥3 entity / ≥2 material template / DirectionalLight + shadow / 完整 PostProcessChain 的综合演示，作为 Phase 3 的真正里程碑。
  - **shadow pass 接通由 Task 07 一次性做完**：Task 05 已经把 shadow_caster shader / shadow_pcf .inc / DirectionalLight component / ShadowConfig 都准备好了——Task 07 只需在 Pipeline 加 depth render target、按每帧 DirectionalLight 算 light view-proj、跑第一遍 shadow_caster、把 shadow map 作 sampler 喂给主 pass。
  - **toon / rim_light frag 改动是 Task 07 范围**：Task 05 留下来不动是为了让 sample 03/04 不退化；Task 07 改动后 sample 03/04 / 04_3d_mesh_with_bloom 也都跟着进入 shadowed 主 pass——Pipeline 在没有 DirectionalLight 时仍要绑一张 1×1 的"全亮 dummy shadow map"作 fallback，让 fragment shader 不需要分支 `#ifdef HAS_SHADOW`。
  - **light 来源 = Pipeline 主动 World::View 取**：Pipeline.cpp 内部 `world.View<DirectionalLight>()` 取第一个（或用 `IsPrimary` 标记，0.x 阶段先取 first），不需要在 Pipeline 上加 SetDirectionalLight API——这与 Pipeline 已经 `world.View<RenderableComponent>` 收集 drawable 同模式，多 light 的 priority / count limits 等 API 留给 Phase 6。
  - **light UBO = per-frame，不 per-draw**：light UBO 在 Pipeline 主 pass 起点写一次、所有 drawable 共用同一描述符 binding。push-constant 仍只放 per-draw 数据（uMVP + 颜色等）——Task 02/05 写过的"两者都不需要 UBO"在 Task 07 正式打破，但只引入一个 frame-level UBO，不 per-draw。
  - **样本范围**：07_full_pipeline 至少展示 3 个 entity（toon 立方体 / rim_light 球 / textured 平面），1 个 DirectionalLight 做投影，PostProcessChain 默认 4-pass，光源转动让 shadow 跟着变化——验证 shadow pass 真的随帧重渲。
  - **不动 04_3d_mesh_with_bloom**：Task 06 sample 走完后由 Task 07 的 toon.frag 改动连带迁移到 shadowed 路径——视觉上多了软阴影（如果场景里没 light，走 dummy shadow map = 等价于 Task 06 视觉）。无 light 场景仍能跑是 Pipeline 兼容性的硬约束。
- 验证方式：
  1. cmake build 通过（toon/rim_light frag 重编后 SPIR-V 数不变；shadow_caster 在 Task 05 已就位）；
  2. `07_full_pipeline.exe` 启动后窗口内 ≥3 个 mesh 显示，1 个 entity 投影另一 entity 在地面/平面上、随光源转动阴影位移；
  3. 现有 sample 03_textured_quad / 04_3d_mesh / 04_3d_mesh_with_bloom 行为等价（视觉上多软阴影，但场景结构不退化）；
  4. ctest 14 不退化、install_smoke / config_smoke 不退化。
- 验收标准：上述 4 条 + 新 sample 在 PR 审核时附运行截图（重点对比 light 转动时的 shadow 跟随）。Phase 3 完成标准的"软阴影"项兑现——Phase 3 收尾后只剩 Task 08 自定义 shader sample 验证 MaterialSystem 扩展点。
- Critical Path：是

#### Task 08：自定义 shader sample（验证 MaterialSystem::RegisterTemplate 扩展点） ✅
- 描述：交付 `samples/08_custom_shader`——一个独立 sample 演示游戏侧不修改引擎源码、用 MaterialSystem::RegisterTemplate 注册自己的 shader（一个简化的 fresnel / dissolve / 流光风格 shader）+ 一个挂着这个自定义 MaterialInstance 的 entity。这是 Phase 3 完成标准里"自定义 shader 注入 API 通过 sample 验证"的兑现项；同时是 Phase 3 收尾里程碑（Phase 3 完成所有六任务后视觉基线稳定，Phase 4 起开 Animation / Physics）。
- 输入：Phase 3 / Task 04（MaterialSystem）+ Task 06（Pipeline 真按 MaterialInstance 路由）+ Task 07（toon/rim_light 已迁到 shadowed 路径）
- 输出：
  - `Proposed: samples/08_custom_shader/shaders/fresnel.vert.glsl` + `fresnel.frag.glsl`（顶点 layout pos+uv 与内置一致，push-constant uMVP + uFresnelColor + uFresnelPower + uPulseSpeed；fragment 用 fresnel = pow(1 - dot(N, V), uFresnelPower) 加时间脉动；与 toon/rim_light 走同样 descriptor set 0 的 light UBO + shadow sampler binding，验证"游戏侧自定义 shader 也能享受 shadow"）
  - `Proposed: samples/08_custom_shader/CMakeLists.txt`（在 sample 目录里调用 glslangValidator 编 fresnel.vert/frag .spv 落到 sample 的 RUNTIME 输出目录，不在 orange_engine_compile_builtin_shader 里——**自定义 shader 的编译归 sample / 游戏侧负责**，extension-points 也是这么写的）
  - `Proposed: samples/08_custom_shader/main.cpp`（建 World + AssetRegistry + MaterialSystem(registry) + matSys.RegisterBuiltins() + 用 ShaderTemplateDesc 注册 fresnel template + matSys.CreateInstance("fresnel") + entity 挂 MaterialInstance + 一个 DirectionalLight + 完整 PostProcessChain；摄像机绕 entity 旋转）
  - `Modified: samples/CMakeLists.txt`（注册 08_custom_shader）
- 影响路径/模块：samples、构建系统（仅 sample 一侧；引擎不动）
- 前置依赖：Task 04 / 06 / 07
- 实现要点：
  - **核心承诺：sample 不 #include 任何 src/ 内部头**：自定义 shader sample 必须只通过公共 include/orange/engine/ 头消费引擎，验证 extension-points "游戏需要能不修改引擎源码地注册自己的 shader" 不是空话。
  - **shader 编译归 sample 自己**：sample CMakeLists 自己 add_custom_command 调 glslangValidator——与 extension-points 已写明的"shader 文件路径走 Asset 系统加载"一致。引擎只负责加载 .spv，不负责编 .glsl→.spv。
  - **fresnel shader 走与内置一致的 descriptor set 0**：light UBO + shadow sampler binding 和 toon/rim_light 同 layout——这是 sample 的隐性合同，验证"自定义 shader 也能消费引擎 frame-level uniform"。文档化：`docs/extension-points.md` 加一条"自定义 shader 必须把 descriptor set 0 binding 0/1 留给引擎 light UBO + shadow sampler"。
  - **uniform / texture 槽显式声明**：`ShaderTemplateDesc.uniforms` 列出 uFresnelColor / uFresnelPower / uPulseSpeed 三项，与 push-constant block 的字段顺序对齐。这条与 extension-points "ShaderSourceDesc 必须显式声明 uniform 与纹理槽——引擎不在运行时反射 SPIR-V 自动生成"一致。
  - **不为 sample 单独写 ctest**：08_custom_shader 是视觉验证 sample，验证靠 PR 截图 + 启动不崩；ctest 不增加（与 03/04 sample 同策略）。如果未来发现 RegisterTemplate 路径有 silent failure，回 Task 04 的 material_system_test 加路径而不是给 sample 加 ctest。
- 验证方式：
  1. cmake build 通过（sample 的 glslangValidator 调用产出 fresnel.vert.spv / frag.spv 到 `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/shaders/sample_08/`）；
  2. `08_custom_shader.exe` 启动后窗口内 entity 显示 fresnel rim 效果 + 时间脉动 + DirectionalLight 投影正常；
  3. ctest 14 个不退化、install_smoke / config_smoke 不退化、其它 sample 不退化。
- 验收标准：上述 3 条 + 新 sample 在 PR 审核时附运行截图。Phase 3 完成标准全部兑现：① Material 系统支持 uniform 块 + 纹理槽 + 自定义 shader；② PostProcessChain 至少串通 Bloom + Tonemap 两个 pass；③ 自定义 shader 注入 API 通过 sample 验证。Phase 4 可以开始。
- Critical Path：是

### Phase 4：可玩性

> Phase 4 引入 Animation / Physics / Input / Audio 四个模块。**关键架构决定**：Animation 第一版即承认 Skeletal + Procedural 双后端（按 `vendor/Orange-Wiki/wiki/techniques/animation/animation-state-machine.md` 的"分层 ASM + blend tree"思路设计公共面，但 Phase 4 只交付 flat-weighted-average 的最简 FSM，blend tree 留 Phase 6）。Physics 走 Box2D 3.x（2D 平台跳跃刚好够），公共面把 Box2D 类型完全藏在 PIMPL，3.x 接口与 2.x 不兼容由集成层吃。Input 走 GLFW 原生事件 + JSON 配置 ActionMap。Audio 用 miniaudio 单头集成。
>
> Phase 4 内任务有强弱依赖：**01 / 05 / 08（公共接口）零外部依赖**，可在外部库未就位时先做；**02 / 06 / 09（后端集成）+ 03 / 04 / 07（功能实现）**必须等对应 dep 装机器才能跑通；**10 / 11 是 sample 收尾**，必须最后做。建议推进顺序：先把 01/05/08 公共面合并、再按 dep 就位顺序逐个起后端。

#### Task 01：Animation 公共接口（IAnimator / AnimationStateMachine / AnimatorRegistry） ✅
- 描述：交付 Animation 模块的零依赖公共面：`IAnimator`（PIMPL 风格抽象基类）+ `AnimationStateMachine`（flat-weighted FSM，状态名 + 过渡条件 + 进入/退出回调）+ `AnimatorComponent`（ECS 组件）+ `AnimatorRegistry`（按名注册 backend factory，与 MaterialSystem 同节奏）。**不**实现 SkeletalAnimator / ProceduralAnimator 的具体内容（那是 Task 03/04），但要确保两条后端路径通过本期接口能落地——不出现"接口不够用要回头扩"的破坏性变更。
- 输入：Phase 3 收尾（World / RenderableComponent 的组件挂载惯例已稳）
- 输出：
  - `Proposed: include/orange/engine/animation/IAnimator.h`（抽象基类：`virtual void Tick(float dt) = 0;` `virtual bool IsFinished() const noexcept = 0;` `virtual std::string_view BackendName() const noexcept = 0;`）
  - `Proposed: include/orange/engine/animation/AnimationStateMachine.h`（轻量 FSM：`AddState(name, on_enter, on_exit)` / `AddTransition(from, to, condition_fn)` / `Tick(dt, ctx)` / `CurrentState() const`）
  - `Proposed: include/orange/engine/animation/AnimatorComponent.h`（ECS 组件，持 `std::unique_ptr<IAnimator>`，与 MaterialInstance 类似的"游戏侧持有所有权 + ECS 仅引用"模式——但 Animator 比 Material 更状态化，本期就做拥有式即 unique_ptr 进 component）
  - `Proposed: include/orange/engine/animation/AnimatorRegistry.h`（注册 backend factory：`RegisterBackend(name, std::function<unique_ptr<IAnimator>(args)>)` + `Create(name, args)`，与 MaterialSystem::RegisterTemplate 同形态）
  - `Proposed: src/animation/AnimationStateMachine.cpp`（FSM 实现）
  - `Proposed: src/animation/AnimatorRegistry.cpp`（registry 实现）
  - `Proposed: tests/animation/AnimationStateMachineTest.cpp`（覆盖：3 状态 / 2 过渡 / on_enter / on_exit 调用顺序 / 条件不成立时不 transition / 多帧 Tick 累计）
  - `Proposed: tests/animation/AnimatorRegistryTest.cpp`（覆盖：RegisterBackend / Create / 重复注册返回 AlreadyExists / 未知 name 返回 nullptr）
  - `Modified: CMakeLists.txt`（添加 src/animation 子目录 + 注册两条 ctest）
  - `Modified: docs/extension-points.md`（在 §3 之后加 §4：Custom Animator backend 注册——RegisterBackend(name, factory)）
- 影响路径/模块：Animation（新模块）、tests、CMake
- 前置依赖：无外部 dep（纯 std + EnTT）
- 实现要点：
  - **IAnimator 不暴露 pose / uniform 输出 API**：本期接口不规定"动画如何把结果交给 Render"。Skeletal 后端（Task 03）通过 Render 模块自有的 SkinningMatrixPalette uniform 路径，Procedural 后端（Task 04）通过 MaterialInstance::SetUniform——两条路径**通过 ECS 自然解耦**，不需要在 IAnimator 上再加一层。`Tick()` 是唯一驱动入口。
  - **AnimationStateMachine 走 flat weighted-average，不实现 blend tree**：Phase 4 范围只做"切换状态时 cross-fade 一段时间"的最简 FSM——状态本身的 blend 由 backend 自管（DragonBones 里就是切 anim name + 设 fade 时长）。Blend tree 留 Phase 6。这条与 wiki "Flat WA"路径对齐。
  - **过渡条件用 `std::function<bool(const StateContext&)>`**：StateContext = `{currentStateName, elapsedSeconds, transitionTriggers}`，调用方按需读。**不**引入任何专用 condition DSL（与"无反射"原则一致）。
  - **AnimatorRegistry 与 MaterialSystem 镜像但更轻**：MaterialSystem 内部要 SPIR-V 加载 + Pipeline 缓存；AnimatorRegistry 只是个 `std::unordered_map<string, factory>`，不持 GPU 资源。
  - **不预先建 AnimationClip / AnimationAsset**：clip 是 backend 内部细节（DragonBones 自己读 .json/.dbbin、ProceduralAnimator 没有 clip 概念）。本期不在 Asset 层引入新 AssetType。Phase 5 / Task 03（SkeletalAnimator）按需在 src/animation/dragonbones 自己处理资源加载。
- 验证方式：
  1. cmake build 通过，Animation 头文件能 include；
  2. ctest 19/20（新增 2 条：animation_state_machine_test / animator_registry_test）全过；
  3. install_smoke 不退化、config_smoke 不退化；
  4. 头隔离不变量：`include/orange/engine/animation/**` 不 include 任何 src/ / vendor/dragonbones/ 内部头。
- 验收标准：上述 4 条 + AnimatorRegistry 公共面能注册一个"echo backend"（Tick 累计计数 / IsFinished 永假）单测过，证明扩展点字面可用。
- Critical Path：是

#### Task 02：DragonBones C++ runtime 集成（`src/animation/dragonbones/`） ✅
- 描述：把 DragonBones C++ runtime（GitHub 上有官方 cpp port）按 in-tree 源码方式集成到 `src/animation/dragonbones/`，让本仓库的 build 能直接编它的 `.cpp`、链进 orange_engine。runtime 头不暴露到 `include/orange/engine/`——它的存在仅供 Task 03 SkeletalAnimator 消费。
- 输入：Task 01（Animation 公共面）
- 输出：
  - `Proposed: vendor/DragonBones/`（git submodule 或 clone-without-history 在树里）
  - `Proposed: src/animation/dragonbones/DragonBonesContext.{h,cpp}`（runtime 的 EventDispatcher / ResourceProvider / SoundEventManager 桩——给 runtime 不需要的回调走 no-op）
  - `Modified: CMakeLists.txt`（add_library(orange_engine_dragonbones STATIC vendor/DragonBones/...sources... ) + 链接给 orange_engine PRIVATE）
  - `Modified: cmake/Dependencies.cmake`（runtime 走 in-tree，不需 find_package；状态摘要输出 "DragonBones: in-tree (vendor/DragonBones)"）
- 影响路径/模块：vendor、src/animation/dragonbones、CMake
- 前置依赖：Task 01；vendor/DragonBones/ 源码就位
- 实现要点：
  - **runtime 用 in-tree 源码，不 find_package**：DragonBones C++ runtime 没有 CMake config，没有 find_package 端点；标准做法是把它的 `cpp/dragonBones/` 目录拉进自己的 build。
  - **EventDispatcher / SoundEventManager 桩为 no-op**：runtime 的事件回调供游戏接 UI / SFX；Phase 4 内只挂空实现，Task 09 Audio 上线后再让 SoundEventManager 转发到 AudioEngine。
  - **运行时坐标系约定固定 2D**：DragonBones 是 2D 骨架运行时，Z 维不参与；与 OrangeEngine"2D / 2.5D"定位一致。
  - **dragonbones 头隔离**：`#include <dragonBones/...>` 仅出现在 `src/animation/dragonbones/**`（CLAUDE.md 不变量）。
- 验证方式：
  1. cmake build 通过，DragonBones 编译 0 警告（warnings-as-errors 关掉 dragonbones 子库 / 单独 push/pop）；
  2. 加一条 smoke ctest：`tests/animation/dragonbones_runtime_smoke.cpp` —— include <dragonBones/DragonBonesHeaders.h> + 实例化一个空 EventDispatcher 不崩；
  3. 既有 ctest 不退化。
- 验收标准：上述 3 条 + runtime 与 orange_engine 一同安装成功（installed lib 能跑 install_smoke）。
- Critical Path：是

#### Task 03：SkeletalAnimator 实现（DragonBones 后端 + matrix palette 输出） ✅
- 描述：实现 IAnimator 的 SkeletalAnimator 后端：`SkeletalAnimator(SkeletonAsset, ArmatureName)` 构造；`Tick(dt)` 推进 DragonBones armature 动画；`Pose() const` 返回 `std::span<const glm::mat4>` matrix palette（per-joint world transform）；`Play(animName, fadeIn)` API。SkeletonAsset 由 AssetRegistry 加载 .json + .dbbin（DragonBones 双文件资源）。Render 模块在 Phase 4 内**先不接** matrix palette 进 vertex shader——Task 03 范围只到 CPU 端 pose 计算正确，渲染端把 palette 上传 GPU + skinning vertex shader 留 Phase 6 / 配合 Material UBO 完整化。
- 输入：Task 01 / Task 02
- 输出：
  - `Proposed: include/orange/engine/animation/SkeletalAnimator.h`（公共面：构造、Play、Tick、Pose）
  - `Proposed: include/orange/engine/asset/SkeletonAsset.h`（资源类型：joints layout + DragonBones runtime 内部句柄；payload PIMPL 藏 dragonbones types）
  - `Proposed: src/animation/dragonbones/SkeletalAnimator.cpp`
  - `Proposed: src/animation/dragonbones/SkeletonLoader.cpp`（IAssetLoader<SkeletonAsset>：读 .json + 配套 .dbbin，喂 dragonbones runtime 的 ArmatureFactory）
  - `Proposed: tests/animation/SkeletalAnimatorTest.cpp`（覆盖：Tick 推进 elapsedTime / Play 切 anim / 非循环 anim 走完 IsFinished == true）
- 影响路径/模块：Animation、Asset、tests
- 前置依赖：Task 01 / 02
- 实现要点：
  - **Pose() 返回 span<const mat4>，调用方不持有所有权**：SkeletalAnimator 内部按当前帧重算 palette；调用方下一次 Tick 之前消费完。这与 EnTT 视图同生命周期模式。
  - **DragonBones armature 树形 → 扁平 palette**：DragonBones 给 per-bone local transform，本类负责按 bone hierarchy 做累乘（Tj_world = Tj_local × Tparent_world）输出 flat palette。
  - **Skin 上传留 Phase 6**：本期 Pose() 给出 CPU 数据即结束。Render 端如何消费（per-skeleton uniform buffer / push constant 装填）等 Material UBO 上线后再说——Task 03 不动 Render 模块。
- 验证方式：
  1. cmake build 通过；
  2. ctest 新增 1 条 SkeletalAnimatorTest 全过；
  3. 用 DragonBones 官方测试资源加载 + Play 一个 anim，10 帧 Tick 后 Pose() 元素个数 == bone count、首 bone 的 translation 在已知曲线上；
  4. 既有 ctest 不退化。
- 验收标准：上述 4 条。
- Critical Path：是

#### Task 04：ProceduralAnimator 实现（shader uniform 驱动） ✅
- 描述：实现 IAnimator 的 ProceduralAnimator 后端：通过若干 `Channel<T>(name, fn)` 把"时间 → uniform 值"的曲线 / 噪声驱动函数挂到 MaterialInstance 上——每 Tick 调用 fn(elapsedTime) 算出当前值，调 `MaterialInstance::SetUniform(name, value)`。这是"史莱姆 noise 振幅 / dissolve 进度"这类 procedural shader 效果的基础。**重要**：本路径**依赖 Material UBO 让 SetUniform 真接通到 push-constant**——但 Phase 4 阶段 Material UBO 还没上线（Phase 6 落地），Task 04 在本期只做 CPU 端"算出值并存进 MaterialInstance 内表"，能否真出现在 GPU shader 等 Material UBO 上线。本期 ctest 验证 SetUniform 调用计数即可。
- 输入：Task 01；MaterialInstance（Phase 3 已落）
- 输出：
  - `Proposed: include/orange/engine/animation/ProceduralAnimator.h`（公共面：构造、AddChannel<T>(name, fn)、SetTarget(MaterialInstance*)）
  - `Proposed: src/animation/ProceduralAnimator.cpp`
  - `Proposed: tests/animation/ProceduralAnimatorTest.cpp`（覆盖：AddChannel<float> 时间驱动 / Tick 后 MaterialInstance.GetUniformFloat == 预期 / 多 channel 互不干扰）
- 影响路径/模块：Animation、tests
- 前置依赖：Task 01
- 实现要点：
  - **Channel 用模板 + virtual 实现**：`Channel<T>(name, std::function<T(float)>)`——在 .cpp 里实例化 T = float / vec2 / vec3 / vec4 / mat4 与 MaterialInstance::SetUniform 重载对齐。
  - **不绑 Render 模块**：不直接调 Render，仅通过 MaterialInstance；与 IAnimator "不输出 pose / uniform" 原则一致——一切走 ECS + Material 解耦。
  - **noise 函数自带还是调用方提供**：本期 fn 由调用方自己写（包括 simplex / Perlin）；Phase 6 引入 utility 库再说。
- 验证方式：
  1. cmake build 通过；
  2. ctest 新增 1 条 ProceduralAnimatorTest 全过；
  3. 调用方传 `[](float t) { return std::sin(t * 2.0f); }` 作为 channel，Tick 1 秒后值 ≈ sin(2.0)；
  4. 既有 ctest 不退化。
- 验收标准：上述 4 条。Phase 6 Material UBO 上线后 Task 04 不需重做——本期 SetUniform 的内表已经写对了，到时只需让 Pipeline 把内表读出来 push 到 GPU 即可。
- Critical Path：否（不阻塞 Phase 5 起跑；Material UBO 上线前没法在 sample 里看到效果，但接口可用）

#### Task 05：Physics 公共接口（PhysicsWorld / RigidBody / Collider 组件） ✅
- 描述：交付 Physics 模块的零依赖公共面：`PhysicsWorld`（PIMPL，全 Box2D 类型藏在 .cpp）+ `RigidBodyComponent`（type、mass、velocity 等）+ `ColliderComponent`（shape desc + material props）+ `ColliderDesc`（圆 / 盒 / 多边形 / 链）。**不**集成 Box2D（Task 06）；本期接口设计要让 Box2D 切到其他 2D physics（如 Chipmunk）也无破坏性改动。
- 输入：Phase 3 World
- 输出：
  - `Proposed: include/orange/engine/physics/PhysicsWorld.h`（PIMPL：构造 + Step(dt) + AddBody / RemoveBody）
  - `Proposed: include/orange/engine/physics/RigidBodyComponent.h`（trivially-copyable，含 BodyType enum：Static / Kinematic / Dynamic + linearDamping / angularDamping）
  - `Proposed: include/orange/engine/physics/ColliderComponent.h`（trivially-copyable 描述符，shape variant + density / friction / restitution）
  - `Proposed: include/orange/engine/physics/ColliderDesc.h`（CircleDesc / BoxDesc / PolygonDesc / EdgeChainDesc 几何 desc）
  - `Proposed: src/physics/PhysicsWorld.cpp`（PIMPL stub：Step no-op、AddBody / RemoveBody 维护内表，等 Task 06 接 Box2D 后端）
  - `Proposed: tests/physics/PhysicsInterfaceTest.cpp`（覆盖：World 构造析构 / AddBody-RemoveBody 不崩 / Step 无 body 不崩）
- 影响路径/模块：Physics（新模块）、tests
- 前置依赖：Phase 3 World
- 实现要点：
  - **公共面没有任何 b2 类型**：CLAUDE.md "Header isolation" 要求 `<box2d/...>` 只在 `src/physics/box2d/**`——本期接口设计要严守。
  - **PhysicsWorld 持 BodyHandle 表**：AddBody 返回不透明 BodyHandle（uint64_t typed wrapper）。Task 06 把这表的 entry 关联到真 b2Body*。本期是占位 stub，Step 走个 no-op 就行。
  - **ColliderComponent 用 std::variant**：`std::variant<CircleDesc, BoxDesc, PolygonDesc, EdgeChainDesc>`——比 enum + union 更现代，trivially-copyable 仍维持（所有 desc 都是 POD）。
- 验证方式：
  1. cmake build 通过；
  2. ctest 新增 1 条 PhysicsInterfaceTest 全过；
  3. install_smoke 不退化、头隔离不变量：`include/orange/engine/physics/**` 不 include `<box2d/...>`。
- 验收标准：上述 3 条。
- Critical Path：是

#### Task 06：Box2D 3.x 集成（`src/physics/box2d/`） ✅
- 描述：把 Box2D 3.x 通过 `find_package(box2d CONFIG REQUIRED)` 接入，PhysicsWorld PIMPL 内实现真 b2World 创建 / 步进 / body / fixture 管理。Box2D 3.x 的 API 与 2.x 不兼容（`b2BodyId` 句柄替代 `b2Body*`、`b2WorldDef` / `b2BodyDef` 走结构体 init），集成层吃这个差异、不让上层感知。
- 输入：Task 05；Box2D 3.x 装到 D:/3rdparty/install
- 输出：
  - `Modified: cmake/Dependencies.cmake`（box2d 从 QUIET 转为 REQUIRED）
  - `Modified: src/physics/PhysicsWorld.cpp`（替换 Task 05 stub，转走 b2WorldDef / b2CreateWorld / b2World_Step / b2DestroyWorld）
  - `Proposed: src/physics/box2d/Box2DBridge.{h,cpp}`（ColliderDesc → b2ShapeDef 转换 / RigidBodyComponent → b2BodyDef 转换）
  - `Proposed: tests/physics/Box2DStepTest.cpp`（覆盖：自由落体 ball 1 秒后 y ≈ -0.5*g*t² 误差 < 1%）
- 影响路径/模块：Physics、cmake
- 前置依赖：Task 05；Box2D 3.x 已装
- 实现要点：
  - **3.x 句柄模式**：`b2BodyId` 是 versioned ID 而非 pointer，PhysicsWorld 内表存 `BodyHandle → b2BodyId` 映射，对外仍返 `BodyHandle`。
  - **ECS Transform ↔ b2 同步**：Step() 之后扫所有 Dynamic body，从 b2 取 position/rotation 写回 ECS TransformComponent；Static / Kinematic 反向（ECS 改值 → 下一 Step 前推到 b2）。Phase 4 范围只做 2D x-y 平面同步，Z 不动。
  - **Box2D 头只在 src/physics/box2d/ 出现**：CLAUDE.md 头隔离不变量。
- 验证方式：
  1. cmake build 通过、box2d 链接成功；
  2. Box2DStepTest 全过；
  3. PhysicsInterfaceTest 不退化（接口语义一致）；
  4. validation 静默 / install_smoke 不退化。
- 验收标准：上述 4 条。
- Critical Path：是

#### Task 07：Fixture 替换 API（运行时碰撞器变形支持） ✅
- 描述：在 PhysicsWorld 加 `ReplaceFixture(BodyHandle, ColliderDesc)`：原子地把已挂在 body 上的 collider 换成新 desc——这是史莱姆"swallowing boss form"路径上"角色形状中途变化"的 must-have 入口（虽是首游戏需求，但**接口本身是引擎中性的**——任何 2D 平台跳跃在变形 / 拾取大件物品 / 状态变身时都会用到，不算游戏特化）。
- 输入：Task 06
- 输出：
  - `Modified: include/orange/engine/physics/PhysicsWorld.h`（增 ReplaceFixture API）
  - `Modified: src/physics/PhysicsWorld.cpp`（Box2D 3.x 走 b2DestroyShape + b2CreatePolygonShape / b2CreateCircleShape 替换；同帧不冲突取决于 physics 解算时点，Step() 之外调用安全）
  - `Proposed: tests/physics/FixtureReplaceTest.cpp`（覆盖：dynamic body 半径 1 → 0.3 替换后 contact filtering 正确 / mass 重算）
- 影响路径/模块：Physics、tests
- 前置依赖：Task 06
- 实现要点：
  - **替换在 Step() 调用之外**：调用时点应在帧的"逻辑阶段"完成，物理 Step 取最新形状；与 b2 文档一致。
  - **mass 自动重算**：Box2D 3.x 自动处理 ApplyMassFromShapes（如果 dynamic）；本 API 不需要调用方手动算。
- 验证方式：
  1. ctest 新增 1 条 FixtureReplaceTest 全过；
  2. 既有 ctest 不退化。
- 验收标准：上述 2 条。
- Critical Path：否（首游戏会用，但 Phase 4 sample 不展示——留 Task 11 综合 demo 时再展示）

#### Task 08：Input 模块（Action / ActionMap / InputContext + JSON 加载） ✅
- 描述：交付 Input 模块：`Action`（trigger 类型 + dead zone 等）+ `ActionMap`（Action 列表，按名查 / 按物理 binding 触发）+ `InputContext`（栈式上下文，暂存 ActionMap + 优先级；UI / 菜单切到不同 context 不打扰主玩法）。底层走 GLFW 原生 key / mouse / gamepad 事件。`*.actions.json` 加载 ActionMap 描述。
- 输入：Phase 1 / 2 已落 Window 与 GLFW
- 输出：
  - `Proposed: include/orange/engine/input/Action.h`
  - `Proposed: include/orange/engine/input/ActionMap.h`
  - `Proposed: include/orange/engine/input/InputContext.h`
  - `Proposed: include/orange/engine/input/InputDevice.h`（key / mouse / gamepad button enums）
  - `Proposed: src/input/InputContext.cpp` + 配套 JSON loader（走 Core::JsonReader 不暴露 nlohmann）
  - `Proposed: assets/configs/default.actions.json`（示例 / sample 用）
  - `Proposed: tests/input/ActionMapTest.cpp` + `tests/input/InputContextStackTest.cpp`
- 影响路径/模块：Input（新模块）、Asset（无新 type，但用 Core::JsonReader）
- 前置依赖：Phase 1 GLFW 已就位
- 实现要点：
  - **底层物理事件桥**：GLFW key callback → InputContext::PostKeyEvent；context 栈顶的 ActionMap 把 key code → Action name + state（pressed / held / released）。
  - **Schema 走 Core::JsonReader**：与 docs/extension-points.md "JSON 都走 JsonReader / JsonWriter" 一致。Schema：actions[].name / actions[].bindings[]。
  - **不引入 reflection / 自动绑定**：每个 binding 字段在 loader 内手写 read。
- 验证方式：
  1. ctest 新增 2 条全过；
  2. 加载 default.actions.json + 模拟 key event 触发 Action::Triggered 状态正确；
  3. install_smoke / config_smoke 不退化。
- 验收标准：上述 3 条 + extension-points.md 加 §5：Custom Input bindings（说明 JSON schema + 游戏侧自加 actions 的路径）。
- Critical Path：是

#### Task 09：Audio 模块（miniaudio 集成） ✅
- 描述：把 miniaudio（单头库）集成到 `src/audio/miniaudio/`，公共面 `AudioEngine`（init / shutdown）+ `Sound`（资源句柄）+ `SoundInstance`（播放控制：Play / Pause / Stop / SetVolume）。`SoundAsset` 通过 AssetRegistry 加载（wav / mp3 / flac，走 miniaudio 内置 decoder）。
- 输入：Phase 1 Asset / 2 Core
- 输出：
  - `Proposed: vendor/miniaudio/miniaudio.h`（单头）
  - `Proposed: include/orange/engine/audio/AudioEngine.h`
  - `Proposed: include/orange/engine/audio/Sound.h` / `SoundInstance.h`
  - `Proposed: include/orange/engine/asset/SoundAsset.h`
  - `Proposed: src/audio/miniaudio/AudioEngine.cpp`（define MINIAUDIO_IMPLEMENTATION 在唯一 TU）
  - `Proposed: src/audio/miniaudio/SoundLoader.cpp`（IAssetLoader<SoundAsset>）
  - `Proposed: tests/audio/AudioEngineSmokeTest.cpp`（init / shutdown 不崩；不放真声音文件，CI 友好）
  - `Modified: CMakeLists.txt`（add vendor/miniaudio include path PRIVATE BUILD_INTERFACE）
- 影响路径/模块：Audio（新模块）、Asset、CMake
- 前置依赖：Phase 1 / 2；vendor/miniaudio/miniaudio.h 就位
- 实现要点：
  - **miniaudio 头隔离**：`#include "miniaudio.h"` 仅在 `src/audio/miniaudio/**`（CLAUDE.md 不变量）。
  - **MA_NO_DECODING / MA_NO_ENCODING 不开**：默认带 wav / flac / mp3 decoder；游戏侧最常见格式都覆盖。
  - **CI 友好**：smoke ctest 不真播放声音（CI 没声卡可能崩），仅验证 ma_engine_init / ma_engine_uninit 路径。
- 验证方式：
  1. cmake build 通过；
  2. AudioEngineSmokeTest 全过；
  3. 既有 ctest 不退化。
- 验收标准：上述 3 条。
- Critical Path：是

#### Task 10：`samples/05_skeletal_animation` + `samples/06_physics_platformer` ✅
- 描述：交付两个 sample：`05_skeletal_animation` 加载 DragonBones 官方测试资源（如 mecha_1002_101d 或类似 free demo skeleton）、Play 一个 walk anim、屏幕上看到骨骼带蒙皮 mesh 动起来；`06_physics_platformer` 一个 dynamic ball 受重力下落、撞到 plane 反弹 / 滑动，DirectionalLight + 完整 PostProcessChain 沿用 sample 07/08 视觉基线。
- 输入：Task 03（SkeletalAnimator）/ Task 06（Box2D）/ Phase 3 视觉基线
- 输出：
  - `Proposed: samples/05_skeletal_animation/{CMakeLists.txt, main.cpp, assets/mecha/...}`
  - `Proposed: samples/06_physics_platformer/{CMakeLists.txt, main.cpp}`
  - `Modified: samples/CMakeLists.txt`
- 影响路径/模块：samples、构建系统
- 前置依赖：Task 03 / 06
- 实现要点：
  - **Skeletal sample 不接 GPU skinning**：Phase 4 范围 SkeletalAnimator.Pose() 只在 CPU 算 palette，sample 端可以用 imgui debug 把 palette 文本输出 / 或在 plane 上画几个 dot 表示 joint 位置——视觉验收"动起来"即可，等 Phase 6 GPU skinning 上线再让 mesh 真变形。
  - **Physics sample 用 sphere mesh + plane**：与 sample 07/08 视觉风格对齐。
  - **shadow / bloom 不变**：直接复用 Phase 3 的 PostProcessChain。
- 验证方式：
  1. cmake build 通过；
  2. 两个 sample 启动不崩；
  3. 截图 sample 05 看到 joint 位移、sample 06 看到 ball 下落 + 弹跳；
  4. ctest 不退化、install_smoke 不退化。
- 验收标准：上述 4 条。
- Critical Path：是

#### Task 11：`samples/07_full_pipeline` 升级为可玩 demo（Animation + Physics + Input 综合） ✅
- 描述：把 sample 07_full_pipeline 升级：把 cube 换成"由骨骼动画驱动的 mecha + Box2D dynamic body"，键盘 A/D 走（InputContext + ActionMap）、空格跳；DirectionalLight + shadow + bloom 全保留。**这条是 Phase 4 收尾里程碑**——证明 Animation + Physics + Input 在 Phase 3 视觉基线上能合得起来。
- 输入：Task 03 / 06 / 07 / 08 / 09
- 输出：
  - `Modified: samples/07_full_pipeline/main.cpp`（替换 cube → 动画 character + 物理 body + Input）
  - `Modified: samples/07_full_pipeline/CMakeLists.txt`（新加 *.actions.json copy）
  - `Proposed: samples/07_full_pipeline/configs/default.actions.json`
- 影响路径/模块：samples
- 前置依赖：Task 03 / 06 / 07 / 08 / 09
- 实现要点：
  - **音效在 Task 09 上线后挂上**：跳跃 / 落地音效——验证 SoundEventManager 能从 DragonBones runtime 转发到 AudioEngine。
  - **首游戏特化保持引擎中性**：character 走"通用 mecha" 资源，不用 slime / boss form——避免与 game-specific 概念混淆（CLAUDE.md "Game-specific 禁忌"原则）。
- 验证方式：
  1. cmake build 通过；
  2. 启动后键盘控制角色走 + 跳，动画切换 idle/walk/jump，影子跟着角色动；
  3. 跳跃 / 落地有音效；
  4. ctest 不退化。
- 验收标准：上述 4 条。Phase 4 完成标准全部兑现：DragonBones 接通 + ProceduralAnimator + Box2D 步进 + Action JSON 加载 + sample 07 综合演示。Phase 5（生产化）可启动。
- Critical Path：是

### Phase 5：生产化

- Task 01：Scene 序列化（JSON schema + 版本字段）
- Task 02：VFX 完整化（粒子系统 + dissolve shader + emission）
- Task 03：体积光（screen-space god rays）
- Task 04：自定义 RenderPass 注入正式启用
- Task 05：异步资源加载
- Task 06：30 秒可玩关卡 demo
- Task 07：游戏仓库雏形（独立仓库；引擎 0.5 release）

### Phase 5.5：Save Game 系统

- Task 01：`SaveGameRegistry` 公共接口与 component 注册流程
- Task 02：`SaveGameSystem::Save` / `Load` 主流程（原子写入 + CRC + schema 校验）
- Task 03：平台路径解析（Windows `SHGetKnownFolderPath` 包装；Linux/Mac 接口预留）
- Task 04：Slot 元信息 + autosave 调度
- Task 05：Migrator hook 接口 + 至少一个版本迁移示例
- Task 06：损坏存档与错误路径测试覆盖
- Task 07：Sample 关卡集成存档 / 加载流程

---

## Self-Check

- 已显式区分 `Existing` / `Proposed` / `Removed` 三类文件。
- 已假设 OrangeRender 已可用，没有把 Phase 1 退化为"自己写 RHI"。
- 已识别"公共头不许漏 Box2D / DragonBones / miniaudio / OrangeRender"为项目级 invariant。
- 已识别"Animation 必须双后端并列"为 Phase 4 起的硬约束。
- 已识别"Material 自定义 shader 注入"为 Phase 3 必交付（不能推迟到 Phase 5）。
- 已让"角色形态系统"留在游戏仓库，不污染引擎。
- 序列化框架在 Phase 1 就以 `Core::Serialization` 原语落地，避免 Phase 2 起每个模块自行 `nlohmann::json` 裸用造成 schema / 错误处理风格分裂。
- 反射策略明确锁定为"完全手写"。Phase 1–6 不引入任何形式的运行时反射库或 AST codegen；如有 Phase 6 编辑器开工后实际烦扰程度过高，再评估是否升级到 EnTT meta（局部反射，不进运行时热路径）。
- Save Game 抽出为独立 Phase 5.5，承认它解决的是"玩家产出 + 跨引擎升级兼容"这一与"关卡 content"完全不同的问题。
- 每个 Phase 显式列出"本阶段引入的持久化 schema"，让 schema 演化有可追踪入口。
- 每个 Phase 都给出可演示里程碑（一个 sample）。
- Phase 1 的 Task Breakdown 完全展开；Phase 2–5、5.5 列要点，详细 Task 表在阶段开工时增量产出。
- 双仓库交付模式（引擎 = 第三方库，游戏 = 独立仓库消费）已在分层结构与目录建议中体现。

## 后续篇章

- [`docs/roadmap.md`](./roadmap.md)：Phase 6+ 长期演进路线（编辑器 / 脚本层 / Hot reload / cook 工具链）
- [`docs/extension-points.md`](./extension-points.md)：扩展点详细设计（ECS / Asset 工厂 / Material shader / RenderPass 注入 / Animation 后端注册 / 模块按需启用）
- [`docs/coding-standards.md`](./coding-standards.md)：本仓编码规范（基于 OrangeRender 同名文档）
- [`docs/case-studies/character-forms.md`](./case-studies/character-forms.md)：第一款游戏的"流体角色 + 形态吞噬"机制设计笔记（不是引擎规约，是引擎扩展点的下游消费记录）
