// SavePath 实现 —— Windows 走 SHGetKnownFolderPath；其它平台留接口、返
// 回 Unsupported。
//
// 依赖：Windows 平台需要 Shell32.lib + Ole32.lib（CoTaskMemFree）。MSVC
// 通过 `#pragma comment(lib, ...)` auto-link，不必在 CMakeLists 里手挂。

#include "orange/engine/save/SavePath.h"

#include "orange/engine/core/Log.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Shlobj.h>
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#endif

namespace Orange::Engine::Save
{
    namespace
    {

        // 路径片段合法性 —— vendor / game / subfolder / slotName 都不允许出现
        // 路径分隔符或 ".."（防止把 game 当作 "../../etc/passwd" 越权写入）。
        bool IsSafePathSegment(std::string_view segment) noexcept
        {
            if (segment.empty())
            {
                return false;
            }
            if (segment == "." || segment == "..")
            {
                return false;
            }
            for (char c : segment)
            {
                if (c == '/' || c == '\\')
                {
                    return false;
                }
                // 控制字符（含 NUL）— Windows 文件名禁止；POSIX 允许但我们也禁，
                // 避免在 game 侧写出难以排查的诡异路径。
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    return false;
                }
            }
            return true;
        }

    } // namespace

#if defined(_WIN32)

    Result<std::filesystem::path, ResultCode> ResolveUserDataRoot()
    {
        PWSTR   raw = nullptr;
        HRESULT hr  = ::SHGetKnownFolderPath(FOLDERID_RoamingAppData,
                                             KF_FLAG_CREATE,
                                             nullptr,
                                             &raw);
        if (FAILED(hr) || raw == nullptr)
        {
            // SHGetKnownFolderPath 即使返回错误码也可能写出 raw —— 按文档要
            // 求一律 CoTaskMemFree。
            if (raw != nullptr)
            {
                ::CoTaskMemFree(raw);
            }
            ORANGE_LOG_ERROR("SavePath: SHGetKnownFolderPath(RoamingAppData) 失败 (hr=0x{:08x})",
                             static_cast<unsigned>(hr));
            return ResultCode::IoError;
        }
        std::filesystem::path root(raw);
        ::CoTaskMemFree(raw);
        return root;
    }

#else // !_WIN32

    Result<std::filesystem::path, ResultCode> ResolveUserDataRoot()
    {
        // Linux / macOS 留待后续按需补：
        //   * Linux : `$XDG_DATA_HOME` 或 `~/.local/share`
        //   * macOS : `~/Library/Application Support`
        // SaveGameSystem 自身是平台无关的，只需要拿到一个绝对路径即可。
        ORANGE_LOG_WARN("SavePath: 当前平台未实现 user-data 根目录解析");
        return ResultCode::Unsupported;
    }

#endif // _WIN32

    Result<std::filesystem::path, ResultCode>
    ResolveSaveDirectory(const SavePathOptions& options)
    {
        // 1) 字段校验：game 必填；vendor / game / subfolder 都得是单段安全
        //    目录名。
        if (!IsSafePathSegment(options.vendor))
        {
            ORANGE_LOG_ERROR("SavePath: vendor 不合法 (vendor='{}')", options.vendor);
            return ResultCode::InvalidArgument;
        }
        if (options.game.empty())
        {
            ORANGE_LOG_ERROR("SavePath: game 字段不能为空");
            return ResultCode::InvalidArgument;
        }
        if (!IsSafePathSegment(options.game))
        {
            ORANGE_LOG_ERROR("SavePath: game 不合法 (game='{}')", options.game);
            return ResultCode::InvalidArgument;
        }
        if (!IsSafePathSegment(options.subfolder))
        {
            ORANGE_LOG_ERROR("SavePath: subfolder 不合法 (subfolder='{}')", options.subfolder);
            return ResultCode::InvalidArgument;
        }

        // 2) 解析 user-data 根目录（平台相关）。
        auto rootResult = ResolveUserDataRoot();
        if (rootResult.IsErr())
        {
            return rootResult.Error();
        }
        std::filesystem::path dir = std::move(rootResult).Value();

        // 3) 拼路径片段。
        dir /= options.vendor;
        dir /= options.game;
        dir /= options.subfolder;

        // 4) mkdir -p。已存在不报错；权限不足 / 路径冲突 → IoError。
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec)
        {
            ORANGE_LOG_ERROR("SavePath: 创建存档目录失败 (dir={}, ec='{}')",
                             dir.string(), ec.message());
            return ResultCode::IoError;
        }

        return dir;
    }

    Result<std::filesystem::path, ResultCode>
    ResolveSaveSlotPath(const SavePathOptions& options, std::string_view slotName)
    {
        if (!IsSafePathSegment(slotName))
        {
            ORANGE_LOG_ERROR("SavePath: slotName 不合法 (slotName='{}')",
                             std::string(slotName));
            return ResultCode::InvalidArgument;
        }

        auto dirResult = ResolveSaveDirectory(options);
        if (dirResult.IsErr())
        {
            return dirResult.Error();
        }
        std::filesystem::path dir = std::move(dirResult).Value();

        // 文件名固定 `<slotName>.save` 后缀 —— SaveGameSystem 不依赖后缀但
        // 让玩家 / 工具识别更直观。
        std::string fileName(slotName);
        fileName.append(".save");
        dir /= fileName;

        return dir;
    }

} // namespace Orange::Engine::Save
