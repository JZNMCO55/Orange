// EditorGroupTransform 单元测试 —— 多选群组变换 pivot 数学基元。
//
// 覆盖（gap 报告 §2.1/P1 "多选群组变换 pivot：数学可单测"——交互手感留
// dogfood，本测试只锁底层确定性数学）：
//   * ComputeCentroid：空 / 单点 / 对称集
//   * RotateAroundPivot：identity / 点在 pivot / 绕 Z 90° / 非原点 pivot
//   * ScaleAroundPivot：factor 1 / 点在 pivot / 均匀放大 / 各轴不同

#include "EditorGroupTransform.h"

#include <glm/gtc/constants.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

using Orange::Editor::Util::ComputeCentroid;
using Orange::Editor::Util::RotateAroundPivot;
using Orange::Editor::Util::ScaleAroundPivot;

namespace
{

constexpr float kEps = 1e-5f;

bool ApproxEq(const glm::vec3& a, const glm::vec3& b)
{
    return std::fabs(a.x - b.x) < kEps
        && std::fabs(a.y - b.y) < kEps
        && std::fabs(a.z - b.z) < kEps;
}

void TestComputeCentroid()
{
    // 空 / nullptr → 原点（安全兜底，不 NaN）。
    assert(ApproxEq(ComputeCentroid(nullptr, 0), glm::vec3(0.0f)));
    glm::vec3 dummy(5.0f);
    assert(ApproxEq(ComputeCentroid(&dummy, 0), glm::vec3(0.0f)));

    // 单点 → 该点本身。
    glm::vec3 one(2.0f, -3.0f, 4.0f);
    assert(ApproxEq(ComputeCentroid(&one, 1), one));

    // 对称四点 → 几何中心。
    const glm::vec3 quad[4] = {
        {0.0f, 0.0f, 0.0f},
        {2.0f, 0.0f, 0.0f},
        {2.0f, 2.0f, 0.0f},
        {0.0f, 2.0f, 0.0f},
    };
    assert(ApproxEq(ComputeCentroid(quad, 4), glm::vec3(1.0f, 1.0f, 0.0f)));

    std::fprintf(stdout, "  [PASS] ComputeCentroid\n");
}

void TestRotateAroundPivot()
{
    const glm::quat ident = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 p(3.0f, 1.0f, -2.0f);

    // identity → 原样。
    assert(ApproxEq(RotateAroundPivot(p, glm::vec3(0.0f), ident), p));

    // 点在 pivot 上 → 旋转不动。
    const glm::quat q90z = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
    assert(ApproxEq(RotateAroundPivot(p, p, q90z), p));

    // (1,0,0) 绕原点 Z 转 +90° → (0,1,0)。
    assert(ApproxEq(RotateAroundPivot(glm::vec3(1, 0, 0), glm::vec3(0.0f), q90z),
                    glm::vec3(0, 1, 0)));

    // 非原点 pivot：(3,0,0) 绕 pivot (2,0,0) 转 90°Z → (2,1,0)。
    assert(ApproxEq(RotateAroundPivot(glm::vec3(3, 0, 0), glm::vec3(2, 0, 0), q90z),
                    glm::vec3(2, 1, 0)));

    std::fprintf(stdout, "  [PASS] RotateAroundPivot\n");
}

void TestScaleAroundPivot()
{
    const glm::vec3 p(4.0f, 2.0f, 6.0f);

    // factor 1 → 原样。
    assert(ApproxEq(ScaleAroundPivot(p, glm::vec3(0.0f), glm::vec3(1.0f)), p));

    // 点在 pivot 上 → 缩放不动。
    assert(ApproxEq(ScaleAroundPivot(p, p, glm::vec3(3.0f)), p));

    // 均匀放大 2×，pivot 原点：距离翻倍。
    assert(ApproxEq(ScaleAroundPivot(glm::vec3(1, 1, 1), glm::vec3(0.0f), glm::vec3(2.0f)),
                    glm::vec3(2, 2, 2)));

    // 非原点 pivot：(3,0,0) 绕 pivot (1,0,0) 放大 2× → 1 + 2*(3-1) = 5。
    assert(ApproxEq(ScaleAroundPivot(glm::vec3(3, 0, 0), glm::vec3(1, 0, 0), glm::vec3(2.0f)),
                    glm::vec3(5, 0, 0)));

    // 各轴不同 factor：(2,2,2) 绕原点 (2,0.5,1) → (4,1,2)。
    assert(ApproxEq(ScaleAroundPivot(glm::vec3(2, 2, 2), glm::vec3(0.0f), glm::vec3(2.0f, 0.5f, 1.0f)),
                    glm::vec3(4, 1, 2)));

    std::fprintf(stdout, "  [PASS] ScaleAroundPivot\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[EditorGroupTransformTest] running\n");
    TestComputeCentroid();
    TestRotateAroundPivot();
    TestScaleAroundPivot();
    std::fprintf(stdout, "[EditorGroupTransformTest] all tests passed.\n");
    return 0;
}
