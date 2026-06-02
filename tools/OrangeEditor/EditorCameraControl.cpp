// EditorCameraControl 实现 —— 见 EditorCameraControl.h 的注释。

#include "EditorCameraControl.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

void UpdateEditorCameraFromInput(EditorHost& host)
{
    auto& ec = host.camera;
    const ImGuiIO& io = ImGui::GetIO();

    const bool hovered = ImGui::IsWindowHovered();

    // ---- gizmo gate（v0.4 c5 之后 bug-fix）------------------------------
    // 用户撞 bug：hover gizmo handle 时按下 LMB → 既启动相机轨道又启动
    // gizmo 拖动，鼠标移动时两者同时消费 MouseDelta → 场景旋转 + entity
    // 沿 axis 飞速移动 + handle 视觉跟随，看起来像 "gizmo 抢事件 + handle
    // 变长"。
    //
    // gate 逻辑：
    //   * gizmo 正在拖动（IsDragging）→ camera 既不启动也不维持 dragging
    //     —— 与 gizmo drag 互斥，松手前 camera 完全冻结
    //   * gizmo hovered（IsHovered）+ camera 还没在 dragging → 不让本帧
    //     LMB click 启动 camera dragging（让 gizmo dispatch 抢这次 click）
    //   * camera 已经 dragging（用户先 click 空白处启动后再划过 gizmo
    //     handle）→ 维持原状不打断（按 capture-on-press 语义）
    //
    // 读 host.gizmo 字段时拿的是**上一帧**值（gizmo dispatch 在 ScenePanel
    // 后段调用，更新 hoveredAxis 在本函数之后）。实际 UX 下用户 hover →
    // click 至少跨多帧（人类反应时间 >> 16ms 单帧），上一帧 hover 状态
    // 正确反映"按下 LMB 那一刻"。
    // collider 顶点编辑子模式 active 时相机 LMB 完全冻结（同 gizmo drag 互斥），
    // 把 LMB 让给顶点选 / 拖 / 加 / 删（GAP-2026-05-21）。
    const bool gizmoBusy = host.gizmo.IsDragging() || host.colliderEdit.active;
    const bool gizmoHover = host.gizmo.IsHovered();

    // LMB 轨道旋转 —— capture-on-press 状态机：
    //   * 仅当 LMB 在本面板内 *按下* 时进入 dragging 模式；
    //   * dragging 期间无视 hover，连续吃 MouseDelta —— 修复"拖快了鼠标
    //     划出面板 → 旋转中断"的体感问题；
    //   * LMB 释放退出 dragging。
    if (ec.dragging && (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || gizmoBusy))
    {
        ec.dragging = false;
    }
    if (!ec.dragging && hovered
        && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !gizmoHover && !gizmoBusy)
    {
        ec.dragging = true;
    }
    if (ec.dragging)
    {
        const ImVec2 d = io.MouseDelta;
        // 拖右 → azimuth 减小 → 相机向左绕轨道 → 场景向右转，与鼠标方向一致
        ec.azimuth  -= d.x * ec.lookSensitivity;
        // 屏幕 Y 向下为正；拖下 → d.y > 0 → elevation 减小 → 相机下沉 → 视角上仰
        ec.elevation += d.y * ec.lookSensitivity;
        constexpr float kMaxElev = 1.5533430343f;   // glm::radians(89°)
        if (ec.elevation >  kMaxElev) ec.elevation =  kMaxElev;
        if (ec.elevation < -kMaxElev) ec.elevation = -kMaxElev;
    }

    // MMB 平移 pivot（capture-on-press，同 LMB orbit 状态机语义）。MMB 未被
    // orbit(LMB) / zoom(滚轮) / gizmo(LMB) 占用 → 与它们正交不冲突；gizmoBusy
    // 时同样冻结（与 collider 编辑等互斥，保持一致）。导航刚需（gap 报告 §3 P0）。
    if (ec.panning && (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) || gizmoBusy))
    {
        ec.panning = false;
    }
    if (!ec.panning && hovered
        && ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && !gizmoBusy)
    {
        ec.panning = true;
    }
    if (ec.panning)
    {
        const ImVec2 d = io.MouseDelta;
        // 相机基向量从 az/el 推（与 BuildEditorCamera 的 offset 同源）。
        const float     cosE = std::cos(ec.elevation);
        const glm::vec3 offset(cosE * std::sin(ec.azimuth),
                               std::sin(ec.elevation),
                               cosE * std::cos(ec.azimuth));
        const glm::vec3 fwd   = -glm::normalize(offset);  // 相机 → pivot
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 up    = glm::cross(right, fwd);
        // grab-pan：拖右(d.x>0)→内容右移→pivot 左移；拖下(d.y>0，屏幕 y-down)→
        // 内容下移→pivot 上移。速度随 radius 缩放保持手感一致。
        const float scale = ec.radius * ec.panSensitivity;
        ec.pivot += (-right * d.x + up * d.y) * scale;
    }

    // ---- RMB-held 飞行导航（Unreal/Unity 标准 RMB+WASD）-------------------
    // RMB 拖动 = look-in-place（保持相机位置不动、只转视角，与轨道 LMB 区别）；
    // RMB 按住时 WASD/QE 沿视向飞行（移动 pivot → 相机随之移动，offset 不变）。
    // RMB gate 让 WASD 不与 W/E/R gizmo 快捷键冲突（ScenePanel 侧 RMB 按住时
    // 不切 gizmo mode）。与 LMB orbit / MMB pan / 滚轮 zoom 正交（RMB 未占用）。
    if (ec.flying && (!ImGui::IsMouseDown(ImGuiMouseButton_Right) || gizmoBusy))
    {
        ec.flying = false;
    }
    if (!ec.flying && hovered
        && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !gizmoBusy)
    {
        ec.flying = true;
    }
    if (ec.flying)
    {
        // 当前（旋转前）相机世界位置——look-in-place 锚点。
        const float cosE0 = std::cos(ec.elevation);
        const glm::vec3 off0(ec.radius * cosE0 * std::sin(ec.azimuth),
                             ec.radius * std::sin(ec.elevation),
                             ec.radius * cosE0 * std::cos(ec.azimuth));
        const glm::vec3 camPos = ec.pivot + off0;

        // RMB 拖动看（与 LMB orbit 同方向，但锚定相机位置不动）。
        const ImVec2 d = io.MouseDelta;
        ec.azimuth   -= d.x * ec.lookSensitivity;
        ec.elevation += d.y * ec.lookSensitivity;
        constexpr float kMaxElev = 1.5533430343f;   // glm::radians(89°)
        if (ec.elevation >  kMaxElev) ec.elevation =  kMaxElev;
        if (ec.elevation < -kMaxElev) ec.elevation = -kMaxElev;

        // 旋转后保持相机位置不动：pivot = camPos - newOffset（look-in-place）。
        const float cosE1 = std::cos(ec.elevation);
        const glm::vec3 off1(ec.radius * cosE1 * std::sin(ec.azimuth),
                             ec.radius * std::sin(ec.elevation),
                             ec.radius * cosE1 * std::cos(ec.azimuth));
        ec.pivot = camPos - off1;

        // WASD/QE 沿视向飞行（仅无文本输入焦点时，避免抢键）。
        if (!io.WantTextInput)
        {
            const glm::vec3 fwd   = -glm::normalize(off1);  // 相机 → 视线前方
            const glm::vec3 right = glm::normalize(
                glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
            const float dt = (io.DeltaTime > 0.0f && io.DeltaTime < 0.2f)
                                 ? io.DeltaTime : 0.016f;
            const float speed = ec.flySpeed * (io.KeyShift ? 3.0f : 1.0f) * dt;
            glm::vec3 move(0.0f);
            if (ImGui::IsKeyDown(ImGuiKey_W)) { move += fwd; }
            if (ImGui::IsKeyDown(ImGuiKey_S)) { move -= fwd; }
            if (ImGui::IsKeyDown(ImGuiKey_D)) { move += right; }
            if (ImGui::IsKeyDown(ImGuiKey_A)) { move -= right; }
            if (ImGui::IsKeyDown(ImGuiKey_E)) { move += worldUp; }
            if (ImGui::IsKeyDown(ImGuiKey_Q)) { move -= worldUp; }
            if (glm::length(move) > 1e-5f)
            {
                ec.pivot += glm::normalize(move) * speed;
            }
        }
    }

    // 滚轮：飞行中（RMB 按住）= 调飞行速度（Unreal/Unity 标准，每格 ±10%
    // 乘改，clamp [0.1,200]）；非飞行 = 缩放 radius（推近 / 拉远）。hover 才生效
    // 避免误触其它面板滚动条。不被 gizmo gate 影响——gizmo 不消费滚轮。
    if (hovered && io.MouseWheel != 0.0f)
    {
        if (ec.flying)
        {
            ec.flySpeed *= std::pow(1.1f, io.MouseWheel);
            if (ec.flySpeed < 0.1f)   ec.flySpeed = 0.1f;
            if (ec.flySpeed > 200.0f) ec.flySpeed = 200.0f;
        }
        else
        {
            ec.radius -= io.MouseWheel * ec.zoomSensitivity;
            // 下限从 0.5 降到 0.02 —— 让 Frame Selected 聚焦到 Avocado 这种
            // 0.04 单位级小模型后，滚轮仍能贴近观察而不被 clamp 弹远。
            if (ec.radius < 0.02f) ec.radius = 0.02f;
        }
    }
}

