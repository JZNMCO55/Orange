# OrangeEngine 扩展点设计

本文档枚举 OrangeEngine 对游戏侧（以及编辑器侧）开放的所有扩展点，并给出每个扩展点的 API 草案与"游戏如何使用"的最小示例。

引擎扩展性的根本原则：**所有 game-specific 行为都通过扩展点实现，引擎源码不为单一游戏改一行**。

---

## 1. ECS 组件 / 系统注册（默认扩展，零成本）

### 设计思路
ECS（EnTT）天然就是扩展点。游戏侧的 component 是普通 struct，system 是实现 `ISystem` 接口的类。引擎完全不感知它们的存在。

### API
```cpp
// 游戏侧定义自己的 component（在游戏仓库的头文件里）
struct PlayerStats
{
    int   mHp{100};
    float mMana{0.0f};
};

// 游戏侧定义自己的 system（继承引擎接口）
class CombatSystem : public Orange::Engine::ISystem
{
public:
    void OnAttach(Orange::Engine::World& world) override;
    void Update(Orange::Engine::World& world, const Orange::Engine::FrameContext& ctx) override;
};

// 注册到 world
app.GetWorld().RegisterSystem(std::make_unique<CombatSystem>());

// add component 到 entity（直通 EnTT 接口）
auto* pRegistry = static_cast<entt::registry*>(world.GetNativeRegistry());
auto entity = pRegistry->create();
pRegistry->emplace<PlayerStats>(entity, PlayerStats{.mHp = 50});
```

### 约束
- 系统执行顺序按注册顺序串行（Phase 5+ 才考虑并行调度）
- 组件类型需要可平凡构造 + 移动；引擎不替你处理 RAII
- `World::GetNativeRegistry()` 返回 `void*` 是为了避免在公共头中暴露 EnTT 内部类型；游戏侧 `static_cast<entt::registry*>` 后可获得完整 EnTT API

### 游戏不应做的事
- 不要试图修改引擎内置组件的行为（`TransformComponent` 等）；如需新行为，新建一个游戏专属组件并加 system
- 不要在 system 内同步 IO 或网络阻塞调用；这会冻结主循环

---

## 2. Asset 类型注册（工厂模式）

### 设计思路
引擎内置 Mesh / Texture / Shader / Skeleton / Sound / Font 几类 asset。游戏侧可以注册新的 asset 类型，例如 dialogue、quest data、关卡蓝图、剧情时间线。

### API
```cpp
// include/orange/engine/asset/AssetRegistry.h
namespace Orange::Engine
{

class IAssetLoader
{
public:
    virtual ~IAssetLoader() = default;
    virtual Result<AssetPayload> Load(const std::filesystem::path& path) = 0;
    virtual void Free(AssetPayload& payload) = 0;
};

class AssetRegistry
{
public:
    template <typename T>
    void Register(std::string_view extension, std::unique_ptr<IAssetLoader> pLoader);

    template <typename T>
    AssetHandle<T> Load(const std::filesystem::path& path);
};

}  // namespace Orange::Engine
```

### 游戏侧使用
```cpp
// 游戏定义自己的 asset 类型
struct DialogueAsset
{
    std::vector<DialogueLine> mLines;
};

// 游戏定义 loader
class DialogueLoader : public Orange::Engine::IAssetLoader
{
public:
    Orange::Engine::Result<Orange::Engine::AssetPayload> Load(const std::filesystem::path& path) override;
    void Free(Orange::Engine::AssetPayload& payload) override;
};

// 注册
app.GetAssetRegistry().Register<DialogueAsset>(".dialogue", std::make_unique<DialogueLoader>());

// 使用
auto handle = app.GetAssetRegistry().Load<DialogueAsset>("dialogues/intro.dialogue");
```

### 约束
- AssetPayload 的具体存储是引擎内部细节；loader 只负责转换 payload，不持有所有权
- 同步加载是 Phase 1–4 的契约；异步加载在 Phase 5 通过同 API 增加 `LoadAsync` 方法

---

## 3. Material Shader 注入（Phase 3 启用）

### 设计思路
引擎内置 MaterialTemplate（卡通照明 + rim light + 法线贴图）。但 Ori 风格的 dissolve、史莱姆 fresnel、能量流动这些都是游戏特定 shader。游戏需要能不修改引擎源码地注册自己的 shader 与对应 MaterialTemplate。

