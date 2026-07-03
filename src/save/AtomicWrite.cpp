// AtomicWrite 实现 —— 把 SaveGameSystem.cpp 原本的 FsyncFile + WriteAtomic
// 整体迁过来。逻辑未变，只是从 anonymous namespace 提到 `Save::Detail`
// 命名空间，让 SlotManager 也能复用同一份原子写入。

#include "save/AtomicWrite.h"

#include "orange/engine/core/Log.h"

#include <cstring>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Orange::Engine::Save::Detail
{
    namespace
    {

#if defined(_WIN32)
        // 真 fsync：std::ofstream::flush 只把字节交给 OS buffer，断电仍可能丢。
        // CreateFileA + FlushFileBuffers 才真正强制 OS 把脏页冲到物理介质。
        // 失败不视为 fatal —— 仅 warn 后继续 rename，避免只读介质 / 网络盘等
        // 无法 flush 的合法场景被无故 fail。
        void FsyncFile(const std::filesystem::path& path) noexcept
        {
            const auto pathStr = path.string();
            HANDLE     handle  = ::CreateFileA(pathStr.c_str(),
                                               GENERIC_WRITE,
                                               FILE_SHARE_READ,
                                               nullptr,
                                               OPEN_EXISTING,
                                               FILE_ATTRIBUTE_NORMAL,
                                               nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                ORANGE_LOG_WARN("AtomicWrite: 无法打开 .tmp 做 fsync (path={}, GetLastError={})",
                                pathStr, ::GetLastError());
                return;
            }
            if (::FlushFileBuffers(handle) == 0)
            {
                ORANGE_LOG_WARN("AtomicWrite: FlushFileBuffers 失败 (path={}, GetLastError={})",
                                pathStr, ::GetLastError());
            }
            ::CloseHandle(handle);
        }
#else
        void FsyncFile(const std::filesystem::path& /*path*/) noexcept
        {
            // 非 Windows 平台暂时只走 std::ofstream 的 close()——POSIX fsync(2)
            // 留到接平台层时再补。
        }
#endif

    } // namespace

    Result<void, ResultCode> WriteFileAtomic(const std::filesystem::path&     finalPath,
                                             const std::vector<std::uint8_t>& bytes)
    {
        std::filesystem::path tmpPath = finalPath;
        tmpPath += ".tmp";

        // 1) 先确保目标目录存在 —— 否则 ofstream open 会失败但错误信息不
        //    够明确。create_directories 对已存在目录是 no-op。
        std::error_code ec;
        if (auto parent = finalPath.parent_path(); !parent.empty())
        {
            std::filesystem::create_directories(parent, ec);
            // ec 非零不立即 fail —— ofstream open 自己会复检；这里仅尝试。
        }

        // 2) 写到 .tmp。binary | trunc 保证没有残留。
        {
            std::ofstream stream(tmpPath, std::ios::binary | std::ios::trunc);
            if (!stream.is_open())
            {
                ORANGE_LOG_ERROR("AtomicWrite: 无法打开临时文件写入 (path={})",
                                 tmpPath.string());
                return ResultCode::IoError;
            }
            if (!bytes.empty())
            {
                stream.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
                if (!stream)
                {
                    ORANGE_LOG_ERROR("AtomicWrite: 写入临时文件失败 (path={})",
                                     tmpPath.string());
                    std::filesystem::remove(tmpPath, ec);
                    return ResultCode::IoError;
                }
            }
            // ofstream dtor 关闭文件 —— 关闭后才能 fsync / rename。
        }

        // 3) fsync —— 强制 OS 把脏页冲到介质。失败仅 warn，rename 继续。
        FsyncFile(tmpPath);

        // 4) rename(tmp, final) —— Windows 上 std::filesystem::rename 内部走
        //    MoveFileExA + MOVEFILE_REPLACE_EXISTING，对同卷 NTFS 是原子的。
        std::error_code renameEc;
        std::filesystem::rename(tmpPath, finalPath, renameEc);
        if (renameEc)
        {
            ORANGE_LOG_ERROR("AtomicWrite: 原子 rename 失败 (tmp={}, target={}, ec='{}')",
                             tmpPath.string(), finalPath.string(), renameEc.message());
            std::filesystem::remove(tmpPath, ec); // 尽力清理
            return ResultCode::IoError;
        }

        return {};
    }

} // namespace Orange::Engine::Save::Detail
