// SplineTest —— 样条 / 曲线子系统 headless 验收。裸 <cassert> + 独立 main()，
// 进程 exit 0 = pass。
//
// 子测试：
//   * Linear 求值：端点 / 段内 lerp / 控制点边界
//   * CatmullRom 过控制点：段边界 t=k/numSeg 恰等对应控制点（C1 光滑但过点）
//   * 闭合样条环绕：t=0 与 t=1 同点、t=1.5 ≡ t=0.5（wrap 语义）
//   * 切线：共线样条 → 单位方向；端点单侧差分；退化单点 / 空 → (0,0,0) 不崩
//   * 弧长匀速：直角折线总长精确、ByDistance 沿弧长匀速取点
//   * 匀速 vs 参数非匀速：非对称段长下 ByDistance(total/2) ≠ EvaluateSpline(0.5)
//   * 退化输入：空 points / 单点不崩

#include "orange/engine/spline/Spline.h"

#include <glm/vec3.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

namespace Spl = Orange::Engine::Spline;

namespace
{

    bool Near(float a, float b, float tol = 1e-4f)
    {
        return std::fabs(a - b) <= tol;
    }

    bool NearV3(glm::vec3 a, glm::vec3 b, float tol = 1e-4f)
    {
        return Near(a.x, b.x, tol) && Near(a.y, b.y, tol) && Near(a.z, b.z, tol);
    }

    // ---- Linear 求值 ---------------------------------------------------------

    void TestLinearEvaluate()
    {
        Spl::SplinePath path;
        path.type   = Spl::SplineType::Linear;
        path.closed = false;
        path.points = {glm::vec3(0, 0, 0), glm::vec3(10, 0, 0), glm::vec3(10, 10, 0)};

        // numSegments=2：t=0 首点、t=1 末点。
        assert(NearV3(Spl::EvaluateSpline(path, 0.0f), glm::vec3(0, 0, 0)));
        assert(NearV3(Spl::EvaluateSpline(path, 1.0f), glm::vec3(10, 10, 0)));
        // t=0.25 → ft=0.5，seg0 s=0.5 → lerp((0,0,0),(10,0,0),0.5)=(5,0,0)。
        assert(NearV3(Spl::EvaluateSpline(path, 0.25f), glm::vec3(5, 0, 0)));
        // t=0.5 → ft=1.0，落在控制点 (10,0,0)。
        assert(NearV3(Spl::EvaluateSpline(path, 0.5f), glm::vec3(10, 0, 0)));
        // clamp：t<0 / t>1 收到端点。
        assert(NearV3(Spl::EvaluateSpline(path, -0.5f), glm::vec3(0, 0, 0)));
        assert(NearV3(Spl::EvaluateSpline(path, 2.0f), glm::vec3(10, 10, 0)));

        std::printf("[ok] Linear evaluate\n");
    }

    // ---- CatmullRom 过控制点 -------------------------------------------------

    void TestCatmullRomPassesControlPoints()
    {
        Spl::SplinePath path;
        path.type   = Spl::SplineType::CatmullRom;
        path.closed = false;
        path.points = {glm::vec3(0, 0, 0), glm::vec3(1, 2, 0), glm::vec3(3, -1, 0), glm::vec3(5, 1, 0)};

        // numSegments=3；t=k/3（段边界）恰好等于第 k 个控制点（Catmull-Rom 过点性质）。
        const int numSeg = 3;
        for (int k = 0; k < static_cast<int>(path.points.size()); ++k)
        {
            const float     t = static_cast<float>(k) / static_cast<float>(numSeg);
            const glm::vec3 p = Spl::EvaluateSpline(path, t);
            assert(NearV3(p, path.points[static_cast<std::size_t>(k)]));
        }

        // 段内点不等于任一控制点（确认真在插值而非常量返回）。
        const glm::vec3 mid = Spl::EvaluateSpline(path, 0.5f);
        assert(!NearV3(mid, path.points[1]) && !NearV3(mid, path.points[2]));

        std::printf("[ok] CatmullRom passes control points\n");
    }

    // ---- 闭合样条环绕 --------------------------------------------------------

