// `.orangeproject` 工程清单的解析 / 序列化往返（PIE 工程模型）。
//
// 锁住的不变量：
//   (a) 满字段 ProjectFile → SerializeProjectFile → ParseProjectFile 后所有字段
//       逐一存活（name / 多 assetRoots / startupScene / csharp gameModule /
//       clearColor）；
//   (b) 仅带 schemaVersion + name + assetRoots 的最小 JSON 解析成功，所有可选字段
//       （startupScene / gameModules / renderSettings）落到默认，不报错；
//   (c) schema namespace 错的 JSON 解析返回 Err（SchemaMismatch），不产出。
//
// 纯 Core::Serialization + ProjectFile 逻辑，无 Vulkan / GUI / EditorHost 依赖：
// ProjectFile.cpp 直接编进本测试 exe，只链 orange_engine（同 material_file_io_test
// / editor_settings_test 模式）。

#include "project/ProjectFile.h"

#include <cassert>
#include <cstdio>
#include <string>

using Orange::Editor::Project::ParseProjectFile;
using Orange::Editor::Project::ProjectFile;
using Orange::Editor::Project::ProjectGameModuleRef;
using Orange::Editor::Project::SerializeProjectFile;

namespace
{

    // (a) 满字段往返：构造 → 序列化 → 解析 → 逐字段比对。
    void TestFullRoundTrip()
    {
        ProjectFile in;
        in.name         = "Orange Demo";
        in.assetRoots   = {"assets", "shared/assets"};
        in.startupScene = "assets/scenes/pbr_showcase.scene.json";
        in.gameModules.push_back(ProjectGameModuleRef{"csharp", "scripts/Game.dll"});
        in.hasClearColor = true;
        in.clearColor    = glm::vec3(0.1f, 0.1f, 0.12f);

        auto serialized = SerializeProjectFile(in);
        assert(serialized.IsOk());

        auto parsed = ParseProjectFile(serialized.Value());
        assert(parsed.IsOk());
        const ProjectFile& out = parsed.Value();

        assert(out.name == "Orange Demo");
        assert(out.assetRoots.size() == 2);
        assert(out.assetRoots[0] == "assets");
        assert(out.assetRoots[1] == "shared/assets");
        assert(out.startupScene == "assets/scenes/pbr_showcase.scene.json");
        assert(out.gameModules.size() == 1);
        assert(out.gameModules[0].kind == "csharp");
        assert(out.gameModules[0].ref == "scripts/Game.dll");
        assert(out.hasClearColor);
        assert(out.clearColor == glm::vec3(0.1f, 0.1f, 0.12f));

        std::printf("  [ok] 满字段 SerializeProjectFile → ParseProjectFile 往返保真\n");
    }

    // (b) 最小 JSON：只有 schemaVersion + name + assetRoots，可选字段全缺 → 默认。
    void TestMinimalDefaults()
    {
        const std::string minimal = R"({
  "schemaVersion": { "namespace": "orange/project", "major": 1, "minor": 0 },
  "name": "Minimal",
  "assetRoots": ["content"]
})";

        auto parsed = ParseProjectFile(minimal);
        assert(parsed.IsOk());
        const ProjectFile& out = parsed.Value();

        assert(out.name == "Minimal");
        assert(out.assetRoots.size() == 1);
        assert(out.assetRoots[0] == "content");
        // 可选字段缺失 → 默认：空 startupScene / 空 gameModules / 无 clearColor。
        assert(out.startupScene.empty());
        assert(out.gameModules.empty());
        assert(!out.hasClearColor);

        std::printf("  [ok] 最小 JSON 可选字段缺失 → 默认，无错\n");
    }

    // (c) schema namespace 错 → Err（SchemaMismatch）。
    void TestWrongSchemaNamespaceRejected()
    {
        const std::string wrongNs = R"({
  "schemaVersion": { "namespace": "scene/world", "major": 1, "minor": 0 },
  "name": "WrongNamespace",
  "assetRoots": ["assets"]
})";

        auto parsed = ParseProjectFile(wrongNs);
        assert(parsed.IsErr());
        assert(parsed.Error() == Orange::Engine::ResultCode::SchemaMismatch);

        std::printf("  [ok] schema namespace 错 → SchemaMismatch（不产出）\n");
    }

} // namespace

int main()
{
    std::printf("[project_file_round_trip_test]\n");
    TestFullRoundTrip();
    TestMinimalDefaults();
    TestWrongSchemaNamespaceRejected();
    std::printf("[project_file_round_trip_test] all passed\n");
    return 0;
}
