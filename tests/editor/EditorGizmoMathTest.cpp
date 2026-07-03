// EditorGizmoMath 单元测试 —— viewport gizmo 子系统的反投影 / 最近点 /
// ray-plane / 屏幕投影数学。这是 translate/rotate/scale 三 gizmo 拖动 +
// hit-test 的共用地基；纯 glm、noexcept、无 ImGui/World 依赖 → 可 headless
// 单测。锁住数学正确性可把 gizmo dogfood 的注意力集中到交互手感层
// （gap 报告 §5 "先单测逻辑内核，再 dogfood 交互层"）。
//
// 覆盖：
//   * ProjectWorldToScreen ↔ ScreenToWorldRay 往返（worldPos 必在反算 ray 上）
//   * ClosestPointOnAxisToRay：已知几何 + 平行退化 nullopt
//   * RayPlaneIntersect：已知 t + 平行 nullopt + plane 在背后 nullopt
//   * PointSegmentDistance2D：垂距 / 端点 clamp / 落在段上

#include "EditorGizmoMath.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

namespace GM = OrangeEditor::Internal::GizmoMath;

namespace
{

    constexpr float kEps = 1e-3f;

    bool ApproxEq(float a, float b)
    {
        return std::fabs(a - b) < kEps;
    }

    bool ApproxEqV(const glm::vec3& a, const glm::vec3& b)
    {
        return std::fabs(a.x - b.x) < kEps && std::fabs(a.y - b.y) < kEps && std::fabs(a.z - b.z) < kEps;
    }

    // 点到无限直线（origin + t*dir，dir 单位）的距离 = |cross(p-origin, dir)|。
    float DistPointToRay(const glm::vec3& p, const glm::vec3& origin, const glm::vec3& dir)
    {
        return glm::length(glm::cross(p - origin, dir));
    }

    void TestProjectScreenRoundTrip()
    {
        // 透视相机看向原点。NDC z-convention 不影响本不变量：worldPos 与反算
        // ray 上两点共享同一 ndc.xy → 三者落在同一投影射线 → worldPos 必在 ray 上
        // （无论 z 约定）。
        const glm::mat4 view        = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0),
                                                  glm::vec3(0, 1, 0));
        const glm::mat4 proj        = glm::perspective(glm::radians(45.0f), 4.0f / 3.0f, 0.1f, 100.0f);
        const glm::mat4 viewProj    = proj * view;
        const glm::mat4 invViewProj = glm::inverse(viewProj);

        const glm::vec2 imageOrigin(0.0f, 0.0f);
        const glm::vec2 imageSize(800.0f, 600.0f);
        const glm::vec3 worldPos(1.0f, 0.5f, 0.0f); // 相机前方

        const auto proj2d = GM::ProjectWorldToScreen(worldPos, viewProj, imageOrigin, imageSize);
        assert(proj2d.has_value());
        assert(proj2d->clipW > 0.0f); // 相机前方
        // 投影点应落在图像矩形内（粗略 sanity）。
        assert(proj2d->screen.x >= imageOrigin.x && proj2d->screen.x <= imageOrigin.x + imageSize.x);
        assert(proj2d->screen.y >= imageOrigin.y && proj2d->screen.y <= imageOrigin.y + imageSize.y);

        const auto ray = GM::ScreenToWorldRay(proj2d->screen, imageOrigin, imageSize, invViewProj);
        assert(ray.has_value());
        assert(ApproxEq(glm::length(ray->dir), 1.0f)); // 单位方向
        // worldPos 必在反算 ray 上（往返不变量）。
        assert(DistPointToRay(worldPos, ray->origin, ray->dir) < 1e-2f);

        std::fprintf(stdout, "  [PASS] Project ↔ ScreenToWorldRay 往返\n");
    }

    void TestClosestPointOnAxisToRay()
    {
        // X 轴 + 竖直向下的 ray（穿过 x=2,z=0）→ 轴上最近点 = (2,0,0)。
        const auto hit = GM::ClosestPointOnAxisToRay(
            glm::vec3(2, 5, 0), glm::vec3(0, -1, 0), // ray origin / dir
            glm::vec3(0, 0, 0), glm::vec3(1, 0, 0)); // axis origin / dir
        assert(hit.has_value());
        assert(ApproxEqV(*hit, glm::vec3(2, 0, 0)));

        // ray 与 axis 平行 → denom≈0 → nullopt。
        const auto deg = GM::ClosestPointOnAxisToRay(
            glm::vec3(0, 1, 0), glm::vec3(1, 0, 0),
            glm::vec3(0, 0, 0), glm::vec3(1, 0, 0));
        assert(!deg.has_value());

        std::fprintf(stdout, "  [PASS] ClosestPointOnAxisToRay\n");
    }

    void TestRayPlaneIntersect()
    {
        // ray 从 (0,0,5) 向 -Z，plane z=0 → t=5。
        const auto t = GM::RayPlaneIntersect(
            glm::vec3(0, 0, 5), glm::vec3(0, 0, -1),
            glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));
        assert(t.has_value());
        assert(ApproxEq(*t, 5.0f));

        // ray 平行于 plane → nullopt。
        const auto par = GM::RayPlaneIntersect(
            glm::vec3(0, 0, 5), glm::vec3(1, 0, 0),
            glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));
        assert(!par.has_value());

        // ray 背向 plane（t<0）→ nullopt。
        const auto behind = GM::RayPlaneIntersect(
            glm::vec3(0, 0, 5), glm::vec3(0, 0, 1),
            glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));
        assert(!behind.has_value());

        std::fprintf(stdout, "  [PASS] RayPlaneIntersect\n");
    }

    void TestPointSegmentDistance2D()
    {
        // 垂距：p 在段中点正上方 1。
        assert(ApproxEq(GM::PointSegmentDistance2D(
                            glm::vec2(0, 1), glm::vec2(-1, 0), glm::vec2(1, 0)),
                        1.0f));
        // 端点 clamp：p 在 b 外侧 → 最近点 = b。
        assert(ApproxEq(GM::PointSegmentDistance2D(
                            glm::vec2(2, 0), glm::vec2(-1, 0), glm::vec2(1, 0)),
                        1.0f));
        // 落在段上 → 0。
        assert(ApproxEq(GM::PointSegmentDistance2D(
                            glm::vec2(0, 0), glm::vec2(-1, 0), glm::vec2(1, 0)),
                        0.0f));

        std::fprintf(stdout, "  [PASS] PointSegmentDistance2D\n");
    }

} // namespace

int main()
{
    std::fprintf(stdout, "[EditorGizmoMathTest] running\n");
    TestProjectScreenRoundTrip();
    TestClosestPointOnAxisToRay();
    TestRayPlaneIntersect();
    TestPointSegmentDistance2D();
    std::fprintf(stdout, "[EditorGizmoMathTest] all tests passed.\n");
    return 0;
}
