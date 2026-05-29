// EditorMathUtil::SnapToStep 单测（gizmo snap 量化核心，gap 报告 §2.1 "量化函数
// 纯逻辑可单测"）。纯 std header，零 ImGui/引擎依赖。

#include "EditorMathUtil.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using Orange::Editor::Util::SnapToStep;

namespace
{
bool Eq(float a, float b) { return std::fabs(a - b) < 1e-5f; }
}

int main()
{
    // step <= 0 → 原样返回（snap 关 / 非法步长）。
    assert(Eq(SnapToStep(3.7f, 0.0f), 3.7f));
    assert(Eq(SnapToStep(3.7f, -1.0f), 3.7f));

    // 量化到最近整数倍（std::round 半数远离零）。
    assert(Eq(SnapToStep(0.0f, 0.5f), 0.0f));
    assert(Eq(SnapToStep(0.24f, 0.5f), 0.0f));   // round(0.48)=0
    assert(Eq(SnapToStep(0.26f, 0.5f), 0.5f));   // round(0.52)=1
    assert(Eq(SnapToStep(0.75f, 0.5f), 1.0f));   // round(1.5)=2（半数远离零）
    assert(Eq(SnapToStep(-0.26f, 0.5f), -0.5f)); // 负值对称

    // 不同步长（scale 步 0.25 / angle 步 15）。
    assert(Eq(SnapToStep(1.2f, 0.25f), 1.25f));  // round(4.8)=5
    assert(Eq(SnapToStep(1.1f, 0.25f), 1.0f));   // round(4.4)=4
    assert(Eq(SnapToStep(22.0f, 15.0f), 15.0f)); // round(1.467)=1
    assert(Eq(SnapToStep(23.0f, 15.0f), 30.0f)); // round(1.533)=2

    std::printf("editor_math_util_test: all assertions passed\n");
    return 0;
}
