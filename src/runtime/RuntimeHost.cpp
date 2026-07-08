// RuntimeHost 实现 —— 见 RuntimeHost.h。发布 runtime 宿主的 window 编排：
// 建窗 + 装配 + 主循环，装配逻辑全部经引擎公共 API（零 <orange/...> RHI 直接
// include，同 spike 瘦 main），Play 装配复用引擎层 PlayAssembly helper（与编辑器
// 宿主同源）。

#include <orange/engine/runtime/RuntimeHost.h>

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>

#include <orange/engine/animation/AnimationClip.h>
#include <orange/engine/animation/AnimationClipLoader.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/asset/PrefabAsset.h>
#include <orange/engine/asset/PrefabLoader.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/asset/SkeletonAsset.h>
#include <orange/engine/asset/SkeletonLoader.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/asset/SoundLoader.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/asset/TextureLoader.h>

#include <orange/engine/audio/AudioEngine.h>
#include <orange/engine/audio/SoundInstance.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/game/GameModuleHost.h>
#include <orange/engine/game/IGameModule.h>
#include <orange/engine/game/PlayAssembly.h>
#include <orange/engine/physics/PhysicsWorld.h>
#include <orange/engine/platform/Window.h>
#include <orange/engine/platform/WindowEvent.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/World.h>

