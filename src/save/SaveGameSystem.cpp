// SaveGameSystem 实现 —— Save / Load 主流程 + 原子写入 + CRC 校验。
//
// 头隔离：本 .cpp 不直接 include <nlohmann/json.hpp>，所有 JSON 操作走
// Core::Serialization 的 JsonReader / JsonWriter。
//
// 文件格式见 SaveGameSystem.h 顶部注释。

#include "orange/engine/save/SaveGameSystem.h"

#include "orange/engine/core/Log.h"
#include "orange/engine/core/SchemaVersion.h"
#include "orange/engine/core/Serialization.h"
#include "orange/engine/save/SaveGameRegistry.h"
#include "orange/engine/save/SaveableComponent.h"
#include "orange/engine/scene/Entity.h"
#include "orange/engine/scene/World.h"

#include "save/AtomicWrite.h"

#include <entt/entt.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Orange::Engine::Save
{
    namespace
    {

        // ---------------------------------------------------------------------------
        // 文件级常量
        // ---------------------------------------------------------------------------

        // 顶层 schema 版本。一旦"出厂"（写入玩家硬盘）即冻结；major / minor 只
        // 增不改，新增字段走 minor bump（向后兼容），结构性破坏走 major bump
        // （reader 拒绝读取，调用方按需路由 migrator）。
        const SchemaVersion& SaveSchemaVersion()
        {
            static const SchemaVersion kVersion{"save/game", 1, 0};
            return kVersion;
        }

        constexpr std::array<char, 4> kMagic{'O', 'S', 'A', 'V'};
        constexpr std::uint32_t       kHeaderFormatVersion = 1u;
        constexpr std::size_t         kHeaderSize          = 16; // 4 magic + 3*uint32

        constexpr std::string_view kSchemaVersionPath = "schemaVersion";
        constexpr std::string_view kEntitiesPath      = "entities";

        // ---------------------------------------------------------------------------
        // CRC32（IEEE 802.3 多项式 0xEDB88320 / 反向位序）—— 表驱动。表在首次
        // 调用时一次性生成。
        // ---------------------------------------------------------------------------

        std::uint32_t Crc32(const void* data, std::size_t size) noexcept
        {
            static const std::array<std::uint32_t, 256> kTable = []
            {
                std::array<std::uint32_t, 256> t{};
                constexpr std::uint32_t        kPoly = 0xEDB88320u;
                for (std::uint32_t i = 0; i < 256; ++i)
                {
                    std::uint32_t c = i;
                    for (int j = 0; j < 8; ++j)
                    {
                        c = (c & 1u) ? (kPoly ^ (c >> 1)) : (c >> 1);
                    }
                    t[i] = c;
                }
                return t;
            }();

            std::uint32_t       crc   = 0xFFFFFFFFu;
            const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0; i < size; ++i)
            {
                crc = (crc >> 8) ^ kTable[(crc ^ bytes[i]) & 0xFFu];
            }
            return crc ^ 0xFFFFFFFFu;
        }

        // ---------------------------------------------------------------------------
        // 路径拼装 helper —— 与 src/scene/SceneSerialization.cpp 同模式，让 Save
        // / Load 两侧拼出完全一致的 JSON 路径。
        // ---------------------------------------------------------------------------

        std::string EntityBasePath(std::size_t index)
        {
            std::string p;
            p.reserve(kEntitiesPath.size() + 1 + 12);
            p.append(kEntitiesPath);
            p.push_back('/');
            p.append(std::to_string(index));
            return p;
        }

        std::string ComponentBasePath(const std::string& entityBase, std::string_view componentName)
        {
            std::string p;
            p.reserve(entityBase.size() + std::string_view{"/components/"}.size() + componentName.size());
            p.append(entityBase);
            p.append("/components/");
            p.append(componentName);
            return p;
        }

        // ---------------------------------------------------------------------------
        // 二进制 header 编/解码 —— 单独抽出来让 Save / Load 两端共享。
        // ---------------------------------------------------------------------------

        void EncodeUint32LE(std::uint8_t* dst, std::uint32_t value) noexcept
        {
            dst[0] = static_cast<std::uint8_t>(value & 0xFFu);
            dst[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
            dst[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
            dst[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
        }

        std::uint32_t DecodeUint32LE(const std::uint8_t* src) noexcept
        {
            return static_cast<std::uint32_t>(src[0]) | (static_cast<std::uint32_t>(src[1]) << 8) | (static_cast<std::uint32_t>(src[2]) << 16) | (static_cast<std::uint32_t>(src[3]) << 24);
        }

        // 把 16 字节 header 写到 dst[0..16]：magic + formatVersion + crc + payloadLen。
        void WriteHeader(std::uint8_t* dst,
                         std::uint32_t crc,
                         std::uint32_t payloadLen) noexcept
        {
            dst[0] = static_cast<std::uint8_t>(kMagic[0]);
            dst[1] = static_cast<std::uint8_t>(kMagic[1]);
            dst[2] = static_cast<std::uint8_t>(kMagic[2]);
            dst[3] = static_cast<std::uint8_t>(kMagic[3]);
            EncodeUint32LE(dst + 4, kHeaderFormatVersion);
            EncodeUint32LE(dst + 8, crc);
            EncodeUint32LE(dst + 12, payloadLen);
        }

        struct ParsedHeader
        {
            std::uint32_t formatVersion;
            std::uint32_t payloadCrc;
            std::uint32_t payloadLen;
        };

        // 校验 magic + formatVersion，返回剩余 header 字段。失败返回错误码。
        Result<ParsedHeader, ResultCode> ParseHeader(const std::uint8_t* src,
                                                     std::size_t         availableBytes)
        {
            if (availableBytes < kHeaderSize)
            {
                ORANGE_LOG_ERROR("SaveGameSystem: 文件不足 16 字节 header (size={})", availableBytes);
                return ResultCode::IoError;
            }
            if (src[0] != static_cast<std::uint8_t>(kMagic[0]) || src[1] != static_cast<std::uint8_t>(kMagic[1]) || src[2] != static_cast<std::uint8_t>(kMagic[2]) || src[3] != static_cast<std::uint8_t>(kMagic[3]))
            {
                ORANGE_LOG_ERROR("SaveGameSystem: header magic 不匹配（不是 OrangeEngine 存档）");
                return ResultCode::IoError;
            }
            ParsedHeader h{};
            h.formatVersion = DecodeUint32LE(src + 4);
            h.payloadCrc    = DecodeUint32LE(src + 8);
            h.payloadLen    = DecodeUint32LE(src + 12);

            if (h.formatVersion != kHeaderFormatVersion)
            {
                ORANGE_LOG_ERROR("SaveGameSystem: header format version 不匹配 (file={}, expected={})",
                                 h.formatVersion, kHeaderFormatVersion);
                return ResultCode::IoError;
            }
            return h;
        }

    } // namespace

    // ---------------------------------------------------------------------------
    // SaveGameSystem
    // ---------------------------------------------------------------------------

    SaveGameSystem::SaveGameSystem(const SaveGameRegistry& registry) noexcept
        : mRegistry(registry)
    {
    }

    // ---------------------------------------------------------------------------
    // Save
    // ---------------------------------------------------------------------------

    Result<void, ResultCode> SaveGameSystem::Save(const World& world, std::string_view path) const
    {
        // 1) 收集所有"挂着 SaveableComponent"的 entity，按 view 顺序分配持
        //    久 ID。无 SaveableComponent 的 entity（关卡 prop / 相机 / 灯光
        //    等）由 Scene 序列化负责，本流程不碰。
        auto                view = world.Registry().view<const SaveableComponent>();
        std::vector<Entity> entityList;
        entityList.reserve(view.size());
        for (auto e : view)
        {
            entityList.push_back(World::FromEntt(e));
        }

        // 2) 组装 JSON payload。
        JsonWriter writer;
        writer.WriteSchemaVersion(kSchemaVersionPath, SaveSchemaVersion());
        writer.BeginArray(kEntitiesPath, entityList.size());

        const auto& entries = mRegistry.Entries();

        for (std::size_t i = 0; i < entityList.size(); ++i)
        {
            const std::string base   = EntityBasePath(i);
            const Entity      entity = entityList[i];

            writer.WriteInt(base + "/id", static_cast<std::int64_t>(i));

            // 按注册顺序遍历——`Has` 命中才写出。"哪些 component 该入存档"
            // 完全由 game 侧通过 Register / 是否 attach 决定，引擎不内置策略。
            for (const auto& entry : entries)
            {
                if (!entry.Has || !entry.Has(world, entity))
                {
                    continue;
                }

                const std::string componentBase = ComponentBasePath(base, entry.name);

                // per-component schemaVersion —— 一旦存档发到玩家手里，每条
                // component 的 version 即冻结；后续游戏侧 bump 后由
                // migrator hook 接管。
                writer.WriteSchemaVersion(componentBase + "/version", entry.version);

                // 用户字段统一落在 .../data 子树下，与 .../version 隔开。
                const std::string dataPath = componentBase + "/data";
                entry.Write(writer, dataPath, world, entity);
            }
        }

        // 3) 序列化为 UTF-8 文本（indent=2 保持 diff 友好）。
        const std::string payloadText = writer.Dump(2);
        const std::size_t payloadLen  = payloadText.size();
        if (payloadLen > 0xFFFFFFFFu)
        {
            // header 用 uint32 编码 length，2^32 字节的存档基本不存在但理论
            // 上要拒掉。
            ORANGE_LOG_ERROR("SaveGameSystem: payload 超过 4 GiB header 不支持 (size={})",
                             payloadLen);
            return ResultCode::Unsupported;
        }

        // 4) 计算 payload CRC。
        const std::uint32_t crc = Crc32(payloadText.data(), payloadLen);

        // 5) 拼接 header + payload 成一段连续字节流。
        std::vector<std::uint8_t> bytes(kHeaderSize + payloadLen);
        WriteHeader(bytes.data(), crc, static_cast<std::uint32_t>(payloadLen));
        if (payloadLen > 0)
        {
            std::memcpy(bytes.data() + kHeaderSize, payloadText.data(), payloadLen);
        }

        // 6) 原子写入。
        return Detail::WriteFileAtomic(std::filesystem::path(std::string{path}), bytes);
    }

    // ---------------------------------------------------------------------------
    // Load
    // ---------------------------------------------------------------------------

    namespace
    {

        // Load 失败时把本次创建的 entity 全部销毁，让 World 回到调用前状态。
        void RollbackCreatedEntities(World& world, const std::vector<Entity>& created) noexcept
        {
            for (auto e : created)
            {
                world.DestroyEntity(e);
            }
        }

        // 从 entry.migrators 里找一条 from == cur 的记录；找不到返 nullptr。
        // migrator 链短（典型 2-5 条），线性扫描即可。
        const SaveGameComponentEntry::Migrator*
        FindMigratorFrom(const SaveGameComponentEntry& entry, const SchemaVersion& cur) noexcept
        {
            for (const auto& m : entry.migrators)
            {
                if (m.from == cur)
                {
                    return &m;
                }
            }
            return nullptr;
        }

        // 链上限 —— migrator 注册时已 reject from == to，正常 chain 不会循环；
        // 但跨 namespace migration 容易写出 v1@nsA → v2@nsB → v1@nsA 这类回环，
        // 加个上限作为最后一道防线。
        constexpr int kMaxMigrationHops = 32;

    } // namespace

    Result<void, ResultCode> SaveGameSystem::Load(std::string_view path, World& world) const
    {
        // 1) 读全文件到内存。
        auto bytesResult = BinaryReader::LoadFile(path);
        if (bytesResult.IsErr())
        {
            return bytesResult.Error();
        }
        const std::vector<std::uint8_t>& bytes = bytesResult.Value();

        // 2) 解 header。magic / formatVersion 在 ParseHeader 内 fail-fast。
        auto headerResult = ParseHeader(bytes.data(), bytes.size());
        if (headerResult.IsErr())
        {
            return headerResult.Error();
        }
        const ParsedHeader header = headerResult.Value();

        // 3) 校 payloadLength —— 文件实际剩余字节必须等于 header 声明长度。
        //    多 / 少都视为损坏（不允许 trailing garbage / 截断）。
        const std::size_t actualPayload = bytes.size() - kHeaderSize;
        if (actualPayload != header.payloadLen)
        {
            ORANGE_LOG_ERROR(
                "SaveGameSystem: payload length 不匹配 (header={}, actual={})",
                header.payloadLen, actualPayload);
            return ResultCode::InvalidArgument;
        }

        // 4) 校 CRC。计算实际 payload 的 CRC 与 header 中的对比。
        const std::uint32_t actualCrc = Crc32(bytes.data() + kHeaderSize, actualPayload);
        if (actualCrc != header.payloadCrc)
        {
            ORANGE_LOG_ERROR(
                "SaveGameSystem: payload CRC 不匹配 (header=0x{:08x}, actual=0x{:08x})",
                header.payloadCrc, actualCrc);
            return ResultCode::InvalidArgument;
        }

        // 5) 解析 JSON。Core::Serialization 的 JsonReader 对损坏 JSON 直接
        //    返回 ParseError。
        const std::string_view payloadText{
            reinterpret_cast<const char*>(bytes.data() + kHeaderSize), actualPayload};
        auto readerResult = JsonReader::FromString(payloadText);
        if (readerResult.IsErr())
        {
            ORANGE_LOG_ERROR("SaveGameSystem: JSON 解析失败 (path='{}', message='{}')",
                             readerResult.Error().path, readerResult.Error().message);
            return ResultCode::InvalidArgument;
        }
        JsonReader reader = std::move(readerResult).Value();

        // 6) 校顶层 schema。
        auto fileSchemaResult = reader.ReadSchemaVersion(kSchemaVersionPath);
        if (fileSchemaResult.IsErr())
        {
            ORANGE_LOG_ERROR("SaveGameSystem: 顶层 schemaVersion 缺失 / 损坏");
            return ResultCode::InvalidArgument;
        }
        const SchemaVersion fileSchema = std::move(fileSchemaResult).Value();
        if (!SaveSchemaVersion().CanRead(fileSchema))
        {
            ORANGE_LOG_ERROR(
                "SaveGameSystem: 顶层 schemaVersion 不兼容 "
                "(file=ns='{}' v{}.{}, expected=ns='{}' v{}.{})",
                fileSchema.Namespace(), fileSchema.Major(), fileSchema.Minor(),
                SaveSchemaVersion().Namespace(),
                SaveSchemaVersion().Major(),
                SaveSchemaVersion().Minor());
            return ResultCode::SchemaMismatch;
        }

        // 7) 遍历 entities，逐个 entity 创建 + attach SaveableComponent + 按
        //    registry 反序列化每个组件。任一中间步骤失败 → 回滚本次 Load 创
        //    建的全部 entity。
        const std::size_t entityCount = reader.ArraySize(kEntitiesPath);

        std::vector<Entity> created;
        created.reserve(entityCount);

        const auto& entries = mRegistry.Entries();

        for (std::size_t i = 0; i < entityCount; ++i)
        {
            const std::string base = EntityBasePath(i);

            Entity entity = world.CreateEntity();
            created.push_back(entity);
            // SaveableComponent 是空结构体；EnTT 对空类型走存储优化，
            // emplace_or_replace 返回 void —— World::AddComponent 模板要求
            // T& 返回值，类型不匹配。绕开 World::AddComponent 直接走 entt
            // 的 registry 接口，与 World 暴露 Registry() 的"逃生舱口"语义
            // 一致。
            world.Registry().emplace_or_replace<SaveableComponent>(World::ToEntt(entity));

            for (const auto& entry : entries)
            {
                const std::string componentBase = ComponentBasePath(base, entry.name);
                if (!reader.Has(componentBase))
                {
                    continue;
                }

                // per-component schema 校验 —— 不通过直接 fail，让调用方
                // （migrator hook）决定是否做迁移。
                auto compSchemaResult = reader.ReadSchemaVersion(componentBase + "/version");
                if (compSchemaResult.IsErr())
                {
                    ORANGE_LOG_ERROR(
                        "SaveGameSystem: component '{}' 缺 'version' 字段 (entity {})",
                        entry.name, i);
                    RollbackCreatedEntities(world, created);
                    return ResultCode::InvalidArgument;
                }
                const SchemaVersion fileCompSchema = std::move(compSchemaResult).Value();
                const std::string   dataPath       = componentBase + "/data";

                if (entry.version.CanRead(fileCompSchema))
                {
                    // 直读路径——典型情况，无需 migrator。
                    if (!entry.Read(reader, dataPath, world, entity))
                    {
                        ORANGE_LOG_ERROR(
                            "SaveGameSystem: component '{}' read 失败 (entity {})",
                            entry.name, i);
                        RollbackCreatedEntities(world, created);
                        return ResultCode::InvalidArgument;
                    }
                }
                else
                {
                    // schema 不匹配 —— 走 migrator chain。
                    //
                    // 算法：从 fileCompSchema 起，循环找 entry.migrators 中
                    // from == 当前版本的记录，apply（migrator 写到 fresh
                    // JsonWriter，dump+reparse 成新 reader），直到当前版本
                    // 满足 entry.version.CanRead 或链断（→ SchemaMismatch）。
                    //
                    // 第一次迭代的输入是文件 reader 在 dataPath 子树；后续
                    // 迭代输入是上一次迁移产出的"独立"reader，路径固定为
                    // "data"（migrator 在自己的 fresh writer 里写到 data/...）。
                    JsonReader        migratedReader;
                    bool              hasMigratedReader = false;
                    const JsonReader* curReader         = &reader;
                    std::string       curPath           = dataPath;
                    SchemaVersion     curVer            = fileCompSchema;
                    int               hops              = 0;

                    while (!entry.version.CanRead(curVer))
                    {
                        if (++hops > kMaxMigrationHops)
                        {
                            ORANGE_LOG_ERROR(
                                "SaveGameSystem: component '{}' migrator chain 超过 {} 跳"
                                "（疑似环形 / 跨 namespace 死循环）",
                                entry.name, kMaxMigrationHops);
                            RollbackCreatedEntities(world, created);
                            return ResultCode::SchemaMismatch;
                        }

                        const auto* mig = FindMigratorFrom(entry, curVer);
                        if (mig == nullptr)
                        {
                            ORANGE_LOG_ERROR(
                                "SaveGameSystem: component '{}' schemaVersion 不兼容且无 migrator "
                                "(file=ns='{}' v{}.{}, expected=ns='{}' v{}.{}, current=ns='{}' v{}.{})",
                                entry.name,
                                fileCompSchema.Namespace(), fileCompSchema.Major(), fileCompSchema.Minor(),
                                entry.version.Namespace(), entry.version.Major(), entry.version.Minor(),
                                curVer.Namespace(), curVer.Major(), curVer.Minor());
                            RollbackCreatedEntities(world, created);
                            return ResultCode::SchemaMismatch;
                        }

                        JsonWriter                 dst;
                        constexpr std::string_view kMigratedPath{"data"};
                        if (!mig->fn(*curReader, curPath, dst, kMigratedPath))
                        {
                            ORANGE_LOG_ERROR(
                                "SaveGameSystem: migrator (component='{}', from=ns='{}' v{}.{} → "
                                "to=ns='{}' v{}.{}) 返回 false (entity {})",
                                entry.name,
                                mig->from.Namespace(), mig->from.Major(), mig->from.Minor(),
                                mig->to.Namespace(), mig->to.Major(), mig->to.Minor(), i);
                            RollbackCreatedEntities(world, created);
                            return ResultCode::InvalidArgument;
                        }

                        auto parsed = JsonReader::FromString(dst.Dump());
                        if (parsed.IsErr())
                        {
                            // migrator 用 JsonWriter 写出来再 dump 应当永远是合法
                            // JSON——走到这里属于内部 invariant 破裂。
                            ORANGE_LOG_ERROR(
                                "SaveGameSystem: migrator 产出的 JSON re-parse 失败 "
                                "(component='{}', message='{}')",
                                entry.name, parsed.Error().message);
                            RollbackCreatedEntities(world, created);
                            return ResultCode::InternalError;
                        }
                        migratedReader    = std::move(parsed).Value();
                        hasMigratedReader = true;
                        curReader         = &migratedReader;
                        curPath           = std::string(kMigratedPath);
                        curVer            = mig->to;
                    }

                    // 链走完，curReader / curPath 指向"已升级到 entry.version
                    // 可读"的数据。
                    (void)hasMigratedReader; // 仅文档化"我们确实做了至少一次迁移"
                    if (!entry.Read(*curReader, curPath, world, entity))
                    {
                        ORANGE_LOG_ERROR(
                            "SaveGameSystem: 迁移后 component '{}' read 失败 (entity {})",
                            entry.name, i);
                        RollbackCreatedEntities(world, created);
                        return ResultCode::InvalidArgument;
                    }
                }
            }

            // 未识别的 component key（forward-compat）—— 当前 JsonReader
            // 没有"列出对象 key"接口，所以仅做"跳过"，不发 warning。与
            // SceneSerialization.cpp 保持一致行为，等 reader API 扩展后再补
            // 逐个未知字段的 warn。
        }

        return {};
    }

} // namespace Orange::Engine::Save