Orange::Engine::Render::Camera
BuildEditorCamera(const EditorCameraState& ec, float aspect)
{
    using ::Orange::Engine::Render::Camera;
    const float safeAspect = (aspect > 0.0f) ? aspect : 1.0f;
    Camera cam = Camera::Perspective(glm::radians(ec.fovYDegrees),
                                     safeAspect, ec.zNear, ec.zFar);
    const float cosElev = std::cos(ec.elevation);
    const glm::vec3 offset(
        ec.radius * cosElev * std::sin(ec.azimuth),
        ec.radius * std::sin(ec.elevation),
        ec.radius * cosElev * std::cos(ec.azimuth));
    const glm::vec3 position = ec.pivot + offset;
    cam.view = glm::lookAt(position, ec.pivot, glm::vec3(0.0f, 1.0f, 0.0f));
    return cam;
}

namespace
{

// world matrix 合成（与 EditorPicking.cpp / src/render/RenderScene.cpp 同款；
// 渲染器以外模块不引私有头，按 invariant 自包含复述）。
glm::mat4
ComposeWorldMatrix(const Orange::Engine::Scene::TransformComponent& xform) noexcept
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), xform.position);
    m *= glm::mat4_cast(xform.rotation);
    m  = glm::scale(m, xform.scale);
    return m;
}

}  // anonymous namespace