#include <glm/vec2.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Orange::Engine::Runtime
{

    namespace
    {

        // 非拥有 IGameModule 包装 —— GameModuleHost::AddModule 要 unique_ptr（取所有权），
        // 但 Run 的 module 由调用方拥有。本适配器由 host 拥有（host 析构时删它），只**引用**
        // 被包装的 module（绝不删 module），扇出全部透传。这样两宿主复用 GameModuleHost
        // 的同一套扇出 / play-state 护栏。
        class NonOwningModuleAdapter final : public Game::IGameModule
        {
        public:
            explicit NonOwningModuleAdapter(Game::IGameModule& module) : mModule(module) {}

            const char* Name() const noexcept override { return mModule.Name(); }

            void RegisterRenderPasses(Render::Pipeline& pipeline) override
            {
                mModule.RegisterRenderPasses(pipeline);
            }

            void UnregisterRenderPasses(Render::Pipeline& pipeline) override
            {
                mModule.UnregisterRenderPasses(pipeline);
            }

            std::span<const Scene::ComponentSerializerEntry> ComponentSerializers() const override
            {
                return mModule.ComponentSerializers();
            }

            void OnEnterPlay(Game::GameModuleContext& ctx) override { mModule.OnEnterPlay(ctx); }

            bool WantsOwnPhysicsStep() const noexcept override
            {
                return mModule.WantsOwnPhysicsStep();
            }

            void Tick(Game::GameModuleContext& ctx, float dt) override { mModule.Tick(ctx, dt); }

            void OnEvent(const Platform::WindowEvent& event) override { mModule.OnEvent(event); }

            void OnExitPlay(Game::GameModuleContext& ctx) override { mModule.OnExitPlay(ctx); }

        private:
            Game::IGameModule& mModule;
        };

        // 内部主循环 Layer：每帧同步 framebuffer resize、推进 simulation、渲染。
        // 与编辑器 EditorRenderLayer 的 OnUpdate 对偶（Play 态），但走纯引擎公共面。
        class RuntimeLayer final : public Layer
        {
        public:
            RuntimeLayer(Game::GameModuleHost&    host,
                         Game::GameModuleContext  ctx,
                         World&                   world,
                         Physics::PhysicsWorld&   physics,
                         Render::Pipeline&        pipeline)
                : Layer("RuntimeLayer"), mHost(host), mCtx(ctx), mWorld(world), mPhysics(physics), mPipeline(pipeline)
            {
            }

            void OnUpdate(const FrameContext& frame) override
            {
                const float dt = static_cast<float>(frame.time.deltaSeconds);

                // 时间驱动 shader（dissolve / 流光等）拿单调递增帧时间。
                mPipeline.SetFrameTime(static_cast<float>(frame.time.totalSeconds));

                // module Tick → physics → animators 扇出，与编辑器同源（vfx 见 RuntimeHost
                // 头注：window 模式暂不 init VfxSystem，传 nullptr；无 preStepHook，
                // ApplyLayerVisibility 是编辑器独有）。
                Game::StepSimulation(mHost, mCtx, mWorld, &mPhysics, /*vfx=*/nullptr, dt);

                mPipeline.Render(mWorld);
            }

            bool OnEvent(const Platform::WindowEvent& event) override
            {
                // 窗口 resize → 通知 Pipeline 重建 swap-chain（引擎公共面，免 GLFW 依赖）。
                if (const auto* resize = std::get_if<Platform::WindowResizeEvent>(&event))
                {
                    mPipeline.OnResize(resize->width, resize->height);
                }
                // 视口输入转发给游戏模块（发布态永远聚焦，无编辑器 fly-cam 争用）。
                mHost.OnEvent(event);
                return false;
            }

        private:
            Game::GameModuleHost&   mHost;
            Game::GameModuleContext mCtx;
            World&                  mWorld;
            Physics::PhysicsWorld&  mPhysics;
            Render::Pipeline&       mPipeline;
        };

        // 注册发布 runtime 需要的资产 loader —— 与编辑器 InitializeEditorAssets /
        // RegisterImportLoaders 装配对齐（Shader / Mesh / Texture / AnimationClip /
        // Skeleton / Sound / Prefab）。失败仅 log，不阻断启动。
        void RegisterRuntimeLoaders(Asset::AssetRegistry& assets)
        {
            using namespace Orange::Engine::Asset;
            namespace Anim = Orange::Engine::Animation;

            auto check = [](auto res, const char* name)
            {
                if (res.IsErr())
                {
                    ORANGE_LOG_ERROR("[OrangeRuntime] RegisterLoader<{}> 失败 (code={})",
                                     name, static_cast<unsigned>(res.Error()));
                }
            };

            check(assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>()),
                  "ShaderAsset");
            check(assets.RegisterLoader<MeshAsset>(std::make_unique<MeshLoader>()),
                  "MeshAsset");
            check(assets.RegisterLoader<TextureAsset>(std::make_unique<TextureLoader>()),
                  "TextureAsset");
            check(assets.RegisterLoader<Anim::AnimationClip>(
                      std::make_unique<Anim::AnimationClipLoader>()),
                  "AnimationClip");
            check(assets.RegisterLoader<SkeletonAsset>(std::make_unique<SkeletonLoader>()),
                  "SkeletonAsset");
            check(assets.RegisterLoader<SoundAsset>(std::make_unique<SoundLoader>()),
                  "SoundAsset");
            check(assets.RegisterLoader<PrefabAsset>(std::make_unique<PrefabLoader>()),
                  "PrefabAsset");
        }

    } // namespace

    int Run(Game::IGameModule& module, const RuntimeConfig& config)
    {
        // ---- cwd 定位（相对路径资产 IO 基准）--------------------------------
        if (!config.assetRoot.empty())
        {
            std::error_code ec;
            std::filesystem::current_path(config.assetRoot, ec);
            if (ec)
            {
                ORANGE_LOG_WARN("[OrangeRuntime] chdir 到 assetRoot 失败：{}（code={}）",
                                config.assetRoot, ec.value());
            }
        }

        // ---- AppHost（窗口 + 主循环）---------------------------------------
        AppConfig cfg{};
        cfg.window.title  = config.windowTitle;
        cfg.window.width  = config.windowWidth;
        cfg.window.height = config.windowHeight;
        auto hostRes      = AppHost::Create(cfg);
        if (hostRes.IsErr())
        {
            ORANGE_LOG_ERROR("[OrangeRuntime] AppHost::Create 失败 (code={})",
                             static_cast<unsigned>(hostRes.Error()));
            return 1;
        }
        auto host = std::move(hostRes).Value();

        // ---- AssetRegistry + loaders + MaterialSystem ----------------------
        Asset::AssetRegistry assets;
        RegisterRuntimeLoaders(assets);

        Render::MaterialSystem materials(assets);
        if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
        {
            ORANGE_LOG_WARN("[OrangeRuntime] MaterialSystem::RegisterBuiltins 失败 (code={}) "
                            "—— 场景几何可能不显示",
                            static_cast<unsigned>(rb.Error()));
        }

        // ---- GameModuleHost：非拥有包装 module + 收集 serializer ------------
        Game::GameModuleHost gameModules;
        gameModules.AddModule(std::make_unique<NonOwningModuleAdapter>(module));
        // module serializer 让含游戏组件的场景可 Load round-trip（必须在 Scene::Load 前）。
        const std::vector<Scene::ComponentSerializerEntry> extraSerializers =
            gameModules.CollectSerializers();

        // ---- World + PhysicsWorld + Scene::Load ----------------------------
        World world;

        Physics::PhysicsWorldDesc physDesc{};
        physDesc.gravity = glm::vec2(config.gravity[0], config.gravity[1]);
        Physics::PhysicsWorld physics(physDesc);

        if (!config.startupScene.empty())
        {
            Scene::LoadOptions loadOpts{};
            loadOpts.assetRegistry   = &assets;
            loadOpts.extraSerializers = extraSerializers;
            // 刻意**不**传 loadOpts.physicsWorld —— 物理 body 由下方 PopulatePhysicsFromWorld
            // 统一装配（与编辑器 EnterPlay 同源），避免两条建 body 路径漂移。
            if (auto r = Scene::Load(config.startupScene, world, loadOpts); r.IsErr())
            {
                ORANGE_LOG_WARN("[OrangeRuntime] 启动场景加载失败：{} (code={}) —— 起空世界",
                                config.startupScene, static_cast<unsigned>(r.Error()));
            }
        }

        // 世界 seed 钩子（authoring / 程序化关卡）。场景加载后、进 Play 前调一次。
        if (config.onSeedWorld)
        {
            config.onSeedWorld(world);
        }

        // ---- Pipeline（window 模式）----------------------------------------
        Render::Pipeline pipeline;
        if (auto r = pipeline.Initialize(host->GetWindow(), assets); r.IsErr())
        {
            ORANGE_LOG_ERROR("[OrangeRuntime] Pipeline::Initialize 失败 (code={})",
                             static_cast<unsigned>(r.Error()));
            return 1;
        }
        pipeline.SetMaterialSystem(&materials);
        pipeline.SetSceneClearColor(config.clearColor[0], config.clearColor[1], config.clearColor[2]);
        pipeline.SetSkyEnabled(config.skyEnabled);

        // 注册期扇出：module 自定义 pass 接进帧路径（Edit 态即生效，故在 Play 前）。
        gameModules.RegisterRenderPasses(pipeline);

        // ---- 物理装配同源 --------------------------------------------------
        Game::PopulatePhysicsFromWorld(world, physics);

        // ---- Audio + AudioSource 实例化同源 --------------------------------
        Audio::AudioEngineDesc audioDesc{};
        audioDesc.useNullBackend = config.useNullAudioBackend;
        Audio::AudioEngine audio(audioDesc);
        std::unordered_map<Entity, std::unique_ptr<Audio::SoundInstance>> soundInstances;
        if (audio.IsInitialized())
        {
            Game::InstantiateAudioSources(world, audio, assets, soundInstances);
        }

        // ---- module EnterPlay（装配序全部就绪后扇出）----------------------
        Game::GameModuleContext ctx{};
        ctx.pWorld    = &world;
        ctx.pPhysics  = &physics;
        ctx.pAssets   = &assets;
        ctx.pPipeline = &pipeline;
        gameModules.EnterPlay(ctx);

        // ---- 可选：引擎托管 ImGui overlay（游戏侧 debug UI）----------------
        if (config.enableDebugImGui)
        {
            if (auto im = pipeline.EnableImGui(); im.IsErr())
            {
                ORANGE_LOG_WARN("[OrangeRuntime] Pipeline::EnableImGui 失败 (code={})",
                                static_cast<unsigned>(im.Error()));
            }
            else
            {
                pipeline.SetImGuiSubmit([h = host.get()]()
                                        { h->DispatchImGui(); });
            }
        }

        // ---- 主循环 --------------------------------------------------------
        host->PushLayer(std::make_unique<RuntimeLayer>(gameModules, ctx, world, physics, pipeline));
        const int rc = host->Run();

        // ---- 关停：module ExitPlay（pipeline/physics 仍活）→ Pipeline.Shutdown
        //      （清 module pass，此刻 module 代码仍加载）。soundInstances 在函数
        //      返回时先于 audio 析构（声明序），满足 SoundInstance 先于 AudioEngine
        //      释放的契约。
        gameModules.ExitPlay(ctx);
        pipeline.Shutdown();
        return rc;
    }

} // namespace Orange::Engine::Runtime
