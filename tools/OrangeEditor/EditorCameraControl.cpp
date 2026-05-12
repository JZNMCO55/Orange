// EditorCameraControl 实现 —— 见 EditorCameraControl.h 的注释。

#include "EditorCameraControl.h"

#include <orange/engine/scene/World.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <imgui.h>

#include <cmath>

void UpdateEditorCameraFromInput(EditorState::EditorCamera& ec)
{
    const ImGuiIO& io = ImGui::GetIO();

    const bool hovered = ImGui::IsWindowHovered();

    // LMB 轨道旋转 —— capture-on-press 状态机：
    //   * 仅当 LMB 在本面板内 *按下* 时进入 dragging 模式；
    //   * dragging 期间无视 hover，连续吃 MouseDelta —— 修复"拖快了鼠标
    //     划出面板 → 旋转中断"的体感问题；
    //   * LMB 释放退出 dragging。
    if (ec.dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        ec.dragging = false;
    }
    if (!ec.dragging && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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
    if (hovered && io.MouseWheel != 0.0f)
    {
        ec.radius -= io.MouseWheel * ec.zoomSensitivity;
        if (ec.radius < 0.5f) ec.radius = 0.5f;
    }
}

Orange::Engine::Render::Camera
BuildEditorCamera(const EditorState::EditorCamera& ec, float aspect)
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

void ApplyEditorCameraToWorld(EditorState& state, float aspect)
{
    if (state.pWorld == nullptr)
    {
        return;
    }
    using ::Orange::Engine::Render::Camera;
    auto& reg  = state.pWorld->Registry();
    auto  view = reg.view<Camera>();
    if (view.empty())
    {
        return;
    }
    const auto e   = view.front();
    auto&      cam = view.get<Camera>(e);
    cam            = BuildEditorCamera(state.editorCamera, aspect);
}