namespace
{

// 把一组 entity 的世界 bounds 合并后塞进相机视野 —— FrameSelected（选区）与
// FrameAll（全场景）共用。entities 空 / 全无效 → 不动相机、返回 false。
bool FrameEntitiesCamera(EditorHost&                                host,
                         const std::vector<Orange::Engine::Entity>& entities)
{
    using namespace Orange::Engine;
    if (host.scene.pWorld == nullptr || entities.empty()) { return false; }

    auto&     ec    = host.camera;
    auto&     world = *host.scene.pWorld;
    glm::vec3 wmn(std::numeric_limits<float>::max());
    glm::vec3 wmx(std::numeric_limits<float>::lowest());
    bool      any = false;

    // 累加一个 entity 的世界 bounds：有 Renderable mesh → 世界 AABB（local 8
    // 角点过 world matrix，与 picking 同款粗 AABB）；否则（灯光 / 空 entity）→
    // 该 entity 的 Transform 位置作退化点。无 Transform → 不贡献。
    auto accumulate = [&](Entity e) {
        if (!world.IsValid(e)) { return; }
        const auto* pXform = world.GetComponent<Scene::TransformComponent>(e);
        if (pXform == nullptr) { return; }
        bool        addedMesh = false;
        const auto* pRC = world.GetComponent<Render::RenderableComponent>(e);
        if (pRC != nullptr && host.assets.pAssets != nullptr)
        {
            const auto* pMesh = host.assets.pAssets->Get(pRC->mesh);
            if (pMesh != nullptr && !pMesh->Empty())
            {
                const auto& positions = pMesh->Positions();
                if (!positions.empty())
                {
                    glm::vec3 mn(std::numeric_limits<float>::max());
                    glm::vec3 mx(std::numeric_limits<float>::lowest());
                    for (const auto& p : positions)
                    {
                        mn.x = std::min(mn.x, p.x);  mx.x = std::max(mx.x, p.x);
                        mn.y = std::min(mn.y, p.y);  mx.y = std::max(mx.y, p.y);
                        mn.z = std::min(mn.z, p.z);  mx.z = std::max(mx.z, p.z);
                    }
                    const glm::mat4 worldMat = ComposeWorldMatrix(*pXform);
                    const glm::vec3 corners[8] = {
                        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z},
                        {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z},
                        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z},
                        {mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z},
                    };
                    for (const auto& c : corners)
                    {
                        const glm::vec3 w = glm::vec3(worldMat * glm::vec4(c, 1.0f));
                        wmn = glm::min(wmn, w);
                        wmx = glm::max(wmx, w);
                    }
                    addedMesh = true;
                }
            }
        }
        if (!addedMesh)
        {
            wmn = glm::min(wmn, pXform->position);
            wmx = glm::max(wmx, pXform->position);
        }
        any = true;
    };

    for (const auto e : entities) { accumulate(e); }
    if (!any) { return false; }

    const glm::vec3 center  = (wmn + wmx) * 0.5f;
    const float     sphereR = glm::length(wmx - wmn) * 0.5f;

    ec.pivot = center;

    if (sphereR > 1e-6f)
    {
        // 把包围球塞进垂直 FOV：sinHalfFov = R / dist → dist = R / sin(fov/2)。
        // ×1.25 留边距。视口通常横宽（水平 FOV ≥ 垂直），垂直是约束方向。
        const float halfFov = glm::radians(ec.fovYDegrees) * 0.5f;
        const float sinHalf = std::max(0.01f, std::sin(halfFov));
        ec.radius = (sphereR / sinHalf) * 1.25f;
        // 按物体尺度重算近 / 远裁剪面，跨 0.04 单位（Avocado）~ 165 单位
        // （Duck）都能完整 bracket，不被 zNear/zFar 裁。
        ec.zNear = std::max(0.001f, sphereR * 0.02f);
        ec.zFar  = ec.radius + sphereR * 4.0f + 1.0f;
    }
    else
    {
        // 无几何（灯光 / 空 entity）：给个温和默认距离，不动 near/far。
        ec.radius = std::max(ec.radius, 4.0f);
    }
    return true;
}

}  // anonymous namespace

