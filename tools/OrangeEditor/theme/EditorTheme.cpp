// EditorTheme c2：Cocos 3.8.8 炭灰采色 + Color / Spacing / Rounding token
// 实装 + ApplyToImGui 把 token 灌进 ImGui Style。视觉首次变化：编辑器
// 整体从"深蓝黑（ImGui StyleColorsDark）" → "Cocos 炭灰"。
//
// 决策来源 + 通用知识不抄进本文件（按 CLAUDE.md wiki 引用纪律），完整背
// 景见 EditorTheme.h 顶部注释 + docs/editor-roadmap.md §D5.1。
//
// 采色方法学说明：本 commit RGB 值基于 Cocos Creator 3.8.8 截图的**视觉
// 印象** + §D5.1 决策范围（#2A2A2A ~ #2C2C2C 主背景）+ 工业 dark theme
// 惯例（panel 分层只靠 1 档亮度差 / 字色偏浅灰非纯白 / 分隔线深到几乎隐
// 形）。**不是 pixel-perfect Cocos 还原**——肉眼可能感觉 1–2 档亮度差，
// 通过 c6 polish 或 c2 follow-up 微调到位。
//
// hardcode RGBA 红线豁免：本文件是 EditorTheme token 集中点，定义层面
// 必然写字面量 ImVec4——这是与 §D5.1 红线"禁止直接调 ImGui::PushStyleColor
// 字面量 RGBA"对偶的（红线针对调用方，不针对 token 定义方）。c7 写
// invariant lint 时把本文件加白名单豁免。

#include "EditorTheme.h"

