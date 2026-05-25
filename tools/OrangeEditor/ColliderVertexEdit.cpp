#include "ColliderVertexEdit.h"

#include "EditorCameraControl.h"
#include "EditorGizmoMath.h"
#include "EditorHost.h"
#include "command/SetFieldValueCommand.h"

#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <imgui.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>

namespace Orange::Editor
{
namespace
{

namespace GM = OrangeEditor::Internal::GizmoMath;

using Orange::Engine::Entity;
using Orange::Engine::Physics::ColliderComponent;
using Orange::Engine::Physics::EdgeChainDesc;
using Orange::Engine::Physics::PolygonDesc;
using TC = Orange::Engine::Scene::TransformComponent;

// handle 绘制 / 命中半径（屏幕像素）。命中容差略大于绘制半径，方便点中。
constexpr float kHandleRadiusPx = 5.0f;
constexpr float kHitRadiusPx    = 9.0f;

// 顶点 handle 配色（与 ColliderDebugDraw 琥珀 wireframe 同系；选中走青、
// hover 走白，三态可辨）。IM_COL32 是 RGBA。
const ImU32 kColNormal   = IM_COL32(255, 200, 64, 255);
const ImU32 kColHover    = IM_COL32(255, 255, 255, 255);
const ImU32 kColSelected = IM_COL32(64, 200, 255, 255);
const ImU32 kColOutline  = IM_COL32(0, 0, 0, 255);

// local.xy → world：与 ColliderDebugDraw.cpp::RotateLocalXY 一致 —— 经 entity
// quaternion 旋转后丢弃 z 分量、加 entity.position。保证 viewport wireframe 与
// 可拖拽 handle 像素级吻合（yaw-only collider 平面假设下精确）。
glm::vec3 LocalToWorld(const glm::quat& rot, const glm::vec3& origin, glm::vec2 local)
{
    const glm::vec3 r = rot * glm::vec3(local.x, local.y, 0.0f);
    return origin + glm::vec3(r.x, r.y, 0.0f);
}

// world → local.xy：LocalToWorld 的逆（yaw-only 下精确）。
glm::vec2 WorldToLocal(const glm::quat& rot, const glm::vec3& origin, const glm::vec3& world)
{
    const glm::vec3 l = glm::inverse(rot) * (world - origin);
    return glm::vec2(l.x, l.y);
}

// 处理一种顶点容器（Polygon / EdgeChain 共性，都有 vertices[] + count +
// kMaxVertices）。minVertices = 删点下限；closedLoop = 加点找最近边时是否把
// (count-1 → 0) 也算候选边（Polygon 恒闭环；EdgeChain 取 isLoop）。
//
// 返回 true 恒表示子模式 active（caller 据此跳过 gizmo + picking）。
template<typename DescT>
bool EditDesc(EditorHost& host, const TC& tc,
              glm::vec2 imageOrigin, glm::vec2 imageSize,
              const glm::mat4& viewProj, const glm::mat4& invViewProj,
              std::uint32_t minVertices, bool closedLoop)
{
    auto& cs        = host.colliderEdit;
    const Entity ent = cs.entity;

    auto* cc = host.scene.pWorld->GetComponent<ColliderComponent>(ent);
    if (cc == nullptr) { cs.Reset(); return false; }
    auto* pCur = std::get_if<DescT>(&cc->shape);
    if (pCur == nullptr) { cs.Reset(); return false; }
    const DescT cur = *pCur;  // 值快照 —— 命令 oldVal 基准、本帧绘制基准

    const glm::quat rot    = tc.rotation;
    const glm::vec3 origin = tc.position;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 投影所有顶点到屏幕。
    struct ScreenVtx { bool valid; glm::vec2 s; };
    std::array<ScreenVtx, DescT::kMaxVertices> screen{};
    for (std::uint32_t i = 0; i < cur.count; ++i)
    {
        const glm::vec3 w = LocalToWorld(rot, origin, cur.vertices[i]);
        const auto proj = GM::ProjectWorldToScreen(w, viewProj, imageOrigin, imageSize);
        screen[i].valid = proj.has_value();
        if (proj) { screen[i].s = proj->screen; }
    }

    const ImVec2    mpos = ImGui::GetMousePos();
    const glm::vec2 mouse(mpos.x, mpos.y);
    const bool inViewport =
        mouse.x >= imageOrigin.x && mouse.x <= imageOrigin.x + imageSize.x &&
        mouse.y >= imageOrigin.y && mouse.y <= imageOrigin.y + imageSize.y;

    // hover 命中最近顶点（屏幕距离 < kHitRadiusPx）。
    int   hover  = -1;
    float bestD2 = kHitRadiusPx * kHitRadiusPx;
    for (std::uint32_t i = 0; i < cur.count; ++i)
    {
        if (!screen[i].valid) { continue; }
        const glm::vec2 d  = screen[i].s - mouse;
        const float     d2 = d.x * d.x + d.y * d.y;
        if (d2 <= bestD2) { bestD2 = d2; hover = static_cast<int>(i); }
    }
    cs.hoverVertex = hover;

    // 命令 apply：写回 component.shape。捕获 &host（生命周期长于 cmdStack）+
    // ent；切场景时 cmdStack.Clear() 会清掉这些命令，不会悬空。
    auto apply = [&host, ent](const DescT& d) {
        if (host.scene.pWorld == nullptr) { return; }
        auto* c = host.scene.pWorld->GetComponent<ColliderComponent>(ent);
        if (c != nullptr) { c->shape = d; }
    };
    auto pushEdit = [&](const DescT& oldVal, const DescT& newVal, int opId) {
        const std::string key = "collider.vtx." + std::to_string(opId);
        host.cmdStack.Push(std::make_unique<SetFieldValueCommand<DescT>>(
            ent, key, oldVal, newVal, apply));
    };

    // 鼠标 → entity collider 平面上的 local 坐标。
    auto mouseToLocal = [&](glm::vec2& outLocal) -> bool {
        const auto ray = GM::ScreenToWorldRay(mouse, imageOrigin, imageSize, invViewProj);
        if (!ray) { return false; }
        const glm::vec3 planeN = rot * glm::vec3(0.0f, 0.0f, 1.0f);
        const auto t = GM::RayPlaneIntersect(ray->origin, ray->dir, origin, planeN);
        if (!t) { return false; }
        const glm::vec3 worldHit = ray->origin + ray->dir * (*t);
        outLocal = WorldToLocal(rot, origin, worldHit);
        return true;
    };

    if (cs.dragging && cs.selectedVertex >= 0
        && cs.selectedVertex < static_cast<int>(cur.count))
    {
        // ---- 拖拽中：每帧把选中顶点更新到鼠标平面位置（coalesce by dragOpId）
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            glm::vec2 newLocal;
            if (mouseToLocal(newLocal))
            {
                DescT next = cur;
                next.vertices[cs.selectedVertex] = newLocal;
                pushEdit(cur, next, cs.dragOpId);
            }
        }
        else
        {
            cs.dragging = false;
            cs.dragOpId = -1;
        }
    }
    else if (inViewport && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (hover >= 0)
        {
            // 命中顶点 → 起拖（新一次拖拽 = 新 opId，与上次拖拽不 coalesce）。
            cs.selectedVertex = hover;
            cs.dragging       = true;
            cs.dragOpId       = ++cs.opSeq;
        }
        else if (cur.count < DescT::kMaxVertices)
        {
            // 空白 → 在最近边后插入新顶点（屏幕空间找最近边）。
            glm::vec2 newLocal;
            if (mouseToLocal(newLocal))
            {
                int insertAfter = static_cast<int>(cur.count) - 1;  // 默认末尾追加
                if (cur.count >= 2)
                {
                    float best = 1e30f;
                    const std::uint32_t edgeCount =
                        closedLoop ? cur.count : (cur.count - 1);
                    for (std::uint32_t i = 0; i < edgeCount; ++i)
                    {
                        const std::uint32_t j = (i + 1) % cur.count;
                        if (!screen[i].valid || !screen[j].valid) { continue; }
                        const float d =
                            GM::PointSegmentDistance2D(mouse, screen[i].s, screen[j].s);
                        if (d < best) { best = d; insertAfter = static_cast<int>(i); }
                    }
                }
                const int insertPos = insertAfter + 1;
                DescT     next      = cur;
                for (int k = static_cast<int>(cur.count); k > insertPos; --k)
                {
                    next.vertices[k] = next.vertices[k - 1];
                }
                next.vertices[insertPos] = newLocal;
                next.count               = cur.count + 1;
                pushEdit(cur, next, ++cs.opSeq);
                cs.selectedVertex = insertPos;
            }
        }
    }
    else if (inViewport && hover >= 0
             && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
             && cur.count > minVertices)
    {
        // 双击顶点 → 删除（避开 RMB —— viewport RMB 留给相机/其它）。
        DescT next = cur;
        for (std::uint32_t k = static_cast<std::uint32_t>(hover); k + 1 < cur.count; ++k)
        {
            next.vertices[k] = next.vertices[k + 1];
        }
        next.count        = cur.count - 1;
        pushEdit(cur, next, ++cs.opSeq);
        cs.selectedVertex = -1;
    }

