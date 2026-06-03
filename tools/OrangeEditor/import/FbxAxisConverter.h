#ifndef ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_FBX_AXIS_CONVERTER_H
#define ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_FBX_AXIS_CONVERTER_H

// ---------------------------------------------------------------------------
// FbxAxisConverter —— FBX 坐标系 → 引擎坐标系的换轴器（FbxImporter 单 mesh 路径
// + FbxSceneImporter scene 路径共用）。把"FBX up-axis / unit scale → 引擎
// Y-up / 米"的换轴矩阵 + 缩放集中一处，保证两条导入路径的顶点 / 法线换轴一致，
// 也是 scene importer 对 node local transform 做 R·M·R⁻¹ 共轭时取 R 的同一来源。
//
// 引擎约定：右手 Y-up、单位米（与 glTF 一致）。FBX 文件可能是 Z-up（Blender /
// 3ds Max 默认）或 Y-up（Maya）。
//
//   * up-axis：FBX GlobalSettings.UpAxis==Z 时需把 Z-up 旋到 Y-up。这是**未被
//     烘进顶点的**元数据 —— Blender 导出 Z-up FBX 时顶点确实是 Z-up，靠本旋转
//     转正。标准换轴（保持右手系、不引入镜像）：(x,y,z)_fbx → (x,z,-y)_engine。
//     Y-up / UNKNOWN 文件直接透传。
//   * unit scale：positions 默认**信任已烘单位**（unitScale=1）。主流导出器
//     （尤其 Blender，apply_unit_scale=True）会把单位烘进顶点 → 顶点已是米，
//     而 UnitScaleFactor 仍写 1.0。直接按 /100 折算会把已是米的几何缩成 1/100。
//     故取"信任烘好的米"，不做单位折算（真正未烘的 cm 文件偏大 100× 是已知限制）。
//
// 换轴用一个 3x3 旋转 + 标量缩放表达；位置走 (rot * pos) * unitScale，法线走
// normalize(rot * normal)。本头不引 ofbx.h —— MakeAxisConverter 接 GlobalSettings
// 的实现在 .cpp（取 ofbx 声明那一侧），头里只暴露纯数据 + 顶点 / 法线 / 矩阵共轭 API。
// ---------------------------------------------------------------------------

#include <orange/engine/asset/MeshAsset.h>  // VertexPosition3 / VertexNormal3

#include <glm/mat4x4.hpp>

#include <cmath>

namespace ofbx
{
struct GlobalSettings;
}

namespace Orange::Editor::Import
{

struct AxisConverter
{
    // 旋转矩阵按行存（rowX / rowY / rowZ 是输出各分量对输入的线性组合）。
    float rowX[3]{1.0f, 0.0f, 0.0f};
    float rowY[3]{0.0f, 1.0f, 0.0f};
    float rowZ[3]{0.0f, 0.0f, 1.0f};
    float unitScale{1.0f};  // FBX 单位 → 米

    ::Orange::Engine::Asset::VertexPosition3 Position(double x, double y,
                                                      double z) const
    {
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float fz = static_cast<float>(z);
        ::Orange::Engine::Asset::VertexPosition3 p;
        p.x = (rowX[0] * fx + rowX[1] * fy + rowX[2] * fz) * unitScale;
        p.y = (rowY[0] * fx + rowY[1] * fy + rowY[2] * fz) * unitScale;
        p.z = (rowZ[0] * fx + rowZ[1] * fy + rowZ[2] * fz) * unitScale;
        return p;
    }

    ::Orange::Engine::Asset::VertexNormal3 Normal(double x, double y,
                                                  double z) const
    {
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float fz = static_cast<float>(z);
        ::Orange::Engine::Asset::VertexNormal3 n;
        n.x = rowX[0] * fx + rowX[1] * fy + rowX[2] * fz;
        n.y = rowY[0] * fx + rowY[1] * fy + rowY[2] * fz;
        n.z = rowZ[0] * fx + rowZ[1] * fy + rowZ[2] * fz;
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 1e-8f)
        {
            n.x /= len;
            n.y /= len;
            n.z /= len;
        }
        return n;
    }

    // 换轴的 3x3 旋转 R（齐次 4x4，平移 0）。scene importer 对 node local
    // transform 做基变换共轭 R·M·R⁻¹ 时取它（不含 unitScale —— 共轭是纯旋转的
    // 相似变换，平移分量另行按 R 旋转 + unitScale 缩放，见 ConjugateNodeLocal）。
    glm::mat4 RotationMat4() const
    {
        glm::mat4 r(1.0f);
        // glm 列主序：r[col][row]。R 的第 row 行 = rowX/rowY/rowZ。
        r[0][0] = rowX[0]; r[1][0] = rowX[1]; r[2][0] = rowX[2];
        r[0][1] = rowY[0]; r[1][1] = rowY[1]; r[2][1] = rowY[2];
        r[0][2] = rowZ[0]; r[1][2] = rowZ[1]; r[2][2] = rowZ[2];
        return r;
    }
};

// 从 GlobalSettings 构造换轴器。UpAxis 决定换轴矩阵；单位 MVP 信任已烘（=1）。
// 实现在 FbxAxisConverter.cpp（取 ofbx 声明那一侧）。
AxisConverter MakeAxisConverter(const ofbx::GlobalSettings* settings);

}  // namespace Orange::Editor::Import

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_FBX_AXIS_CONVERTER_H