namespace Orange::Editor::Theme
{

// ---- Color ----------------------------------------------------------

namespace Color
{

namespace
{

// 主 dock space 背景：#242424 (~14% 亮度)。略偏深以让 panel / dock 分层。
const ImVec4 kBackgroundPrimary{0.141f, 0.141f, 0.141f, 1.000f};

// panel 内容背景：#2E2E2E (~18%)。比主背景亮一档让 panel 边界明确。
const ImVec4 kBackgroundSecondary{0.180f, 0.180f, 0.180f, 1.000f};

// popup / tooltip 背景：#363636 (~21%)。让 popup 比主 UI 浮出来。
const ImVec4 kBackgroundTertiary{0.212f, 0.212f, 0.212f, 1.000f};

// 输入框 idle：#1E1E1E (~12%)。比主背景更深让输入框"凹陷"，对应 Cocos
// Inspector 字段输入框视觉。
const ImVec4 kControlBg{0.118f, 0.118f, 0.118f, 1.000f};

// 输入框 hover：#3C3C3C (~24%)。
const ImVec4 kControlBgHovered{0.235f, 0.235f, 0.235f, 1.000f};

// 输入框 active：#464646 (~28%)。
const ImVec4 kControlBgActive{0.275f, 0.275f, 0.275f, 1.000f};

// header / TreeNode / Selectable idle：#363636 (~21%)。
const ImVec4 kHeaderBg{0.212f, 0.212f, 0.212f, 1.000f};

// header hover：#464646 (~28%)。
const ImVec4 kHeaderBgHovered{0.275f, 0.275f, 0.275f, 1.000f};

// header active / selected：#555555 (~33%)。
const ImVec4 kHeaderBgActive{0.333f, 0.333f, 0.333f, 1.000f};

// menu bar / toolbar 背景：#282828 (~16%)。介于主背景与 panel 之间。
const ImVec4 kMenuBarBg{0.157f, 0.157f, 0.157f, 1.000f};

// 主字色：#DCDCDC (~86%)。浅灰非纯白，长时间盯不疲劳。
const ImVec4 kTextPrimary{0.863f, 0.863f, 0.863f, 1.000f};

// 次字色：#A0A0A0 (~63%)。
const ImVec4 kTextSecondary{0.627f, 0.627f, 0.627f, 1.000f};

// 禁用字色：#6E6E6E (~43%)。
const ImVec4 kTextDisabled{0.431f, 0.431f, 0.431f, 1.000f};

// 分隔线 / border：#141414 (~8%)。深到几乎隐形，仅作视觉断点。
const ImVec4 kSeparator{0.078f, 0.078f, 0.078f, 1.000f};
const ImVec4 kBorder   {0.078f, 0.078f, 0.078f, 1.000f};

// brand accent：OrangeEngine 橙 #FF8A3D。c4 起 ApplyToImGui 才消费。
const ImVec4 kAccentPrimary{1.000f, 0.541f, 0.239f, 1.000f};

}  // namespace

const ImVec4& GetBackgroundPrimary()    { return kBackgroundPrimary;    }
const ImVec4& GetBackgroundSecondary()  { return kBackgroundSecondary;  }
const ImVec4& GetBackgroundTertiary()   { return kBackgroundTertiary;   }
const ImVec4& GetControlBg()            { return kControlBg;            }
const ImVec4& GetControlBgHovered()     { return kControlBgHovered;     }
const ImVec4& GetControlBgActive()      { return kControlBgActive;      }
const ImVec4& GetHeaderBg()             { return kHeaderBg;             }
const ImVec4& GetHeaderBgHovered()      { return kHeaderBgHovered;      }
const ImVec4& GetHeaderBgActive()       { return kHeaderBgActive;       }
const ImVec4& GetMenuBarBg()            { return kMenuBarBg;            }
const ImVec4& GetTextPrimary()          { return kTextPrimary;          }
const ImVec4& GetTextSecondary()        { return kTextSecondary;        }
const ImVec4& GetTextDisabled()         { return kTextDisabled;         }
const ImVec4& GetSeparator()            { return kSeparator;            }
const ImVec4& GetBorder()               { return kBorder;               }
const ImVec4& GetAccentPrimary()        { return kAccentPrimary;        }

}  // namespace Color

// ---- Spacing --------------------------------------------------------

namespace Spacing
{

namespace
{

// c2 = ImGui 默认 + ScaleAllSizes 后基线值（main.cpp 已按 DPI 缩放，本
// 处取的是设计基准像素，调用方使用 ImGui 默认 widget 渲染规则自动正确）。
const ImVec2 kWindowPadding    {8.0f, 8.0f};
const ImVec2 kFramePadding     {6.0f, 4.0f};
const ImVec2 kItemSpacing      {8.0f, 4.0f};
const ImVec2 kItemInnerSpacing {4.0f, 4.0f};

}  // namespace

const ImVec2& GetWindowPadding()    { return kWindowPadding;    }
const ImVec2& GetFramePadding()     { return kFramePadding;     }
const ImVec2& GetItemSpacing()      { return kItemSpacing;      }
const ImVec2& GetItemInnerSpacing() { return kItemInnerSpacing; }
float         GetIndentSpacing()    { return 18.0f;             }

}  // namespace Spacing

// ---- Rounding -------------------------------------------------------

namespace Rounding
{

// Cocos 工具感方向：全部 0px 方正。c6 如需个别项微调（如 popup 1px），
// 在该 commit 内单独调整对应 getter。
float GetWindow()    { return 0.0f; }
float GetFrame()     { return 0.0f; }
float GetPopup()     { return 0.0f; }
float GetScrollbar() { return 0.0f; }
float GetGrab()      { return 0.0f; }
float GetTab()       { return 0.0f; }
float GetChild()     { return 0.0f; }

}  // namespace Rounding

// ---- Font -----------------------------------------------------------

namespace Font
{

float GetSizeBody() { return 18.0f; }

}  // namespace Font

// ---- Icon -----------------------------------------------------------

namespace Icon
{

const char* GetSave() { return "Save"; }
const char* GetPlay() { return "Play"; }

}  // namespace Icon

// ---- ComponentTypeBand ----------------------------------------------

namespace ComponentTypeBand
{

namespace
{

const ImVec4 kTransform{0.45f, 0.65f, 0.40f, 1.00f};

}  // namespace

float          GetBandWidthPx() { return 4.0f;        }
const ImVec4&  GetTransform()   { return kTransform;  }

}  // namespace ComponentTypeBand

// ---- 顶层 API -------------------------------------------------------

void ApplyToImGui()
{
    // 路径：先 StyleColorsDark 作为 baseline（覆盖 ImGui 默认所有 ImGuiCol_*），
    // 然后用 EditorTheme token 覆盖关键项。未被本函数显式覆盖的 ImGuiCol_*
    // 保留 StyleColorsDark 值——避免视觉空白 / 未定义状态。
    //
    // 后续 c4 / c6 添加新 token 时只需在本函数末尾追加 Style.Colors 赋值
    // 即可，不破坏现有覆盖。
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // ---- Color：背景层级 ----
    style.Colors[ImGuiCol_WindowBg]          = Color::GetBackgroundSecondary();
    style.Colors[ImGuiCol_ChildBg]           = Color::GetBackgroundSecondary();
    style.Colors[ImGuiCol_PopupBg]           = Color::GetBackgroundTertiary();
    style.Colors[ImGuiCol_MenuBarBg]         = Color::GetMenuBarBg();
    style.Colors[ImGuiCol_DockingEmptyBg]    = Color::GetBackgroundPrimary();

    // ---- Color：输入框 / 控件三态 ----
    style.Colors[ImGuiCol_FrameBg]           = Color::GetControlBg();
    style.Colors[ImGuiCol_FrameBgHovered]    = Color::GetControlBgHovered();
    style.Colors[ImGuiCol_FrameBgActive]     = Color::GetControlBgActive();

    // ---- Color：Button 三态（c4 起 accent 橙覆盖 dirty Save 等局部） ----
    style.Colors[ImGuiCol_Button]            = Color::GetControlBg();
    style.Colors[ImGuiCol_ButtonHovered]     = Color::GetControlBgHovered();
    style.Colors[ImGuiCol_ButtonActive]      = Color::GetControlBgActive();

    // ---- Color：Header / TreeNode / Selectable（selection 实色 dot；c4
    // 把 selection 切到半透 accent overlay） ----
    style.Colors[ImGuiCol_Header]            = Color::GetHeaderBg();
    style.Colors[ImGuiCol_HeaderHovered]     = Color::GetHeaderBgHovered();
    style.Colors[ImGuiCol_HeaderActive]      = Color::GetHeaderBgActive();

    // ---- Color：Tab（c4 起 TabActive 切 accent 橙） ----
    style.Colors[ImGuiCol_Tab]               = Color::GetHeaderBg();
    style.Colors[ImGuiCol_TabHovered]        = Color::GetHeaderBgHovered();
    style.Colors[ImGuiCol_TabActive]         = Color::GetHeaderBgActive();
    style.Colors[ImGuiCol_TabUnfocused]      = Color::GetBackgroundSecondary();
    style.Colors[ImGuiCol_TabUnfocusedActive]= Color::GetHeaderBg();

    // ---- Color：Title bar（detached window） ----
    style.Colors[ImGuiCol_TitleBg]           = Color::GetBackgroundPrimary();
    style.Colors[ImGuiCol_TitleBgActive]     = Color::GetHeaderBg();
    style.Colors[ImGuiCol_TitleBgCollapsed]  = Color::GetBackgroundPrimary();

    // ---- Color：字色 ----
    style.Colors[ImGuiCol_Text]              = Color::GetTextPrimary();
    style.Colors[ImGuiCol_TextDisabled]      = Color::GetTextDisabled();

    // ---- Color：分隔 / 边框 ----
    style.Colors[ImGuiCol_Separator]         = Color::GetSeparator();
    style.Colors[ImGuiCol_SeparatorHovered]  = Color::GetTextSecondary();
    style.Colors[ImGuiCol_SeparatorActive]   = Color::GetTextPrimary();
    style.Colors[ImGuiCol_Border]            = Color::GetBorder();
    style.Colors[ImGuiCol_BorderShadow]      = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};

