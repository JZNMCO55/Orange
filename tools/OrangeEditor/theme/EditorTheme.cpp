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

#include "codicons/IconsCodicons.h"

#include <cstring>  // std::strcmp（ComponentTypeBand::LookupByTypeName）

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

// brand accent：OrangeEngine 橙 #FF8A3D (RGB 1.000 / 0.541 / 0.239)。
// c4 起 ApplyToImGui 消费：
//   * kAccentPrimary  alpha=1.0 —— focus outline / Save dirty 高亮
//   * kAccentHovered  alpha=0.5 —— ImGuiCol_HeaderHovered / TabHovered
//   * kAccentActive   alpha=0.6 —— ImGuiCol_HeaderActive / ButtonActive
//   * kAccentSelection alpha=0.3 —— ImGuiCol_Header 整行半透叠加（§D5.1
//     决策"selection 用半透不用实色"的落地）
// 多档 alpha 共用同一 RGB 让 hover→active 视觉过渡自然（仅亮度变化）。
const ImVec4 kAccentPrimary  {1.000f, 0.541f, 0.239f, 1.000f};
const ImVec4 kAccentHovered  {1.000f, 0.541f, 0.239f, 0.500f};
const ImVec4 kAccentActive   {1.000f, 0.541f, 0.239f, 0.600f};
const ImVec4 kAccentSelection{1.000f, 0.541f, 0.239f, 0.300f};

// alert 色系（c6）：饱和度高（status 提示需要"跳出来"），色相距离 brand
// 橙 #FF8A3D (hue ~22°) 远——warn 黄 hue ~50°、error 红 hue ~0°/360°
// 避开橙、success 绿 hue ~110°。
const ImVec4 kAlertWarn   {0.961f, 0.749f, 0.169f, 1.000f};  // #F5BF2B 黄
const ImVec4 kAlertError  {0.890f, 0.310f, 0.310f, 1.000f};  // #E34F4F 红
const ImVec4 kAlertSuccess{0.330f, 0.780f, 0.420f, 1.000f};  // #54C76B 绿

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
const ImVec4& GetAccentHovered()        { return kAccentHovered;        }
const ImVec4& GetAccentActive()         { return kAccentActive;         }
const ImVec4& GetAccentSelection()      { return kAccentSelection;      }
const ImVec4& GetAlertWarn()            { return kAlertWarn;            }
const ImVec4& GetAlertError()           { return kAlertError;           }
const ImVec4& GetAlertSuccess()         { return kAlertSuccess;         }

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

// c3：返回 Codicons codepoint UTF-8 字节序列（来自 IconsCodicons.h）。
// 调用方 `ImGui::Button(EditorTheme::Icon::GetSave())` 即得 icon 按钮。
// 主字体 atlas 已 merge Codicons（main.cpp v0.6.5 c3）；Codicons 加载
// 失败时该 codepoint 渲染为 "?" 占位（main.cpp 内 log warning）。
//
// 选 DEBUG_START / DEBUG_PAUSE / DEBUG_STOP 而非 PLAY / PAUSE / STOP：
// VS Code 里前者就是 transport-control toolbar 三件套，语义与编辑器
// Play Mode 完全对应；c4 实际替换按钮时维持这条选择。
const char* GetSave()    { return ICON_CI_SAVE;        }
const char* GetPlay()    { return ICON_CI_DEBUG_START; }
const char* GetPause()   { return ICON_CI_DEBUG_PAUSE; }
const char* GetStop()    { return ICON_CI_DEBUG_STOP;  }
const char* GetClose()   { return ICON_CI_CLOSE;       }
const char* GetSearch()  { return ICON_CI_SEARCH;      }
const char* GetAdd()     { return ICON_CI_ADD;         }
const char* GetArrowUp() { return ICON_CI_ARROW_UP;    }
const char* GetFolder()  { return ICON_CI_FOLDER;      }
const char* GetGear()    { return ICON_CI_GEAR;        }

}  // namespace Icon

// ---- ComponentTypeBand ----------------------------------------------

