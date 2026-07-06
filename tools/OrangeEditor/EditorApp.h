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
// step1（本次，机械搬迁）：main() 全量下沉为 RunEditorApp，行为逐字节不变。
// step2（后续）：引入 EditorAppConfig（projectRoot / configDir / assetRoot /
// IGameModule 列表显式注入），ChdirToRepoRoot 降级为"无配置时默认值推导"，
// 消灭 cwd 全局副作用 + imgui.ini 显式 SetIniFilename。

namespace Orange::Editor
{

    // 编辑器应用主入口：启动装配（AppHost / RenderDevice / Renderer / ImGui /
    // EditorHost / plugin 注册 / 启动场景）+ 主循环 + 关停序列。含 headless
    // 资产导入 CLI 分支（import-mesh / import-scene，在任何 GUI init 之前判 argv）。
    // 返回进程退出码。exe 端 main() 直接 `return RunEditorApp(argc, argv);`。
    int RunEditorApp(int argc, char** argv);

} // namespace Orange::Editor

#endif // ORANGE_EDITOR_EDITOR_APP_H
