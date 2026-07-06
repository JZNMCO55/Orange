#ifndef ORANGE_EDITOR_EDITOR_APP_H
#define ORANGE_EDITOR_EDITOR_APP_H

// EditorApp —— 编辑器应用入口（M3 lib 化）。
//
// 历史脉络：原先 OrangeEditor 是 74 个 .cpp 直编进 exe + 约 1100 行巨型
// main()，路径依赖靠 ChdirToRepoRoot() cwd 副作用兜。M3 把编辑器拆成
// `orange_editor` STATIC lib（承载全部子系统 + 应用装配）+ 瘦
// `OrangeEditor.exe`（main 仅转发 argc/argv 进 lib）。lib 化让 per-game
// editor（M4 SlimeEditor.exe）能 find_package + 链 orange_editor 复用同一
// 套装配，不必复制 main()。
//
// step1（机械搬迁 ✅）：main() 全量下沉为 RunEditorApp，行为逐字节不变。
// step2（本次）：引入 EditorAppConfig（projectRoot / editorResourceRoot /
// startupScene / windowTitle / IGameModule 列表显式注入），ChdirToRepoRoot
// 降级为"projectRoot 空时的默认值推导"，消灭 cwd 全局副作用 + imgui.ini
// 显式 SetIniFilename。

#include "EditorAppConfig.h"

namespace Orange::Editor
{

    // 编辑器应用主入口：启动装配（AppHost / RenderDevice / Renderer / ImGui /
    // EditorHost / plugin 注册 / config.modules 注入 / 启动场景）+ 主循环 + 关停
    // 序列。含 headless 资产导入 CLI 分支（import-mesh / import-scene，在任何 GUI
    // init 之前判 argv；此路径忽略 config）。返回进程退出码。
    //
    // config 按值取（move-only：内含 unique_ptr<IGameModule>），默认空 config
    // 复现原版 OrangeEditor 行为（projectRoot 空 → 推导仓库根 + chdir）。per-game
    // editor 的 main 填好 config 后 `return RunEditorApp(argc, argv, std::move(cfg));`。
    int RunEditorApp(int argc, char** argv, EditorAppConfig config = {});

} // namespace Orange::Editor

#endif // ORANGE_EDITOR_EDITOR_APP_H
