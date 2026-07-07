#ifndef ORANGE_ENGINE_GAME_GAME_MODULE_LIBRARY_H
#define ORANGE_ENGINE_GAME_GAME_MODULE_LIBRARY_H

// ---------------------------------------------------------------------------
// GameModuleLibrary —— 从 game.dll 动态加载一个 IGameModule 的 RAII 宿主（M7）。
//
// PIE DLL 游戏模块宿主（ADR-023）：编辑器不再把游戏静态链进 per-game editor，
// 而是共享 OrangeEditor 运行时 LoadLibrary(game.dll) 拿到 IGameModule，Play 驱动；
// Stop 后可卸载 + 重编 + 重载（热重载），场景状态从 PIE 快照天然还原。
//
// 本类是引擎级的加载/卸载原语（编辑器 GUI 接线、file watcher、菜单在编辑器层）。
// 两条正交硬约束（ADR-023）都由本类的**析构顺序**与**shadow-copy**兑现：
//   * 卸载安全：模块的 vtable + operator delete 都在 game.dll 内，故析构必须先经
//     game.dll 导出的 OrangeDestroyGameModule 在 dll 内销毁模块，再 FreeLibrary。
//     反序会让宿主碰一个 vtable 已随 FreeLibrary 消失的对象 → 崩溃。
//   * 热重载可行：LoadLibrary 锁住被加载的 dll 文件挡重编，故本类先把 dll（+ 存在
//     的 pdb）拷到临时唯一路径再加载，原 dll 保持可重编。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/game/IGameModule.h>

#include <filesystem>
#include <memory>

namespace Orange::Engine::Game
{

    // game.dll 导出的工厂 / 析构入口的 C 签名（extern"C" = 无 name mangling，宿主
    // GetProcAddress 按名字取；裸指针值语义跨 DLL 边界稳定）。
    extern "C"
    {
        using OrangeCreateGameModuleFn  = IGameModule* (*)();
        using OrangeDestroyGameModuleFn = void (*)(IGameModule*);
    }

    class ORANGE_ENGINE_API GameModuleLibrary
    {
    public:
        // 加载失败（文件不存在 / LoadLibrary 失败 / 缺 OrangeCreateGameModule 或
        // OrangeDestroyGameModule 导出 / 工厂返 nullptr）返回 nullptr 并经 Core::Log
        // 记原因。成功后 Module() 恒非空，生命周期绑本对象。
        static std::unique_ptr<GameModuleLibrary> Load(const std::filesystem::path& dllPath);

        ~GameModuleLibrary();

        GameModuleLibrary(const GameModuleLibrary&)            = delete;
        GameModuleLibrary& operator=(const GameModuleLibrary&) = delete;

        IGameModule* Module() const noexcept { return mpModule; }

        // 原始 dll 路径（热重载时宿主重新 Load 用）。
        const std::filesystem::path& SourcePath() const noexcept { return mSourcePath; }

    private:
        GameModuleLibrary() = default;

        void*                     mHModule{nullptr}; // HMODULE，void* 保持公共头无 windows.h
        IGameModule*              mpModule{nullptr};
        OrangeDestroyGameModuleFn mpDestroy{nullptr};
        std::filesystem::path     mSourcePath; // 原 dll
        std::filesystem::path     mShadowPath; // 临时副本（析构删）
    };

} // namespace Orange::Engine::Game

// ---------------------------------------------------------------------------
// game.dll 侧：在游戏模块某个 .cpp 里写一行导出工厂 + 析构：
//   ORANGE_EXPORT_GAME_MODULE(spike01::SlimeGameModule)
// 模块类须可默认构造。析构在 dll 内 delete → 跨 DLL 堆 / vtable 安全。
// ---------------------------------------------------------------------------
#if defined(_WIN32)
    #define ORANGE_GAME_MODULE_EXPORT __declspec(dllexport)
#else
    #define ORANGE_GAME_MODULE_EXPORT
#endif

#define ORANGE_EXPORT_GAME_MODULE(ModuleClass)                                \
    extern "C" ORANGE_GAME_MODULE_EXPORT ::Orange::Engine::Game::IGameModule* \
    OrangeCreateGameModule()                                                  \
    {                                                                         \
        return new ModuleClass();                                            \
    }                                                                         \
    extern "C" ORANGE_GAME_MODULE_EXPORT void                                 \
    OrangeDestroyGameModule(::Orange::Engine::Game::IGameModule* pModule)     \
    {                                                                         \
        delete pModule;                                                       \
    }

#endif // ORANGE_ENGINE_GAME_GAME_MODULE_LIBRARY_H
