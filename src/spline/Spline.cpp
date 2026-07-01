#include <orange/engine/spline/Spline.h>

#include <glm/geometric.hpp>  // glm::length / glm::normalize / glm::distance
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Orange::Engine::Spline
{

namespace
{

// 单位切线判退化的长度阈值（低于此视为零向量，避免 normalize 除零 NaN）。
constexpr float kTangentEps = 1e-6f;

// 有限差分步长（求切线用）。取够小以逼近导数、又够大以避开浮点噪声。
constexpr float kTangentH = 1e-3f;

int ClampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// 取第 i 个控制点：闭合样条 index 环绕（取模，负 index 也归一到 [0,n)）；开放
// 样条 index clamp 到端点（这样 Catmull-Rom 端段的 p0 / p3 有合理的虚拟延拓）。
glm::vec3 GetPoint(const SplinePath& spline, int i)
{
    const int n = static_cast<int>(spline.points.size());
    if (spline.closed)
    {
        // C++ 的 % 对负数结果可能为负，((i%n)+n)%n 归一到 [0,n)。
        return spline.points[static_cast<std::size_t>(((i % n) + n) % n)];
    }
    return spline.points[static_cast<std::size_t>(ClampInt(i, 0, n - 1))];
}

}  // namespace

glm::vec3 EvaluateSpline(const SplinePath& spline, float t)
{
    const int n = static_cast<int>(spline.points.size());
    if (n < 1)
    {
        return glm::vec3(0.0f);
    }
    if (n == 1)
    {
        return spline.points[0];
    }

    // 段数：闭合首尾相连多一段回到起点，开放为 n-1 段。
    const int numSegments = spline.closed ? n : (n - 1);

    // 参数归一：闭合环绕到 [0,1)（t - floor(t)），开放 clamp 到 [0,1]。
    float tt = spline.closed ? (t - std::floor(t)) : std::clamp(t, 0.0f, 1.0f);

    const float ft  = tt * static_cast<float>(numSegments);
    const int   seg = ClampInt(static_cast<int>(std::floor(ft)), 0, numSegments - 1);
    const float s   = ft - static_cast<float>(seg);

    if (spline.type == SplineType::Linear)
    {
        return glm::mix(GetPoint(spline, seg), GetPoint(spline, seg + 1), s);
    }

    // CatmullRom（uniform，过控制点）：s=0→p1、s=1→p2。
    const glm::vec3 p0 = GetPoint(spline, seg - 1);
    const glm::vec3 p1 = GetPoint(spline, seg);
    const glm::vec3 p2 = GetPoint(spline, seg + 1);
    const glm::vec3 p3 = GetPoint(spline, seg + 2);

    const float s2 = s * s;
    const float s3 = s2 * s;
    return 0.5f
           * (2.0f * p1 + (p2 - p0) * s + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * s2
              + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * s3);
}

glm::vec3 EvaluateSplineTangent(const SplinePath& spline, float t)
{
    const float h = kTangentH;

    // 与 EvaluateSpline 一致地处理 t 越界：开放 clamp 到 [0,1]、闭合环绕——否则越界 t 的
    // 两个差分采样都被 EvaluateSpline clamp 到同一端点 → 退化零切线（而非端点切线），令
    // 下游 lookAt(pos+tangent) 得退化 / NaN 朝向。
    const float tc = spline.closed ? (t - std::floor(t)) : std::clamp(t, 0.0f, 1.0f);

    glm::vec3 d;
    if (spline.closed)
    {
        // 闭合环绕，边界处中心差分照样合法（tc±h 越界由 EvaluateSpline 环绕吸收）。
        d = EvaluateSpline(spline, tc + h) - EvaluateSpline(spline, tc - h);
    }
    else if (tc < h)
    {
        // 近起点：单侧前向差分（避免 tc-h clamp 后与 tc 同点导致零切线）。
        d = EvaluateSpline(spline, tc + h) - EvaluateSpline(spline, tc);
    }
    else if (tc > 1.0f - h)
    {
        // 近终点：单侧后向差分。
        d = EvaluateSpline(spline, tc) - EvaluateSpline(spline, tc - h);
    }
    else
    {
        // 中间：中心差分。
        d = EvaluateSpline(spline, tc + h) - EvaluateSpline(spline, tc - h);
    }

    // 退化（空 / 单点 / 重合控制点）→ 差分近零 → 返回零向量而非 NaN。
    if (glm::length(d) < kTangentEps)
    {
        return glm::vec3(0.0f);
    }
    return glm::normalize(d);
}

SplineArcTable BuildSplineArcTable(const SplinePath& spline, int samplesPerSegment)
{
    SplineArcTable table;

    const int n = static_cast<int>(spline.points.size());
    if (n < 2)
    {
        // 空 / 单点：单项弧长表 {0} / {0}（总长 0，ByDistance 恒返回该点）。
        table.ts.push_back(0.0f);
        table.cumulative.push_back(0.0f);
        return table;
    }

    const int numSegments = std::max(1, spline.closed ? n : (n - 1));
    const int spp         = std::max(1, samplesPerSegment);
    // 64 位算乘积 + 上界 clamp：防 numSegments*spp 有符号 int 溢出（回绕为负 → 下面
    // reserve(size_t) 转成巨值抛 bad_alloc）。4M 采样远超任何现实样条所需。
    constexpr long long kMaxSamples = 4000000LL;
    const long long     sc64        = 1LL * numSegments * spp + 1LL;
    const int           sampleCount = static_cast<int>(std::min(sc64, kMaxSamples));

    table.ts.reserve(static_cast<std::size_t>(sampleCount));
    table.cumulative.reserve(static_cast<std::size_t>(sampleCount));

    glm::vec3 prev     = EvaluateSpline(spline, 0.0f);
    float     accumLen = 0.0f;
    table.ts.push_back(0.0f);
    table.cumulative.push_back(0.0f);

    for (int i = 1; i < sampleCount; ++i)
    {
        const float     t   = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
        const glm::vec3 cur = EvaluateSpline(spline, t);
        accumLen += glm::distance(prev, cur);
        table.ts.push_back(t);
        table.cumulative.push_back(accumLen);
        prev = cur;
    }

    return table;
}

float SplineTotalLength(const SplineArcTable& table)
{
    return table.cumulative.empty() ? 0.0f : table.cumulative.back();
}

glm::vec3 EvaluateSplineByDistance(const SplinePath& spline, const SplineArcTable& table,
                                   float distance)
{
    // SplineArcTable 是公共 struct（ts / cumulative 可被外部直接写）。以两者较短者为
    // 尺寸权威，防手工构造出不等长表时用 cumulative.size() 约束 hi 却越界索引 ts。
    const std::size_t sz = std::min(table.ts.size(), table.cumulative.size());
    if (sz == 0)
    {
        return EvaluateSpline(spline, 0.0f);
    }
    if (sz == 1)
    {
        return EvaluateSpline(spline, table.ts[0]);
    }

    const float total = table.cumulative[sz - 1];
    const float d     = std::clamp(distance, 0.0f, total);

    // 在（前 sz 个）升序 cumulative 里定位使 cumulative[i] ≤ d < cumulative[i+1] 的区间。
    // upper_bound 找首个 > d 的元素；其前一格即区间下界 i。
    auto        it = std::upper_bound(table.cumulative.begin(), table.cumulative.begin() + sz, d);
    std::size_t hi = static_cast<std::size_t>(it - table.cumulative.begin());
    if (hi == 0)
    {
        hi = 1;  // d ≤ cumulative[0]（首点），取第一个区间。
    }
    if (hi >= sz)
    {
        hi = sz - 1;  // d == total（末点），取最后一个区间。
    }
    const std::size_t lo = hi - 1;

    const float denom = table.cumulative[hi] - table.cumulative[lo];
    // 分母 >0 才插值；退化（零长度段）取 0，落在区间下界。
    const float frac = denom > 0.0f ? (d - table.cumulative[lo]) / denom : 0.0f;
    const float t    = glm::mix(table.ts[lo], table.ts[hi], frac);

    return EvaluateSpline(spline, t);
}

}  // namespace Orange::Engine::Spline