namespace ComponentTypeBand
{

namespace
{

// 低饱和色 palette（参 EditorTheme.h 配色原则）。alpha = 1.0 实色画 4px
// 色带，宽度小所以饱和度高一点也不刺眼；色相分布尽量与 brand 橙 #FF8A3D
// 拉开（避免橙 / 红橙 / 黄橙混淆）。
const ImVec4 kTransform        {0.45f, 0.65f, 0.40f, 1.00f};  // 绿
const ImVec4 kRenderable       {0.40f, 0.60f, 0.75f, 1.00f};  // 蓝
const ImVec4 kDirectionalLight {0.85f, 0.75f, 0.35f, 1.00f};  // 黄
const ImVec4 kRigidBody        {0.75f, 0.40f, 0.40f, 1.00f};  // 红
const ImVec4 kCollider         {0.60f, 0.45f, 0.75f, 1.00f};  // 紫
const ImVec4 kParticleEmitter  {0.40f, 0.70f, 0.65f, 1.00f};  // 青
const ImVec4 kAnimator         {0.80f, 0.55f, 0.70f, 1.00f};  // 粉
const ImVec4 kName             {0.55f, 0.55f, 0.55f, 1.00f};  // 灰
const ImVec4 kDefault          {0.50f, 0.50f, 0.50f, 1.00f};  // 浅灰 fallback

// strcmp / std::string_view 比 std::string 构造更便宜——typeName 是
// schema 注册期传入的 const char* 字符串字面量（生命周期与 schema 相同），
// 直接走 const char* 路径。
struct TypeBandEntry { const char* typeName; const ImVec4* color; };
const TypeBandEntry kTable[] = {
    {"Transform",         &kTransform},
    {"Renderable",        &kRenderable},
    {"DirectionalLight",  &kDirectionalLight},
    {"RigidBody",         &kRigidBody},
    {"Collider",          &kCollider},
    {"ParticleEmitter",   &kParticleEmitter},
    {"Animator",          &kAnimator},
    {"Name",              &kName},
};

}  // namespace

float          GetBandWidthPx()      { return 4.0f;             }
const ImVec4&  GetTransform()        { return kTransform;        }
const ImVec4&  GetRenderable()       { return kRenderable;       }
const ImVec4&  GetDirectionalLight() { return kDirectionalLight; }
const ImVec4&  GetRigidBody()        { return kRigidBody;        }
const ImVec4&  GetCollider()         { return kCollider;         }
const ImVec4&  GetParticleEmitter()  { return kParticleEmitter;  }
const ImVec4&  GetAnimator()         { return kAnimator;         }
const ImVec4&  GetName()             { return kName;             }
const ImVec4&  GetDefault()          { return kDefault;          }

const ImVec4& LookupByTypeName(const char* typeName)
{
    if (typeName == nullptr) { return kDefault; }
    for (const auto& entry : kTable)
    {
        if (std::strcmp(typeName, entry.typeName) == 0)
        {
            return *entry.color;
        }
    }
    return kDefault;
}

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

    // ---- v0.6.5 c4：accent 橙 + 半透 selection ----
    // 覆盖 c2 已设的 ImGuiCol_Header* / Tab* / FrameBgActive / CheckMark
    // 等 selection-related 项。橙 alpha 多档：30% selection 整行半透叠
    // 加；50% hover；60% active；100% focus outline / dirty Save 边缘。
    // §D5.1 决策"selection 用半透而非整行实色填充"的落地点。
    //
    // ImGuiCol_Header* —— TreeNode / Selectable / CollapsingHeader 选中
    // 行：整行半透橙叠加，鼠标 hover / active 时 alpha 渐增。
    style.Colors[ImGuiCol_Header]            = Color::GetAccentSelection();
    style.Colors[ImGuiCol_HeaderHovered]     = Color::GetAccentHovered();
    style.Colors[ImGuiCol_HeaderActive]      = Color::GetAccentActive();

    // ImGuiCol_Tab* —— active tab 半透橙（c2 设的灰被 c4 覆盖）。
    // Unfocused 系列保留灰（非主 viewport 时弱化视觉）。
    style.Colors[ImGuiCol_TabActive]         = Color::GetAccentActive();
    style.Colors[ImGuiCol_TabHovered]        = Color::GetAccentHovered();

    // ImGuiCol_TabSelectedOverline / DimmedSelectedOverline —— ImGui 1.91
    // 新增的 active tab 顶部 indicator 细线。c2 / c4 早期未覆盖时保留
    // StyleColorsDark 默认蓝色，与橙 accent 主题冲突；c4 修复（c4 增量
    // 修复用户反馈"Scene tab 顶部蓝线"问题）。
    style.Colors[ImGuiCol_TabSelectedOverline]       = Color::GetAccentPrimary();
    style.Colors[ImGuiCol_TabDimmedSelectedOverline] = Color::GetAccentSelection();

    // ImGuiCol_FrameBgActive —— 输入框 focus 时的轻染（DragFloat 拖动 /
    // InputText focus）。
    style.Colors[ImGuiCol_FrameBgActive]     = Color::GetAccentSelection();

    // CheckMark / SliderGrab —— 复选框勾 / 滑块圆点用 accent 主色（小
    // 面积可饱和）。
    style.Colors[ImGuiCol_CheckMark]         = Color::GetAccentPrimary();
    style.Colors[ImGuiCol_SliderGrab]        = Color::GetAccentPrimary();
    style.Colors[ImGuiCol_SliderGrabActive]  = Color::GetAccentActive();

    // NavHighlight —— 键盘导航焦点框。
    style.Colors[ImGuiCol_NavHighlight]      = Color::GetAccentPrimary();

    // DockingPreview —— 拖 panel 时的预览区域（半透）。
    style.Colors[ImGuiCol_DockingPreview]    = Color::GetAccentSelection();

    // ImGuiCol_TextSelectedBg —— 文本框内选中文字的高亮背景。
    style.Colors[ImGuiCol_TextSelectedBg]    = Color::GetAccentSelection();
}

}  // namespace Orange::Editor::Theme
