#ifndef ORANGE_EDITOR_CONTEXT_EDITOR_KEYBINDINGS_H
#define ORANGE_EDITOR_CONTEXT_EDITOR_KEYBINDINGS_H

// EditorKeybindings —— v0.8 用户自定义快捷键的集中表。
//
// 设计参考：
//   * vendor/LumixEngine/src/editor/settings.h Action 表 —— Lumix 把每条
//     "Action"（id + display name + default key + persisted override）
//     注册进 StudioApp，Settings UI 让用户改 binding 并写盘
//
// 范围（v0.8 首批）：固定常用 5 条 viewport / Inspector 快捷键；用户在
// Settings 面板 "Keybindings" 段可改 binding。Ctrl+Z / Ctrl+Y / Ctrl+S
// 等 chord shortcut 暂保持 hardcode（IsKeyChordPressed 路径，非本表）。
//
// 持久化：与 EditorSettings 共享 editor_settings.json 文件（同 schema 不同
// JSON path 段；v1.0 schema 起步加 keybindings 段，老文件缺段时走默认值）。

#include <orange/engine/core/Serialization.h>

#include <imgui.h>

struct EditorKeybindings
{
    // Viewport 内 W / E / R 切换 gizmo mode。默认值对齐 Unity / Lumix / Godot。
    ImGuiKey gizmoTranslate = ImGuiKey_W;
    ImGuiKey gizmoRotate    = ImGuiKey_E;
    ImGuiKey gizmoScale     = ImGuiKey_R;
    // Entity Tree 内重命名 / 删除。F2 / Del 工业惯例。
    ImGuiKey renameEntity   = ImGuiKey_F2;
    ImGuiKey deleteEntity   = ImGuiKey_Delete;
};

void ReadEditorKeybindings(const Orange::Engine::JsonReader& in,
                           EditorKeybindings& out);
void WriteEditorKeybindings(Orange::Engine::JsonWriter& out,
                            const EditorKeybindings& kb);

// ImGuiKey ↔ 字符串名（用 ImGui::GetKeyName 反查 + ImGuiKey 表达）。
const char* KeyNameFromImGuiKey(ImGuiKey key);

#endif  // ORANGE_EDITOR_CONTEXT_EDITOR_KEYBINDINGS_H