    void TestClosedWrap()
    {
        Spl::SplinePath path;
        path.type   = Spl::SplineType::CatmullRom;
        path.closed = true;
        path.points = {glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(1, 1, 0), glm::vec3(0, 1, 0)};

        // 闭合：t=0 与 t=1 同点（环首尾相接）。
        assert(NearV3(Spl::EvaluateSpline(path, 0.0f), Spl::EvaluateSpline(path, 1.0f)));
        // t 环绕：t=1.5 ≡ t=0.5、t=2.25 ≡ t=0.25。
        assert(NearV3(Spl::EvaluateSpline(path, 1.5f), Spl::EvaluateSpline(path, 0.5f)));
        assert(NearV3(Spl::EvaluateSpline(path, 2.25f), Spl::EvaluateSpline(path, 0.25f)));
        // 负向环绕：t=-0.25 ≡ t=0.75。
        assert(NearV3(Spl::EvaluateSpline(path, -0.25f), Spl::EvaluateSpline(path, 0.75f)));

        std::printf("[ok] closed wrap\n");
    }

    // ---- 切线 ----------------------------------------------------------------

    void TestTangent()
    {
        // 共线样条（沿 +x）：切线处处 ≈ (1,0,0)。
        Spl::SplinePath line;
        line.type   = Spl::SplineType::Linear;
        line.closed = false;
        line.points = {glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(2, 0, 0), glm::vec3(3, 0, 0)};

        assert(NearV3(Spl::EvaluateSplineTangent(line, 0.5f), glm::vec3(1, 0, 0), 1e-3f));
        // 端点单侧差分：t=0 前向、t=1 后向，均得 (1,0,0)。
        assert(NearV3(Spl::EvaluateSplineTangent(line, 0.0f), glm::vec3(1, 0, 0), 1e-3f));
        assert(NearV3(Spl::EvaluateSplineTangent(line, 1.0f), glm::vec3(1, 0, 0), 1e-3f));

        // 越界 t 回归（对抗式复核逮到）：EvaluateSpline clamp t，切线也须一致 clamp/wrap——
        // 否则越界 t 的两差分采样同点 → 退化零切线。t=1.5 / -0.5 切线仍应是端点方向 (1,0,0)。
        assert(NearV3(Spl::EvaluateSplineTangent(line, 1.5f), glm::vec3(1, 0, 0), 1e-3f) &&
               "t>1 切线=末端方向（非零切线）");
        assert(NearV3(Spl::EvaluateSplineTangent(line, -0.5f), glm::vec3(1, 0, 0), 1e-3f) &&
               "t<0 切线=首端方向（非零切线）");

        // CatmullRom 共线 → 曲线仍是直线 → 切线仍 (1,0,0)。
        Spl::SplinePath curve = line;
        curve.type            = Spl::SplineType::CatmullRom;
        assert(NearV3(Spl::EvaluateSplineTangent(curve, 0.5f), glm::vec3(1, 0, 0), 1e-3f));

        // 切线是单位向量。
        const glm::vec3 tg  = Spl::EvaluateSplineTangent(line, 0.3f);
        const float     len = std::sqrt(tg.x * tg.x + tg.y * tg.y + tg.z * tg.z);
        assert(Near(len, 1.0f, 1e-3f));

        // 退化：单点 / 空 → (0,0,0) 不崩。
        Spl::SplinePath single;
        single.points = {glm::vec3(7, 8, 9)};
        assert(NearV3(Spl::EvaluateSplineTangent(single, 0.5f), glm::vec3(0, 0, 0)));

        Spl::SplinePath empty;
        assert(NearV3(Spl::EvaluateSplineTangent(empty, 0.5f), glm::vec3(0, 0, 0)));

        std::printf("[ok] tangent\n");
    }

    // ---- 弧长匀速遍历 --------------------------------------------------------

    void TestArcLengthUniform()
    {
        // 直角折线：两段各长 10，总长精确 20。
        Spl::SplinePath path;
        path.type   = Spl::SplineType::Linear;
        path.closed = false;
        path.points = {glm::vec3(0, 0, 0), glm::vec3(10, 0, 0), glm::vec3(10, 10, 0)};

        const Spl::SplineArcTable table = Spl::BuildSplineArcTable(path);
        assert(Near(Spl::SplineTotalLength(table), 20.0f, 1e-3f));

        // 按弧长匀速取点：d=0 首点、d=20 末点、d=10 拐点、d=5 半段、d=15 二段中点。
        assert(NearV3(Spl::EvaluateSplineByDistance(path, table, 0.0f), glm::vec3(0, 0, 0), 1e-3f));
        assert(NearV3(Spl::EvaluateSplineByDistance(path, table, 20.0f), glm::vec3(10, 10, 0), 1e-3f));
        assert(NearV3(Spl::EvaluateSplineByDistance(path, table, 10.0f), glm::vec3(10, 0, 0), 1e-3f));
        assert(NearV3(Spl::EvaluateSplineByDistance(path, table, 5.0f), glm::vec3(5, 0, 0), 1e-3f));
        assert(NearV3(Spl::EvaluateSplineByDistance(path, table, 15.0f), glm::vec3(10, 5, 0), 1e-3f));

        // distance clamp：负 → 首点、超总长 → 末点。
        assert(NearV3(Spl::EvaluateSplineByDistance(path, table, -3.0f), glm::vec3(0, 0, 0), 1e-3f));
        assert(NearV3(Spl::EvaluateSplineByDistance(path, table, 100.0f), glm::vec3(10, 10, 0), 1e-3f));

        std::printf("[ok] arc-length uniform\n");
    }

