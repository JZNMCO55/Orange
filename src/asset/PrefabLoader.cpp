// PrefabLoader 实现 —— prefab/asset 1.0（形态 B）的读 / 写。
//
// 头隔离：本文件不直接 include <nlohmann/json.hpp>；JSON 全走 Core::
// Serialization 的 JsonReader / JsonWriter。

#include "orange/engine/asset/PrefabLoader.h"

#include "orange/engine/core/Log.h"
#include "orange/engine/core/Serialization.h"

#include <string>
#include <utility>

namespace Orange::Engine::Asset
{
    namespace
    {

        constexpr std::string_view kSchemaVersionPath = "schemaVersion";
        constexpr std::string_view kPrefabNamePath    = "prefabName";
        constexpr std::string_view kTemplatePath      = "template";

        // reader 端期望的 schema：namespace bit-for-bit + major == kSchemaMajor +
        // minor 向后兼容（CanRead 内部 reader.minor >= file.minor）。
        const SchemaVersion& ExpectedSchema()
        {
            static const SchemaVersion kVersion{
                PrefabLoader::kSchemaNamespace, PrefabLoader::kSchemaMajor, PrefabLoader::kSchemaMinor};
            return kVersion;
        }

    } // namespace

    Result<std::unique_ptr<PrefabAsset>, ResultCode> PrefabLoader::Load(std::string_view path)
    {
        auto readerResult = JsonReader::FromFile(path);
        if (readerResult.IsErr())
        {
            // FromFile 把文件不存在 / IO 失败 / JSON 解析失败统一成 ParseError。
            // 这里粗粒度映射为 InvalidArgument——上层 AssetRegistry 只关心
            // 成功与否；细节走日志。
            ORANGE_LOG_ERROR("PrefabLoader: 解析 .prefab.json 失败 (path={}, msg={})",
                             std::string(path), readerResult.Error().message);
            return ResultCode::InvalidArgument;
        }
        const JsonReader& reader = readerResult.Value();

        // schemaVersion 校验：namespace 必须匹配 prefab/asset + major == 1。
        auto verResult = reader.ReadSchemaVersion(kSchemaVersionPath);
        if (verResult.IsErr())
        {
            ORANGE_LOG_ERROR("PrefabLoader: .prefab.json schemaVersion 缺失或错误 (path={})",
                             std::string(path));
            return ResultCode::SchemaMismatch;
        }
        const SchemaVersion fileVer = verResult.Value();
        if (!ExpectedSchema().CanRead(fileVer))
        {
            ORANGE_LOG_ERROR("PrefabLoader: .prefab.json schemaVersion 不兼容 "
                             "(path={}, ns={}, ver={}.{})",
                             std::string(path), fileVer.Namespace(),
                             fileVer.Major(), fileVer.Minor());
            return ResultCode::SchemaMismatch;
        }

        std::string prefabName;
        // prefabName 缺失视为数据坏。空字符串允许（仅显示名）。
        if (!reader.ReadString(kPrefabNamePath, prefabName))
        {
            ORANGE_LOG_ERROR("PrefabLoader: .prefab.json prefabName 缺失 (path={})",
                             std::string(path));
            return ResultCode::InvalidArgument;
        }

        std::string templateBlob;
        if (!reader.ReadString(kTemplatePath, templateBlob))
        {
            ORANGE_LOG_ERROR("PrefabLoader: .prefab.json template 缺失 (path={})",
                             std::string(path));
            return ResultCode::InvalidArgument;
        }

        return std::make_unique<PrefabAsset>(std::move(prefabName), std::move(templateBlob));
    }

    Result<void, ResultCode> PrefabLoader::Save(std::string_view path,
                                                std::string_view prefabName,
                                                std::string_view templateBlob)
    {
        JsonWriter writer;
        writer.WriteSchemaVersion(kSchemaVersionPath, ExpectedSchema());
        writer.WriteString(kPrefabNamePath, prefabName);
        // template 以 JSON 字符串字段落盘（形态 B）：JsonWriter 自会对内嵌引号
        // 等做转义；Load 端 ReadString 反转义后得到与 templateBlob 字节一致的原文。
        writer.WriteString(kTemplatePath, templateBlob);

        auto saveResult = writer.SaveToFile(path);
        if (saveResult.IsErr())
        {
            ORANGE_LOG_ERROR("PrefabLoader: 写 .prefab.json 失败 (path={})", std::string(path));
            return saveResult.Error();
        }
        return {};
    }

} // namespace Orange::Engine::Asset
