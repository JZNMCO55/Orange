#include "EditorGizmoMath.h"

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>

namespace OrangeEditor::Internal::GizmoMath
{

std::optional<ScreenProjection>
ProjectWorldToScreen(const glm::vec3& worldPos,
                     const glm::mat4& viewProj,
                     glm::vec2        imageOrigin,
                     glm::vec2        imageSize) noexcept
{
    const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 1e-4f) { return std::nullopt; }
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    // Vulkan NDC y-down 与 ImGui 屏幕 y-down 同向，不额外翻转 y。
    const float sx = imageOrigin.x + (ndcX * 0.5f + 0.5f) * imageSize.x;
    const float sy = imageOrigin.y + (ndcY * 0.5f + 0.5f) * imageSize.y;
    return ScreenProjection{glm::vec2(sx, sy), clip.w};
}

std::optional<WorldRay>
ScreenToWorldRay(glm::vec2        mouseScreen,
                 glm::vec2        imageOrigin,
                 glm::vec2        imageSize,
                 const glm::mat4& invViewProj) noexcept
{
    if (imageSize.x <= 0.0f || imageSize.y <= 0.0f) { return std::nullopt; }
    const float ndcX = ((mouseScreen.x - imageOrigin.x) / imageSize.x) * 2.0f - 1.0f;
    const float ndcY = ((mouseScreen.y - imageOrigin.y) / imageSize.y) * 2.0f - 1.0f;
    const glm::vec4 nearH = invViewProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farH  = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (std::abs(nearH.w) < 1e-9f || std::abs(farH.w) < 1e-9f) { return std::nullopt; }
    const glm::vec3 origin = glm::vec3(nearH) / nearH.w;
    const glm::vec3 farPt  = glm::vec3(farH)  / farH.w;
    const glm::vec3 diff   = farPt - origin;
    const float len = glm::length(diff);
    if (len < 1e-6f) { return std::nullopt; }
    return WorldRay{origin, diff / len};
}

float
PointSegmentDistance2D(glm::vec2 p, glm::vec2 a, glm::vec2 b) noexcept
{
    const glm::vec2 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    if (len2 < 1e-6f) { return glm::length(p - a); }
    float t = glm::dot(p - a, ab) / len2;
    t = std::clamp(t, 0.0f, 1.0f);
    const glm::vec2 closest = a + ab * t;
    return glm::length(p - closest);
}

std::optional<glm::vec3>
ClosestPointOnAxisToRay(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                        const glm::vec3& axisOrigin, const glm::vec3& axisDir) noexcept
{
    const glm::vec3 w = rayOrigin - axisOrigin;
    const float b = glm::dot(rayDir, axisDir);
    const float denom = 1.0f - b * b;
    if (std::abs(denom) < 1e-5f) { return std::nullopt; }
    const float d = glm::dot(rayDir, w);
    const float e = glm::dot(axisDir, w);
    const float t = (e - b * d) / denom;
    return axisOrigin + axisDir * t;
}

std::optional<float>
RayPlaneIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                  const glm::vec3& planeOrigin, const glm::vec3& planeNormal) noexcept
{
    const float denom = glm::dot(rayDir, planeNormal);
    if (std::abs(denom) < 1e-5f) { return std::nullopt; }  // 几乎平行
    const float t = glm::dot(planeOrigin - rayOrigin, planeNormal) / denom;
    if (t < 0.0f) { return std::nullopt; }  // plane 在 ray 后方
    return t;
}

std::optional<float>
ComputeWorldUnitsForScreenLength(const glm::vec3& worldPos,
                                 const glm::mat4& view,
                                 const glm::mat4& viewProj,
                                 glm::vec2        imageOrigin,
                                 glm::vec2        imageSize,
                                 float            targetScreenPx) noexcept
{
    // glm 列主序：view[col][row]。lookAt 产出的视矩阵第 0 行是相机
    // 右向量在世界坐标系的分量 (rx, ry, rz)。
    const glm::vec3 cameraRightWorld(view[0][0], view[1][0], view[2][0]);
    const float rightLen = glm::length(cameraRightWorld);
    if (rightLen < 1e-6f) { return std::nullopt; }
    const glm::vec3 cameraRightUnit = cameraRightWorld / rightLen;

    const auto projOrigin = ProjectWorldToScreen(worldPos, viewProj,
                                                 imageOrigin, imageSize);
    if (!projOrigin.has_value()) { return std::nullopt; }
    const auto projOriginPlusRight = ProjectWorldToScreen(worldPos + cameraRightUnit,
                                                          viewProj,
                                                          imageOrigin, imageSize);
    if (!projOriginPlusRight.has_value()) { return std::nullopt; }
    const float pxPerUnit = glm::length(projOriginPlusRight->screen - projOrigin->screen);
    if (pxPerUnit < 1e-3f) { return std::nullopt; }
    return targetScreenPx / pxPerUnit;
}

}  // namespace OrangeEditor::Internal::GizmoMath
