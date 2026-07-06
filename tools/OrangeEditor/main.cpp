// OrangeEditor.exe —— 瘦 main（M3 lib 化）。
//
// 全部启动装配 / 主循环 / 关停 / headless import CLI 已下沉进 orange_editor
// STATIC lib 的 Orange::Editor::RunEditorApp（见 EditorApp.{h,cpp}）。本 exe
// 唯一职责 = 提供进程入口并转发命令行。per-game editor（M4 SlimeEditor.exe）
// 将有自己的瘦 main，在调 RunEditorApp 之前 AddModule 注入游戏模块（M3 step2
// 引入 EditorAppConfig 后改为按 config 注入）。
#include "EditorApp.h"

int main(int argc, char** argv)
{
    return Orange::Editor::RunEditorApp(argc, argv);
}
