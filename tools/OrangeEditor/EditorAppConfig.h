#ifndef ORANGE_EDITOR_EDITOR_APP_CONFIG_H
#define ORANGE_EDITOR_EDITOR_APP_CONFIG_H

// EditorAppConfig —— 编辑器应用配置（M3 step2）。
//
// 显式注入路径 + 游戏模块，替代原 main() 里的硬编码 + `ChdirToRepoRoot()`
// cwd 全局副作用。两类宿主共用 `RunEditorApp(argc, argv, EditorAppConfig)`：
//   * 原版 OrangeEditor.exe —— 默认构造（全空字段）→ RunEditorApp 内
//     DeriveDefaults 从 exe 位置向上找仓库根标记，复现原 ChdirToRepoRoot 行为，
//     零行为变化；
//   * per-game editor（M4 起 SlimeEditor.exe）—— 填 projectRoot 指向游戏仓
//     资产根 + modules 注入自家 IGameModule，不复制 main() 装配逻辑。
//
// 头文件隔离：只 include IGameModule.h（无第三方依赖的公共头）+ 标准库。

#include <orange/engine/game/IGameModule.h>

#include <memory>
#include <string>
#include <vector>

namespace Orange::Editor
{

    struct EditorAppConfig
    {
        // 资产 / 配置 / 编辑器资源解析基准目录（绝对路径）。空 = RunEditorApp
        // 从 exe 位置向上找仓库根标记推导（原 ChdirToRepoRoot 语义），并 chdir
        // 过去（兼容现行相对路径 IO）；非空 = 直接用该目录为基准，**不 chdir**
        // （消灭全局 cwd 副作用，consumer 场景必走此路）。
        std::string projectRoot;

        // 编辑器自带资源（codicon 字体 / theme / editor shader spv）所在根目录
        //（绝对路径）。空 = 从 exe 目录旁推导（installed 布局）或回退 projectRoot
        // 下的源树位置（in-tree 开发）。consumer 的 install 把这些资产装到 exe
        // 旁，故一般留空由 exe-relative 推导。
        std::string editorResourceRoot;

        // 启动加载的场景（相对 projectRoot 或绝对）。空 = 不指定启动场景，走
        // RunEditorApp 的默认逻辑（原版 = 试 demo.scene.json 否则 SeedDemoWorld；
        // consumer 可传自家场景，或留空由游戏模块 OnEnterPlay 自 seed 关卡——
        // spike-01 现状即模块代码生成关卡）。
        std::string startupScene;

        // 原生窗口标题。空 = "OrangeEditor"。per-game editor 填 "SlimeEditor" 等。
        std::string windowTitle;

        // 游戏模块注入（ADR-021）。RunEditorApp 启动装配段把它们逐个 AddModule
        // 进 EditorHost.gameModules（所有权转移进宿主），随后走注册期扇出
        //（RegisterRenderPasses / CollectSerializers）+ Play 生命周期。空 =
        // 原版无模块编辑器。move-only（IGameModule 不可拷贝）。
        std::vector<std::unique_ptr<Orange::Engine::Game::IGameModule>> modules;
    };

} // namespace Orange::Editor

#endif // ORANGE_EDITOR_EDITOR_APP_CONFIG_H
