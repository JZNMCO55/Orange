#ifndef ORANGE_ENGINE_SPLINE_SPLINE_H
#define ORANGE_ENGINE_SPLINE_SPLINE_H

// ---------------------------------------------------------------------------
// Spline —— 样条 / 曲线子系统：Catmull-Rom / Linear 曲线 + 沿线求值 + 切线
// (tangent) + 弧长参数化 (arc-length parameterize) 匀速遍历。
//
// 通用引擎能力：相机轨道 / 运动路径 / 程序化摆位。参数 t∈[0,1] 跨整条样条，
// 但沿曲线的速度不均匀（等 t 间隔对应的弧长不等）；要匀速遍历须先 BuildSpline-
// ArcTable 建弧长表，再用 EvaluateSplineByDistance 按弧长距离求点。
//
// 公共头只依赖 glm + 标准库，不引 glm/gtx（gtx 的 catmullRom 需要
// GLM_ENABLE_EXPERIMENTAL，会污染消费者）；Catmull-Rom 用闭式公式在 .cpp 自实现。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <glm/vec3.hpp>

#include <cstddef>
#include <vector>

namespace Orange::Engine::Spline
{

// 插值类型：Linear=折线（过控制点、C0 连续）；CatmullRom=过控制点的光滑曲线（C1 连续）。
enum class SplineType
{
    Linear,
    CatmullRom
};

// 一条样条：有序控制点 + 类型 + 是否闭合（closed 首尾相连成环）。
struct SplinePath
{
    std::vector<glm::vec3> points;
    SplineType             type{SplineType::CatmullRom};
    bool                   closed{false};
};

// ECS 组件：实体携带一条样条（相机轨道 / 巡逻路径等）。纯数据，v1 不序列化。
struct SplineComponent
{
    SplinePath path;
};

// 弧长参数化表（arc-length table）：把 [0,1] 参数域采样出 (t, 累计弧长) 对，供
// EvaluateSplineByDistance 做匀速遍历（参数 t 沿曲线速度不均匀，按弧长走才匀速）。
struct SplineArcTable
{
    std::vector<float> ts;          // 升序采样参数 [0,1]
    std::vector<float> cumulative;  // 对应累计弧长（ts[0] 处 =0）
};

// 在参数 t∈[0,1]（跨整条样条）求点。开放样条 t clamp 到 [0,1]；闭合样条 t 环绕。
// points 空→(0,0,0)；单点→该点。
ORANGE_ENGINE_API glm::vec3 EvaluateSpline(const SplinePath& spline, float t);

// 在 t 处的单位切线（前进方向，用于沿线朝向）。有限差分实现；退化→(0,0,0)。
ORANGE_ENGINE_API glm::vec3 EvaluateSplineTangent(const SplinePath& spline, float t);

// 采样构建弧长表（samplesPerSegment 每段采样数，默认 16）。用于匀速遍历。
ORANGE_ENGINE_API SplineArcTable BuildSplineArcTable(const SplinePath& spline,
                                                     int               samplesPerSegment = 16);

// 样条总弧长（= 弧长表末项）。
ORANGE_ENGINE_API float SplineTotalLength(const SplineArcTable& table);

// 按弧长距离 distance∈[0, total] 求点（匀速遍历）：在弧长表里定位 distance 对应的 t
// 后 EvaluateSpline。distance clamp 到 [0,total]。
ORANGE_ENGINE_API glm::vec3 EvaluateSplineByDistance(const SplinePath&     spline,
                                                     const SplineArcTable& table, float distance);

}  // namespace Orange::Engine::Spline

#endif  // ORANGE_ENGINE_SPLINE_SPLINE_H
