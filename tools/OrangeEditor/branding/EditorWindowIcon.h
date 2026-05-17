#ifndef ORANGE_EDITOR_BRANDING_EDITOR_WINDOW_ICON_H
#define ORANGE_EDITOR_BRANDING_EDITOR_WINDOW_ICON_H

struct GLFWwindow;

namespace OrangeEditorBranding
{

// 把内嵌的多分辨率 OrangeEditor logo 喂给 GLFW，让运行期窗口左上角小
// icon / 任务栏 active icon / 多视口子窗口 icon 全部用品牌图标。
//
// 与 OrangeEditor.rc 嵌进 exe 的 ICO 资源**双重保险**：
//   * .rc 路径 cover 文件资源管理器 / Alt-Tab / taskbar inactive 状态
//   * 本函数 cover 运行期窗口装饰 / multi-viewport 子窗口（GLFW 不会
//     自动让子窗口继承主窗口 icon，必须每个 GLFWwindow 单独 set）
//
// 数据源是 build-time codegen 的 EditorWindowIconData.h（16/32/48 三档
// RGBA），不依赖运行期文件 IO / cwd。
//
// 调用时机：main.cpp 创建 AppHost 之后立刻调用即可；本函数幂等，多次
// 调用无副作用。失败时只会 log（GLFW 不致命 fail）不抛错。
void ApplyEditorWindowIcons(GLFWwindow* window);

}  // namespace OrangeEditorBranding

#endif  // ORANGE_EDITOR_BRANDING_EDITOR_WINDOW_ICON_H
