// ScriptGameModule 的实现 —— C# 脚本运行时的内置 IGameModule 包装（ADR-021 第二
// 宿主实现，PIE roadmap M5）。
//
// **本文件在 src/script/（非 dotnet/）**：它只经 ScriptRuntime / ScriptSystem 的
// 公共门面驱动脚本，零 CLR / hostfxr 头依赖。但依赖 ScriptRuntime（重资源），故
// 与 ScriptSystem.cpp 同样门控在 ORANGE_ENGINE_WITH_DOTNET —— dotnet 关闭时本 TU
// 不参与编译，ScriptGameModule 无 out-of-line 定义，宿主也不会构造它。

#include <orange/engine/game/ScriptGameModule.h>

#include <orange/engine/core/Log.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/script/ScriptComponent.h>
#include <orange/engine/script/ScriptRuntime.h>
#include <orange/engine/script/ScriptSystem.h>

#include <filesystem>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace Orange::Engine::Game
{

    // -----------------------------------------------------------------------
    // Impl —— 持有一次 CoreCLR 运行时 + 脚本系统 + 生命周期状态。
    //
    // runtime 与 scripts 都是重资源，跨 Play/Stop 复用（CoreCLR 每进程只能起一次
    // 且不卸载——可卸载 ALC 是 M8）。故 runtime 惰性 Initialize 后一直存活到模块
    // 析构；每次 Play 只做 ScriptSystem 的 StartWorld / StopWorld。
    // -----------------------------------------------------------------------
    struct ScriptGameModule::Impl
    {
        std::string runtimeConfigPath;
        std::string sdkAssemblyPath;
        std::string scriptBaseDir; // sdkAssemblyPath 的父目录，作相对 assemblyPath 搜索根

        Script::ScriptRuntime runtime;
        Script::ScriptSystem  scripts; // 构造时绑 runtime 引用（见下方 init list 顺序）

        bool initialized = false; // CoreCLR 起成功
        bool initFailed  = false; // 起失败——不再每次 Play 重试刷屏
        bool started     = false; // StartWorld 已调、待 StopWorld 配对

        // 热重载 file-watcher 基准（M8）：resolved 游戏程序集全路径 → 记录时 mtime。
        // StartWorld / ReloadScripts 后刷新；IsScriptStale 逐个比对（读不到返 false
        // 防误报，镜像 GameModuleLibrary::IsSourceStale）。
        std::unordered_map<std::string, std::filesystem::file_time_type> scriptTimestamps;

        explicit Impl(std::string rtConfig, std::string sdk)
            : runtimeConfigPath(std::move(rtConfig)), sdkAssemblyPath(std::move(sdk)),
              scripts(runtime)
        {
            std::error_code             ec;
            const std::filesystem::path sdkPath(sdkAssemblyPath);
            scriptBaseDir = sdkPath.parent_path().string();
            (void)ec;
        }

        // 惰性起 CoreCLR：仅在首个"有脚本组件的 Play"时付启动代价。失败落 initFailed
        // 后本会话不再重试（避免每次 Play 刷 error）。
        void EnsureInitialized()
        {
            if (initialized || initFailed)
            {
                return;
            }
            auto r = runtime.Initialize(runtimeConfigPath, sdkAssemblyPath);
            if (r.IsErr())
            {
                initFailed = true;
                ORANGE_LOG_ERROR(
                    "[ScriptGameModule] ScriptRuntime.Initialize 失败 (code={}) —— "
                    "runtimeconfig='{}' sdk='{}'；C# 脚本本会话不可用",
                    static_cast<unsigned>(r.Error()), runtimeConfigPath, sdkAssemblyPath);
                return;
            }
            initialized = true;
            ORANGE_LOG_INFO("[ScriptGameModule] CoreCLR 脚本运行时就绪 (sdk={})",
                            sdkAssemblyPath);
        }

        // 相对 assemblyPath 解析：优先原样（绝对 / 相对 cwd 命中就用），否则回退
        // scriptBaseDir 下同名文件（令带脚本的场景不写死机器绝对路径也能跨机跑）。
        // 就地改写组件（Play 快照已在 EnterPlay 之前落盘，Stop 时还原，故安全）。
        void ResolveAssemblyPaths(World& world)
        {
            namespace fs = std::filesystem;
            auto& registry = world.Registry();
            for (auto e : registry.view<Script::ScriptComponent>())
            {
                auto& sc = registry.get<Script::ScriptComponent>(e);
                if (sc.assemblyPath.empty())
                {
                    continue;
                }
                std::error_code ec;
                if (fs::exists(sc.assemblyPath, ec))
                {
                    continue; // 原样命中（绝对或相对 cwd）
                }
                const fs::path candidate = fs::path(scriptBaseDir) / sc.assemblyPath;
                if (fs::exists(candidate, ec))
                {
                    sc.assemblyPath = candidate.string();
                }
                // 仍找不到：留原样，StartWorld 的 CreateInstance 会 graceful 失败 + warn。
            }
        }

        // World 上是否有任一 ScriptComponent —— 无则完全惰性，不起 CoreCLR。
        // 用 begin()!=end() 而非 range-for（后者循环体无条件 return 会触发 MSVC
        // C4702 把增量表达式判为不可达 → /WX error）。
        static bool HasAnyScript(World& world)
        {
            auto view = world.Registry().view<Script::ScriptComponent>();
            return view.begin() != view.end();
        }

        // 记录当前（已 resolve 的）游戏程序集集合的 mtime 基准。StartWorld /
        // ReloadScripts 后调，作为 IsScriptStale 的重编检测锚点。读不到的文件跳过。
        void RecordScriptTimestamps(World& world)
        {
            namespace fs = std::filesystem;
            scriptTimestamps.clear();
            auto& registry = world.Registry();
            for (auto e : registry.view<Script::ScriptComponent>())
            {
                const auto& sc = registry.get<Script::ScriptComponent>(e);
                if (sc.assemblyPath.empty() || scriptTimestamps.count(sc.assemblyPath) != 0)
                {
                    continue;
                }
                std::error_code ec;
                const auto      t = fs::last_write_time(sc.assemblyPath, ec);
                if (!ec)
                {
                    scriptTimestamps.emplace(sc.assemblyPath, t);
                }
            }
        }

        // 记录在案的任一游戏程序集是否已被重编（mtime 晚于基准）。读不到（正被覆盖 /
        // 不存在）不误报（跳过该文件）。镜像 GameModuleLibrary::IsSourceStale。
        bool IsStale() const
        {
            namespace fs = std::filesystem;
            for (const auto& [path, baseline] : scriptTimestamps)
            {
                std::error_code ec;
                const auto      now = fs::last_write_time(path, ec);
                if (ec)
                {
                    continue; // 此刻读不到 → 不误报
                }
                if (now > baseline)
                {
                    return true;
                }
            }
            return false;
        }
    };

    ScriptGameModule::ScriptGameModule(std::string runtimeConfigPath, std::string sdkAssemblyPath)
        : mpImpl(std::make_unique<Impl>(std::move(runtimeConfigPath), std::move(sdkAssemblyPath)))
    {
    }

    ScriptGameModule::~ScriptGameModule() = default;

    void ScriptGameModule::OnEnterPlay(GameModuleContext& ctx)
    {
        if (ctx.pWorld == nullptr)
        {
            return;
        }
        World& world = *ctx.pWorld;

        // 无脚本组件：不付 CoreCLR 启动代价（原版编辑器 / 非脚本场景 Play 时全惰性）。
        if (!Impl::HasAnyScript(world))
        {
            return;
        }

        mpImpl->EnsureInitialized();
        if (!mpImpl->initialized)
        {
            return; // 起失败已 warn；带脚本的实体本次 Play 静默不跑
        }

        mpImpl->ResolveAssemblyPaths(world);
        mpImpl->scripts.StartWorld(world);
        // 记录 resolved 程序集 mtime 基准，供 IsScriptStale 检测 Play 中重编。
        mpImpl->RecordScriptTimestamps(world);
        mpImpl->started = true;
    }

    void ScriptGameModule::Tick(GameModuleContext& ctx, float dt)
    {
        if (!mpImpl->started || ctx.pWorld == nullptr)
        {
            return;
        }
        mpImpl->scripts.Tick(*ctx.pWorld, dt);
    }

    void ScriptGameModule::OnExitPlay(GameModuleContext& ctx)
    {
        if (!mpImpl->started)
        {
            return;
        }
        if (ctx.pWorld != nullptr)
        {
            mpImpl->scripts.StopWorld(*ctx.pWorld);
        }
        mpImpl->scriptTimestamps.clear();
        mpImpl->started = false;
    }

    bool ScriptGameModule::IsScriptStale() const
    {
        // 未 Play / 无脚本（未记录基准）时恒 false。
        if (!mpImpl->started)
        {
            return false;
        }
        return mpImpl->IsStale();
    }

    void ScriptGameModule::ReloadScripts(GameModuleContext& ctx)
    {
        // 仅 Play 中（已起脚本）有意义。ResolveAssemblyPaths 已在 OnEnterPlay 把
        // 组件路径就地改写为 resolved 全路径，故这里直接复用（重编覆盖的是同一路径）。
        if (!mpImpl->started || ctx.pWorld == nullptr)
        {
            return;
        }
        World& world = *ctx.pWorld;
        mpImpl->scripts.ReloadWorld(world);
        // 刷新 mtime 基准 —— 否则刚重载完 IsScriptStale 仍报 stale（回到旧基准）。
        mpImpl->RecordScriptTimestamps(world);
    }

} // namespace Orange::Engine::Game
