// PlayAssembly 实现 —— 见 PlayAssembly.h。三段逐字抽自 OrangeEditor 宿主
// （EditorRenderLayer.cpp 的 EnterPlay S3 物理装配 / StepSimulationOnce 扇出 /
// EnterPlay audio 装配），行为不变，两宿主共用。

#include <orange/engine/game/PlayAssembly.h>

#include <orange/engine/animation/AnimationSystem.h> // TickAnimators
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/audio/AudioEngine.h>
#include <orange/engine/audio/AudioSourceComponent.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/PhysicsWorld.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/render/VfxSystem.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace Orange::Engine::Game
{

    void PopulatePhysicsFromWorld(World& world, Physics::PhysicsWorld& physics)
    {
        auto& reg = world.Registry();
        using TC  = Orange::Engine::Scene::TransformComponent;
        using namespace Orange::Engine::Physics;
        for (auto e : reg.view<RigidBodyComponent, ColliderComponent>())
        {
            auto&       rb = reg.get<RigidBodyComponent>(e);
            auto&       cc = reg.get<ColliderComponent>(e);
            const auto* tc = reg.try_get<TC>(e);
            if (tc != nullptr)
            {
                rb.initialPosition = glm::vec2(tc->position.x, tc->position.y);
                // glm::eulerAngles 返回 (pitch, yaw, roll) 弧度；2D 平面物理只用 Z 轴旋转（roll）
                const glm::vec3 euler = glm::eulerAngles(tc->rotation);
                rb.initialAngle       = euler.z;
            }
            const BodyHandle h = physics.AddBody(rb, cc);
            rb.handle          = h;
        }
    }

    void StepSimulation(GameModuleHost&              host,
                        GameModuleContext&           ctx,
                        World&                       world,
                        Physics::PhysicsWorld*       physics,
                        Render::VfxSystem*           vfx,
                        float                        dt,
                        const std::function<void()>& preStepHook)
    {
        // PIE 游戏模块 Tick（ADR-021）—— **先于**宿主 physics step 扇出。模块自管固定
        // 步长（spike-01 现状），dt 为宿主帧步。空 host（无注册模块）时护栏 no-op。
        host.Tick(ctx, dt);

        // Physics step → 把 dynamic body 新位姿写回 ECS Transform。任一模块
        // WantsOwnPhysicsStep 时宿主让位——不自动 Step，写回也交模块（ADR-021）。
        if (physics != nullptr && !host.AnyWantsOwnPhysicsStep())
        {
            // preStepHook：编辑器在此注入 layer 可见性 → body enable/disable（partition
            // 是编辑器概念，见 PlayAssembly.h）；runtime 传空。在 Step 之前调。
            if (preStepHook)
            {
                preStepHook();
            }
            physics->Step(dt);
            auto& reg = world.Registry();
            using TC  = Orange::Engine::Scene::TransformComponent;
            using namespace Orange::Engine::Physics;
            for (auto e : reg.view<RigidBodyComponent>())
            {
                auto& rb = reg.get<RigidBodyComponent>(e);
                if (rb.type == BodyType::Static)
                {
                    continue;
                }
                if (!physics->IsValid(rb.handle))
                {
                    continue;
                }
                const BodyTransform xf = physics->GetBodyTransform(rb.handle);
                auto*               tc = reg.try_get<TC>(e);
                if (tc != nullptr)
                {
                    tc->position.x = xf.position.x;
                    tc->position.y = xf.position.y;
                    // 2D 物理只有 Z 轴旋转，直接从角度重建 quat
                    tc->rotation = glm::quat(glm::vec3(0.0f, 0.0f, xf.angle));
                }
            }
        }

        // Particle emitter tick
        if (vfx != nullptr)
        {
            vfx->Tick(world, dt);
            // 诊断：每秒打一次粒子计数，确认 sim 是否正常运行
            static float sDiagTimer = 0.0f;
            sDiagTimer += dt;
            if (sDiagTimer >= 1.0f)
            {
                sDiagTimer = 0.0f;
                ORANGE_LOG_DEBUG("[vfx-diag] live particles: {}",
                                 vfx->TotalLiveParticleCount());
            }
        }

        // Animator tick —— 走引擎层 Animation::TickAnimators（单一真相源）。
        Orange::Engine::Animation::TickAnimators(world, dt);
    }

    void InstantiateAudioSources(
        World&                                                             world,
        Audio::AudioEngine&                                                audioEngine,
        Asset::AssetRegistry&                                              assets,
        std::unordered_map<Entity, std::unique_ptr<Audio::SoundInstance>>& outInstances)
    {
        using namespace Orange::Engine::Audio;
        using namespace Orange::Engine::Asset;
        auto& reg = world.Registry();
        for (auto e : reg.view<AudioSourceComponent>())
        {
            auto& as = reg.get<AudioSourceComponent>(e);
            if (!as.sound.IsValid())
            {
                continue;
            }
            const auto* pSoundAsset = assets.Get<SoundAsset>(as.sound);
            if (pSoundAsset == nullptr)
            {
                continue;
            }
            auto inst = audioEngine.CreateInstance(*pSoundAsset);
            if (!inst.IsValid())
            {
                continue;
            }
            inst.SetVolume(as.volume);
            if (as.playOnAwake)
            {
                inst.Start();
            }
            Orange::Engine::Entity eWrap{static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(e))};
            outInstances[eWrap] = std::make_unique<SoundInstance>(std::move(inst));
        }
    }

} // namespace Orange::Engine::Game
