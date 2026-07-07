#ifndef ORANGE_EDITOR_PROJECT_PROJECT_CONFIG_H
#define ORANGE_EDITOR_PROJECT_PROJECT_CONFIG_H

// ProjectConfig —— 把 `.orangeproject` 清单解析结果桥接进 EditorAppConfig（M6）。
//
// 从 ProjectFile（纯序列化）跨到 EditorAppConfig（编辑器启动装配）的一步路径解析：
// 主项目根 = 清单目录 / assetRoots[0] 绝对化 → projectRoot（RunEditorApp 随后 chdir，
// 任意 cwd 启动都能解析相对资产）；startupScene 相对主根；windowTitle 取项目名。
//
// 两个消费方共用：①原版 OrangeEditor 的 `--project <path>` CLI；②per-game editor
// （SlimeEditor）加载自家 `slime.orangeproject`。故导出为独立函数而非埋在 EditorApp.cpp。
//
// 头隔离：只依赖 EditorAppConfig.h（其本身只引 IGameModule 引擎公共头）+ std——不碰
// Vulkan/ImGui，可单独编进 headless 测试验路径解析。

#include "../EditorAppConfig.h"

#include <string>

namespace Orange::Editor::Project
{

    // 加载 `.orangeproject` 清单并填进 config（M6）。**只填 config 的空字段**——不覆盖
    // 调用方（per-game editor）已显式设的值：`--project` 给原版编辑器用、per-game editor
    // 走编译期 config 注入，二者语义上不叠加。
    //
    // 路径解析（ADR-022）：projectRoot = 清单目录 / assetRoots[0]（绝对化 + weakly_
    // canonical）；startupScene = 清单的 startupScene（相对主根，chdir 后解析）；
    // windowTitle = 项目名（缺省用清单文件名 stem）。
    //
    // 返回 false（文件不存在 / 解析错 / schema 不匹配）时 config 不被改动——调用方据此
    // 回退默认路径，不因坏清单而崩。
    bool ApplyProjectFileToConfig(const std::string& projectFilePath, EditorAppConfig& config);

} // namespace Orange::Editor::Project

#endif // ORANGE_EDITOR_PROJECT_PROJECT_CONFIG_H
