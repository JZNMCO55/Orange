#ifndef ORANGE_ENGINE_GAME_PLAY_ASSEMBLY_H
#define ORANGE_ENGINE_GAME_PLAY_ASSEMBLY_H

// ---------------------------------------------------------------------------
// PlayAssembly —— 编辑器宿主与发布 runtime 宿主共用的 Play 装配同源 helper
// （ADR-021 / PIE M10）。
//
// 背景：编辑器宿主（EditorRenderLayer::ApplyPendingPlayOp / StepSimulationOnce）
// 与发布 runtime 宿主（RuntimeHost）都要做同一套「进 Play 装配 + 每帧推进」逻辑：
// 从 World 建物理 body、每帧 module Tick → physics → vfx → animators 扇出、
// 实例化 AudioSource。若两宿主各写一份，就会漂移出「编辑器能跑、发布行为不同」的
// 缺口。本文件把这三段逐字抽成引擎层自由函数，两宿主共用 → 单一真相源。
//
// 刻意**不含** ApplyLayerVisibility（partition 是编辑器 EditorSceneContext 概念，
// runtime 无）——StepSimulation 经可选 preStepHook 让编辑器注入它，runtime 传空。
//
// 头隔离：只 include 无第三方依赖的引擎公共头（GameModuleHost / Entity /
// SoundInstance）+ 前向声明重量级类型，不碰任何 RHI / vulkan / box2d 头。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/audio/SoundInstance.h>
#include <orange/engine/game/GameModuleHost.h>
#include <orange/engine/scene/Entity.h>

#include <functional>
#include <memory>
#include <unordered_map>

namespace Orange::Engine
{
    class World;
}

namespace Orange::Engine::Physics
{
    class PhysicsWorld;
}

namespace Orange::Engine::Render
{
    class VfxSystem;
}

namespace Orange::Engine::Audio
{
    class AudioEngine;
}

namespace Orange::Engine::Asset
{
    class AssetRegistry;
}

namespace Orange::Engine::Game
{

    // 从 World 建物理 body（EnterPlay 物理装配同源）。遍历同时挂 RigidBody +
    // Collider 的 entity，从 TransformComponent 填 rb.initialPosition /
    // initialAngle（2D 只取 Z 轴旋转），AddBody 进 physics，handle 反写回 ECS。
    //
    // 宿主负责先构造 physics（编辑器 make_unique<PhysicsWorld>()，runtime 值成员），
    // 本函数只做遍历 + AddBody + 反写——不取所有权、不 Step。
    ORANGE_ENGINE_API void PopulatePhysicsFromWorld(World& world, Physics::PhysicsWorld& physics);

    // 推进一帧 simulation（Play tick 扇出同源）。扇出序（ADR-021）：
    //   module Tick（先于 physics）→ physics（任一模块自管物理则宿主让位）→
    //   vfx → animators。
    //
    // preStepHook：可选，在 physics->Step **之前**、physics 分支内调一次。编辑器
    // 传 ApplyLayerVisibility lambda（partition 可见性 → body enable/disable），
    // runtime 传空（无 partition 概念）。physics == nullptr 或任一模块
    // WantsOwnPhysicsStep 时跳过 physics 分支（连带跳过 preStepHook）。
    // vfx == nullptr 时跳过粒子 tick（runtime window 模式暂不 init VfxSystem）。
    ORANGE_ENGINE_API void StepSimulation(GameModuleHost&              host,
                                          GameModuleContext&           ctx,
                                          World&                       world,
                                          Physics::PhysicsWorld*       physics,
                                          Render::VfxSystem*           vfx,
                                          float                        dt,
                                          const std::function<void()>& preStepHook = {});

    // Edit→Play 实例化所有挂 AudioSourceComponent 的实体的 SoundInstance
    // （EnterPlay audio 装配同源）。playOnAwake=true 即刻 Start，volume 应用；
    // 结果填进宿主持有的 outInstances 表（编辑器 mEntityToSoundInstance /
    // runtime 自己的表）。宿主应在调用前自行 gate audioEngine.IsInitialized()。
    ORANGE_ENGINE_API void InstantiateAudioSources(
        World&                                                             world,
        Audio::AudioEngine&                                                audioEngine,
        Asset::AssetRegistry&                                              assets,
        std::unordered_map<Entity, std::unique_ptr<Audio::SoundInstance>>& outInstances);

} // namespace Orange::Engine::Game

#endif // ORANGE_ENGINE_GAME_PLAY_ASSEMBLY_H
