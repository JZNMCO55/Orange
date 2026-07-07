// 主 toolbar 行（v0.6.5 c0：独立 toolbar 抽象，v0.6 c3 的落地补完）。
//
// v0.6 c3 当时把 Save / Play / Pause / Stop 妥协塞进 BeginMainMenuBar
// 右侧——与 c3 deliverable 原意"聚到一条 toolbar 上"未对齐。本文件
// 把 transport-control 按钮独立到紧贴 menu bar 下方的固定 bar，与
// Cocos Creator 3.8.8 的两行布局对齐（行 1 menu / 行 2 toolbar）。
//
// 实现要点：
//   * ImGui `BeginViewportSideBar(... ImGuiDir_Up, height, MenuBar)` 在
//     主 viewport 顶部紧贴 menu bar 下方占一段固定高度，并自动从
//     viewport work area 扣除该段，让 DockSpaceOverViewport 不与 toolbar
//     重叠。首帧瑕疵：BuildWorkOffset 累积本帧、下帧 commit，因此 t=0
//     首帧 dockspace 仍可能短暂覆盖 toolbar 一帧；稳态后正常。
//   * BeginMenuBar 让按钮以与 MainMenuBar 一致的样式渲染（同款 frame
//     padding / item spacing），视觉上与上方 menu bar 自然衔接。
//   * 必须在 DockSpaceOverViewport 之前调用，让 BuildWorkOffset 累积
//     生效（与 BeginMainMenuBar 同款约束）。
//
// 布局：
//   [Save (靠左)]  ...  [Play | Pause | Stop (居中)]  ...  [State (靠右)]
//
// 居中策略：以 GetWindowWidth 为绝对基准，SameLine 跳到
// (winW - playGroupW) / 2 画 Play 控件组；[State] 同款 SameLine(winW -
// stateW - WindowPadding.x) 靠右。不靠 cursor 推进，能容忍 Save 按钮文
// 字宽度变化不抖动中央 group 位置。
//
// 视觉规约：v0.6.5 c0 仅迁布局，按钮仍为裸文字 + 蓝色 dirty 高亮（保留
// v0.6 视觉，与迁出前像素级等价）。EditorTheme token 化 + Codicons icon
// 替换 + accent 橙 + Play 绿 / Stop 红由后续 c2 / c4 / c6 处理；本 commit
// 内部仍有 hardcode ImVec4 RGBA，等 c4 替换为 EditorTheme::Color::* token。

#include "../EditorRenderLayer.h"

#include "../EditorHost.h"
#include "../theme/EditorTheme.h"

#include <orange/engine/game/ScriptGameModule.h> // M8：mpScriptModule->IsScriptStale

#include <imgui.h>
#include <imgui_internal.h> // BeginViewportSideBar（公开但 internal 命名）

#include <algorithm>

