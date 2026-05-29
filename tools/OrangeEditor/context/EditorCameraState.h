#ifndef ORANGE_EDITOR_CONTEXT_EDITOR_CAMERA_STATE_H
#define ORANGE_EDITOR_CONTEXT_EDITOR_CAMERA_STATE_H

// EditorCameraState —— 编辑器 viewport 轨道相机的 viewport-local 状态。
//
// v0.2.5 整骨：从原 god struct EditorState 拆出 camera 子域。
//
// 原 EditorState::EditorCamera 嵌套类型被拍平为本 struct——目前只有一个
// 编辑器相机，没必要再嵌套一层；未来若需要多 viewport / saved view，本
// struct 升级为 "current camera + named view 集合"。
//
// 控制约定（与 UpdateEditorCameraFromInput 内的输入捕获保持一致）：
//   * 鼠标左键拖动（hover Scene 面板时按下） —— 轨道旋转 azimuth / elevation
//   * 滚轮（hover Scene 面板时） —— 缩放 radius（推近 / 拉远）
//
// 灵敏度字段是编辑器经验值；未来可暴露给 Preferences 面板。

#include <glm/vec3.hpp>

#include <array>

struct EditorCameraState
{
    glm::vec3 pivot{0.0f, 0.5f, 0.0f};  // 轨道中心，暂定场景中心
    float     azimuth     = 0.0f;        // 水平角（弧度）；0 = 相机在 +Z 侧
    float     elevation   = 0.19f;       // 垂直角（弧度）；正 = 相机高于 pivot
    float     radius      = 8.15f;       // 相机到 pivot 的距离
    float     fovYDegrees = 45.0f;
    float     zNear       = 0.1f;
    float     zFar        = 100.0f;

    // Saved views（相机书签，gap 报告 §4 #4）—— 快照 7 个视图参数（不含 dragging
    // / 灵敏度等 live 输入态）。View 菜单 "Camera Bookmarks" Save / Go 消费。
    // 当前 **in-memory 单 session**（跨重启持久化留待后续：序列化进 editor_settings.json）。
    struct ViewBookmark
    {
        glm::vec3 pivot{0.0f, 0.5f, 0.0f};
        float     azimuth     = 0.0f;
        float     elevation   = 0.19f;
        float     radius      = 8.15f;
        float     fovYDegrees = 45.0f;
        float     zNear       = 0.1f;
        float     zFar        = 100.0f;
        bool      valid       = false;  // false = 该槽未保存过，Go 置灰
    };
    static constexpr int kBookmarkSlots = 4;
    std::array<ViewBookmark, kBookmarkSlots> savedViews{};

    // 操作灵敏度（编辑器经验值，未来可暴露给 Preferences）
    float     lookSensitivity = 0.0035f;  // 弧度 / pixel
    float     zoomSensitivity = 0.6f;     // radius units / wheel notch

    // LMB 拖动状态机：按下时（且鼠标在 Scene 面板内）置 true，进入"无
    // 论鼠标是否仍 hover 都吃 MouseDelta"模式；释放时清零。
    bool      dragging = false;
};

#endif  // ORANGE_EDITOR_CONTEXT_EDITOR_CAMERA_STATE_H