### API
```cpp
// include/orange/engine/render/MaterialSystem.h
namespace Orange::Engine::Render
{

struct ShaderTemplateDesc
{
    std::string                          name;
    std::filesystem::path                vertexSpirvPath;    // 预编译 .spv，不接受 GLSL source
    std::filesystem::path                fragmentSpirvPath;
    std::vector<MaterialUniformDesc>     uniforms;
    std::vector<MaterialTextureSlotDesc> textureSlots;
};

class MaterialSystem
{
public:
    explicit MaterialSystem(Asset::AssetRegistry& registry);

    Result<void, ResultCode>          RegisterTemplate(const ShaderTemplateDesc& desc);
    const Material*                   FindTemplate(std::string_view name) const;
    std::unique_ptr<MaterialInstance> CreateInstance(std::string_view name);

    // 便利方法：内部调 BuiltinMaterials::LoadToon / LoadRimLight
    Result<void, ResultCode> RegisterBuiltins();
};

}  // namespace Orange::Engine::Render
```

### 游戏侧使用
```cpp
// MaterialSystem 是值构造的类，由调用方持有（典型场景：sample / 游戏 main 里建一个）。
Asset::AssetRegistry registry;
registry.RegisterLoader<Asset::ShaderAsset>(std::make_unique<Asset::ShaderLoader>());

Render::MaterialSystem matSys(registry);
matSys.RegisterBuiltins();  // 注册 toon + rim_light

Render::ShaderTemplateDesc desc;
desc.name              = "slime_fresnel";
desc.vertexSpirvPath   = "assets/shaders/slime.vert.spv";    // 游戏侧自己用 glslangValidator 预编 .spv
desc.fragmentSpirvPath = "assets/shaders/slime.frag.spv";
desc.uniforms = {
    {"uTime",           Render::MaterialUniformType::Float},
    {"uNoiseAmplitude", Render::MaterialUniformType::Float},
    {"uRimColor",       Render::MaterialUniformType::Vec3},
};
desc.textureSlots = {
    {0, "uInnerNoise"},
};

matSys.RegisterTemplate(desc);

auto inst = matSys.CreateInstance("slime_fresnel");
inst->SetUniform("uRimColor", glm::vec3{0.4f, 0.8f, 1.0f});
inst->SetTexture(0, innerNoiseTextureHandle);

// Pipeline 借非拥有引用（Pipeline::SetMaterialSystem(&matSys)）；matSys 必须活到 Pipeline 析构。
```

### 约束
- ShaderTemplateDesc 必须显式声明 uniform 与纹理槽——引擎不在运行时反射 SPIR-V 自动生成
- shader 文件**只接受 SPIR-V 路径**（.spv）；GLSL source 由游戏侧在自己的 build 里用 glslangValidator 预编。运行时 GLSL 编译 / hot-reload 留给 Phase 6
- shader 文件路径走 Asset 系统加载，不直接 fopen
- 每个 MaterialTemplate 编译为一个 RHI Pipeline（OrangeRender PipelineDesc）；切换 MaterialInstance 不重新编译
- 自定义 shader 必须把 descriptor set 0 binding 0/1 留给引擎 light UBO + shadow sampler（与内置 toon / rim_light 同 layout）——这条在 Phase 3 / Task 07 起生效，让自定义 shader 也能消费 frame-level light + shadow 数据

---

## 4. RenderPass 注入（Phase 5 正式启用）

### 设计思路
引擎默认 Pipeline 编排为 Shadow → Opaque → Transparent → PostProcess → UI。游戏可以在任意 stage 之间插入自定义 RenderPass（例如水面、传送门、镜像、屏幕扭曲）。

### API
```cpp
// include/orange/engine/render/IRenderPass.h
namespace Orange::Engine::Render
{

class IRenderPass
{
public:
    virtual ~IRenderPass() = default;
    virtual void Setup(class RenderGraphBuilder& builder) = 0;
    virtual void Execute(class RenderPassContext& ctx) = 0;
};

// include/orange/engine/render/Pipeline.h
class Pipeline
{
public:
    void InsertPass(PipelineStage afterStage, std::unique_ptr<IRenderPass> pPass);
};

}  // namespace Orange::Engine::Render
```

