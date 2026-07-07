#ifndef ORANGE_EDITOR_PROJECT_PROJECT_FILE_H
#define ORANGE_EDITOR_PROJECT_PROJECT_FILE_H

// ProjectFile —— `.orangeproject` 工程模型的解析 / 序列化（PIE 工程模型）。
//
// 一个 `.orangeproject` 是编辑器打开一个"项目"的顶层清单：项目名（→ 窗口标题）、
// 资产根列表、启动场景、游戏模块引用（static / dll / csharp）、可选渲染设置。
// 编辑器据此在启动时装配 EditorAppConfig（projectRoot / startupScene / windowTitle
// / 游戏模块）而不再靠硬编码。
//
// 头隔离：本头刻意只依赖引擎公共 Result + glm + 标准库，**不**碰任何
// editor / Vulkan / ImGui 头——这样 ProjectFile.cpp 能被单独编进 headless 测试
// （只链 orange_engine），与 Core::Serialization 层同款"可离屏往返"纪律。实际
// JSON 读写走 Core::Serialization（JsonReader / JsonWriter），实现落在 .cpp。

#include <orange/engine/core/Result.h>

#include <glm/vec3.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Orange::Editor::Project
{

    // 单条游戏模块引用。kind 决定 ref 的含义；三种宿主对齐 ADR-021 双语言 +
    // 未来 dll 宿主：
    //   * "static"  —— 编译进 exe 的 IGameModule（ref = 模块名，仅信息用途）；
    //   * "dll"     —— 原生动态库宿主（ref = dll 路径，预留能力，尚未接线）；
    //   * "csharp"  —— C# 脚本宿主（ref = 托管程序集路径）。
    struct ProjectGameModuleRef
    {
        std::string kind; // "static" | "dll" | "csharp"
        std::string ref;  // dll: 路径；csharp: 程序集路径；static: 模块名（信息用途）
    };

    struct ProjectFile
    {
        // 项目名 —— 映射到编辑器原生窗口标题。空 = 无名（调用方用文件名兜底）。
        std::string name;

        // 资产根，均相对 .orangeproject 所在目录；assetRoots[0] = 主项目根。
        // 解析时缺失 / 空数组会兜底成 { "." }（见 .cpp ReadInto）。
        std::vector<std::string> assetRoots;

        // 启动场景，相对主项目根；空 = 无启动场景。
        std::string startupScene;

        // 游戏模块引用（可选）。工程模型主要作元数据登记；dll 宿主为预留，
        // csharp / static 已可被编辑器装配消费。
        std::vector<ProjectGameModuleRef> gameModules;

        // renderSettings.clearColor（可选）。hasClearColor=false 时 clearColor
        // 字段无意义（不落盘），编辑器退回引擎默认清屏色。
        bool      hasClearColor = false;
        glm::vec3 clearColor{0.0f};
    };

    // 从磁盘文件读取并解析。文件不存在 / 不可读 → IoError；内容语法坏 →
    // InvalidArgument；schema namespace / major 不匹配 → SchemaMismatch（此时
    // 不产出 ProjectFile）。成功 → 填好的 ProjectFile。
    Orange::Engine::Result<ProjectFile, Orange::Engine::ResultCode>
    LoadProjectFile(std::string_view path);

    // 从内存 JSON 文本解析（LoadProjectFile 与之共享校验 + 读取核心，仅 reader
    // 来源不同——对齐 SceneSerialization 的 file / string 双入口）。语法坏 →
    // InvalidArgument；schema 不匹配 → SchemaMismatch。
    Orange::Engine::Result<ProjectFile, Orange::Engine::ResultCode>
    ParseProjectFile(std::string_view jsonText);

    // 序列化到磁盘文件。写失败 → IoError。
    Orange::Engine::Result<void, Orange::Engine::ResultCode>
    SaveProjectFile(const ProjectFile& project, std::string_view path);

    // 序列化到内存 JSON 文本。
    Orange::Engine::Result<std::string, Orange::Engine::ResultCode>
    SerializeProjectFile(const ProjectFile& project);

} // namespace Orange::Editor::Project

#endif // ORANGE_EDITOR_PROJECT_PROJECT_FILE_H
