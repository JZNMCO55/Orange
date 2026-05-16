#ifndef ORANGE_EDITOR_EDITOR_GIZMO_MATH_H
#define ORANGE_EDITOR_EDITOR_GIZMO_MATH_H

// EditorGizmoMath —— viewport gizmo 子系统（Translate / Rotate / Scale）
// 共用的反投影 / 最近点 / ray-plane / 屏幕投影数学。
//
// 设计意图：c2 期 Translate gizmo 把这些 helper 写在 EditorTranslateGizmo
// .cpp 的 anonymous namespace；c3 引入 Rotate / Scale 同样需要这些函数。
// 三 cpp 各持一份会随时间漂移（典型撞坑：translate 改了 NDC y-down 注释
// 但 rotate / scale 忘改），且违反"禁止跨编辑器源文件硬重复"工程惯例。
// 因此提取到一个**编辑器内部** helper 文件，三 gizmo cpp 共享。
//
// 公共面：仅在 editor target 内消费（tools/OrangeEditor/*）；引擎公共
// API 不感知本头存在，与 EditorPicking 反投影路径（同款几何，在
// EditorPicking.cpp 内独立写）刻意保持独立——picking 是 editor-side
// 自包含工具，gizmo 数学是 editor-side 自包含工具，二者都不污染引擎
// 公共面。两份反投影代码相似但各自归属域清晰，c4+ 若进一步整合视真实
// 需求决定。
//
// 坐标系统约定（与 EditorPicking.cpp 一致）：
//   * Vulkan NDC：y-down + z ∈ [0, 1]；与 ImGui 屏幕 y-down 同向
//   * world → screen：不额外翻转 y
//   * screen → ndc：不额外翻转 y

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <optional>

namespace OrangeEditor::Internal::GizmoMath
{

// world → 屏幕坐标。返回 nullopt = 点在相机后方（clip.w <= ~0）或除法
// 退化；caller 应跳过绘制 / hit-test 该点。
struct ScreenProjection
{
    glm::vec2 screen;
    float     clipW;  // > 0 表示位于相机前方；caller 一般只关心 screen
};

std::optional<ScreenProjection>
ProjectWorldToScreen(const glm::vec3& worldPos,
                     const glm::mat4& viewProj,
                     glm::vec2        imageOrigin,
                     glm::vec2        imageSize) noexcept;

// 屏幕坐标 → world ray。复用 EditorPicking.cpp 同款反投影。返回 nullopt
// = invViewProj 退化（w ≈ 0）/ ray 长度退化 / image 尺寸非法。
struct WorldRay
{
    glm::vec3 origin;
    glm::vec3 dir;  // 单位向量
};

std::optional<WorldRay>
ScreenToWorldRay(glm::vec2        mouseScreen,
                 glm::vec2        imageOrigin,
                 glm::vec2        imageSize,
                 const glm::mat4& invViewProj) noexcept;

// 2D 点到线段最短距离（gizmo handle 屏幕空间 hit-test 用）。
float
PointSegmentDistance2D(glm::vec2 p, glm::vec2 a, glm::vec2 b) noexcept;

// mouse ray 与 axis 线（无限延伸）的最近点（在 axis 上的那一点）。
// 公式（D / A 单位向量，假设）：
//   w = O - P, b = dot(D, A), denom = 1 - b*b
//   t = (dot(A,w) - b * dot(D,w)) / denom
// 退化（denom 接近 0 = ray 与 axis 几乎平行）返回 nullopt。
std::optional<glm::vec3>
ClosestPointOnAxisToRay(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                        const glm::vec3& axisOrigin, const glm::vec3& axisDir) noexcept;

// ray-plane intersection（plane 通过 planeOrigin，法向量 planeNormal）。
// 返回 ray 上的 t（origin + t * dir）；ray 与 plane 几乎平行（|dot(dir,
// normal)| < eps）或 t < 0（plane 在相机后方）返回 nullopt。
std::optional<float>
RayPlaneIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                  const glm::vec3& planeOrigin, const glm::vec3& planeNormal) noexcept;

// 在 worldPos 所在深度求"屏幕 targetScreenPx 像素 ≈ 多少世界单位"。
// 用相机右向量（从 view 矩阵第一行抽出）作为 1 单位探针，保证探针方向
// 与视线垂直 —— 这样 pxPerUnit 与 world 轴朝向 / 相机轨道角度无关，
// 避免"world X 接近视线方向时 foreshortening 让 pxPerUnit 趋近 0、
// gizmo handle / 圆环骤然放大"的视觉抖动。
// 返回 nullopt 当 worldPos 投影失败 / camera right 探针投影失败 /
// pxPerUnit 退化（< 1e-3）—— caller 应回退到固定 fallback（如 1.0）。
std::optional<float>
ComputeWorldUnitsForScreenLength(const glm::vec3& worldPos,
                                 const glm::mat4& view,
                                 const glm::mat4& viewProj,
                                 glm::vec2        imageOrigin,
                                 glm::vec2        imageSize,
                                 float            targetScreenPx) noexcept;

}  // namespace OrangeEditor::Internal::GizmoMath

#endif  // ORANGE_EDITOR_EDITOR_GIZMO_MATH_H