void EditorRenderLayer::DrawMainToolbar()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
    {
        return;
    }

    const float frameH   = ImGui::GetFrameHeight();
    const float toolbarH = frameH * 1.2f;

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;

    // BeginViewportSideBar 内部已调 Begin()，无论是否返回 true 都必须
    // End() 配对（ImGui Begin/End 通用规则）。
    const bool open = ImGui::BeginViewportSideBar("##MainToolbar", viewport,
                                                  ImGuiDir_Up, toolbarH, flags);
    if (open && ImGui::BeginMenuBar())
    {
        // 显式 push ButtonTextAlign(0.5, 0.5) 让 icon 在按钮正中——本来
        // 是 ImGui 默认值，但保险写出来对抗后续可能的 style override。配合
        // main.cpp ImFontConfig.GlyphMaxAdvanceX = fontPx 让 icon glyph
        // 在 1em cell 内 advance 居中（c4 修复用户反馈 "icon 偏左上"）。
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));

        const PlayState ps         = mHost.scene.playState;
        const char*     stateLabel = (ps == PlayState::Edit)   ? "[Edit]"
                                     : (ps == PlayState::Play) ? "[Play]"
                                                               : "[Paused]";

        // v0.6.5 c4：按钮 label 切 Codicons icon，size 用 ImVec2(0, 0)
        // **自动尺寸**——ImGui 按 `icon_advance + 2 * framePadding` 算宽，
        // ButtonTextAlign(0.5, 0.5) 自然把 icon 横纵居中。c4 三轮 fix 教训：
        // 固定 ImVec2(frameH, frameH) 正方形 + GlyphMaxAdvanceX 强制 1em
        // 让 glyph 在 cell 内位置失控（视觉偏右），auto-size 路线更稳。
        // 副作用：4 按钮宽度由 icon advance 决定，Codicons 多数 1em advance
        // 视觉一致，偶尔不一致可接受。
        const ImGuiStyle& style     = ImGui::GetStyle();
        const float       framePadX = style.FramePadding.x * 2.0f;
        const float       itemSpc   = style.ItemSpacing.x;

        // playGroupW 改按 icon advance 算（CalcTextSize 与实际 button 宽
        // 一致）。SaveBtn 宽同款 CalcTextSize，但靠左不参与 group 居中。
        const ImVec2 btnSize{0.0f, 0.0f};
        const float  btnPlayW = ImGui::CalcTextSize(
                                   Orange::Editor::Theme::Icon::GetPlay())
                                   .x +
                               framePadX;
        const float btnPauseW = ImGui::CalcTextSize(
                                    Orange::Editor::Theme::Icon::GetPause())
                                    .x +
                                framePadX;
        const float btnStopW = ImGui::CalcTextSize(
                                   Orange::Editor::Theme::Icon::GetStop())
                                   .x +
                               framePadX;

        // 三态 label 取最长 + framePad 保证切换不抖动；用 (std::max)(...)
        // 圆括号绕开 windows.h max 宏污染（与 EditorRenderLayer.cpp 同款手法）。
        const float stateW = (std::max)(ImGui::CalcTextSize("[Edit]").x,
                                        (std::max)(ImGui::CalcTextSize("[Play]").x,
                                                   ImGui::CalcTextSize("[Paused]").x)) +
                             framePadX;
        // M9.2 新增：Step 按钮（文字 label，避免给 codicon 加新 glyph）+ 时间缩放
        // combo；两者一并计入居中 group 宽度，避免与 Save / [State] 溢出重叠。
        const char* stepLabel   = ">|";
        const float btnStepW    = ImGui::CalcTextSize(stepLabel).x + framePadX;
        // M7：DLL 模块热重载按钮（文字 label，避免给 codicon 加新 glyph）。
        const char* reloadLabel = "[R]";
        const float btnReloadW  = ImGui::CalcTextSize(reloadLabel).x + framePadX;
        const float timeComboW  = ImGui::CalcTextSize("0.1x").x + framePadX + frameH;
        const float playGroupW  = btnPlayW + btnPauseW + btnStopW + btnStepW + btnReloadW +
                                 timeComboW + 5.0f * itemSpc;

        // ---- Save 靠左 ---------------------------------------------
        // dirty 时 accent 橙高亮（§D5.1 落地：Save dirty 是"小面积高对
        // 比"位置，用 AccentPrimary 实色填充；非 dirty 时保留 c2 默认
        // 灰）。
        const bool dirty = mHost.scene.dirty;
        if (dirty)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  Orange::Editor::Theme::Color::GetAccentPrimary());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  Orange::Editor::Theme::Color::GetAccentHovered());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  Orange::Editor::Theme::Color::GetAccentActive());
        }
        ImGui::BeginDisabled(!dirty);
        if (ImGui::Button(Orange::Editor::Theme::Icon::GetSave(), btnSize))
        {
            mHost.scene.pendingSceneOp = SceneOp::Save;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Save (Ctrl+S)");
        }
        ImGui::EndDisabled();
        if (dirty)
        {
            ImGui::PopStyleColor(3);
        }

        // ---- Play / Pause / Stop 居中 ------------------------------
        // SameLine(absX) 用绝对 X 不依赖 cursor，Save 按钮宽度变化不影响
        // 中央 group 位置。GetWindowWidth 是 toolbar window 自身的总宽，
        // 不含 ScrollBar（NoScrollbar flag 保证）。
        const float winW        = ImGui::GetWindowWidth();
        const float playCenterX = (winW - playGroupW) * 0.5f;
        ImGui::SameLine(playCenterX);

        const bool canPlay  = (ps == PlayState::Edit || ps == PlayState::Paused);
        const bool canPause = (ps == PlayState::Play);
        const bool canStop  = (ps == PlayState::Play || ps == PlayState::Paused);

        // Play 按钮 idle 时 icon 着绿（success alert）让"就绪可点"状态
        // 跳出来；hover/active 时背景走 c2 默认灰过渡，icon 仍绿保持视
        // 觉一致。disabled 时 EndDisabled 路径自动 dim alpha，绿色也变浅。
        ImGui::BeginDisabled(!canPlay);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              Orange::Editor::Theme::Color::GetAlertSuccess());
        if (ImGui::Button(Orange::Editor::Theme::Icon::GetPlay(), btnSize))
        {
            mHost.scene.pendingPlayOp = (ps == PlayState::Paused) ? PlayOp::Resume
                                                                  : PlayOp::EnterPlay;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Play");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        // Pause icon 保留默认文字色 —— pause 不属于 alert 范畴（既非
        // success 也非 error），保留中性 brand 灰避免与 Play 绿 / Stop
        // 红混淆。
        ImGui::BeginDisabled(!canPause);
        if (ImGui::Button(Orange::Editor::Theme::Icon::GetPause(), btnSize))
        {
            mHost.scene.pendingPlayOp = PlayOp::Pause;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Pause");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        // Stop 按钮 idle 时 icon 着红（error alert）让"停止 / 退出 Play
        // 模式"状态显眼；同 Play 路径，hover/active 背景走默认灰，icon
        // 仍红。
        ImGui::BeginDisabled(!canStop);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              Orange::Editor::Theme::Color::GetAlertError());
        if (ImGui::Button(Orange::Editor::Theme::Icon::GetStop(), btnSize))
        {
            mHost.scene.pendingPlayOp = PlayOp::Stop;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Stop");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        // ---- Step（M9.2 帧步进）：仅 Paused 态可点，单步一个固定 sim 帧 ----
        // 设 ctx.pendingStep，帧末 EditorRenderLayer 消费。文字 label 而非 icon，
        // 与 ButtonTextAlign 居中样式共存无碍。
        const bool canStep = (ps == PlayState::Paused);
        ImGui::BeginDisabled(!canStep);
        if (ImGui::Button(stepLabel, btnSize))
        {
            mHost.scene.pendingStep = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Step (advance one fixed frame; Paused only)");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        // ---- Reload（M7 DLL + M8 C# 脚本热重载）------------------------------
        // M7 DLL：Edit 态 + 有 DLL 模块，原 dll 被重编（AnyLibraryStale）时 accent 橙提示
        //   → pendingReloadModules（摘 pass→卸载→重 Load→重注册 + re-merge serializer）。
        // M8 脚本：Play/Paused 态 + 有 ScriptGameModule + 脚本过期（IsScriptStale）→
        //   pendingReloadScripts（保运行时状态换新代码）。脚本可 Play 中 reload——区别
        //   DLL 的 Edit-only（DLL pass vtable teardown 在 Play 中不安全）。两者共用 [R]。
        const bool hasDllModule    = (mHost.gameModules.LibraryCount() > 0);
        const bool canReloadDll    = (ps == PlayState::Edit) && hasDllModule;
        const bool dllStale        = canReloadDll && mHost.gameModules.AnyLibraryStale();
        const bool hasScriptModule = (mHost.mpScriptModule != nullptr);
        const bool scriptStale     = (ps != PlayState::Edit) && hasScriptModule &&
                                 mHost.mpScriptModule->IsScriptStale();
        const bool canReload = canReloadDll || scriptStale;
        const bool showStale = dllStale || scriptStale;
        ImGui::BeginDisabled(!canReload);
        if (showStale)
        {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  Orange::Editor::Theme::Color::GetAccentPrimary());
        }
        if (ImGui::Button(reloadLabel, btnSize))
        {
            if (ps == PlayState::Edit)
            {
                mHost.scene.pendingReloadModules = true;
            }
            else
            {
                mHost.scene.pendingReloadScripts = true;
            }
        }
        if (showStale)
        {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(scriptStale ? "Reload C# scripts (changed on disk; Play OK)"
                              : dllStale   ? "Reload game module DLL (changed on disk)"
                                           : "Reload game module DLL (Edit only)");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        // ---- 时间缩放 combo（M9.2）：0.1 / 0.5 / 1.0x slow-mo；写 ctx.playTimeScale ----
        // 三档固定值，按当前值反查选中项（浮点近似比较）。1.0x 为默认。
        {
            static const char* const kScaleLabels[] = {"0.1x", "0.5x", "1.0x"};
            static const float       kScaleValues[] = {0.1f, 0.5f, 1.0f};
            int                      scaleIdx       = 2; // 默认 1.0x
            for (int i = 0; i < 3; ++i)
            {
                const float v = mHost.scene.playTimeScale;
                if (v > kScaleValues[i] - 0.001f && v < kScaleValues[i] + 0.001f)
                {
                    scaleIdx = i;
                }
            }
            ImGui::SetNextItemWidth(timeComboW);
            if (ImGui::Combo("##timescale", &scaleIdx, kScaleLabels, 3))
            {
                mHost.scene.playTimeScale = kScaleValues[scaleIdx];
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Play speed (time scale)");
            }
        }

        // ---- [State] 靠右 -----------------------------------------
        // 同款绝对 X 跳转；预留 WindowPadding.x 距离右边距，与 menu bar
        // 项右对齐留白节奏一致。
        ImGui::SameLine(winW - stateW - style.WindowPadding.x);
        ImGui::TextDisabled("%s", stateLabel);

        ImGui::PopStyleVar(); // ButtonTextAlign
        ImGui::EndMenuBar();
    }
    ImGui::End();
}
