// CoplanarDetector —— 选中实体的"是否与邻居 mesh 共面"后台检测。
//
// 触发场景：作者在场景里手动摆贴地 / 贴墙物体，容易让 cube 底面 y == ground
// 顶面 y 这种完全共面，主 pass `depthCompareOp = LessOrEqual` 立刻出现 z-fight
// 斜条纹；编辑器原本没有任何检测，作者必须肉眼对照纹理瑕疵反推。
//
// 本检测：选中 entity 时，遍历"周围 R 米内有 Renderable + Transform 的其他
// entity"，比对它们 world AABB 的 6 个面 → 与本 entity 6 个面 ε-equal 命中即
// 记录一条 hit。Inspector 顶部显示红字提示。
//
// 不在范围：G2 snap-to-ground 默认 ε 偏移 / G3 scene save 时 lint —— 留后续
// session（详见 docs/engine-known-gaps.md GAP-2026-05-21-editor-coplanar-mesh-
// z-fight-prevention）。
//
// 设计要点：
//   * 复用 EditorPicking 同款 mesh local AABB + worldMatrix(T*R*S) + 8 角点
//     变换取新 min/max（rotated 后仍是 axis-aligned 包围，粗但够用）
//   * 距离阈值 R 默认 10m，避免大场景 N² 爆炸
//   * ε 默认 0.001m（1mm）—— 与 GAP G2 拟用 ε 一致
//   * **不缓存**：本函数只在 selection 变 / Transform 改时被 InspectorPanel
//     调用一次/帧；对几十~几百 entity 的常规编辑场景 N²·8 角点变换的成本
//     在 < 1ms 量级，缓存属过度优化

#ifndef ORANGE_EDITOR_COPLANAR_DETECTOR_H
#define ORANGE_EDITOR_COPLANAR_DETECTOR_H

#include "EditorHost.h"

#include <orange/engine/scene/Entity.h>

#include <string>
#include <vector>

namespace Orange::Editor::Coplanar
{

    // AABB 6 个面的语义命名 —— 与 +X / -X / +Y / -Y / +Z / -Z 对应。
    enum class Face
    {
        Right,  // +X
        Left,   // -X
        Top,    // +Y
        Bottom, // -Y
        Front,  // +Z
        Back,   // -Z
    };

    // 单条共面命中：自己的哪一面 / 与谁 / 对方的哪一面 / 两面在该轴上的距离（绝对值）。
    struct Hit
    {
        Face                   selfFace;
        Orange::Engine::Entity otherEntity;
        std::string            otherName;
        Face                   otherFace;
        float                  gap; // |self_face_coord - other_face_coord|；近 0 时即共面
    };

    const char* FaceName(Face f) noexcept;

    // 选中 entity 在世界中查找邻居 mesh 的共面命中。
    //
    // 入参：
    //   * host       —— 拿 World + AssetRegistry（取 mesh 顶点算 local AABB）
    //   * selected   —— 待检测的 entity；必须 valid + 挂 Transform + Renderable
    //   * eps        —— 两面坐标差 |a - b| <= eps 视为共面，默认 1mm
    //   * maxDist    —— 只查 selected entity 周围这么多米内的 entity，默认 10m
    //
    // 返回空 vector 表示无共面（无警告需显示）。
    std::vector<Hit>
    DetectCoplanar(EditorHost&            host,
                   Orange::Engine::Entity selected,
                   float                  eps     = 0.001f,
                   float                  maxDist = 10.0f);

} // namespace Orange::Editor::Coplanar

#endif // ORANGE_EDITOR_COPLANAR_DETECTOR_H
