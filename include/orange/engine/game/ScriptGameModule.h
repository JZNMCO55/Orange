#ifndef ORANGE_ENGINE_GAME_SCRIPT_GAME_MODULE_H
#define ORANGE_ENGINE_GAME_SCRIPT_GAME_MODULE_H

// ---------------------------------------------------------------------------
// ScriptGameModule —— C# 脚本运行时的内置 IGameModule 实现（ADR-021 双宿主的
// 第二实现，PIE roadmap M5）。
//
// 定位：把 dotnet-gated 的 ScriptRuntime + ScriptSystem 包成一个语言无关的
// IGameModule，挂进宿主（编辑器 / 发布 runtime）的同一套生命周期。SlimeGameModule
// （OG，纯 C++）是第一实现；本类是第二实现——两实现并存即证明 IGameModule 接口
// 没有 C++ 特化泄漏（M5 验收标准）。
//
//   OnEnterPlay：若 World 上有任一 ScriptComponent → 惰性起 CoreCLR（一次性，
//                跨 Play/Stop 复用）+ ScriptSystem::StartWorld（批量实例化 +
//                OnStart）。无脚本组件则完全惰性——不付 CoreCLR 启动代价。
//   Tick       ：ScriptSystem::Tick（对所有活脚本 OnUpdate）。
//   OnExitPlay ：ScriptSystem::StopWorld（OnDestroy + 释放）。脚本对 ECS 的改动
//                由宿主的 Play 快照还原（ScriptGameModule 不自做 undo）。
//
// **不含 ComponentSerializers**：ScriptComponent 的序列化器是引擎内置的
// （src/scene/ComponentSerializers.cpp），含 ScriptComponent 的场景在任何构建
// 都能 round-trip，与本模块是否存在无关。
//
// **header isolation / always-compile**：本头是 PIMPL façade，**不**暴露任何
// CLR / ScriptRuntime 类型，也不因 ORANGE_ENGINE_WITH_DOTNET 是否开启而改变
// 可解析性（只 include IGameModule.h + 标准库）。实现 TU
// （src/script/ScriptGameModule.cpp）才 dotnet-gated：dotnet 关闭时该 TU 不编，
// 本类无 out-of-line 定义，宿主也不会去构造它（编辑器侧同样门控在
// ORANGE_EDITOR_WITH_DOTNET）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/game/IGameModule.h>

#include <memory>
#include <string>

namespace Orange::Engine::Game
{

    class ORANGE_ENGINE_API ScriptGameModule : public IGameModule
    {
    public:
        // deployment 路径由宿主注入（编辑器按 exe-相对解析）：
        //   * runtimeConfigPath —— 托管 glue 旁的 *.runtimeconfig.json（hostfxr
        //     用它定位 .NET runtime 框架）；
        //   * sdkAssemblyPath   —— OrangeScriptSDK.dll 的路径（托管 glue 程序集）。
        // 其父目录同时作为**脚本程序集搜索根**：ScriptComponent.assemblyPath 为
        // 相对路径且相对当前 cwd 找不到时，回退到该根解析（令带脚本的场景可跨机
        // round-trip，不写死绝对路径）。
        ScriptGameModule(std::string runtimeConfigPath, std::string sdkAssemblyPath);
        ~ScriptGameModule() override;

        const char* Name() const noexcept override
        {
            return "ScriptGameModule";
        }

        // WantsOwnPhysicsStep 沿用 IGameModule 默认 false —— 脚本不接管物理 step
        // （宿主照常 step，脚本经绑定 API 读写 Transform），区别于 spike-01 的自管
        // accumulator。故不覆写。

        void OnEnterPlay(GameModuleContext& ctx) override;
        void Tick(GameModuleContext& ctx, float dt) override;
        void OnExitPlay(GameModuleContext& ctx) override;

        // 热重载支持（M8，镜像 M7 GameModuleLibrary::IsSourceStale）：
        //   * IsScriptStale —— OnEnterPlay 时记录的游戏程序集集合里，是否有任一文件
        //     的 mtime 晚于记录基准（= 被重编）。读不到文件返 false 防误报。未 Play /
        //     无脚本时返 false。宿主 file-watcher 轮询它决定是否提示重载。
        //   * ReloadScripts —— 保留运行时状态换新代码（ScriptSystem::ReloadWorld）+
        //     刷新 mtime 基准。仅在 Play 中（已 OnEnterPlay 且起过脚本）有效。
        bool IsScriptStale() const;
        void ReloadScripts(GameModuleContext& ctx);

    private:
        struct Impl;
        std::unique_ptr<Impl> mpImpl;
    };

} // namespace Orange::Engine::Game

#endif // ORANGE_ENGINE_GAME_SCRIPT_GAME_MODULE_H