### 游戏侧使用
```cpp
class WaterPass : public Orange::Engine::Render::IRenderPass
{
public:
    void Setup(Orange::Engine::Render::RenderGraphBuilder& builder) override
    {
        builder.Read(mDepthBuffer);
        builder.Read(mSceneColor);
        builder.Write(mWaterOutput);
    }

    void Execute(Orange::Engine::Render::RenderPassContext& ctx) override
    {
        // 提交水面 mesh + 折射 sample
    }
};

app.GetPipeline().InsertPass(PipelineStage::Transparent, std::make_unique<WaterPass>());
```

### 约束
- IRenderPass 不直接接触 OrangeRender 的 RHI 类型；引擎提供 `RenderGraphBuilder` 与 `RenderPassContext` 作为隔离层
- Phase 3 的 Pipeline 抽象保留 `InsertPass` 接口签名但实现为 `assert(false, "not implemented before Phase 5")`
- 第一款游戏在 Phase 5 之前**不允许**使用此扩展点

---

## 5. Animation 后端注册（Phase 4 / Task 01 启用）

### 设计思路
引擎内置 Skeletal（DragonBones）+ Procedural 两个后端。游戏侧（例如未来引入 Spine、Live2D、自写 mesh deformation 等）通过 `AnimatorRegistry::RegisterBackend` 不修改引擎源码地接入自己的 backend。

### API
```cpp
// include/orange/engine/animation/IAnimator.h
namespace Orange::Engine::Animation
{

class IAnimator
{
public:
    virtual ~IAnimator() = default;
    virtual void             Tick(float dt)                      = 0;
    virtual bool             IsFinished() const noexcept         = 0;
    virtual std::string_view BackendName() const noexcept        = 0;
};

}

// include/orange/engine/animation/AnimatorRegistry.h
namespace Orange::Engine::Animation
{

class AnimatorRegistry
{
public:
    using FactoryFn = std::function<std::unique_ptr<IAnimator>()>;

    Result<void, ResultCode> RegisterBackend(std::string_view name, FactoryFn factory);
    std::unique_ptr<IAnimator> Create(std::string_view name) const;
    bool                       HasBackend(std::string_view name) const noexcept;
};

}
```

### 游戏侧使用
```cpp
// 假设 Phase 4 之后游戏决定加入 Spine
class SpineAnimator : public Orange::Engine::Animation::IAnimator
{
public:
    explicit SpineAnimator(const SkeletonAsset& asset) { /* ... */ }
    void             Tick(float dt) override            { /* ... */ }
    bool             IsFinished() const noexcept override { return false; }
    std::string_view BackendName() const noexcept override { return "spine"; }
};

Animation::AnimatorRegistry registry;
registry.RegisterBackend("spine", [&] {
    return std::make_unique<SpineAnimator>(spineSkeletonAsset);
});

auto inst = registry.Create("spine");
// 挂到 entity 上：
world.AddComponent(entity, Animation::AnimatorComponent{ std::move(inst) });
```

### 约束
- `IAnimator` 抽象**不**规定 pose / uniform 输出 API——Skeletal backend 通过 SkeletonAsset palette + Render 模块的 SkinningMatrixPalette uniform 路径，Procedural backend 通过 `MaterialInstance::SetUniform`，两条路径**通过 ECS 自然解耦**
- backend factory 需要参数（资产 / 初始动画名）时由调用方在闭包里 capture，registry 不增加 args 形参——避免 `std::any` 之类传染性类型走到公共面
- 同名重复注册返回 `ResultCode::AlreadyExists`，表内不被覆盖
- AnimationStateMachine（与 backend 解耦的 flat-weighted FSM）由调用方独立持有，状态切换时在 OnEnter 回调里调 backend 的 Play(animName) 即可——不需要在 IAnimator 上加 state-machine 字段

---

## 6. 模块按需启用（EngineConfig::modules）

### 设计思路
不是所有应用都需要全部模块。编辑器工具可能不需要 Audio；纯渲染 demo 可能不需要 Physics。让模块通过 flag 选择性启用。

### API
```cpp
// include/orange/engine/app/AppConfig.h
namespace Orange::Engine
{

enum class ModuleFlags : uint32_t
{
    None        = 0,
    Render      = 1 << 0,
    Animation   = 1 << 1,
    Physics     = 1 << 2,
    Audio       = 1 << 3,
    Input       = 1 << 4,

    All = Render | Animation | Physics | Audio | Input
};

struct AppConfig
{
    std::string  mAppName{"OrangeEngine App"};
    uint32_t     mWindowWidth{1280};
    uint32_t     mWindowHeight{720};
    ModuleFlags  mModules{ModuleFlags::All};
};

}  // namespace Orange::Engine
```

