#ifndef ORANGE_EDITOR_EDITOR_MATH_UTIL_H
#define ORANGE_EDITOR_EDITOR_MATH_UTIL_H

// EditorMathUtil —— 编辑器侧无依赖的小数学工具（header-only inline）。
// 抽出动机：gizmo snap（translate 网格 / rotate 角度 / scale 步进）共用量化，
// 集中可单测（editor_math_util_test）。纯 std。

#include <cmath>

namespace Orange::Editor::Util
{

// 把 value 量化到 step 的最近整数倍。step <= 0 → 原样返回（snap 关 / 非法步长）。
// gizmo grid/angle/scale snap 共用：translate 传 grid 米数、rotate 传角度步、
// scale 传比例步。
inline float SnapToStep(float value, float step)
{
    if (step <= 0.0f) { return value; }
    return std::round(value / step) * step;
}

}  // namespace Orange::Editor::Util

#endif  // ORANGE_EDITOR_EDITOR_MATH_UTIL_H
