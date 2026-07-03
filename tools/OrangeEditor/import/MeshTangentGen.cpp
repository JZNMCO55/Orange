// MeshTangentGen 实现 —— 见 MeshTangentGen.h 头注释。

#include "MeshTangentGen.h"

#include "mikktspace.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace Orange::Editor::Import
{
    namespace
    {

        using Orange::Engine::Asset::VertexNormal3;
        using Orange::Engine::Asset::VertexPosition3;
        using Orange::Engine::Asset::VertexTangent4;
        using Orange::Engine::Asset::VertexUV2;

        // MikkTSpace 回调共享的客户端数据。读取走 index（合法——契约只禁止"写回"
        // 用已有 index）；写入收到 per-face-vertex 切线存进 faceVertTangents（与
        // indices 等长，face f 的 vert v 在槽 f*3+v）。
        struct MikktUserData
        {
            const std::vector<VertexPosition3>* positions = nullptr;
            const std::vector<VertexUV2>*       uvs       = nullptr;
            const std::vector<VertexNormal3>*   normals   = nullptr;
            const std::vector<std::uint32_t>*   indices   = nullptr;
            std::vector<VertexTangent4>         faceVertTangents;
        };

        std::uint32_t FaceVert(const MikktUserData* ud, int iFace, int iVert) noexcept
        {
            return (*ud->indices)[static_cast<std::size_t>(iFace) * 3u + static_cast<std::size_t>(iVert)];
        }

        int GetNumFaces(const SMikkTSpaceContext* ctx)
        {
            const auto* ud = static_cast<const MikktUserData*>(ctx->m_pUserData);
            return static_cast<int>(ud->indices->size() / 3u);
        }

        int GetNumVerticesOfFace(const SMikkTSpaceContext*, const int)
        {
            return 3;
        }

        void GetPosition(const SMikkTSpaceContext* ctx, float out[], const int iFace, const int iVert)
        {
            const auto*            ud = static_cast<const MikktUserData*>(ctx->m_pUserData);
            const VertexPosition3& p  = (*ud->positions)[FaceVert(ud, iFace, iVert)];
            out[0]                    = p.x;
            out[1]                    = p.y;
            out[2]                    = p.z;
        }

        void GetNormal(const SMikkTSpaceContext* ctx, float out[], const int iFace, const int iVert)
        {
            const auto*          ud = static_cast<const MikktUserData*>(ctx->m_pUserData);
            const VertexNormal3& n  = (*ud->normals)[FaceVert(ud, iFace, iVert)];
            out[0]                  = n.x;
            out[1]                  = n.y;
            out[2]                  = n.z;
        }

        void GetTexCoord(const SMikkTSpaceContext* ctx, float out[], const int iFace, const int iVert)
        {
            const auto*      ud = static_cast<const MikktUserData*>(ctx->m_pUserData);
            const VertexUV2& uv = (*ud->uvs)[FaceVert(ud, iFace, iVert)];
            out[0]              = uv.u;
            out[1]              = uv.v;
        }

        void SetTSpaceBasic(const SMikkTSpaceContext* ctx, const float tan[], const float sign,
                            const int iFace, const int iVert)
        {
            auto* ud = static_cast<MikktUserData*>(ctx->m_pUserData);
            ud->faceVertTangents[static_cast<std::size_t>(iFace) * 3u + static_cast<std::size_t>(iVert)] =
                VertexTangent4{tan[0], tan[1], tan[2], sign};
        }

        // 两个切线是否"同一个"（焊接判据）：手性符号一致 + 方向夹角极小。
        // 0.9999 ≈ cos(0.81°)；同 origIdx 的 face-vert 在 MikkTSpace 内部已被 weld
        // 成同一顶点 → 切线本就相同，缝隙处才会不同 → 阈值只需排除真正分裂。
        bool TangentApproxEqual(const VertexTangent4& a, const VertexTangent4& b) noexcept
        {
            if ((a.w < 0.0f) != (b.w < 0.0f))
            {
                return false;
            }
            const float d = a.x * b.x + a.y * b.y + a.z * b.z;
            return d > 0.9999f;
        }

    } // namespace

    bool GenerateMikkTSpaceTangents(
        std::vector<VertexPosition3>& positions,
        std::vector<VertexUV2>&       uvs,
        std::vector<VertexNormal3>&   normals,
        std::vector<std::uint32_t>&   indices,
        std::vector<VertexTangent4>&  outTangents)
    {
        if (positions.empty() || indices.empty() || (indices.size() % 3u) != 0u)
        {
            return false;
        }
        if (uvs.size() != positions.size() || normals.size() != positions.size())
        {
            return false;
        }

        MikktUserData ud;
        ud.positions = &positions;
        ud.uvs       = &uvs;
        ud.normals   = &normals;
        ud.indices   = &indices;
        ud.faceVertTangents.assign(indices.size(), VertexTangent4{1.0f, 0.0f, 0.0f, 1.0f});

        SMikkTSpaceInterface iface{};
        iface.m_getNumFaces          = &GetNumFaces;
        iface.m_getNumVerticesOfFace = &GetNumVerticesOfFace;
        iface.m_getPosition          = &GetPosition;
        iface.m_getNormal            = &GetNormal;
        iface.m_getTexCoord          = &GetTexCoord;
        iface.m_setTSpaceBasic       = &SetTSpaceBasic;
        iface.m_setTSpace            = nullptr;

        SMikkTSpaceContext ctx{};
        ctx.m_pInterface = &iface;
        ctx.m_pUserData  = &ud;

        if (genTangSpaceDefault(&ctx) == 0)
        {
            return false;
        }

        // ---- re-weld：per-face-vertex 切线 → 索引网格 -----------------------
        // 每个 face-vertex 引用原顶点 origIdx + MikkTSpace 切线 t；同 origIdx 但
        // 切线不同（UV / 法线缝、镜像 UV）→ 分裂出新顶点。pos/uv/normal 对同一
        // origIdx 恒等，故 key 只需 (origIdx, 切线方向+符号)。
        std::vector<VertexPosition3> newPos;
        std::vector<VertexUV2>       newUv;
        std::vector<VertexNormal3>   newNrm;
        std::vector<VertexTangent4>  newTan;
        std::vector<std::uint32_t>   newIdx;
        newPos.reserve(positions.size());
        newUv.reserve(positions.size());
        newNrm.reserve(positions.size());
        newTan.reserve(positions.size());
        newIdx.reserve(indices.size());

        // 每个原顶点 → 已建新顶点列表 (tangent, newIndex)。绝大多数 origIdx 只
        // 对应 1 个（偶尔 2~3 个）切线，线性查找成本可忽略，免去浮点 hash。
        std::vector<std::vector<std::pair<VertexTangent4, std::uint32_t>>>
            perOrig(positions.size());

        constexpr std::uint32_t kNone = 0xFFFFFFFFu;
        for (std::size_t fv = 0; fv < indices.size(); ++fv)
        {
            const std::uint32_t  origIdx = indices[fv];
            const VertexTangent4 t       = ud.faceVertTangents[fv];

            std::uint32_t mapped = kNone;
            for (const auto& cand : perOrig[origIdx])
            {
                if (TangentApproxEqual(cand.first, t))
                {
                    mapped = cand.second;
                    break;
                }
            }
            if (mapped == kNone)
            {
                mapped = static_cast<std::uint32_t>(newPos.size());
                newPos.push_back(positions[origIdx]);
                newUv.push_back(uvs[origIdx]);
                newNrm.push_back(normals[origIdx]);
                newTan.push_back(t);
                perOrig[origIdx].emplace_back(t, mapped);
            }
            newIdx.push_back(mapped);
        }

        positions   = std::move(newPos);
        uvs         = std::move(newUv);
        normals     = std::move(newNrm);
        indices     = std::move(newIdx);
        outTangents = std::move(newTan);
        return true;
    }

} // namespace Orange::Editor::Import