### 使用
```cpp
Orange::Engine::AppConfig cfg;
cfg.mModules = ModuleFlags::Render | ModuleFlags::Input;  // 编辑器工具不需要 Physics/Audio
Orange::Engine::AppHost app(cfg);
```

### 约束
- 未启用的模块不应被任何代码引用；启用 flag 控制的是 AppHost 内部模块构造，而不是头文件可见性
- `Render` 是隐含必选——窗口存在就要有渲染（即便只是清屏）

---

## 7. 数据驱动配置

### Action 配置（Phase 4 启用）
```jsonc
// assets/input/default.actions.json
{
    "version": 1,
    "actions": [
        { "name": "jump",       "bindings": ["Key.Space", "Gamepad.A"] },
        { "name": "transform",  "bindings": ["Key.E", "Gamepad.X"] },
        { "name": "attack",     "bindings": ["Mouse.Left", "Gamepad.RT"] }
    ]
}
```

### Scene 配置（Phase 5 启用）
JSON schema 化，schema_version 字段；引擎只反序列化内置组件，未知组件由游戏侧提供 reader 注册（Phase 5 末尾决定是否暴露）。

### Material 配置（Phase 3 启用）
```jsonc
// assets/materials/slime_default.material
{
    "template": "slime_fresnel",
    "uniforms": {
        "uRimColor": [0.4, 0.8, 1.0],
        "uNoiseAmplitude": 0.05
    },
    "textures": {
        "uInnerNoise": "textures/inner_noise.png"
    }
}
```

### EngineConfig（启动配置，TOML 或 INI）
启动 prefs：分辨率、vsync、log level 等。游戏可叠加自己的 config 文件。

---

## 8. Save Game 注册（Phase 5.5 启用）

### 设计思路
存档系统专门解决"玩家产出 + 跨引擎升级兼容"问题，与 Scene 序列化（关卡 content，跟随版本走）解耦。游戏侧手写每个可参与存档的 component 的 read/write 函数，注册到 `SaveGameRegistry`。引擎负责原子写入、CRC 校验、平台路径解析、跨版本 migrator 调度。

### API
```cpp
// include/orange/engine/save/SaveGameRegistry.h
namespace Orange::Engine::Save
{

template <typename Component>
struct ComponentSaveAdapter
{
    using ReadFn  = void (*)(JsonReader&, Component&);
    using WriteFn = void (*)(JsonWriter&, const Component&);
    ReadFn  mRead;
    WriteFn mWrite;
    SchemaVersion mVersion;
};

class ORANGE_ENGINE_API SaveGameRegistry
{
public:
    template <typename Component>
    void Register(const std::string& tag, ComponentSaveAdapter<Component> adapter);

    // Migrator: 从 old 版本读出的中间态升级到 current 版本
    using MigratorFn = void (*)(JsonReader& oldData, JsonWriter& newData);
    void RegisterMigrator(const std::string& tag, SchemaVersion from, SchemaVersion to, MigratorFn fn);
};

class ORANGE_ENGINE_API SaveGameSystem
{
public:
    Result<void> Save(World& world, std::string_view slotName);
    Result<void> Load(World& world, std::string_view slotName);
    std::vector<SlotMetadata> ListSlots() const;
};

}  // namespace Orange::Engine::Save
```

### 游戏侧使用
```cpp
// 游戏定义可存档的 component 与其 read/write
struct PlayerProgress
{
    int mLevel{1};
    std::vector<std::string> mUnlockedForms;  // 已吞噬的 boss 形态名
    Vec2 mLastCheckpoint;
};

void Read(Orange::Engine::JsonReader& r, PlayerProgress& p)
{
    r.Read("level", p.mLevel);
    r.Read("unlockedForms", p.mUnlockedForms);
    r.Read("lastCheckpoint", p.mLastCheckpoint);
}

void Write(Orange::Engine::JsonWriter& w, const PlayerProgress& p)
{
    w.Write("level", p.mLevel);
    w.Write("unlockedForms", p.mUnlockedForms);
    w.Write("lastCheckpoint", p.mLastCheckpoint);
}

// 注册到引擎
app.GetSaveRegistry().Register<PlayerProgress>(
    "PlayerProgress",
    {&Read, &Write, SchemaVersion{"PlayerProgress", 1, 0}});

// 触发存档 / 读档
app.GetSaveSystem().Save(world, "slot1");
app.GetSaveSystem().Load(world, "slot1");
```

