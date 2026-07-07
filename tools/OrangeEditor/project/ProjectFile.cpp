// `.orangeproject` 工程清单的读 / 写实现。
//
// 全程走 Core::Serialization 公共面（JsonReader / JsonWriter / SchemaVersion）——
// 不直接碰 nlohmann::json（项目 invariant）。字符串数组必须先 BeginArray 再逐
// 元素写：JsonWriter 的路径自动建树对"纯数字段"仅在节点已是数组时当下标，否则
// 会把 "assetRoots/0" 建成 object 键 "0"，回读 ArraySize 会得 0 而丢数据——故
// 与 SceneSerialization 写 entities 数组同款，先 BeginArray 占坑。

#include "project/ProjectFile.h"

#include <orange/engine/core/Serialization.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace Orange::Editor::Project
{
    namespace
    {
        using Orange::Engine::JsonReader;
        using Orange::Engine::JsonWriter;
        using Orange::Engine::Result;
        using Orange::Engine::ResultCode;
        using Orange::Engine::SchemaVersion;

        // .orangeproject 顶层 schema 版本。独立 namespace "orange/project"，与
        // scene/world、editor/settings 各自演进、互不牵连版本号。出厂即冻结：加
        // 字段走 minor bump（旧文件缺字段时 reader 填默认，向后兼容）。
        const SchemaVersion& ProjectSchemaVersion()
        {
            static const SchemaVersion kVersion{"orange/project", 1, 0};
            return kVersion;
        }

        constexpr std::string_view kSchemaVersionPath = "schemaVersion";
        constexpr std::string_view kNamePath          = "name";
        constexpr std::string_view kAssetRootsPath    = "assetRoots";
        constexpr std::string_view kStartupScenePath  = "startupScene";
        constexpr std::string_view kGameModulesPath   = "gameModules";
        constexpr std::string_view kClearColorPath    = "renderSettings/clearColor";

        // 数组元素路径 "<base>/<index>"。
        std::string IndexPath(std::string_view base, std::size_t index)
        {
            std::string p;
            p.reserve(base.size() + 1 + 12);
            p.append(base);
            p.push_back('/');
            p.append(std::to_string(index));
            return p;
        }

        // 把已解析 + schema 已校验的 reader 内容读进 ProjectFile。所有字段宽容读
        // 取——缺失走默认，不报错（forward-compat：新 minor 加的 optional 字段在
        // 旧文件里缺失时保留 struct 默认值）。
        void ReadInto(const JsonReader& reader, ProjectFile& out)
        {
            // name：缺省留空串（无名项目 → 调用方用文件名兜底窗口标题）。
            reader.ReadString(kNamePath, out.name);

            // assetRoots：缺失 / 空数组兜底成 { "." }——即 .orangeproject 所在目录
            // 本身作主项目根，保证 assetRoots[0] 恒有值供路径解析（同 SceneSerialization
            // 对缺字段填宽容默认的思路）。
            out.assetRoots.clear();
            const std::size_t rootCount = reader.ArraySize(kAssetRootsPath);
            for (std::size_t i = 0; i < rootCount; ++i)
            {
                std::string root;
                if (reader.ReadString(IndexPath(kAssetRootsPath, i), root))
                {
                    out.assetRoots.push_back(std::move(root));
                }
            }
            if (out.assetRoots.empty())
            {
                out.assetRoots.emplace_back(".");
            }

            // startupScene：可选，缺失留空串（无启动场景）。
            reader.ReadString(kStartupScenePath, out.startupScene);

            // gameModules：可选，缺失留空 vector。逐元素读 kind + ref（各自宽容）。
            out.gameModules.clear();
            const std::size_t moduleCount = reader.ArraySize(kGameModulesPath);
            for (std::size_t i = 0; i < moduleCount; ++i)
            {
                const std::string    base = IndexPath(kGameModulesPath, i);
                ProjectGameModuleRef moduleRef;
                reader.ReadString(base + "/kind", moduleRef.kind);
                reader.ReadString(base + "/ref", moduleRef.ref);
                out.gameModules.push_back(std::move(moduleRef));
            }

            // renderSettings.clearColor：可选，present 才置 hasClearColor。
            float clear[3];
            if (reader.ReadFloatArray(kClearColorPath, clear, 3))
            {
                out.hasClearColor = true;
                out.clearColor    = glm::vec3(clear[0], clear[1], clear[2]);
            }
        }

        // schema 校验 + 字段读取核心。LoadProjectFile / ParseProjectFile 只是 reader
        // 来源不同（FromFile vs FromString），校验 + 读取逻辑完全共享。
        Result<ProjectFile, ResultCode> ParseFromReader(const JsonReader& reader)
        {
            // schema 校验：namespace bit-for-bit + major 相等 + minor 向后兼容。
            // 缺 schemaVersion 段 / 格式坏 → SchemaMismatch，不部分产出。
            auto schemaResult = reader.ReadSchemaVersion(kSchemaVersionPath);
            if (schemaResult.IsErr())
            {
                return ResultCode::SchemaMismatch;
            }
            if (!ProjectSchemaVersion().CanRead(schemaResult.Value()))
            {
                return ResultCode::SchemaMismatch;
            }

            ProjectFile project;
            ReadInto(reader, project);
            return project;
        }

        // 序列化核心。SaveProjectFile / SerializeProjectFile 共享——只是落盘 vs Dump。
        void WriteInto(JsonWriter& writer, const ProjectFile& project)
        {
            writer.WriteSchemaVersion(kSchemaVersionPath, ProjectSchemaVersion());
            writer.WriteString(kNamePath, project.name);

            // assetRoots 恒写（core 字段，assetRoots[0] = 主项目根）。BeginArray 先
            // 占真数组坑（见文件头注释），再逐元素覆盖成字符串。空则写 []。
            writer.BeginArray(kAssetRootsPath, project.assetRoots.size());
            for (std::size_t i = 0; i < project.assetRoots.size(); ++i)
            {
                writer.WriteString(IndexPath(kAssetRootsPath, i), project.assetRoots[i]);
            }

            // 以下可选段：空则整段省略，保持 .orangeproject 干净（同 SceneSerialization
            // "component 不存在就不写" 的思路，避免为默认值写噪声）。
            if (!project.startupScene.empty())
            {
                writer.WriteString(kStartupScenePath, project.startupScene);
            }

            if (!project.gameModules.empty())
            {
                writer.BeginArray(kGameModulesPath, project.gameModules.size());
                for (std::size_t i = 0; i < project.gameModules.size(); ++i)
                {
                    const std::string base = IndexPath(kGameModulesPath, i);
                    writer.WriteString(base + "/kind", project.gameModules[i].kind);
                    writer.WriteString(base + "/ref", project.gameModules[i].ref);
                }
            }

            if (project.hasClearColor)
            {
                const float arr[3] = {project.clearColor.x, project.clearColor.y,
                                      project.clearColor.z};
                writer.WriteFloatArray(kClearColorPath, arr, 3);
            }
        }

    } // namespace

    Result<ProjectFile, ResultCode> LoadProjectFile(std::string_view path)
    {
        auto readerResult = JsonReader::FromFile(path);
        if (readerResult.IsErr())
        {
            // ParseError.code 区分 IO（文件不存在 / 不可读 → IoError）与 parse
            //（语法坏 → InvalidArgument）；直接透传内层 ResultCode。
            return readerResult.Error().code;
        }
        return ParseFromReader(readerResult.Value());
    }

    Result<ProjectFile, ResultCode> ParseProjectFile(std::string_view jsonText)
    {
        auto readerResult = JsonReader::FromString(jsonText);
        if (readerResult.IsErr())
        {
            // 语法坏 → InvalidArgument（ParseError.code）；不产出 ProjectFile。
            return readerResult.Error().code;
        }
        return ParseFromReader(readerResult.Value());
    }

    Result<void, ResultCode> SaveProjectFile(const ProjectFile& project, std::string_view path)
    {
        JsonWriter writer;
        WriteInto(writer, project);
        auto saveResult = writer.SaveToFile(path);
        if (saveResult.IsErr())
        {
            return saveResult.Error();
        }
        return Result<void, ResultCode>{};
    }

    Result<std::string, ResultCode> SerializeProjectFile(const ProjectFile& project)
    {
        JsonWriter writer;
        WriteInto(writer, project);
        return writer.Dump();
    }

} // namespace Orange::Editor::Project
