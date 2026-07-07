// `.orangeproject` 清单 → EditorAppConfig 的路径解析桥（M6，见 ProjectConfig.h）。

#include "project/ProjectConfig.h"

#include "project/ProjectFile.h"

#include <orange/engine/core/Log.h>

#include <filesystem>
#include <system_error>

namespace Orange::Editor::Project
{

    bool ApplyProjectFileToConfig(const std::string& projectFilePath, EditorAppConfig& config)
    {
        namespace fs = std::filesystem;
        auto         pr = LoadProjectFile(projectFilePath);
        if (pr.IsErr())
        {
            ORANGE_LOG_ERROR("[OrangeEditor] --project 加载失败：{}（code={}）—— 回退默认路径",
                             projectFilePath,
                             static_cast<unsigned>(pr.Error()));
            return false;
        }
        const ProjectFile& proj = pr.Value();

        std::error_code ec;
        const fs::path  projPath = fs::absolute(fs::path(projectFilePath), ec);
        const fs::path  projDir  = projPath.parent_path();
        // assetRoots 恒非空（ProjectFile 缺失兜底 { "." }）。主根 = projDir / roots[0]。
        fs::path primaryRoot = fs::weakly_canonical(projDir / fs::path(proj.assetRoots.front()), ec);
        if (ec || primaryRoot.empty())
        {
            primaryRoot = projDir / fs::path(proj.assetRoots.front()); // 规整失败兜底
        }

        if (config.projectRoot.empty())
        {
            config.projectRoot = primaryRoot.string();
        }
        if (config.startupScene.empty())
        {
            config.startupScene = proj.startupScene; // 相对主根，chdir 后解析
        }
        if (config.windowTitle.empty())
        {
            config.windowTitle = !proj.name.empty() ? proj.name : projPath.stem().string();
        }
        // M7：把 kind="dll" 的 gameModules ref 解析为绝对路径填 config.dllGameModulePaths
        //（RunEditorApp 启动装配段用 GameModuleLibrary::Load 加载 + AddModuleLibrary）。
        // 相对 ref 按主项目根解析（游戏 dll 通常与项目 / exe 同根）；绝对 ref 原样。
        // "static" / "csharp" 由别处消费，此处只挑 dll。本步在 chdir 前，故解析成绝对。
        for (const ProjectGameModuleRef& mod : proj.gameModules)
        {
            if (mod.kind != "dll" || mod.ref.empty())
            {
                continue;
            }
            fs::path dllPath(mod.ref);
            if (dllPath.is_relative())
            {
                fs::path resolved = fs::weakly_canonical(primaryRoot / dllPath, ec);
                dllPath           = (ec || resolved.empty()) ? (primaryRoot / fs::path(mod.ref)) : resolved;
                ec.clear();
            }
            config.dllGameModulePaths.push_back(dllPath.string());
            ORANGE_LOG_INFO("[OrangeEditor] 项目声明 DLL 游戏模块：{}", dllPath.string());
        }

        ORANGE_LOG_INFO("[OrangeEditor] 项目 '{}' 加载：projectRoot='{}' startupScene='{}'",
                        config.windowTitle, config.projectRoot, config.startupScene);
        return true;
    }

} // namespace Orange::Editor::Project
