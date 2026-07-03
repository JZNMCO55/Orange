#ifndef ORANGE_EDITOR_IMPORT_MESH_TANGENT_GEN_H
#define ORANGE_EDITOR_IMPORT_MESH_TANGENT_GEN_H

// MeshTangentGen —— 用 MikkTSpace（Morten S. Mikkelsen 标准切线空间算法，
// vendor/mikktspace，zlib 许可）为导入网格生成高质量 per-vertex tangent。
//
// 为什么用 MikkTSpace 而非引擎自带的 Lengyel fallback
// （MeshAsset::ComputeTangentsFromTriangles）：MikkTSpace 是法线贴图烘焙工具
// （Substance / Blender / xNormal / Marmoset）事实标准——它对网格做内部
// welding + order-independent 评估，保证与烘焙器算出的切线一致，从而消除法线
// 贴图在接缝 / 镜像 UV 处的光照错位。Lengyel 按三角面积加权累加，接缝处会有
// 可见误差。A2（DCC import v1.2）的 mikktspace 切线交付即此。
//
// 归属：importer-only。MikkTSpace vendored 仅暴露给 OrangeEditor target，引擎
// runtime 不消费（ADR-008 invariant "importer 全部在 tools/OrangeEditor/
// import/，不污染 src/asset/"）。引擎侧只保留 Lengyel 作为 Load 端兜底。

#include <orange/engine/asset/MeshAsset.h>

#include <cstdint>
#include <vector>

namespace Orange::Editor::Import
{

    // 在索引三角网格上跑 MikkTSpace 生成切线。
    //
    // 契约（见 vendor/mikktspace/mikktspace.h 行 86-101）：MikkTSpace 输出
    // per-face-vertex、**未索引**，且明确"禁止写回已有 index list，否则
    // averaging/overwriting 会产生错误结果"。故本函数：de-index 读入喂 MikkTSpace
    // → 收 per-face-vertex 切线 → 按 (原顶点, 切线) 重新焊接成索引网格。
    // positions / uvs / normals / indices 全部被改写（顶点数可能因 UV / 法线缝隙
    // 处切线分裂而增加），outTangents 与改写后的 positions 等长、一一对应。
    //
    // 前置：uvs 与 normals 均与 positions 等长（非空）、indices 非空且为 3 的倍数。
    // 不满足 → 返回 false 且五个数组原样不动（caller 落到引擎 Load 端 Lengyel
    // fallback：MeshAsset::ComputeTangentsFromTriangles）。MikkTSpace 自身失败
    // （genTangSpaceDefault 返回 0，理论上仅内存分配失败）同样返回 false。
    bool GenerateMikkTSpaceTangents(
        std::vector<Orange::Engine::Asset::VertexPosition3>& positions,
        std::vector<Orange::Engine::Asset::VertexUV2>&       uvs,
        std::vector<Orange::Engine::Asset::VertexNormal3>&   normals,
        std::vector<std::uint32_t>&                          indices,
        std::vector<Orange::Engine::Asset::VertexTangent4>&  outTangents);

} // namespace Orange::Editor::Import

#endif // ORANGE_EDITOR_IMPORT_MESH_TANGENT_GEN_H