    // ---- 匀速 vs 参数非匀速（锁语义） ---------------------------------------

    void TestUniformVsParametric()
    {
        // 非对称段长：seg0 长 2、seg1 长 10，总长 12。参数 t 在两段各占 0.5 → 非匀速。
        Spl::SplinePath path;
        path.type   = Spl::SplineType::Linear;
        path.closed = false;
        path.points = {glm::vec3(0, 0, 0), glm::vec3(2, 0, 0), glm::vec3(12, 0, 0)};

        const Spl::SplineArcTable table = Spl::BuildSplineArcTable(path);
        assert(Near(Spl::SplineTotalLength(table), 12.0f, 1e-3f));

        // 参数 t=0.5 落在 seg1 起点（控制点）→ (2,0,0)（沿 t 非匀速）。
        const glm::vec3 byParam = Spl::EvaluateSpline(path, 0.5f);
        assert(NearV3(byParam, glm::vec3(2, 0, 0), 1e-3f));

        // 按弧长 total/2 = 6 → 真几何中点 (6,0,0)（沿弧长匀速）。
        const glm::vec3 byDist = Spl::EvaluateSplineByDistance(path, table, 6.0f);
        assert(NearV3(byDist, glm::vec3(6, 0, 0), 1e-3f));

        // 两者不同 → 锁死"匀速遍历 ≠ 参数遍历"语义。
        assert(!NearV3(byParam, byDist, 1e-2f));

        std::printf("[ok] uniform vs parametric\n");
    }

    // ---- 退化输入 ------------------------------------------------------------

    void TestDegenerate()
    {
        // 空 points → (0,0,0) 不崩。
        Spl::SplinePath empty;
        assert(NearV3(Spl::EvaluateSpline(empty, 0.0f), glm::vec3(0, 0, 0)));
        assert(NearV3(Spl::EvaluateSpline(empty, 0.7f), glm::vec3(0, 0, 0)));

        const Spl::SplineArcTable emptyTable = Spl::BuildSplineArcTable(empty);
        assert(Near(Spl::SplineTotalLength(emptyTable), 0.0f));
        assert(NearV3(Spl::EvaluateSplineByDistance(empty, emptyTable, 5.0f), glm::vec3(0, 0, 0)));

        // 单点 → 恒返回该点。
        Spl::SplinePath single;
        single.points = {glm::vec3(4, 5, 6)};
        assert(NearV3(Spl::EvaluateSpline(single, 0.0f), glm::vec3(4, 5, 6)));
        assert(NearV3(Spl::EvaluateSpline(single, 0.9f), glm::vec3(4, 5, 6)));

        const Spl::SplineArcTable singleTable = Spl::BuildSplineArcTable(single);
        assert(Near(Spl::SplineTotalLength(singleTable), 0.0f));
        assert(NearV3(Spl::EvaluateSplineByDistance(single, singleTable, 3.0f), glm::vec3(4, 5, 6)));

        // SplineComponent 纯数据可携带一条样条。
        Spl::SplineComponent comp;
        comp.path.points = {glm::vec3(0, 0, 0), glm::vec3(1, 1, 1)};
        assert(comp.path.points.size() == 2);

        std::printf("[ok] degenerate inputs\n");
    }

} // namespace

int main()
{
    TestLinearEvaluate();
    TestCatmullRomPassesControlPoints();
    TestClosedWrap();
    TestTangent();
    TestArcLengthUniform();
    TestUniformVsParametric();
    TestDegenerate();

    std::printf("SplineTest: all passed\n");
    return 0;
}
