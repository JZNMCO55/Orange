// SlotManager 实现 —— 在 SaveGameSystem + SavePath 之上做"多槽位 +
// sidecar 元数据"管理。
//
// sidecar `.meta.json` 用 Core::Serialization 的 JsonReader / JsonWriter
// 落地，与项目内"序列化必须走 Core::Serialization"约束一致。

#include "orange/engine/save/SlotManager.h"

#include "orange/engine/core/Log.h"
#include "orange/engine/core/SchemaVersion.h"
#include "orange/engine/core/Serialization.h"
#include "orange/engine/save/SaveGameSystem.h"

#include "save/AtomicWrite.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Orange::Engine::Save
{
    namespace
    {

        // sidecar JSON schema 版本。一旦"出厂"即冻结；major / minor 只增不改。
        const SchemaVersion& MetadataSchemaVersion()
        {
            static const SchemaVersion kVersion{"save/slot_metadata", 1, 0};
            return kVersion;
        }

        // 当前 SaveGameSystem 的 header format version —— 与 src/save/SaveGameSystem.cpp
        // 内的 kHeaderFormatVersion 必须一致。这里的副本仅是给 metadata
        // 的"快速预筛"字段用，真兼容性判定仍由 SaveGameSystem::Load 内部走。
        constexpr std::uint32_t kCurrentSaveFormatVersion = 1u;

        constexpr const char* kSaveExtension = ".save";
        constexpr const char* kMetaExtension = ".meta.json";

        // 路径片段安全校验 —— 与 SavePath::IsSafePathSegment 同语义但局部实现，
        // 避免把内部 helper 暴露到公共面：'.' / '..' / 含路径分隔符 / 控制字符
        // 全 reject。Note: SavePath 内部已经会校验 vendor / game / subfolder，
        // 这里只校 slotName（SavePath 不知道 slotName 概念）。
        bool IsSafeSlotName(std::string_view name) noexcept
        {
            if (name.empty())
            {
                return false;
            }
            if (name == "." || name == "..")
            {
                return false;
            }
            for (char c : name)
            {
                if (c == '/' || c == '\\')
                {
                    return false;
                }
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    return false;
                }
            }
            return true;
        }

        std::filesystem::path ComposeSlotPath(const std::filesystem::path& dir,
                                              std::string_view             slotName,
                                              const char*                  extension)
        {
            std::string fileName(slotName);
            fileName.append(extension);
            return dir / fileName;
        }

        std::int64_t CurrentUnixSeconds() noexcept
        {
            using namespace std::chrono;
            return static_cast<std::int64_t>(
                duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
        }

        // 把 mtime 转为 Unix 秒——用于 sidecar 缺失 / 损坏时的 placeholder
        // 时间戳。filesystem::file_time_type 与 system_clock 之间在 C++20 之
        // 前没有直接转换；这里走 last_write_time → 估算秒数差的近似办法。
        // 误差对"读档列表展示给玩家"的用途完全可接受。
        std::int64_t FileMtimeToUnixSeconds(const std::filesystem::path& path) noexcept
        {
            std::error_code ec;
            auto            ftime = std::filesystem::last_write_time(path, ec);
            if (ec)
            {
                return 0;
            }
            // C++17 标准没规定 file_time_type 的 epoch，所以走下面这套：
            // 把当前 file_clock::now() 转 system_clock::now() 拿到 epoch 偏移，
            // 再把目标 ftime 套进同一偏移。所有 std 实现里 file_clock 都是稳
            // 定时钟，本计算线程不安全也不重要——单次 ListSlots 调用内串行。
            using namespace std::chrono;
            auto fileNow = std::filesystem::file_time_type::clock::now();
            auto sysNow  = system_clock::now();
            auto delta   = ftime - fileNow; // 目标 vs 当前 file 时刻的差
            auto sysTime = sysNow + duration_cast<system_clock::duration>(delta);
            return static_cast<std::int64_t>(
                duration_cast<seconds>(sysTime.time_since_epoch()).count());
        }

        // 把 metadata 序列化成 UTF-8 JSON 字节流，用于 WriteFileAtomic。
        std::vector<std::uint8_t> EncodeMetadataJson(const SlotMetadata& meta)
        {
            JsonWriter w;
            w.WriteSchemaVersion("schemaVersion", MetadataSchemaVersion());
            w.WriteString("slotName", meta.slotName);
            w.WriteString("displayName", meta.displayName);
            w.WriteString("summary", meta.summary);
            w.WriteInt("savedAtUnixSeconds", meta.savedAtUnixSeconds);
            w.WriteInt("playTimeSeconds", meta.playTimeSeconds);
            w.WriteInt("engineSaveFormatVersion",
                       static_cast<std::int64_t>(meta.engineSaveFormatVersion));
            w.WriteBool("isAutosave", meta.isAutosave);

            const std::string         text = w.Dump(2);
            std::vector<std::uint8_t> bytes(text.begin(), text.end());
            return bytes;
        }

        // 反序列化 metadata。失败时按字段缺失 / schema 不兼容分别返不同错误码。
        Result<SlotMetadata, ResultCode> DecodeMetadataJson(const std::filesystem::path& path)
        {
            auto readerResult = JsonReader::FromFile(path.string());
            if (readerResult.IsErr())
            {
                ORANGE_LOG_ERROR("SlotManager: 解析 metadata JSON 失败 (path={}, msg='{}')",
                                 path.string(), readerResult.Error().message);
                return ResultCode::InvalidArgument;
            }
            JsonReader r = std::move(readerResult).Value();

            auto schemaResult = r.ReadSchemaVersion("schemaVersion");
            if (schemaResult.IsErr())
            {
                ORANGE_LOG_ERROR("SlotManager: metadata 缺 schemaVersion (path={})", path.string());
                return ResultCode::InvalidArgument;
            }
            if (!MetadataSchemaVersion().CanRead(schemaResult.Value()))
            {
                ORANGE_LOG_ERROR(
                    "SlotManager: metadata schemaVersion 不兼容 (path={}, file=ns='{}' v{}.{})",
                    path.string(),
                    schemaResult.Value().Namespace(),
                    schemaResult.Value().Major(),
                    schemaResult.Value().Minor());
                return ResultCode::SchemaMismatch;
            }

            SlotMetadata meta{};
            // 缺字段时 GetXxx 走默认值，metadata 是"宽容读"路径——单字段缺失
            // 不致命。
            meta.slotName           = r.GetString("slotName", {});
            meta.displayName        = r.GetString("displayName", {});
            meta.summary            = r.GetString("summary", {});
            meta.savedAtUnixSeconds = r.GetInt("savedAtUnixSeconds", 0);
            meta.playTimeSeconds    = r.GetInt("playTimeSeconds", 0);
            meta.engineSaveFormatVersion =
                static_cast<std::uint32_t>(r.GetInt("engineSaveFormatVersion", 0));
            meta.isAutosave = r.GetBool("isAutosave", false);
            return meta;
        }

    } // namespace

    // ---------------------------------------------------------------------------
    // SlotManager
    // ---------------------------------------------------------------------------

    SlotManager::SlotManager(SavePathOptions options) noexcept
        : mOptions(std::move(options))
    {
    }

    Result<std::filesystem::path, ResultCode>
    SlotManager::ResolveSavePath(std::string_view slotName) const
    {
        if (!IsSafeSlotName(slotName))
        {
            ORANGE_LOG_ERROR("SlotManager: slotName 不合法 (slotName='{}')", std::string(slotName));
            return ResultCode::InvalidArgument;
        }
        auto dirResult = ResolveSaveDirectory(mOptions);
        if (dirResult.IsErr())
        {
            return dirResult.Error();
        }
        return ComposeSlotPath(std::move(dirResult).Value(), slotName, kSaveExtension);
    }

    Result<std::filesystem::path, ResultCode>
    SlotManager::ResolveMetadataPath(std::string_view slotName) const
    {
        if (!IsSafeSlotName(slotName))
        {
            ORANGE_LOG_ERROR("SlotManager: slotName 不合法 (slotName='{}')", std::string(slotName));
            return ResultCode::InvalidArgument;
        }
        auto dirResult = ResolveSaveDirectory(mOptions);
        if (dirResult.IsErr())
        {
            return dirResult.Error();
        }
        return ComposeSlotPath(std::move(dirResult).Value(), slotName, kMetaExtension);
    }

    bool SlotManager::SlotExists(std::string_view slotName) const noexcept
    {
        if (!IsSafeSlotName(slotName))
        {
            return false;
        }
        auto dirResult = ResolveSaveDirectory(mOptions);
        if (dirResult.IsErr())
        {
            return false;
        }
        auto            path = ComposeSlotPath(std::move(dirResult).Value(), slotName, kSaveExtension);
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    Result<void, ResultCode>
    SlotManager::Save(const SaveGameSystem& system,
                      const World&          world,
                      std::string_view      slotName,
                      SlotMetadata          metadata) const
    {
        if (!IsSafeSlotName(slotName))
        {
            ORANGE_LOG_ERROR("SlotManager: slotName 不合法 (slotName='{}')", std::string(slotName));
            return ResultCode::InvalidArgument;
        }

        auto savePathResult = ResolveSavePath(slotName);
        if (savePathResult.IsErr())
        {
            return savePathResult.Error();
        }
        auto metaPathResult = ResolveMetadataPath(slotName);
        if (metaPathResult.IsErr())
        {
            return metaPathResult.Error();
        }
        const auto savePath = std::move(savePathResult).Value();
        const auto metaPath = std::move(metaPathResult).Value();

        // 1) .save 主体 —— 失败直接返；metadata 不写。
        auto saveRc = system.Save(world, savePath.string());
        if (saveRc.IsErr())
        {
            return saveRc.Error();
        }

        // 2) metadata 字段补全 —— 由 SlotManager 写权威字段，game 侧不必管。
        metadata.slotName                = std::string(slotName);
        metadata.engineSaveFormatVersion = kCurrentSaveFormatVersion;
        if (metadata.savedAtUnixSeconds == 0)
        {
            metadata.savedAtUnixSeconds = CurrentUnixSeconds();
        }

        // 3) 序列化 + 原子写入 sidecar。失败时 .save 已落地，记录 warn 后返
        //    IoError —— slot 仍然可加载，仅 metadata 缺失会让 ListSlots 走
        //    placeholder 退化路径。
        const auto bytes       = EncodeMetadataJson(metadata);
        auto       metaWriteRc = Detail::WriteFileAtomic(metaPath, bytes);
        if (metaWriteRc.IsErr())
        {
            ORANGE_LOG_WARN(
                "SlotManager: metadata sidecar 写入失败 (slot='{}', path={})—— .save 已落地，"
                "但 ListSlots 将退化到 placeholder。",
                std::string(slotName), metaPath.string());
            return metaWriteRc.Error();
        }

        return {};
    }

    Result<SlotMetadata, ResultCode>
    SlotManager::ReadMetadata(std::string_view slotName) const
    {
        auto pathResult = ResolveMetadataPath(slotName);
        if (pathResult.IsErr())
        {
            return pathResult.Error();
        }
        const auto path = std::move(pathResult).Value();

        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            return ResultCode::IoError;
        }
        return DecodeMetadataJson(path);
    }

    Result<std::vector<SlotMetadata>, ResultCode>
    SlotManager::ListSlots() const
    {
        auto dirResult = ResolveSaveDirectory(mOptions);
        if (dirResult.IsErr())
        {
            return dirResult.Error();
        }
        const auto dir = std::move(dirResult).Value();

        std::vector<SlotMetadata> result;

        std::error_code ec;
        if (!std::filesystem::exists(dir, ec))
        {
            // 首次启动：目录不存在 ≠ 错误。返回空 vector。
            return result;
        }

        auto iter = std::filesystem::directory_iterator(dir, ec);
        if (ec)
        {
            ORANGE_LOG_ERROR("SlotManager: directory_iterator 失败 (dir={}, ec='{}')",
                             dir.string(), ec.message());
            return ResultCode::IoError;
        }

        const std::filesystem::path saveExt = kSaveExtension;

        for (const auto& entry : iter)
        {
            std::error_code itEc;
            if (!entry.is_regular_file(itEc) || itEc)
            {
                continue;
            }

            const auto& path = entry.path();
            if (path.extension() != saveExt)
            {
                continue;
            } // 跳过 .meta.json / .tmp / 其它文件

            const std::string slotName = path.stem().string();
            if (!IsSafeSlotName(slotName))
            {
                // 防御性：用户手工塞了奇怪文件名时跳过，不让 ListSlots 整
                // 体失败。
                ORANGE_LOG_WARN("SlotManager: 跳过非法文件名 (path={})", path.string());
                continue;
            }

            // 查 sidecar；缺失 / 损坏 → placeholder。
            auto         metaPath = ComposeSlotPath(dir, slotName, kMetaExtension);
            SlotMetadata meta{};
            if (std::filesystem::exists(metaPath, itEc))
            {
                auto decoded = DecodeMetadataJson(metaPath);
                if (decoded.IsOk())
                {
                    meta = std::move(decoded).Value();
                }
                else
                {
                    ORANGE_LOG_WARN(
                        "SlotManager: metadata 解析失败 (slot='{}'), 退化到 placeholder",
                        slotName);
                }
            }

            // 保险：sidecar 里 slotName 可能与文件名不一致（被手工编辑），
            // 这里以文件名为准。
            meta.slotName = slotName;

            // 时间戳缺 → 用文件 mtime。
            if (meta.savedAtUnixSeconds == 0)
            {
                meta.savedAtUnixSeconds = FileMtimeToUnixSeconds(path);
            }

            result.push_back(std::move(meta));
        }

        // 最近的存档排前。同时间戳保持 directory_iterator 顺序（filesystem
        // 不保证顺序，但稳定排序让重复 ListSlots 在同 mtime 下结果一致）。
        std::stable_sort(result.begin(), result.end(),
                         [](const SlotMetadata& a, const SlotMetadata& b)
                         {
                             return a.savedAtUnixSeconds > b.savedAtUnixSeconds;
                         });

        return result;
    }

    Result<void, ResultCode>
    SlotManager::DeleteSlot(std::string_view slotName) const
    {
        auto savePathResult = ResolveSavePath(slotName);
        if (savePathResult.IsErr())
        {
            return savePathResult.Error();
        }
        auto metaPathResult = ResolveMetadataPath(slotName);
        if (metaPathResult.IsErr())
        {
            return metaPathResult.Error();
        }
        const auto savePath = std::move(savePathResult).Value();
        const auto metaPath = std::move(metaPathResult).Value();

        std::error_code ec;
        bool            saveRemoveError = false;

        // 先删 .save —— 这一步失败会 raise 错误码，但仍然 best-effort 把
        // .meta.json 也清掉，避免留 orphan sidecar。
        if (std::filesystem::exists(savePath, ec))
        {
            if (!std::filesystem::remove(savePath, ec))
            {
                ORANGE_LOG_ERROR("SlotManager: 删除 .save 失败 (path={}, ec='{}')",
                                 savePath.string(), ec.message());
                saveRemoveError = true;
            }
        }
        // .meta.json 不存在视为无操作；存在则尽力删，删失败仅 warn——sidecar
        // orphan 不影响数据正确性，下次 ListSlots 会跳过。
        if (std::filesystem::exists(metaPath, ec))
        {
            std::error_code metaEc;
            if (!std::filesystem::remove(metaPath, metaEc))
            {
                ORANGE_LOG_WARN("SlotManager: 删除 metadata sidecar 失败 (path={}, ec='{}')",
                                metaPath.string(), metaEc.message());
            }
        }

        if (saveRemoveError)
        {
            return ResultCode::IoError;
        }
        return {};
    }

} // namespace Orange::Engine::Save