    // ---- 绘制顶点 handle（实心圆 + 黑描边，三态配色）----
    for (std::uint32_t i = 0; i < cur.count; ++i)
    {
        if (!screen[i].valid) { continue; }
        ImU32 col = kColNormal;
        if (static_cast<int>(i) == cs.selectedVertex && cs.dragging) { col = kColSelected; }
        else if (static_cast<int>(i) == hover)                       { col = kColHover; }
        const ImVec2 c(screen[i].s.x, screen[i].s.y);
        dl->AddCircleFilled(c, kHandleRadiusPx, col);
        dl->AddCircle(c, kHandleRadiusPx, kColOutline, 0, 1.5f);
    }

    return true;
}

}  // anonymous namespace

bool HandleColliderVertexEdit(EditorHost& host, glm::vec2 imageOrigin,
                              glm::vec2 imageSize, float aspect)
{
    auto& cs = host.colliderEdit;
    if (!cs.active) { return false; }

    // Esc 退出子模式。
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        cs.Reset();
        return false;
    }

    if (host.scene.pWorld == nullptr || !cs.entity.IsValid())
    {
        cs.Reset();
        return false;
    }
    auto* tc = host.scene.pWorld->GetComponent<TC>(cs.entity);
    auto* cc = host.scene.pWorld->GetComponent<ColliderComponent>(cs.entity);
    if (tc == nullptr || cc == nullptr)
    {
        cs.Reset();
        return false;
    }

    const auto      cam         = BuildEditorCamera(host.camera, aspect);
    const glm::mat4 viewProj    = cam.projection * cam.view;
    const glm::mat4 invViewProj = glm::inverse(viewProj);

    if (std::get_if<PolygonDesc>(&cc->shape) != nullptr)
    {
        return EditDesc<PolygonDesc>(host, *tc, imageOrigin, imageSize,
                                     viewProj, invViewProj,
                                     /*minVertices*/ 3, /*closedLoop*/ true);
    }
    if (auto* ecDesc = std::get_if<EdgeChainDesc>(&cc->shape))
    {
        return EditDesc<EdgeChainDesc>(host, *tc, imageOrigin, imageSize,
                                       viewProj, invViewProj,
                                       /*minVertices*/ 2, /*closedLoop*/ ecDesc->isLoop);
    }

    // shape 不是 Polygon / EdgeChain（Circle / Box）→ 无顶点可编辑，退出。
    cs.Reset();
    return false;
}

}  // namespace Orange::Editor
