#ifndef ORANGE_EDITOR_CONTEXT_EDITOR_SETTINGS_H
#define ORANGE_EDITOR_CONTEXT_EDITOR_SETTINGS_H

// EditorSettings —— OrangeEditor v0.8 集中化的视觉常量 / 编辑器偏好。
//
// 历史脉络（与 docs/editor-roadmap.md v0.8 "L13 消除" 段对齐）：
//   * v0.1 ~ v0.4 期：gizmo 线宽 / handle 长度 / hit threshold / 配色等视觉
//     常量散落在每个 .cpp 文件的 anonymous namespace 内 hardcode。新增同
//     类常量没有统一注册点，调参要修代码 + recompile
//   * v0.4.5 c0 起标记为已知架构债 L13
//   * v0.8 (本期)：集中到一个 POD struct，挂在 EditorHost 上，所有 gizmo
//     从 `host.settings.*` 读取。Settings 面板按 schema-like 方式编辑；
//     保存到 `editor_settings.json` 持久化
//
// 设计参考：
//   * vendor/LumixEngine/src/editor/settings.h —— Lumix 手写 reflection 风
//     格的 Settings struct，每条字段经命名注册路径喂给 Settings UI；保存
//     到 `studio.ini` 文本。与 OrangeEditor CLAUDE.md "禁止 hardcode + 禁止
//     reflection 库" 纪律方向一致
//
// 范围（首批 v0.8）：
//   * Gizmo 6 条线宽（translate / rotate / scale 各 idle + highlight）
//   * Gizmo handle 屏幕长度（3 gizmo 共享）
//   * Gizmo hit threshold（3 gizmo 共享）
//   * Gizmo 6 条配色（X / Y / Z 各 idle + highlight）
//
// 不在 v0.8 范围（留作未来 milestone 扩展）：
//   * Theme / 配色总盘（v0.6.5 EditorTheme 已存在，独立体系，不重复）
//   * Panel layout 偏好（ImGui::SaveIniSettingsToDisk 已自动处理）
//   * 输入 / Keybinding（v0.8 另一条 deliverable，独立 struct）

#include <glm/vec4.hpp>

#include <orange/engine/core/Serialization.h>

struct EditorSettings
{
    // ---- Gizmo 线宽（屏幕像素） ------------------------------------------
    // Idle = 用户未 hover / drag 的常态；Highlight = hovered / dragging。
    // 默认值对齐 v0.4 期 hardcode（3.5 / 5.5）。
    float gizmoLineWidthTranslateIdle      = 3.5f;
    float gizmoLineWidthTranslateHighlight = 5.5f;
    float gizmoLineWidthRotateIdle         = 3.0f;
    float gizmoLineWidthRotateHighlight    = 5.0f;
    float gizmoLineWidthScaleIdle          = 3.5f;
    float gizmoLineWidthScaleHighlight     = 5.5f;

    // ---- Gizmo handle 屏幕长度 / hit threshold（3 gizmo 共享） --------------
    float gizmoHandleScreenLengthPx        = 90.0f;
    float gizmoHitThresholdPx              = 8.0f;

    // ---- Gizmo 配色（线性 RGB + A） ----------------------------------------
    // 默认值 = v0.4 期 hardcode IM_COL32 转 0..1 浮点。
    // X = 红 / Y = 绿 / Z = 蓝（Lumix / Unity / Godot 工业惯例）。
    glm::vec4 gizmoColorXIdle              {0.863f, 0.235f, 0.235f, 1.0f};
    glm::vec4 gizmoColorXHighlight         {1.000f, 0.706f, 0.471f, 1.0f};
    glm::vec4 gizmoColorYIdle              {0.235f, 0.784f, 0.235f, 1.0f};
    glm::vec4 gizmoColorYHighlight         {0.706f, 1.000f, 0.471f, 1.0f};
    glm::vec4 gizmoColorZIdle              {0.235f, 0.471f, 0.941f, 1.0f};
    glm::vec4 gizmoColorZHighlight         {0.549f, 0.784f, 1.000f, 1.0f};

    // ---- 视口显示开关（编辑器偏好，退出时随 settings 持久化） --------------
    // 迁自 ScenePanel.cpp 的 file-static（v0.8 整骨遗留的"暂不持久化"状态）。
    // 默认对齐原 hardcode：Grid / Sky / Colliders 开，Debug Draw 关。schema
    // minor 1 新增；老 editor_settings.json 缺这些字段时 ReadBool 保留默认值。
    bool viewportGridEnabled      = true;
    bool viewportSkyEnabled       = true;
    bool viewportDebugDrawEnabled = false;
    bool viewportCollidersEnabled = true;
};

// JSON 持久化：与项目内 Core::Serialization 同节奏（手写 Read / Write，无
// 反射库）。.material / .scene 等的 Read / Write 都是同款模式。schema
// v1.0 minor=0，未来加字段时升 minor + 老文件缺字段走默认值。
void ReadEditorSettings(const Orange::Engine::JsonReader& in,
                        EditorSettings& out);
void WriteEditorSettings(Orange::Engine::JsonWriter& out,
                         const EditorSettings& settings);

#endif  // ORANGE_EDITOR_CONTEXT_EDITOR_SETTINGS_H
