#include "CameraFrustumGizmoPlugin.h"

#include "../EditorGizmoMath.h"
#include "../EditorHost.h"
#include "../schema/ComponentSchema.h"
#include "GizmoContext.h"

#include <orange/engine/render/Camera.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldTransformComponent.h>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cmath>
#include <string_view>

namespace Orange::Editor::Plugin
{

namespace
{

namespace GM = OrangeEditor::Internal::GizmoMath;

constexpr ImU32 kFrustumColor     = IM_COL32(140, 250, 240, 220);  // 浅青色（与 ParticleEmitter spawn box 同色系）
constexpr ImU32 kFrustumNearColor = IM_COL32(180, 255, 240, 255);  // near plane 更亮（区分远近）

}  // anonymous namespace

bool CameraFrustumGizmoPlugin::CanHandle(
    const Orange::Editor::Schema::ComponentSchema& schema) const
{
    if (schema.typeName == nullptr) { return false; }
    return std::string_view(schema.typeName) == std::string_view("Camera");
}

void CameraFrustumGizmoPlugin::Draw(
    EditorHost&                                            host,
    Orange::Engine::Entity                                 entity,
    const Orange::Editor::Schema::ComponentSchema&         schema,
    void*                                                  component,
    const GizmoContext&                                    ctx)
{
    (void)schema;

    using TC = Orange::Engine::Scene::TransformComponent;
    using Camera = Orange::Engine::Render::Camera;

    if (ctx.drawList == nullptr) { return; }
    if (component == nullptr)    { return; }
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return; }
    auto* pTC = pWorld->GetComponent<TC>(entity);
    if (pTC == nullptr) { return; }

    // GAP-2026-05-15 落地后路径：ECS Camera 组件不再被编辑器轨道相机覆写
    //（ScenePanel 改用 Pipeline::SetEditorCameraOverride），可以安全读 component
    // 真实数据。projection 直接来自 Camera（fov / aspect / near / far 由用户在
    // DemoWorld / Inspector 设置）；view 仍由 entity.Transform 推（让用户拖
    // entity 时 frustum 实时跟随位置 / 朝向，与"摆位预览"的 UX 一致——参 Unity
    // / Lumix 同款约定）。
    const auto* pCam = static_cast<const Camera*>(component);
    const glm::mat4& proj = pCam->projection;

    // ---- view: 从 entity.Transform 推 ----
    // Camera"看向 -Z"是工业惯例（OpenGL / GLTF / Vulkan 同款）。entity
    // rotation 决定 forward / up 方向。
    // eye + forward/up 取累积后 world matrix（ADR-016 / A1.1 step 2）；parented
    // 相机的 frustum 对齐世界位姿。cache 缺失退回 entity local 兜底。
    glm::vec3 forward = pTC->rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up      = pTC->rotation * glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 eye     = pTC->position;
    if (const auto* wtc =
            pWorld->GetComponent<Orange::Engine::Scene::WorldTransformComponent>(entity))
    {
        forward = glm::normalize(glm::vec3(wtc->world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        up      = glm::normalize(glm::vec3(wtc->world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
        eye     = glm::vec3(wtc->world[3]);
    }
    const glm::mat4 view = glm::lookAt(eye, eye + forward, up);

    // ---- frustum 8 corners：NDC → world ----
    // Vulkan NDC: x/y ∈ [-1, 1], z = 0 (near) / 1 (far)。
    // worldCorner = inverse(proj * view) * NDC homogeneous corner
    const glm::mat4 invPV = glm::inverse(proj * view);
    const glm::vec3 ndcCorners[8] = {
        {-1.0f, -1.0f, 0.0f}, { 1.0f, -1.0f, 0.0f},  // near: 左下, 右下
        { 1.0f,  1.0f, 0.0f}, {-1.0f,  1.0f, 0.0f},  // near: 右上, 左上
        {-1.0f, -1.0f, 1.0f}, { 1.0f, -1.0f, 1.0f},  // far : 左下, 右下
        { 1.0f,  1.0f, 1.0f}, {-1.0f,  1.0f, 1.0f},  // far : 右上, 左上
    };
    std::array<glm::vec2, 8> screenCorners;
    bool allVisible = true;
    for (int i = 0; i < 8; ++i)
    {
        const glm::vec4 h = invPV * glm::vec4(ndcCorners[i], 1.0f);
        if (std::abs(h.w) < 1e-6f) { allVisible = false; break; }
        const glm::vec3 worldP = glm::vec3(h) / h.w;
        const auto sp = GM::ProjectWorldToScreen(worldP, ctx.viewProj,
                                                 ctx.imageOrigin, ctx.imageSize);
        if (!sp.has_value()) { allVisible = false; break; }
        screenCorners[i] = sp->screen;
    }
    if (!allVisible) { return; }

    // ---- 画 12 段：4 near + 4 far + 4 connecting ----
    auto line = [&](int a, int b, ImU32 col, float thickness)
    {
        ctx.drawList->AddLine(
            ImVec2(screenCorners[a].x, screenCorners[a].y),
            ImVec2(screenCorners[b].x, screenCorners[b].y),
            col, thickness);
    };
    // near rect (0..3)：更亮 + 略粗，区分远近
    line(0, 1, kFrustumNearColor, 2.0f);
    line(1, 2, kFrustumNearColor, 2.0f);
    line(2, 3, kFrustumNearColor, 2.0f);
    line(3, 0, kFrustumNearColor, 2.0f);
    // far rect (4..7)
    line(4, 5, kFrustumColor, 1.5f);
    line(5, 6, kFrustumColor, 1.5f);
    line(6, 7, kFrustumColor, 1.5f);
    line(7, 4, kFrustumColor, 1.5f);
    // connecting edges
    line(0, 4, kFrustumColor, 1.5f);
    line(1, 5, kFrustumColor, 1.5f);
    line(2, 6, kFrustumColor, 1.5f);
    line(3, 7, kFrustumColor, 1.5f);
}

}  // namespace Orange::Editor::Plugin
