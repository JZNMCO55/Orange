#ifndef ORANGE_ENGINE_GAME_GAME_MODULE_HOST_H
#define ORANGE_ENGINE_GAME_GAME_MODULE_HOST_H

// ---------------------------------------------------------------------------
// GameModuleHost —— 持有并驱动一组 IGameModule 的可复用宿主核心（ADR-021）。
//
// 编辑器宿主（ApplyPendingPlayOp）与发布 runtime 宿主共用本类：注册期扇出
// RegisterRenderPasses / 收集 serializers；Play 生命周期扇出 EnterPlay /
// Tick / OnEvent / ExitPlay。ExitPlay 按**逆序**扇出（后进先出，析构式对称）。
//
// 模块两种来源，统一驱动（M7 DLL 宿主，ADR-023）：
//   * 静态模块（AddModule）：per-game editor 静态链入的 IGameModule，宿主经
//     unique_ptr<IGameModule> 拥有。
//   * DLL 模块（AddModuleLibrary）：从 game.dll 动态加载的 IGameModule，宿主经
//     unique_ptr<GameModuleLibrary> 拥有——GameModuleLibrary 析构时先在 dll 内
//     销毁模块、再 FreeLibrary（vtable 安全）。
// 驱动视图 mModules 存两类模块的裸 IGameModule*（非拥有），扇出无差别遍历。
//
// 本类只做扇出 + play-state 护栏，不碰 Pipeline / World 的具体类型（全部
// 透传引用）。Tick 前必须 EnterPlay，重复 EnterPlay / 未 EnterPlay 的 Tick /
// ExitPlay 均被护栏 no-op。ClearModules 须在 Edit 态（非 Play）调。
// ---------------------------------------------------------------------------

#include <orange/engine/game/GameModuleLibrary.h>
#include <orange/engine/game/IGameModule.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace Orange::Engine::Game
{

    class GameModuleHost
    {
    public:
        // 注册一个静态模块，返回非拥有裸指针（供宿主后续喂数据；所有权留 host）。
        // nullptr 静默忽略，返回 nullptr。
        IGameModule* AddModule(std::unique_ptr<IGameModule> module)
        {
            if (!module)
            {
                return nullptr;
            }
            IGameModule* raw = module.get();
            mModules.push_back(raw);
            mOwnedStatic.push_back(std::move(module));
            return raw;
        }

        // 注册一个 DLL 模块库（M7）。lib 无效 / 其 Module() 空则静默忽略返 nullptr。
        // 宿主拥有 GameModuleLibrary，其析构序保证 dll 卸载安全（见 GameModuleLibrary）。
        IGameModule* AddModuleLibrary(std::unique_ptr<GameModuleLibrary> lib)
        {
            if (!lib || lib->Module() == nullptr)
            {
                return nullptr;
            }
            IGameModule* raw = lib->Module();
            mModules.push_back(raw);
            mOwnedLibraries.push_back(std::move(lib));
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

        // 卸载全部模块（清空注册 —— 供项目切换 / 宿主拆卸）。须在 Edit 态调：
        // Play 中直接清会跳过 OnExitPlay，故先护栏拦（调用方应先 ExitPlay）。
        // 先清驱动视图（非拥有）、再销毁静态模块、最后销毁 DLL 库（每个 lib 析构
        // = dll 内销毁模块 + FreeLibrary）。
        void ClearModules()
        {
            if (mInPlay)
            {
                return;
            }
            mModules.clear();
            mOwnedStatic.clear();
            mOwnedLibraries.clear();
        }

        // 数：当前持有的 DLL 模块库数（ReloadLibrary 的 libIndex 上界；不含静态模块）。
        std::size_t LibraryCount() const noexcept
        {
            return mOwnedLibraries.size();
        }

        // 任一 DLL 库的原 dll 自加载以来被重编（file watcher）→ 编辑器提示"模块已过期"。
        bool AnyLibraryStale() const
        {
            for (const auto& lib : mOwnedLibraries)
            {
                if (lib && lib->IsSourceStale())
                {
                    return true;
                }
            }
            return false;
        }

        // 热重载一个 DLL 模块库（M7 session 级热重载，ADR-023）。libIndex = DLL
        // 库序（0-based，对应 AddModuleLibrary 顺序，非 mModules 混合序）。流程：
        //   卸载前摘该模块的 pass（否则 FreeLibrary 后 Pipeline 悬垂 pass → 崩）→
        //   销毁旧库（dll 内销毁模块 + FreeLibrary）→ 从原 dll 路径重 Load（新
        //   shadow copy 拿重编后的代码）→ 重注册 pass。
        // 须在 Edit 态调（Play 中热重载会砸运行态，护栏拦返 nullptr）。返回新模块
        // 裸指针；失败（在 Play / 越界 / 空 slot / 重 Load 失败）返 nullptr——重 Load
        // 失败时该库 slot 已移除、旧 pass 已摘、mModules 裸指针已断，调用方据此
        // 提示"模块失效"并重新枚举。serializer / schema 由调用方 reload 后重新
        // CollectSerializers / 重注册（本类只管 pass + 库生命周期）。
        IGameModule* ReloadLibrary(std::size_t libIndex, Render::Pipeline& pipeline)
        {
            if (mInPlay || libIndex >= mOwnedLibraries.size() || !mOwnedLibraries[libIndex])
            {
                return nullptr;
            }
            IGameModule* const          oldModule  = mOwnedLibraries[libIndex]->Module();
            const std::filesystem::path sourcePath = mOwnedLibraries[libIndex]->SourcePath();

            // 卸载前摘 pass + 从驱动视图断引用（pass 代码在即将 FreeLibrary 的 dll 内）。
            oldModule->UnregisterRenderPasses(pipeline);
            mModules.erase(std::remove(mModules.begin(), mModules.end(), oldModule), mModules.end());

            // 销毁旧库（dll 内销毁模块 + FreeLibrary），再从原路径重 Load 新 shadow。
            mOwnedLibraries[libIndex].reset();
            auto fresh = GameModuleLibrary::Load(sourcePath);
            if (!fresh || fresh->Module() == nullptr)
            {
                mOwnedLibraries.erase(mOwnedLibraries.begin() + static_cast<std::ptrdiff_t>(libIndex));
                return nullptr;
            }

            IGameModule* const newModule = fresh->Module();
            mOwnedLibraries[libIndex]     = std::move(fresh);
            mModules.push_back(newModule);
            newModule->RegisterRenderPasses(pipeline);
            return newModule;
        }

        // ---- 注册期扇出（宿主启动即调）----------------------------------

        void RegisterRenderPasses(Render::Pipeline& pipeline)
        {
            for (IGameModule* m : mModules)
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
            for (const IGameModule* m : mModules)
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
            for (const IGameModule* m : mModules)
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
            for (IGameModule* m : mModules)
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
            for (IGameModule* m : mModules)
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
            for (IGameModule* m : mModules)
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
        // 驱动视图（非拥有裸指针，扇出无差别遍历静态 + DLL 模块）。
        std::vector<IGameModule*> mModules;
        // 所有权：静态模块 + DLL 模块库。析构逆声明序——先库后静态、都在 mModules 之后，
        // 保证 mModules 裸指针在其指向对象销毁前已不再被扇出（析构期不扇出）。
        std::vector<std::unique_ptr<IGameModule>>       mOwnedStatic;
        std::vector<std::unique_ptr<GameModuleLibrary>> mOwnedLibraries;
        bool                                            mInPlay{false};
    };

} // namespace Orange::Engine::Game

#endif // ORANGE_ENGINE_GAME_GAME_MODULE_HOST_H
