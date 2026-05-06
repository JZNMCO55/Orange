# OrangeEngine 设计与实施规划

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

### Phase 3：Ori 视觉基线

- Task 01：Material / MaterialInstance 公共接口
- Task 02：MaterialTemplate 内置（卡通 + rim light）
- Task 03：PostProcessChain 接口与默认链（HDR → Bloom → Tonemap → LUT）
- Task 04：自定义 shader 注入接口（MaterialSystem::RegisterShader）
- Task 05：软阴影实现（PCF）
- Task 06：升级 `samples/04_3d_mesh_with_bloom` 显示 Ori 风格
- Task 07：`samples/07_full_pipeline` 雏形（带后处理的综合演示）
- Task 08：自定义 shader sample（验证游戏侧能不改引擎注册新 shader）

### Phase 4：可玩性

- Task 01：Animation 模块公共接口（IAnimator / AnimationStateMachine）
- Task 02：DragonBones runtime 集成（`src/animation/dragonbones/`）
- Task 03：SkeletalAnimator 实现
- Task 04：ProceduralAnimator 实现（shader uniform 驱动）
- Task 05：Physics 模块公共接口（PhysicsWorld / RigidBody / Collider）
- Task 06：Box2D 3.x 集成（`src/physics/box2d/`）
- Task 07：Fixture 替换 API（运行时碰撞器变形支持）
- Task 08：Input 模块（Action / ActionMap / InputContext，JSON 加载）
- Task 09：Audio 模块（miniaudio 集成）
- Task 10：`samples/05_skeletal_animation`、`samples/06_physics_platformer`
- Task 11：`samples/07_full_pipeline` 升级为可玩 demo

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