    // ---- Color：滚动条 ----
    style.Colors[ImGuiCol_ScrollbarBg]       = Color::GetBackgroundPrimary();
    style.Colors[ImGuiCol_ScrollbarGrab]     = Color::GetHeaderBg();
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = Color::GetHeaderBgHovered();
    style.Colors[ImGuiCol_ScrollbarGrabActive] = Color::GetHeaderBgActive();

    // ---- Spacing ----
    style.WindowPadding     = Spacing::GetWindowPadding();
    style.FramePadding      = Spacing::GetFramePadding();
    style.ItemSpacing       = Spacing::GetItemSpacing();
    style.ItemInnerSpacing  = Spacing::GetItemInnerSpacing();
    style.IndentSpacing     = Spacing::GetIndentSpacing();

    // ---- Rounding ----
    style.WindowRounding    = Rounding::GetWindow();
    style.FrameRounding     = Rounding::GetFrame();
    style.PopupRounding     = Rounding::GetPopup();
    style.ScrollbarRounding = Rounding::GetScrollbar();
    style.GrabRounding      = Rounding::GetGrab();
    style.TabRounding       = Rounding::GetTab();
    style.ChildRounding     = Rounding::GetChild();

    // ---- Border ----
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
}

}  // namespace Orange::Editor::Theme