### 跨版本兼容
```cpp
// 1.0 → 2.0 加了一个字段
struct PlayerProgressV2
{
    int  mLevel{1};
    std::vector<std::string> mUnlockedForms;
    Vec2 mLastCheckpoint;
    int  mDeathCount{0};   // 新加
};

// 注册 migrator
app.GetSaveRegistry().RegisterMigrator(
    "PlayerProgress",
    SchemaVersion{"PlayerProgress", 1, 0},
    SchemaVersion{"PlayerProgress", 2, 0},
    [](Orange::Engine::JsonReader& oldData, Orange::Engine::JsonWriter& newData) {
        // 从 1.0 读出，写到 2.0；deathCount 默认为 0
        int level; std::vector<std::string> forms; Vec2 cp;
        oldData.Read("level", level);
        oldData.Read("unlockedForms", forms);
        oldData.Read("lastCheckpoint", cp);
        newData.Write("level", level);
        newData.Write("unlockedForms", forms);
        newData.Write("lastCheckpoint", cp);
        newData.Write("deathCount", 0);
    });
```

### 约束
- read/write 函数必须**完全手写**（项目级反射策略；见越界禁令）
- 任何可存档类型必须声明 `SchemaVersion`，read 路径强制校验
- Component 的 read/write 一旦发布到玩家手里，原版本就**永远不能改**——只能新建版本 + migrator
- 不允许在 read/write 内做 IO / 网络 / 任意阻塞调用

---

## 越界禁令（Hard Constraints）

下列限制是**项目级 invariant**，违反会被当作架构 bug 处理：

| 禁令 | 范围 |
| --- | --- |
| 公共头不出现 OrangeRender 类型 | `include/orange/engine/**/*.h` 不允许 `#include <orange/...>`（OrangeRender 头） |
| 公共头不出现 Box2D 类型 | 不允许 `#include <box2d/...>` 出现在公共头 |
| 公共头不出现 DragonBones 类型 | 不允许 `#include <dragonBones/...>` 出现在公共头 |
| 公共头不出现 miniaudio 类型 | 不允许 `#include "miniaudio.h"` 出现在公共头 |
| 公共头不出现 Vulkan 类型 | 不允许 `#include <vulkan/...>` 出现在公共头 |
| OrangeRender 头只在 Render 模块出现 | `<orange/...>` 仅允许在 `src/render/**` |
| Box2D 头只在 Box2D 适配层出现 | `<box2d/...>` 仅允许在 `src/physics/box2d/**` |
| DragonBones 头只在 DragonBones 适配层出现 | `<dragonBones/...>` 仅允许在 `src/animation/dragonbones/**` |
| miniaudio 头只在 miniaudio 适配层出现 | `"miniaudio.h"` 仅允许在 `src/audio/miniaudio/**` |
| 引擎不引入游戏特定概念 | 引擎不出现 "Slime"、"Boss"、"Player"、"Form" 等游戏术语 |
| **序列化必须手写** | Phase 1–6 不允许引入运行时反射库（`entt::meta` / RTTR / cereal 反射）或 AST codegen；所有 read/write 函数必须手写并显式注册 |
| **不允许裸用 `nlohmann::json`** | 模块代码不允许直接调用 `nlohmann::json` API；必须经由 `Core::Serialization` 提供的 `JsonReader` / `JsonWriter`（公共头不暴露 `nlohmann::json` 类型也是同一约束的等价表述） |
| **每个可序列化类型必须声明 SchemaVersion** | read 路径必须先校验 schema_version，namespace 不匹配或 major 版本不兼容立刻返回错误，不允许"尽力而为地解析" |
| **已发布的 schema 版本不可修改** | 任何 schema 一旦在 release 版本中暴露给玩家（用于 Save Game）或暴露给游戏仓库（用于 Scene），原版本永不变更——只能新建下一个版本 + migrator |

这些禁令在 PR 评审 / CLAUDE.md / 代码评审清单中需明确出现，违反则需先修复后合并。
