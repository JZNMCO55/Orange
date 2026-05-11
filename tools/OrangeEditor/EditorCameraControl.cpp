// EditorCameraControl 实现 —— 见 EditorCameraControl.h 的注释。

#include "EditorCameraControl.h"

#include <orange/engine/scene/World.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <imgui.h>

#include <cmath>

glm::vec3 EditorCameraForward(float yaw, float pitch) noexcept
{
    return glm::vec3(
        std::cos(pitch) * std::sin(yaw),
        std::sin(pitch),
       -std::cos(pitch) * std::cos(yaw));
}

void UpdateEditorCameraFromInput(EditorState::EditorCamera& ec)
{
    const ImGuiIO& io = ImGui::GetIO();
    const float    dt = io.DeltaTime;
    if (dt <= 0.0f)
    {
        return;
    }

    const bool hovered = ImGui::IsWindowHovered();
    const bool focused = ImGui::IsWindowFocused();

    // 鼠标右键拖动：旋转 yaw / pitch。仅当鼠标 down 且本面板 hover 才
    // 累加 delta —— 用户从 Scene 之外按住 RMB 拖进来不会突然转动相机。
    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        const ImVec2 d = io.MouseDelta;
        ec.yaw   -= d.x * ec.lookSensitivity;
        ec.pitch -= d.y * ec.lookSensitivity;
        constexpr float kMaxPitch = 1.5533430343f;   // glm::radians(89°)
        if (ec.pitch >  kMaxPitch) ec.pitch =  kMaxPitch;
        if (ec.pitch < -kMaxPitch) ec.pitch = -kMaxPitch;
    }

    // 滚轮：沿 forward 距离方向缩放（前 / 后）；hover 才生效避免在其它
    // 面板滚动条上误触。
    if (hovered && io.MouseWheel != 0.0f)
    {
        const glm::vec3 forward = EditorCameraForward(ec.yaw, ec.pitch);
        ec.position += forward * io.MouseWheel * ec.zoomSensitivity;
    }

    // WASD / QE：相对相机朝向移动。仅当 Scene 面板有焦点 —— 否则在
    // 别的窗口里编辑 InputText 也会触发相机走 / 转动。focused 同时也
    // 让 ImGui::IsKeyDown 拿到的是 Scene 面板上下文的输入路由结果。
    if (focused)
    {
        const float     speed   = ec.moveSpeed * dt;
        const glm::vec3 forward = EditorCameraForward(ec.yaw, ec.pitch);
        const glm::vec3 right   = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
        const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

        if (ImGui::IsKeyDown(ImGuiKey_W)) { ec.position += forward * speed; }
        if (ImGui::IsKeyDown(ImGuiKey_S)) { ec.position -= forward * speed; }
        if (ImGui::IsKeyDown(ImGuiKey_A)) { ec.position -= right   * speed; }
        if (ImGui::IsKeyDown(ImGuiKey_D)) { ec.position += right   * speed; }
        if (ImGui::IsKeyDown(ImGuiKey_E)) { ec.position += worldUp * speed; }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) { ec.position -= worldUp * speed; }
    }
}

Orange::Engine::Render::Camera
BuildEditorCamera(const EditorState::EditorCamera& ec, float aspect)
{
    using ::Orange::Engine::Render::Camera;
    const float safeAspect = (aspect > 0.0f) ? aspect : 1.0f;
    Camera cam = Camera::Perspective(glm::radians(ec.fovYDegrees),
                                     safeAspect, ec.zNear, ec.zFar);
    const glm::vec3 forward = EditorCameraForward(ec.yaw, ec.pitch);
    cam.view = glm::lookAt(ec.position, ec.position + forward, glm::vec3(0, 1, 0));
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
