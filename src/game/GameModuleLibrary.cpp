#include "orange/engine/game/GameModuleLibrary.h"

#include "orange/engine/core/Log.h"

#include <string>
#include <system_error>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>

    #include <atomic>
#endif

namespace Orange::Engine::Game
{

#if defined(_WIN32)

    namespace
    {
        // 唯一 shadow 路径：<temp>/orange-game-modules/<stem>.shadow.<pid>.<counter>.dll。
        // pid + 进程内单调 counter 保证同机多进程 / 多次热重载互不撞名，且无 Date /
        // random 依赖。
        std::filesystem::path MakeShadowPath(const std::filesystem::path& src)
        {
            static std::atomic<unsigned> sCounter{0};
            const unsigned                n = sCounter.fetch_add(1, std::memory_order_relaxed);

            std::error_code ec;
            auto            dir = std::filesystem::temp_directory_path(ec) / "orange-game-modules";
            std::filesystem::create_directories(dir, ec);

            const std::string name = src.stem().string() + ".shadow." +
                                     std::to_string(static_cast<unsigned long>(::GetCurrentProcessId())) + "." +
                                     std::to_string(n) + ".dll";
            return dir / name;
        }
    } // namespace

    std::unique_ptr<GameModuleLibrary> GameModuleLibrary::Load(const std::filesystem::path& dllPath)
    {
        std::error_code ec;
        if (!std::filesystem::exists(dllPath, ec))
        {
            ORANGE_LOG_ERROR("[GameModuleLibrary] dll 不存在：{}", dllPath.string());
            return nullptr;
        }

        // shadow-copy 原 dll（+ 存在的 pdb）到临时唯一路径，避免 LoadLibrary 锁住原
        // 文件、挡热重编。
        const auto shadow = MakeShadowPath(dllPath);
        std::filesystem::copy_file(dllPath, shadow, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            ORANGE_LOG_ERROR("[GameModuleLibrary] shadow-copy 失败：{} → {}（{}）",
                             dllPath.string(), shadow.string(), ec.message());
            return nullptr;
        }
        // pdb 一并 shadow（可选，失败不致命，只影响该模块调试符号）。
        auto pdbSrc = dllPath;
        pdbSrc.replace_extension(L".pdb");
        if (std::filesystem::exists(pdbSrc, ec))
        {
            auto pdbDst = shadow;
            pdbDst.replace_extension(L".pdb");
            std::filesystem::copy_file(pdbSrc, pdbDst, std::filesystem::copy_options::overwrite_existing, ec);
            ec.clear();
        }

        HMODULE h = ::LoadLibraryW(shadow.wstring().c_str());
        if (h == nullptr)
        {
            ORANGE_LOG_ERROR("[GameModuleLibrary] LoadLibrary 失败：{}（GetLastError={}）",
                             shadow.string(), static_cast<unsigned long>(::GetLastError()));
            std::filesystem::remove(shadow, ec);
            return nullptr;
        }

        auto create = reinterpret_cast<OrangeCreateGameModuleFn>(
            reinterpret_cast<void*>(::GetProcAddress(h, "OrangeCreateGameModule")));
        auto destroy = reinterpret_cast<OrangeDestroyGameModuleFn>(
            reinterpret_cast<void*>(::GetProcAddress(h, "OrangeDestroyGameModule")));
        if (create == nullptr || destroy == nullptr)
        {
            ORANGE_LOG_ERROR("[GameModuleLibrary] 缺 OrangeCreateGameModule / OrangeDestroyGameModule 导出（"
                             "game.dll 是否用了 ORANGE_EXPORT_GAME_MODULE 宏？）：{}",
                             dllPath.string());
            ::FreeLibrary(h);
            std::filesystem::remove(shadow, ec);
            return nullptr;
        }

        IGameModule* mod = create();
        if (mod == nullptr)
        {
            ORANGE_LOG_ERROR("[GameModuleLibrary] OrangeCreateGameModule 返回 nullptr：{}", dllPath.string());
            ::FreeLibrary(h);
            std::filesystem::remove(shadow, ec);
            return nullptr;
        }

        auto lib          = std::unique_ptr<GameModuleLibrary>(new GameModuleLibrary());
        lib->mHModule     = h;
        lib->mpModule     = mod;
        lib->mpDestroy    = destroy;
        lib->mSourcePath  = dllPath;
        lib->mShadowPath  = shadow;
        ORANGE_LOG_INFO("[GameModuleLibrary] 已加载游戏模块 '{}'（{}）", mod->Name(), dllPath.string());
        return lib;
    }

    GameModuleLibrary::~GameModuleLibrary()
    {
        // 顺序关键：先在 dll 内销毁模块（vtable / operator delete 都在 dll 内），
        // 再 FreeLibrary，最后删 shadow 副本。反序 = 悬空 vtable 崩溃。
        if (mpModule != nullptr && mpDestroy != nullptr)
        {
            mpDestroy(mpModule);
        }
        mpModule = nullptr;

        if (mHModule != nullptr)
        {
            ::FreeLibrary(static_cast<HMODULE>(mHModule));
            mHModule = nullptr;
        }

        std::error_code ec;
        if (!mShadowPath.empty())
        {
            std::filesystem::remove(mShadowPath, ec);
            auto pdb = mShadowPath;
            pdb.replace_extension(L".pdb");
            std::filesystem::remove(pdb, ec);
        }
    }

#else // 非 Windows

    std::unique_ptr<GameModuleLibrary> GameModuleLibrary::Load(const std::filesystem::path&)
    {
        ORANGE_LOG_ERROR("[GameModuleLibrary] DLL 游戏模块宿主目前仅支持 Windows");
        return nullptr;
    }

    GameModuleLibrary::~GameModuleLibrary() = default;

#endif

} // namespace Orange::Engine::Game
