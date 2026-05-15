// EditorCameraControl 实现 —— 见 EditorCameraControl.h 的注释。

#include "EditorCameraControl.h"

#include <orange/engine/scene/World.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <imgui.h>

#include <cmath>

void UpdateEditorCameraFromInput(EditorHost& host)
{
    auto& ec = host.camera;
    const ImGuiIO& io = ImGui::GetIO();

    const bool hovered = ImGui::IsWindowHovered();

    // ---- gizmo gate（v0.4 c5 之后 bug-fix）------------------------------
    // 用户撞 bug：hover gizmo handle 时按下 LMB → 既启动相机轨道又启动
    // gizmo 拖动，鼠标移动时两者同时消费 MouseDelta → 场景旋转 + entity
    // 沿 axis 飞速移动 + handle 视觉跟随，看起来像 "gizmo 抢事件 + handle
    // 变长"。
    //
    // gate 逻辑：
    //   * gizmo 正在拖动（IsDragging）→ camera 既不启动也不维持 dragging
    //     —— 与 gizmo drag 互斥，松手前 camera 完全冻结
    //   * gizmo hovered（IsHovered）+ camera 还没在 dragging → 不让本帧
    //     LMB click 启动 camera dragging（让 gizmo dispatch 抢这次 click）
    //   * camera 已经 dragging（用户先 click 空白处启动后再划过 gizmo
    //     handle）→ 维持原状不打断（按 capture-on-press 语义）
    //
    // 读 host.gizmo 字段时拿的是**上一帧**值（gizmo dispatch 在 ScenePanel
    // 后段调用，更新 hoveredAxis 在本函数之后）。实际 UX 下用户 hover →
    // click 至少跨多帧（人类反应时间 >> 16ms 单帧），上一帧 hover 状态
    // 正确反映"按下 LMB 那一刻"。
    const bool gizmoBusy = host.gizmo.IsDragging();
    const bool gizmoHover = host.gizmo.IsHovered();

    // LMB 轨道旋转 —— capture-on-press 状态机：
    //   * 仅当 LMB 在本面板内 *按下* 时进入 dragging 模式；
    //   * dragging 期间无视 hover，连续吃 MouseDelta —— 修复"拖快了鼠标
    //     划出面板 → 旋转中断"的体感问题；
    //   * LMB 释放退出 dragging。
    if (ec.dragging && (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || gizmoBusy))
    {
        ec.dragging = false;
    }
    if (!ec.dragging && hovered
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !gizmoHover && !gizmoBusy)
    {
        ec.dragging = true;
    }
    if (ec.dragging)
    {
        const ImVec2 d = io.MouseDelta;
        // 拖右 → azimuth 减小 → 相机向左绕轨道 → 场景向右转，与鼠标方向一致
        ec.azimuth  -= d.x * ec.lookSensitivity;
        // 屏幕 Y 向下为正；拖下 → d.y > 0 → elevation 减小 → 相机下沉 → 视角上仰
        ec.elevation += d.y * ec.lookSensitivity;
        constexpr float kMaxElev = 1.5533430343f;   // glm::radians(89°)
        if (ec.elevation >  kMaxElev) ec.elevation =  kMaxElev;
        if (ec.elevation < -kMaxElev) ec.elevation = -kMaxElev;
    }

    // 滚轮：缩放 radius（推近 / 拉远）；hover 才生效避免误触其它面板滚动条。
    // 不被 gizmo gate 影响——滚轮缩放与 gizmo drag 不冲突（gizmo 不消费滚轮）。
    if (hovered && io.MouseWheel != 0.0f)
    {
        ec.radius -= io.MouseWheel * ec.zoomSensitivity;
        if (ec.radius < 0.5f) ec.radius = 0.5f;
    }
}

Orange::Engine::Render::Camera
BuildEditorCamera(const EditorCameraState& ec, float aspect)
{
    using ::Orange::Engine::Render::Camera;
    const float safeAspect = (aspect > 0.0f) ? aspect : 1.0f;
    Camera cam = Camera::Perspective(glm::radians(ec.fovYDegrees),
                                     safeAspect, ec.zNear, ec.zFar);
    const float cosElev = std::cos(ec.elevation);
    const glm::vec3 offset(
        ec.radius * cosElev * std::sin(ec.azimuth),
        ec.radius * std::sin(ec.elevation),
        ec.radius * cosElev * std::cos(ec.azimuth));
    const glm::vec3 position = ec.pivot + offset;
    cam.view = glm::lookAt(position, ec.pivot, glm::vec3(0.0f, 1.0f, 0.0f));
    return cam;
}

void ApplyEditorCameraToWorld(EditorHost& host, float aspect)
{
    if (host.scene.pWorld == nullptr)
    {
        return;
    }
    using ::Orange::Engine::Render::Camera;
    auto& reg  = host.scene.pWorld->Registry();
    auto  view = reg.view<Camera>();
    if (view.empty())
    {
        return;
    }
    const auto e   = view.front();
    auto&      cam = view.get<Camera>(e);
    cam            = BuildEditorCamera(host.camera, aspect);
}