bool FrameSelectedCamera(EditorHost& host)
{
    using namespace Orange::Engine;
    const Entity sel = host.selection.selectedEntity;
    if (!sel.IsValid()) { return false; }

    // 多选 Frame（hierarchy gap 报告 §4 quick-win #3）：把整个选区（primary +
    // additional set）的世界 bounds 合并。单选时仅一个 entity → 与原行为一致。
    std::vector<Entity> ents;
    ents.push_back(sel);
    for (const auto a : host.selection.additionalSelectedEntities)
    {
        ents.push_back(a);
    }
    return FrameEntitiesCamera(host, ents);
}

bool FrameAllCamera(EditorHost& host)
{
    using namespace Orange::Engine;
    if (host.scene.pWorld == nullptr) { return false; }

    // 全场景 Frame（Home 键 / View 菜单）：合并所有带 Transform 的 entity 世界
    // bounds（有 mesh 的算世界 AABB、无 mesh 的算位置点），把相机拉到能看全场景
    // 的距离。空场景 → false。对齐 Unity/Unreal "Frame All / Home"。
    std::vector<Entity> ents;
    auto view = host.scene.pWorld->Registry()
                    .view<Orange::Engine::Scene::TransformComponent>();
    for (auto e : view) { ents.push_back(World::FromEntt(e)); }
    return FrameEntitiesCamera(host, ents);
}
