#ifndef ORANGE_ENGINE_GAME_GAME_MODULE_HOST_H
#define ORANGE_ENGINE_GAME_GAME_MODULE_HOST_H

// ---------------------------------------------------------------------------
// GameModuleHost —— 持有并驱动一组 IGameModule 的可复用宿主核心（ADR-021）。
//
// 编辑器宿主（ApplyPendingPlayOp）与发布 runtime 宿主共用本类：注册期扇出
// RegisterRenderPasses / 收集 serializers；Play 生命周期扇出 EnterPlay /
// Tick / OnEvent / ExitPlay。ExitPlay 按**逆序**扇出（后进先出，析构式对称）。
//
// 本类只做扇出 + play-state 护栏，不碰 Pipeline / World 的具体类型（全部
// 透传引用），故 header-only：Tick 前必须 EnterPlay，重复 EnterPlay / 未
// EnterPlay 的 Tick / ExitPlay 均被护栏 no-op，避免宿主状态机 bug 传导成
// 模块的双初始化 / 空跑。
// ---------------------------------------------------------------------------

#include <orange/engine/game/IGameModule.h>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace Orange::Engine::Game
{

    class GameModuleHost
    {
    public:
        // 注册一个模块，返回非拥有裸指针（供宿主后续喂数据；所有权留 host）。
        // nullptr 静默忽略，返回 nullptr。
        IGameModule* AddModule(std::unique_ptr<IGameModule> module)
        {
            if (!module)
            {
                return nullptr;
            }
            IGameModule* raw = module.get();
            mModules.push_back(std::move(module));
            return raw;
        }

        std::size_t ModuleCount() const noexcept
        {
            return mModules.size();
        }

        bool IsInPlay() const noexcept
        {
            return mInPlay;
        }

        // ---- 注册期扇出（宿主启动即调）----------------------------------

        void RegisterRenderPasses(Render::Pipeline& pipeline)
        {
            for (auto& m : mModules)
            {
                m->RegisterRenderPasses(pipeline);
            }
        }

        // 收集所有模块的 serializer 条目 flatten 成一份连续 vector，宿主拿它
        // 的 data()/size() 喂 Scene::Save/LoadOptions.extraSerializers。返回值
        // 须活到 Save/Load 调用结束。
        std::vector<Scene::ComponentSerializerEntry> CollectSerializers() const
        {
            std::vector<Scene::ComponentSerializerEntry> out;
            for (auto& m : mModules)
            {
                for (const auto& e : m->ComponentSerializers())
                {
                    out.push_back(e);
                }
            }
            return out;
        }

        // 任一模块自管物理 step 即返回 true（宿主据此让位自动 step）。
        bool AnyWantsOwnPhysicsStep() const noexcept
        {
            for (auto& m : mModules)
            {
                if (m->WantsOwnPhysicsStep())
                {
                    return true;
                }
            }
            return false;
        }

        // ---- Play 生命周期扇出（护栏保证配对）----------------------------

        // 进 Play：已在 Play 中则 no-op（护栏，防重复初始化）。
        void EnterPlay(GameModuleContext& ctx)
        {
            if (mInPlay)
            {
                return;
            }
            mInPlay = true;
            for (auto& m : mModules)
            {
                m->OnEnterPlay(ctx);
            }
        }

        // 推进：未在 Play 中则 no-op（护栏，防 edit 态误 tick）。
        void Tick(GameModuleContext& ctx, float dt)
        {
            if (!mInPlay)
            {
                return;
            }
            for (auto& m : mModules)
            {
                m->Tick(ctx, dt);
            }
        }

        // 输入路由：仅 Play 期转发（edit 态输入归编辑器 fly-cam / gizmo）。
        void OnEvent(const Platform::WindowEvent& event)
        {
            if (!mInPlay)
            {
                return;
            }
            for (auto& m : mModules)
            {
                m->OnEvent(event);
            }
        }

        // 退 Play：未在 Play 中则 no-op。逆序扇出 OnExitPlay（后进先出）。
        void ExitPlay(GameModuleContext& ctx)
        {
            if (!mInPlay)
            {
                return;
            }
            for (auto it = mModules.rbegin(); it != mModules.rend(); ++it)
            {
                (*it)->OnExitPlay(ctx);
            }
            mInPlay = false;
        }

    private:
        std::vector<std::unique_ptr<IGameModule>> mModules;
        bool                                      mInPlay{false};
    };

} // namespace Orange::Engine::Game

#endif // ORANGE_ENGINE_GAME_GAME_MODULE_HOST_H
