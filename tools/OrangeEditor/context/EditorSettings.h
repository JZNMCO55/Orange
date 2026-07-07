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

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <orange/engine/core/Serialization.h>

#include <array>
#include <string>
#include <vector>

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
    float gizmoHandleScreenLengthPx = 90.0f;
    float gizmoHitThresholdPx       = 8.0f;

    // ---- Gizmo 配色（线性 RGB + A） ----------------------------------------
    // 默认值 = v0.4 期 hardcode IM_COL32 转 0..1 浮点。
    // X = 红 / Y = 绿 / Z = 蓝（Lumix / Unity / Godot 工业惯例）。
    glm::vec4 gizmoColorXIdle{0.863f, 0.235f, 0.235f, 1.0f};
    glm::vec4 gizmoColorXHighlight{1.000f, 0.706f, 0.471f, 1.0f};
    glm::vec4 gizmoColorYIdle{0.235f, 0.784f, 0.235f, 1.0f};
    glm::vec4 gizmoColorYHighlight{0.706f, 1.000f, 0.471f, 1.0f};
    glm::vec4 gizmoColorZIdle{0.235f, 0.471f, 0.941f, 1.0f};
    glm::vec4 gizmoColorZHighlight{0.549f, 0.784f, 1.000f, 1.0f};

    // ---- 视口显示开关（编辑器偏好，退出时随 settings 持久化） --------------
    // 迁自 ScenePanel.cpp 的 file-static（v0.8 整骨遗留的"暂不持久化"状态）。
    // 默认对齐原 hardcode：Grid / Sky / Colliders 开，Debug Draw 关。schema
    // minor 1 新增；老 editor_settings.json 缺这些字段时 ReadBool 保留默认值。
    bool viewportGridEnabled      = true;
    bool viewportSkyEnabled       = true;
    bool viewportDebugDrawEnabled = false;
    bool viewportCollidersEnabled = true;

    // ---- Autosave（GAP-2026-05-29-editor-autosave-wiring） ----------------
    // Edit 态下场景 dirty 时，按 interval 周期把当前 World 序列化到 temp 的
    // .autosave 文件；崩溃 / 异常退出后下次启动检测到残留 autosave → 弹恢复
    // 提示。手动 Save / New / Open 成功后删除 autosave（基线已干净）。底层走
    // 引擎 `Save::AutosaveScheduler`（纯时间逻辑 + 节流），编辑器只接线。
    // schema minor 2 新增；老 editor_settings.json 缺字段时保留 struct 默认值。
    bool  autosaveEnabled            = true;
    float autosaveIntervalSeconds    = 180.0f; // 周期触发（秒）；clamp ≥10
    float autosaveMinIntervalSeconds = 30.0f;  // 节流：两次写入最小间隔（秒）

    // ---- Gizmo snap（gap 报告 §3 P0；schema minor 4） --------------------
    // 拖动 gizmo 时把结果量化到步长。默认关 = 对现有 gizmo 零回归（snap 关时
    // 走原连续拖动路径）。translate=世界网格(米) / rotate=角度(度) / scale=比例步。
    // 量化走 EditorMathUtil::SnapToStep。老文件缺字段走默认。
    bool  snapEnabled       = false;
    float snapTranslateStep = 0.5f;  // 世界米
    float snapRotateStepDeg = 15.0f; // 度
    float snapScaleStep     = 0.1f;  // 比例

    // ---- 相机书签 / saved views（gap 报告 §4 #4） ------------------------
    // 快照 viewport 轨道相机的 7 个视图参数（不含 dragging / 灵敏度等 live 输入
    // 态）。View 菜单 "Camera Bookmarks" Save / Go 消费；放在 EditorSettings 而非
    // EditorCameraState 是为复用既有序列化 + 随启动/退出自动读写（同视口显示开关
    // 先例）。schema minor 3 新增；老文件缺段时 valid 全默认 false（无书签）。
    struct CameraBookmark
    {
        glm::vec3 pivot{0.0f, 0.5f, 0.0f};
        float     azimuth     = 0.0f;
        float     elevation   = 0.19f;
        float     radius      = 8.15f;
        float     fovYDegrees = 45.0f;
        float     zNear       = 0.1f;
        float     zFar        = 100.0f;
        bool      valid       = false; // false = 该槽未保存过，Go 置灰
    };
    static constexpr int                             kCameraBookmarkSlots = 4;
    std::array<CameraBookmark, kCameraBookmarkSlots> cameraBookmarks{};

    // ---- 最近场景（File → Open Recent，schema minor 5）-------------------
    // 最近打开 / 另存的 .scene.json 路径，front = 最近。持久化进 editor_settings
    // .json（固定 kMaxRecentScenes 槽序列化，空串占位 = 未用）。对齐各编辑器
    // File → Open Recent。
    static constexpr int     kMaxRecentScenes = 10;
    std::vector<std::string> recentScenes;

    // 把一个场景路径加进最近列表（去重后置顶 + cap kMaxRecentScenes）。空 path
    // no-op。Open / SaveAs 成功后调。
    void AddRecentScene(const std::string& path);

    // ---- 最近工程（File → Open Recent Project，schema minor 6）------------
    // 最近打开的 .orangeproject 路径，front = 最近。与 recentScenes 同款持久化
    // （固定 kMaxRecentProjects 槽，空串占位 = 未用）。M6 mid-session 切项目。
    static constexpr int     kMaxRecentProjects = 10;
    std::vector<std::string> recentProjects;

    // 把一个工程路径加进最近列表（去重后置顶 + cap kMaxRecentProjects）。空 path
    // no-op。Open Project 成功后调。
    void AddRecentProject(const std::string& path);
};

// JSON 持久化：与项目内 Core::Serialization 同节奏（手写 Read / Write，无
// 反射库）。.material / .scene 等的 Read / Write 都是同款模式。schema
// v1.0 minor=0，未来加字段时升 minor + 老文件缺字段走默认值。
void ReadEditorSettings(const Orange::Engine::JsonReader& in,
                        EditorSettings&                   out);
void WriteEditorSettings(Orange::Engine::JsonWriter& out,
                         const EditorSettings&       settings);

#endif // ORANGE_EDITOR_CONTEXT_EDITOR_SETTINGS_H
