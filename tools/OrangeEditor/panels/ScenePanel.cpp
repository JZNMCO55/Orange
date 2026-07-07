// Scene 面板（off-screen pipeline + sampler + descriptor set + ImGui::Image）。
// 与 EditorRenderLayer.cpp 主 TU 共享同一类声明 EditorRenderLayer。

#include "../EditorRenderLayer.h"

#include <orange/engine/core/Log.h>

#include "../ColliderDebugDraw.h"
#include "../ColliderVertexEdit.h"
#include "../EditorAssetDropHandler.h" // v1.2.3 patch · viewport ORANGE_ASSET DnD
#include "../EditorPrefabActions.h"    // prefab 拖入实例化（drop 先判 .prefab.json）
#include "../EditorCameraControl.h"
#include "../EditorPicking.h"
#include "../EditorRotateGizmo.h"
#include "../EditorScaleGizmo.h"
#include "../EditorTranslateGizmo.h"
#include "../plugin/GizmoContext.h"
#include "../plugin/IEditorGizmoPlugin.h"
#include "../render/ThumbnailService.h" // mHost.thumbnails->SetPipeline（完整类型）
#include "../schema/ComponentSchema.h"
#include "../schema/ComponentSchemaRegistry.h"
#include "../theme/EditorTheme.h" // M9.3 Game 视口无相机提示走 Color::GetAlertWarn token

#include <orange/engine/asset/AssetRegistry.h> // 统计 overlay 取 mesh 三角数
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/render/BuiltinPostProcessChain.h>
#include <orange/engine/render/Camera.h> // M9.3 Game 视口检测 World 是否有游戏相机
#include <orange/engine/render/Pipeline.h>            // DebugViewMode
#include <orange/engine/render/RenderableComponent.h> // 统计 overlay
#include <orange/engine/render/ShadowConfig.h>
#include <orange/engine/render/DebugDrawScene.h>
#include <orange/engine/scene/NameComponent.h> // 统计 overlay 选中名
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/renderer/VulkanInterop.h>
#include <orange/rhi/RHITexture.h>

#include <backends/imgui_impl_vulkan.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <cstdio>
#include <initializer_list>

// viewport 工具栏 grid / sky / debug-draw / colliders 开关。已迁入
// EditorSettings（`mHost.settings.viewport*Enabled`），随 settings 在进程退出时
// 持久化、跨重启保留——此前是 file-static 的"暂不持久化"遗留状态，v0.8 整骨
// 本次补完迁移。默认值（Grid/Sky/Colliders 开、Debug Draw 关）见 EditorSettings.h：
//   * Grid/Sky 默认开（地面参考线 + cubemap 未烘焙时 fallback clear color 不黑屏）
//   * Debug Draw 默认关（opt-in 调试：原点坐标轴 + selected sphere）
//   * Colliders 默认开（碰撞盒可视化是 collider 工作流关键反馈，开销极低）

// debug render views 的 viewport 切换状态（session-only，诊断功能无需持久化）：
// 0=Lit（正常渲染）/ 1=Normals（world-normal-as-RGB）。toolbar combo 写它，
// DrawScenePanel 每帧 push 给 Pipeline::SetDebugViewMode。Wireframe/Unlit/
// Overdraw 后续接入（见 GAP-2026-05-30-debug-render-views-engine-side）。
namespace
{
    int sViewportDebugViewMode = 0;
} // namespace

void EditorRenderLayer::DrawScenePanel()
{
    ImGui::Begin("Scene");

    // ---- v0.4 c5：Viewport 工具栏 ---------------------------------------
    // 参 Cocos Creator 3.6.0 截图的"Scene 面板顶部工具栏"布局（editor-roadmap
    // .md D5 节）。本期落 3 个能消费的功能 + 3 个占位（依赖引擎能力，登记
    // engine-known-gaps 后独立 session 处理）。
    //
    //   * **Gizmos**（checkbox）—— 全部 gizmo overlay 总开关；写
    //     `host.gizmo.visible`，c2/c3 内置 Translate/Rotate/Scale 早退 +
    //     c4 plugin dispatch gate 都消费本字段
    //   * **View Mode**（placeholder disabled）—— Persp / 2D Lock 切换；
    //     依赖编辑器相机引入"锁定 XY 平面 + 关闭 elevation"模式（当前
    //     EditorCameraControl 只支持自由轨道）
    //   * **Shading**（placeholder disabled）—— Shaded / Wireframe；依赖
    //     OrangeRender 提供 wireframe pass（按 engine-known-gaps 工作流
    //     推到 OrangeRender incoming_feature 单独 session 处理）
    //   * **Camera Mode**（placeholder disabled）—— Design Resolution 锁定；
    //     依赖引擎 CameraDesc + viewport resolution lock 概念
    //
    // disabled 项的 tooltip 用 `ImGuiHoveredFlags_AllowWhenDisabled` 让 hover
    // 在 disabled 状态下仍弹出，向用户解释"为什么不可用 + 哪里登记了"。
    ImGui::Checkbox("Gizmos", &mHost.gizmo.visible);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Viewport gizmo overlay 总开关（Translate / Rotate / Scale\n"
                          "内置 gizmo + Light / ParticleEmitter / Camera frustum plugin）");
    }

    ImGui::SameLine();
    ImGui::Checkbox("Grid", &mHost.settings.viewportGridEnabled);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("地面参考网格（Y=0 平面，每 1 m 细线 + 每 10 m 粗线）\n"
                          "走 fullscreen-quad + PristineGrid + depth test，被几何遮挡");
    }

    ImGui::SameLine();
    ImGui::Checkbox("Sky", &mHost.settings.viewportSkyEnabled);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("天空盒背景（采 EnvironmentComponent.cubemap）\n"
                          "未挂 EnvironmentComponent / cubemap 未烘焙 → 显示深蓝灰 fallback");
    }

    ImGui::SameLine();
    ImGui::Checkbox("Debug Draw", &mHost.settings.viewportDebugDrawEnabled);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("v0.9 调试几何 overlay：原点坐标轴 + selected entity 位置 sphere\n"
                          "走 OrangeRender DebugDraw immediate-mode（line / triangle）\n"
                          "always-on-top，不被场景几何遮挡");
    }

    ImGui::SameLine();
    ImGui::Checkbox("Colliders", &mHost.settings.viewportCollidersEnabled);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("物理碰撞盒可视化（绿 = 未选中 / 黄 = 选中）\n"
                          "Circle / Box / Polygon / Edge Chain 全部以 wireframe 投影到 XY 平面\n"
                          "走 OrangeRender DebugDraw immediate-mode，不被场景几何遮挡");
    }

    // Snap 开关 —— gizmo 网格吸附的快捷切换（之前只在 Settings 面板埋着）。
    // 对齐 Unity / Lumix viewport snap toggle；步进值仍在 Settings 调。
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &mHost.settings.snapEnabled);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Gizmo 网格吸附：translate / rotate / scale 拖动时按步进对齐。\n"
            "步进 translate=%.3g  rotate=%.3g°  scale=%.3g（Settings 面板 Snap 段可调）。\n"
            "对齐 Unity / Lumix snap 开关；关闭时连续拖动（零回归）。\n"
            "提示：开关关闭时，拖动中按住 Ctrl 可临时吸附（Unity 标准）。",
            mHost.settings.snapTranslateStep,
            mHost.settings.snapRotateStepDeg,
            mHost.settings.snapScaleStep);
    }

    // Gizmo 变换工具按钮（Move / Rotate / Scale + World/Local）—— W/E/R/X 快捷键
    // 的可点 + 可发现入口，对齐 Unity 左上变换工具栏。激活态高亮。
    ImGui::SameLine();
    {
        using GMode     = EditorGizmoState::Mode;
        using GSpace    = EditorGizmoState::Space;
        auto modeButton = [&](const char* label, GMode m, const char* tip)
        {
            const bool active = (mHost.gizmo.mode == m);
            if (active)
            {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ImGui::SmallButton(label))
            {
                mHost.gizmo.mode = m;
            }
            if (active)
            {
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", tip);
            }
            ImGui::SameLine();
        };
        modeButton("Move", GMode::Translate, "平移 gizmo（快捷键 W）");
        modeButton("Rotate", GMode::Rotate, "旋转 gizmo（快捷键 E）");
        modeButton("Scale", GMode::Scale, "缩放 gizmo（快捷键 R）");
        const char* spaceLabel =
            (mHost.gizmo.space == GSpace::Local) ? "Local" : "World";
        if (ImGui::SmallButton(spaceLabel))
        {
            mHost.gizmo.space = (mHost.gizmo.space == GSpace::Local)
                                    ? GSpace::World
                                    : GSpace::Local;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Gizmo 参考系 World ⇄ Local 切换（快捷键 X）\n"
                              "作用 translate / rotate 轴向；scale 始终 local");
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Combo 列宽从 CalcTextSize 派生 —— v0.4 期硬编码 70 / 80 像素在
    // 1680×1120 × 150% scale 上会被字体撑出 combo 控件框（Segoe UI 24px 下
    // "Shaded" 文本宽 ~58px，加 framePad + arrow 按钮 ~24px 远超 80px）。
    // FramePadding / arrow 按钮宽由 main.cpp ScaleAllSizes(dpiScale) 同步缩放。
    // 用 (std::max)(...) 圆括号包装绕开 windows.h max 宏污染（本 TU 通过
    // imgui_impl_vulkan / VulkanInterop 间接拉 windows.h，没 #define NOMINMAX）。
    auto comboItemWidth = [&](std::initializer_list<const char*> items)
    {
        const ImGuiStyle& s    = ImGui::GetStyle();
        float             maxW = 0.0f;
        for (const char* it : items)
        {
            const float w = ImGui::CalcTextSize(it).x;
            if (w > maxW)
            {
                maxW = w;
            }
        }
        // arrow button 宽 ≈ FrameHeight；framePadding 左右各一份。
        return maxW + s.FramePadding.x * 2.0f + ImGui::GetFrameHeight();
    };

    {
        ImGui::BeginDisabled();
        const char* kViewModes[] = {"Persp"};
        int         curView      = 0;
        ImGui::SetNextItemWidth(comboItemWidth({"Persp", "2D Lock"}));
        ImGui::Combo("##ViewMode", &curView, kViewModes, IM_ARRAYSIZE(kViewModes));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("View Mode（Persp / 2D Lock）未实现\n"
                              "依赖 EditorCameraControl 引入 2D 锁定模式");
        }
    }

    ImGui::SameLine();
    {
        // 渲染调试视图（debug render views）：Lit（正常）+ Normals（world-normal
        // as-RGB）已实现，选中即调 Pipeline::SetDebugViewMode（下方每帧 push）。
        // 全 5 mode（Lit/Normals/Unlit/Overdraw/Wireframe）已接；Wireframe 用
        // device feature fillModeNonSolid（不支持则 GetOrCompilePipeline fallback
        // Fill，退化为实心绿）。
        const char* kDebugViews[] = {"Lit", "Normals", "Unlit", "Overdraw", "Wireframe"};
        ImGui::SetNextItemWidth(comboItemWidth({"Lit", "Normals", "Wireframe"}));
        ImGui::Combo("##DebugView", &sViewportDebugViewMode, kDebugViews,
                     IM_ARRAYSIZE(kDebugViews));
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("渲染调试视图\n"
                              "  Lit       —— 正常 PBR / forward 渲染\n"
                              "  Normals   —— world-space normal 映射到 RGB（诊断法线朝向）\n"
                              "  Unlit     —— 直出 base color，无光照（诊断 albedo）\n"
                              "  Overdraw  —— 过绘热图（同像素覆盖越多越亮）\n"
                              "  Wireframe —— 线框（polygonMode=Line，诊断网格密度 / 拓扑）");
        }
    }

    ImGui::SameLine();
    {
        ImGui::BeginDisabled();
        // v0.6.5 c4：SmallButton → Button，让 disabled 占位与左侧 Persp /
        // Shaded combo 等高（SmallButton 无 FramePadding.y 比 Combo 矮一档，
        // 视觉上突兀）。
        ImGui::Button("Camera Mode");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("Camera Mode (Design Resolution 锁定) 未实现\n"
                              "依赖引擎 CameraDesc + viewport resolution lock —— 见\n"
                              "engine-known-gaps GAP-2026-05-15-camera-editor-vs-runtime-separation");
        }
    }

    ImGui::Separator();

    // S3：相机输入捕获 + push 编辑器轨道相机给 Pipeline override（GAP-2026-
    // 05-15 落地后路径——不再 mutate World ECS Camera，CameraFrustumGizmo
    // 等读 ECS Camera 的下游路径拿到的是游戏侧原始数据）。
    const ImVec2 region = ImGui::GetContentRegionAvail();
    const float  aspect = (region.y > 0.0f) ? (region.x / region.y) : 1.0f;
    UpdateEditorCameraFromInput(mHost);
    mEditorCameraOverride = BuildEditorCamera(mHost.camera, aspect);

    // S4：把 Scene 面板接到 Pipeline::InitializeOffscreen 上 ——
    // viewport-sized off-screen RT 渲染场景 → Interop::GetVulkanImageView
    // → ImGui_ImplVulkan_AddTexture → ImGui::Image。
    //
    // 尺寸退化（面板折叠 / 极小 < 1px）走占位文案分支：Pipeline 既不
    // 占资源也不渲染，避免反复 Resize 抖动。常规阈值 1px 已足够，编
    // 辑器实际可用尺寸至少几十像素。
    const std::uint32_t panelW =
        (region.x > 1.0f) ? static_cast<std::uint32_t>(region.x) : 0u;
    const std::uint32_t panelH =
        (region.y > 1.0f) ? static_cast<std::uint32_t>(region.y) : 0u;

    bool drewImage = false;
    if (EnsureScenePipeline(panelW, panelH) && mHost.scene.pWorld != nullptr)
    {
        // v0.6 c4：每帧 wire partition 给 Pipeline —— Pipeline 内部按
        // partition.IsEntityVisible(world, e) 过滤 drawable + shadow caster；
        // hide 的 layer 立即从 viewport 消失，无需 mutate ECS。
        // pointer 非拥有，partition 与 EditorSceneContext 同生命周期，
        // 始终 valid，无需 null 检查。
        mpScenePipeline->SetWorldPartition(&mHost.scene.partition);
        mpScenePipeline->SetEditorCameraOverride(&mEditorCameraOverride);
        // viewport toolbar toggle → Pipeline 状态：每帧 push（开销极小，
        // 避免在 toggle 改变时维护额外 dirty 标记）。
        // v1.3.0 grid 真迁出：toggle 直接写编辑器自家 provider 的 enable 状态
        //（不再走 engine 公共 API），engine 端无任何 grid 资源残留。
        if (mpEditorGridProvider)
        {
            mpEditorGridProvider->SetEnabled(mHost.settings.viewportGridEnabled);
        }
        mpScenePipeline->SetSkyEnabled(mHost.settings.viewportSkyEnabled);
        // debug-view mode（toolbar combo）每帧 push。0=Lit / 1=Normals。
        mpScenePipeline->SetDebugViewMode(
            sViewportDebugViewMode == 1   ? Orange::Engine::Render::DebugViewMode::Normals
            : sViewportDebugViewMode == 2 ? Orange::Engine::Render::DebugViewMode::Unlit
            : sViewportDebugViewMode == 3 ? Orange::Engine::Render::DebugViewMode::Overdraw
            : sViewportDebugViewMode == 4 ? Orange::Engine::Render::DebugViewMode::Wireframe
                                          : Orange::Engine::Render::DebugViewMode::Lit);
        // v1.3.1 Render Settings 面板编辑后下一帧立即生效 —— mShadowConfig 是
        // panel UI 直写字段，每帧 push（by-value 32 bytes 拷贝到 mpImpl，开销
        // 可忽略）。mapResolution 变化时 Pipeline EnsureShadowMap 下帧自重建
        // shadow target；其余字段当帧立即对 ubo 生效。
        mpScenePipeline->SetShadowConfig(mShadowConfig);

        // v0.9 c2 DebugDraw 接通 + Collider 可视化（v0.9.5 后置补丁）：dbg
        // 通道由 Debug Draw / Colliders 两个独立 toggle 共享，任一开启即启用
        // dbg；两段几何提交各自由对应 toggle 门控。
        if (auto* dbg = mpScenePipeline->GetDebugDrawScene())
        {
            const bool anyDebugGeom =
                mHost.settings.viewportDebugDrawEnabled || mHost.settings.viewportCollidersEnabled;
            dbg->SetEnabled(anyDebugGeom);

            if (mHost.settings.viewportDebugDrawEnabled)
            {
                // 原点 3 轴坐标（X 红 / Y 绿 / Z 蓝，长度 1.5）—— ABGR
                // packed：低 8 位 R，高 8 位 A。
                constexpr std::uint32_t kRed   = 0xFF0000FFu;
                constexpr std::uint32_t kGreen = 0xFF00FF00u;
                constexpr std::uint32_t kBlue  = 0xFFFF0000u;
                constexpr std::uint32_t kHi    = 0xFF00FFFFu; // 选中实体高亮黄
                dbg->AddLine(glm::vec3(0.0f), glm::vec3(1.5f, 0.0f, 0.0f), kRed);
                dbg->AddLine(glm::vec3(0.0f), glm::vec3(0.0f, 1.5f, 0.0f), kGreen);
                dbg->AddLine(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.5f), kBlue);

                // 选中 entities 位置 wireframe sphere —— primary + additional
                // 都画，让多选可视化。
                auto drawSelected = [&](Orange::Engine::Entity e)
                {
                    if (!e.IsValid())
                    {
                        return;
                    }
                    auto* xf = mHost.scene.pWorld->GetComponent<
                        Orange::Engine::Scene::TransformComponent>(e);
                    if (xf == nullptr)
                    {
                        return;
                    }
                    dbg->AddSphere(xf->position, 0.5f, kHi, 16);
                };
                drawSelected(mHost.selection.selectedEntity);
                for (const auto& e : mHost.selection.additionalSelectedEntities)
                {
                    drawSelected(e);
                }
            }

            if (mHost.settings.viewportCollidersEnabled)
            {
                Orange::Editor::DrawColliders(
                    *dbg,
                    *mHost.scene.pWorld,
                    mHost.selection.selectedEntity,
                    mHost.selection.additionalSelectedEntities);
            }
        }

        // Pipeline::RenderOffscreen 内部 WaitIdle —— 本帧返回时 GPU 已
        // 空，之后 RemoveTexture(旧 descriptor) + AddTexture(新) 才安全。
        mpScenePipeline->Render(*mHost.scene.pWorld);
        RebindSceneDescriptorSetIfNeeded();
        if (mSceneDescSet != VK_NULL_HANDLE)
        {
            ImGui::Image(reinterpret_cast<ImTextureID>(mSceneDescSet),
                         ImVec2(static_cast<float>(panelW),
                                static_cast<float>(panelH)));
            drewImage = true;

            const ImVec2    itemMin = ImGui::GetItemRectMin();
            const glm::vec2 imageOrigin(itemMin.x, itemMin.y);
            const glm::vec2 imageSize(static_cast<float>(panelW),
                                      static_cast<float>(panelH));

            // v1.2.3 patch · viewport ORANGE_ASSET DnD：松开时按光标位置
            // raycast 找命中 entity + apply asset。AcceptBeforeDelivery 默
            // 认 false → 仅在松开时触发，避免 hover 期间重复 raycast；
            // hover 高亮反馈留 v1.x minor 拉动。需绑定到 ImGui::Image item，
            // 故 BeginDragDropTarget 紧跟其后调（gizmo 等后续 widget 不影
            // 响，因为 DnD 已绑定到 Image 的 ItemID）。
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p =
                        ImGui::AcceptDragDropPayload("ORANGE_ASSET"))
                {
                    const std::size_t plen = (p->DataSize > 0)
                                                 ? static_cast<std::size_t>(p->DataSize) - 1
                                                 : 0;
                    const std::string assetPath(
                        static_cast<const char*>(p->Data), plen);
                    // **务必先判 prefab**：.prefab.json 走实例化为新根（不需
                    // PickEntityAt，命令栈可 undo）。否则落进 ApplyAssetDropToEntity
                    // 的"不识别扩展名"分支静默 warn 失败。
                    const bool isPrefab =
                        assetPath.size() >= 12 && assetPath.compare(assetPath.size() - 12, 12,
                                                                    ".prefab.json") == 0;
                    if (isPrefab)
                    {
                        Orange::Editor::Prefab::InstantiatePrefabFromPath(
                            mHost, assetPath);
                    }
                    else if (!assetPath.empty() && panelW > 0 && panelH > 0)
                    {
                        const ImVec2 mp = ImGui::GetMousePos();
                        const float  lx = mp.x - itemMin.x;
                        const float  ly = mp.y - itemMin.y;
                        const float  ndcX =
                            (lx / static_cast<float>(panelW)) * 2.0f - 1.0f;
                        const float ndcY =
                            (ly / static_cast<float>(panelH)) * 2.0f - 1.0f;
                        const Orange::Engine::Entity picked =
                            PickEntityAt(mHost,
                                         glm::vec2(ndcX, ndcY),
                                         aspect);
                        if (picked.IsValid())
                        {
                            Orange::Editor::ApplyAssetDropToEntity(
                                mHost, picked, assetPath);
                        }
                        else
                        {
                            // 拖到空白处（没命中实体）：mesh 资源 → 在地面落点
                            // 创建一个新实体（带 mesh + 导入材质），对齐 Unity /
                            // Lumix 拖模型进空场景生成物体。非 mesh（材质 / 音频
                            // 等需要既有实体承载）→ 静默忽略（CreateEntityFromMesh
                            // Asset 内部判扩展名）。命令栈可 Undo。
                            const glm::vec3 dropPos =
                                ScreenRayToGround(mHost,
                                                  glm::vec2(ndcX, ndcY), aspect);
                            const Orange::Engine::Entity created =
                                Orange::Editor::CreateEntityFromMeshAsset(
                                    mHost, assetPath, dropPos);
                            if (created.IsValid())
                            {
                                mHost.selection.selectedEntity = created;
                                mHost.selection.ClearAdditional();
                                mHost.assets.selectedAssetPath.clear();
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // viewport 统计 overlay（左上角紧凑半透）：实体 / 可见 Renderable /
            // 三角 / 选中名 —— 对齐 Lumix StudioApp / Unity scene stats。轻量
            // （每帧扫一遍 Renderable view，几十 entity 可忽略），始终显示。
            {
                using ::Orange::Engine::Entity;
                using ::Orange::Engine::Render::RenderableComponent;
                using ::Orange::Engine::Scene::NameComponent;
                using ::Orange::Engine::Scene::TransformComponent;
                auto* pStatsWorld = mHost.scene.pWorld.get();
                if (pStatsWorld != nullptr)
                {
                    const std::size_t entityCount    = pStatsWorld->Size();
                    std::size_t       visRenderables = 0;
                    std::uint64_t     triCount       = 0;
                    auto              statsView      = pStatsWorld->Registry()
                                         .view<TransformComponent, RenderableComponent>();
                    for (auto e : statsView)
                    {
                        const auto& rc = statsView.get<RenderableComponent>(e);
                        if (!rc.visible || mHost.assets.pAssets == nullptr)
                        {
                            continue;
                        }
                        const auto* pMesh = mHost.assets.pAssets->Get(rc.mesh);
                        if (pMesh == nullptr || pMesh->Empty())
                        {
                            continue;
                        }
                        ++visRenderables;
                        triCount += pMesh->Indices().size() / 3;
                    }
                    std::string  selName = "(none)";
                    const Entity sel     = mHost.selection.selectedEntity;
                    if (sel.IsValid() && pStatsWorld->IsValid(sel))
                    {
                        const auto* nc = pStatsWorld->GetComponent<NameComponent>(sel);
                        if (nc != nullptr && !nc->name.empty())
                        {
                            selName = nc->name;
                        }
                    }
                    // gizmo 模式 + 参考系（X 键切 World/Local 之前无视觉反馈）。
                    const char* gizModeName =
                        (mHost.gizmo.mode == EditorGizmoState::Mode::Rotate)  ? "Rotate"
                        : (mHost.gizmo.mode == EditorGizmoState::Mode::Scale) ? "Scale"
                                                                              : "Move";
                    const char* gizSpaceName =
                        (mHost.gizmo.space == EditorGizmoState::Space::Local) ? "Local"
                                                                              : "World";
                    char buf[224];
                    std::snprintf(buf, sizeof(buf),
                                  "Entities %zu  |  Renderables %zu  |  Tris %llu\n"
                                  "Selected: %s\nGizmo: %s [%s]",
                                  entityCount, visRenderables,
                                  static_cast<unsigned long long>(triCount), selName.c_str(),
                                  gizModeName, gizSpaceName);

                    ImDrawList*  dl = ImGui::GetWindowDrawList();
                    const ImVec2 pad(8.0f, 5.0f);
                    const ImVec2 anchor(imageOrigin.x + 8.0f, imageOrigin.y + 8.0f);
                    const ImVec2 ts = ImGui::CalcTextSize(buf);
                    dl->AddRectFilled(
                        anchor,
                        ImVec2(anchor.x + ts.x + pad.x * 2.0f,
                               anchor.y + ts.y + pad.y * 2.0f),
                        IM_COL32(0, 0, 0, 140), 4.0f);
                    dl->AddText(ImVec2(anchor.x + pad.x, anchor.y + pad.y),
                                IM_COL32(225, 225, 225, 255), buf);
                }
            }

            // viewport gizmo —— v0.4 c2 translate；c3 起 W/E/R 切换 +
            // rotate / scale。必须在 ImGui::Image 之后、picking 触发之前
            // 调：让 gizmo 先消费 LMB / hover，picking 仅在 gizmo 没接管
            // 时触发，避免"拖完 gizmo 松手又触发 picking"。
            //
            // 模式切换：拖动期间不切换（保 mid-drag 一致性，按 v0.2.5 c13
            // 的"BeginGroup 期间不交叉"惯例对偶）。键盘 W/E/R 不依赖 viewport
            // hover（与 Lumix / Unity 同款全局快捷键约定；但要求 ImGui 无
            // 文本输入 active，否则会拦截字母键）。
            // collider 顶点编辑子模式（GAP-2026-05-21）：active 时接管 viewport
            // 鼠标 —— 跳过 W/E/R 切换 + gizmo + picking，避免双重消费。返回 true
            // 即表示子模式激活；内部已绘制顶点 handle + 处理选 / 拖 / 加 / 删。
            const bool colliderEditing = Orange::Editor::HandleColliderVertexEdit(
                mHost, imageOrigin, imageSize, aspect);

            // RMB 按住（飞行导航）时不响应 W/E/R 切 gizmo mode —— 让 WASD 给
            // 相机飞行用，避免 W 既切 translate gizmo 又前进的冲突。
            const bool flyNavActive = ImGui::IsMouseDown(ImGuiMouseButton_Right);
            if (!colliderEditing && !mHost.gizmo.IsDragging() && !ImGui::IsAnyItemActive() && !flyNavActive)
            {
                // v0.8 keybinding：从 EditorKeybindings 读绑定的 key（默认
                // W/E/R，可在 Settings 面板内 rebind）。
                const auto& kb = mHost.keybindings;
                if (ImGui::IsKeyPressed(kb.gizmoTranslate, false))
                {
                    mHost.gizmo.mode = EditorGizmoState::Mode::Translate;
                }
                else if (ImGui::IsKeyPressed(kb.gizmoRotate, false))
                {
                    mHost.gizmo.mode = EditorGizmoState::Mode::Rotate;
                }
                else if (ImGui::IsKeyPressed(kb.gizmoScale, false))
                {
                    mHost.gizmo.mode = EditorGizmoState::Mode::Scale;
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_X, false))
                {
                    // X：切 gizmo 参考系 World ⇄ Local（gap 报告 §3 P0；作用
                    // translate/rotate 轴向，scale 始终 local）。X 为硬编码（同
                    // 报告约定，未走 EditorKeybindings rebind 表）。
                    mHost.gizmo.space =
                        (mHost.gizmo.space == EditorGizmoState::Space::World)
                            ? EditorGizmoState::Space::Local
                            : EditorGizmoState::Space::World;
                }
                // F：聚焦选中物体 —— 相机 pivot 移到选中 entity 世界 AABB
                // 中心 + 拉到合适距离 + 按尺度重算 near/far。解决导入模型
                // （Duck 165 单位 / Avocado 0.04 单位）尺寸悬殊看不到 / 被裁。
                else if (ImGui::IsKeyPressed(kb.frameSelected, false))
                {
                    FrameSelectedCamera(mHost);
                }
                // Home：Frame All（全场景 bounds）—— 对齐 Unity/Unreal。F 聚焦
                // 选中、Home 看全场景。硬编码 Home（与 X 切坐标系同款非 rebind 键）。
                else if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
                {
                    FrameAllCamera(mHost);
                }

                // Delete / Ctrl+D：viewport 聚焦时也响应删除 / 复制选中实体
                // （对齐 Unity/Lumix——此前只 Hierarchy 面板响应）。二者都只设
                // EditorHost 上的幂等标志（pendingDelete=同一 entity、
                // pendingDuplicate=bool true），与 Hierarchy 同帧重复 set 无害，
                // 实际删除/复制走各自既有可撤销路径。独立 if（不入上面 else-if 链）。
                // World::IsValid 补一道防操作死实体（Undo 可能已销毁）。
                if (mHost.selection.selectedEntity.IsValid() && mHost.scene.pWorld->IsValid(mHost.selection.selectedEntity) && !mHost.selection.renamingEntity.IsValid())
                {
                    if (ImGui::IsKeyPressed(mHost.keybindings.deleteEntity, false))
                    {
                        mHost.selection.pendingDelete =
                            mHost.selection.selectedEntity;
                    }
                    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
                    {
                        mHost.selection.pendingDuplicate = true;
                    }
                }
            }

            bool gizmoActive = false;
            if (!colliderEditing)
                switch (mHost.gizmo.mode)
                {
                    case EditorGizmoState::Mode::Translate:
                        gizmoActive = DrawAndHandleTranslateGizmo(
                            mHost, imageOrigin, imageSize, aspect);
                        break;
                    case EditorGizmoState::Mode::Rotate:
                        gizmoActive = DrawAndHandleRotateGizmo(
                            mHost, imageOrigin, imageSize, aspect);
                        break;
                    case EditorGizmoState::Mode::Scale:
                        gizmoActive = DrawAndHandleScaleGizmo(
                            mHost, imageOrigin, imageSize, aspect);
                        break;
                }

            // ---- c4：IEditorGizmoPlugin 调度（v0.2.5 c12 抽象首批真实消费）
            //
            // Edit Mode 且选中实体有效时遍历 schema 注册表，对每个匹配
            // (plugin.CanHandle && schema.has(world, entity)) 的 (plugin,
            // schema) 对调 plugin.Draw。所有返回 true 的 plugin 全画（**不**
            // 互斥；多 plugin 可同时叠加 overlay）。
            //
            // Plugin Draw 是纯装饰 overlay（c4 设计：DirectionalLight 方向
            // 箭头 / ParticleEmitter spawn box + velocity 向量），不参与
            // gizmoActive 判定——Inspector 仍是字段编辑唯一入口，plugin
            // 不接管 LMB / picking 不被 gate。
            //
            // 调用约定见 plugin/IEditorGizmoPlugin.h 头注释"调用约定"段第 1
            // 条："对 selected entity 遍历 host.gizmoPlugins 调 CanHandle，
            // 对所有返回 true 的 plugin 依次调 Draw"。
            if (mHost.gizmo.visible && mHost.scene.playState == PlayState::Edit && mHost.selection.selectedEntity.IsValid() && !mHost.gizmoPlugins.empty())
            {
                const auto      cam      = BuildEditorCamera(mHost.camera, aspect);
                const glm::mat4 viewProj = cam.projection * cam.view;

                Orange::Editor::Plugin::GizmoContext ctx{};
                ctx.viewProj    = viewProj;
                ctx.imageOrigin = imageOrigin;
                ctx.imageSize   = imageSize;
                ctx.drawList    = ImGui::GetWindowDrawList();

                auto& reg = Orange::Editor::Schema::ComponentSchemaRegistry::Instance();
                for (auto& pPlugin : mHost.gizmoPlugins)
                {
                    if (pPlugin == nullptr)
                    {
                        continue;
                    }
                    for (const auto& schema : reg.All())
                    {
                        if (!pPlugin->CanHandle(schema))
                        {
                            continue;
                        }
                        if (schema.has == nullptr || schema.get == nullptr)
                        {
                            continue;
                        }
                        if (!schema.has(*mHost.scene.pWorld,
                                        mHost.selection.selectedEntity))
                        {
                            continue;
                        }
                        void* component = schema.get(*mHost.scene.pWorld,
                                                     mHost.selection.selectedEntity);
                        if (component == nullptr)
                        {
                            continue;
                        }
                        pPlugin->Draw(mHost, mHost.selection.selectedEntity,
                                      schema, component, ctx);
                    }
                }
            }

            // viewport picking —— LMB 释放且累积 drag 距离 < 阈值 → 视为
            // 单击（区分于轨道相机拖动）。屏幕坐标 → image-local → NDC
            // ([-1,1]) → world ray → ECS hit-test → 命中实体写
            // EditorSelection.selectedEntity。Entity Tree 走既有 selection
            // 联动机制自动高亮，无需双向写。
            //
            // 阈值 4px：Cocos / Unreal 内典型 click 容差。drag 大于阈值的
            // 释放视为相机轨道旋转结束，不触发 picking。
            //
            // 命中失败（点空白）= Entity::Invalid → 清当前选中，与
            // Cocos / Unity / Unreal 工业惯例一致。
            //
            // gizmoActive gate：本帧 gizmo 处理了 LMB（hover handle 或正在
            // 拖动）→ 跳过 picking。否则 gizmo 拖动结束时的 LMB-release 会
            // 同时触发 picking，把选中实体改成 gizmo 下方的物体，破坏 UX。
            if (!colliderEditing && !gizmoActive && ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                const ImVec2    drag              = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
                constexpr float kClickThresholdPx = 4.0f;
                if (drag.x * drag.x + drag.y * drag.y <= kClickThresholdPx * kClickThresholdPx)
                {
                    const ImVec2 mousePos = ImGui::GetMousePos();
                    const float  lx       = mousePos.x - itemMin.x;
                    const float  ly       = mousePos.y - itemMin.y;
                    // 屏幕坐标 → NDC：Vulkan NDC y-down 与 ImGui 屏幕 y-down
                    // 同向，不再额外翻转
                    const float                  ndcX = (lx / static_cast<float>(panelW)) * 2.0f - 1.0f;
                    const float                  ndcY = (ly / static_cast<float>(panelH)) * 2.0f - 1.0f;
                    const Orange::Engine::Entity picked =
                        PickEntityAt(mHost, glm::vec2(ndcX, ndcY), aspect);
                    // 锁定实体不可 pick 选中（防误编辑，hierarchy gap §3 P1）：
                    // 点到锁定实体 = no-op（保持当前选择，下面 select/toggle 跳过）；
                    // 点空白仍正常清空。
                    const bool pickedLocked = picked.IsValid() && IsEntityLocked(picked);
                    // 视口多选（gap 报告 §3 P0）：Ctrl+点**另一个**实体 = 加入 /
                    // 移出 additional set（与 Entity Tree Ctrl-toggle 同款
                    // ToggleAdditional，gizmo/Inspector 仍只作用 primary，与既有
                    // 多选语义一致）；普通单击 = 切 primary + 清 additional（点空白
                    // = 全清）；Ctrl+空白 / Ctrl+当前 primary = 保持不变（no-op）。
                    const ImGuiIO& pickIo = ImGui::GetIO();
                    if (pickIo.KeyCtrl && picked.IsValid() && !pickedLocked && mHost.selection.selectedEntity.IsValid() && picked != mHost.selection.selectedEntity)
                    {
                        mHost.selection.ToggleAdditional(picked);
                    }
                    else if (!pickIo.KeyCtrl && !pickedLocked)
                    {
                        mHost.selection.selectedEntity = picked;
                        mHost.selection.ClearAdditional();
                        // 切 primary → Euler 编辑缓存失效（与 Quat case 一致）。
                        mHost.selection.transformEulerCacheEntity =
                            Orange::Engine::Entity::Invalid();
                        // B3 修：选了 valid 实体 → 清 asset 选中，让 Inspector 从
                        // Material 子模式切回实体模式（互斥选择，匹配 Cocos/Unity
                        // 惯例：viewport 点选总是把焦点拉回实体 Inspector）。
                        if (picked.IsValid())
                        {
                            mHost.assets.selectedAssetPath.clear();
                        }
                    }
                }
            }
        }
    }

    if (!drewImage)
    {
        ImGui::TextDisabled("scene viewport 未就绪 —— 面板太小或 Pipeline 初始化失败");
        const auto& ec = mHost.camera;
        ImGui::Text("viewport %.0fx%.0f  aspect=%.2f", region.x, region.y, aspect);
        ImGui::Text("camera pivot=(%.2f, %.2f, %.2f)  az=%.2f  el=%.2f  r=%.2f",
                    ec.pivot.x, ec.pivot.y, ec.pivot.z,
                    ec.azimuth, ec.elevation, ec.radius);
        ImGui::TextDisabled("LMB 拖动 旋转  滚轮 缩放");
    }

    ImGui::End();
}

// 创建 Scene 面板 ImGui::Image 用的 VkSampler。失败仅 log，DrawScenePanel
// 后续会让该面板退化为占位文案。
void EditorRenderLayer::CreateScenePanelSampler()
{
    auto* pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        Orange::Renderer::Interop::GetVulkanGetInstanceProcAddr());
    if (pfnGetInstanceProcAddr == nullptr)
    {
        return;
    }
    const auto handles = Orange::Renderer::Interop::GetVulkanDeviceHandles(mRenderDevice);
    if (handles.vkInstance == nullptr || handles.vkDevice == nullptr)
    {
        return;
    }
    auto pfnGetDeviceProcAddrFn = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        pfnGetInstanceProcAddr(static_cast<VkInstance>(handles.vkInstance), "vkGetDeviceProcAddr"));
    if (pfnGetDeviceProcAddrFn == nullptr)
    {
        return;
    }
    auto pfnCreate = reinterpret_cast<PFN_vkCreateSampler>(
        pfnGetDeviceProcAddrFn(static_cast<VkDevice>(handles.vkDevice), "vkCreateSampler"));
    if (pfnCreate == nullptr)
    {
        return;
    }

    VkSamplerCreateInfo s{};
    s.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    s.magFilter    = VK_FILTER_LINEAR;
    s.minFilter    = VK_FILTER_LINEAR;
    s.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.minLod       = 0.0f;
    s.maxLod       = 0.0f;
    if (pfnCreate(static_cast<VkDevice>(handles.vkDevice), &s, nullptr, &mSceneSampler) != VK_SUCCESS)
    {
        mSceneSampler = VK_NULL_HANDLE;
        ORANGE_LOG_ERROR("[OrangeEditor] vkCreateSampler (scene panel) 失败");
    }
}

void EditorRenderLayer::DestroyScenePanelSampler()
{
    if (mSceneSampler == VK_NULL_HANDLE)
    {
        return;
    }
    auto* pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        Orange::Renderer::Interop::GetVulkanGetInstanceProcAddr());
    if (pfnGetInstanceProcAddr == nullptr)
    {
        return;
    }
    const auto handles = Orange::Renderer::Interop::GetVulkanDeviceHandles(mRenderDevice);
    if (handles.vkDevice == nullptr)
    {
        return;
    }
    auto pfnGetDeviceProcAddrFn = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        pfnGetInstanceProcAddr(static_cast<VkInstance>(handles.vkInstance), "vkGetDeviceProcAddr"));
    if (pfnGetDeviceProcAddrFn == nullptr)
    {
        return;
    }
    auto pfnDestroy = reinterpret_cast<PFN_vkDestroySampler>(
        pfnGetDeviceProcAddrFn(static_cast<VkDevice>(handles.vkDevice), "vkDestroySampler"));
    if (pfnDestroy != nullptr)
    {
        pfnDestroy(static_cast<VkDevice>(handles.vkDevice), mSceneSampler, nullptr);
    }
    mSceneSampler = VK_NULL_HANDLE;
}

// 按当前 Scene 面板的 content region 尺寸初始化 / 重建 Pipeline 的
// off-screen RT。返回 true 时 mpScenePipeline 可用、mSceneDescSet 已绑
// 当帧 viewportColor 的 VkImageView。
//
// 行为：
//   * 首次调用且 w/h > 0：lazy 创建 Pipeline + InitializeOffscreen + 走
//     一次 Render + AddTexture → 拿到 descriptor set；
//   * 后续帧 w/h 与上次相同：no-op，复用 mSceneDescSet；
//   * 尺寸变化：ResizeOffscreen + 下一次 Pipeline.Render → view 句柄轮
//     换，先 RemoveTexture(旧) → AddTexture(新)；
//   * 任一步失败：把 mScenePipelineFailed 置 true，后续 DrawScenePanel
//     不再 retry（避免每帧重复输出 error log），UI 退化到占位文案。
bool EditorRenderLayer::EnsureScenePipeline(std::uint32_t width, std::uint32_t height)
{
    if (mScenePipelineFailed)
    {
        return false;
    }
    if (width == 0 || height == 0)
    {
        return false;
    }
    if (mHost.assets.pAssets == nullptr)
    {
        return false;
    }
    if (mSceneSampler == VK_NULL_HANDLE)
    {
        return false;
    }

    // lazy init
    if (mpScenePipeline == nullptr)
    {
        mpScenePipeline = std::make_unique<Orange::Engine::Render::Pipeline>();

        // v1.3.0 中性化：engine 公共 API 默认值是中性的（dummy IBL ambient
        // (0,0,0) + 主 pass clear 深蓝灰 (0.05, 0.07, 0.10)）；编辑器在
        // Initialize 之前显式 override 到 OrangeEditor UX 量级，避免 PBR
        // 暗面全黑 + viewport 与 main panel 灰度撞色。两条公共 API 都接受
        // Initialize 之前 / 之后调，本路径选"之前"让 dummy IBL 在 Initialize
        // 时一次到位（之后调要走运行时 cmd.Begin/End/Submit/WaitIdle 重填，
        // 多一次 GPU stall 浪费）。
        //
        // - dummyIblAmbient (0.5, 0.5, 0.5)：暗面 ~50% baseColor（v1.0.1 c5
        //   从 0.25 提到 0.5 后零基础用户验收通过，避免"cube 暗面像透明"陷阱）
        // - sceneClearColor (0.12, 0.12, 0.13)：Cocos Creator 风中性灰 #5C5C60，
        //   让 viewport 与 main panel 深炭灰拉开一档亮度差便于辨识渲染区
        mpScenePipeline->SetDummyIblAmbient(0.5f, 0.5f, 0.5f);
        mpScenePipeline->SetSceneClearColor(0.12f, 0.12f, 0.13f);

        auto r = mpScenePipeline->InitializeOffscreen(
            mRenderDevice, *mHost.assets.pAssets, width, height);
        if (r.IsErr())
        {
            ORANGE_LOG_ERROR("[OrangeEditor] Pipeline::InitializeOffscreen 失败 (code={}) —— "
                             "Scene 视口退化为占位文案",
                             static_cast<unsigned>(r.Error()));
            mpScenePipeline.reset();
            mScenePipelineFailed = true;
            // 缩略图服务失去 Pipeline：清空注入，pending 不再烘（GetOrRequest
            // 回退文本 icon）。
            if (mHost.thumbnails)
            {
                mHost.thumbnails->SetPipeline(nullptr);
            }
            return false;
        }
        // 默认 PostProcessChain —— 仅 HDR pipeline 必需的 BuiltinPostProcessChain
        // ::CreateDefault()（HDR + Bloom + GodRays(disabled) + Tonemap + LUT 5
        // pass，sample 13_pbr_direct / 14_pbr_ibl 同款）。漏接时视觉症状：
        // emissive 物体硬边 clamp + PBR cube 偏暗（没 tonemap 曲线把 linear
        // 0.3-0.4 提到 ~0.5 显示），所以这一份不能撤。
        //
        // v1.3.2 GAP-2026-05-28-editor-post-process-defaults-too-aggressive：
        // 历史上本块还 hardcode 追加了 GTAO / SSR / ContactShadow / DoF / TAA /
        // ColorGrade 6 个"美术效果"pass，目的是让编辑器视口与 sample
        // 16_light_family_shadows WYSIWYG 一致 —— 但代价是用户没挂任何
        // PostProcessComponent 就已经看到一堆 fancy effect，违反"组件即语义"
        // 的工业惯例（Unity / UE / Godot 编辑器默认均不带这类 pass，要
        // PostProcessVolume / WorldEnvironment 才生效）。本期撤掉这 6 个
        // hardcode，与 Pipeline 内部"无组件 / 无 chain → 0 post"的中性默认对
        // 齐。用户想要这些效果：在场景里挂 PostProcessComponent，组件按
        // SyncPostProcessFromWorld（src/render/Pipeline.cpp:2064）压过 chain。
        mpScenePostProcessChain = std::make_unique<
            Orange::Engine::Render::PostProcessChain>(
            Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault());
        mpScenePipeline->SetPostProcessChain(mpScenePostProcessChain.get());

        // v1.3.3 GAP-2026-05-27-tonemap-operator-selection：缓存 chain 内
        // TonemapPass* 让 Render Settings 面板"Color · Tonemap" 段读写 op
        // 字段。BuiltinPostProcessChain::CreateDefault 的 5-pass 顺序为
        // HDR(0) / Bloom(1) / GodRays(2) / Tonemap(3) / LUT(4)，索引 3
        // 是 TonemapPass；防御性走 dynamic_cast + 失败 silent skip（Pass
        // 顺序变更后 chain 失锁，cast 失败让 UI 段做 null 守卫）。
        mpTonemapPassRef = nullptr;
        for (std::size_t i = 0; i < mpScenePostProcessChain->PassCount(); ++i)
        {
            if (auto* tp = dynamic_cast<Orange::Engine::Render::TonemapPass*>(
                    mpScenePostProcessChain->PassAt(i)))
            {
                mpTonemapPassRef = tp;
                break;
            }
        }

        // PCSS 软阴影 + 2048 阴影图（directional + spot；与 sample 一致）。
        // v1.3.1 GAP-2026-05-28-editor-render-settings-panel：hardcode 撤掉，
        // 编辑器档默认值挂 EditorRenderLayer::mShadowConfig（designated init
        // mapResolution=2048 + pcssLightSize=12，等价历史 hardcode）；本处
        // first-time push 一次让 Pipeline initial shadow target 按编辑器档分
        // 辨率创建。每帧后续 push 在 DrawScenePanel 入口（让 Render Settings
        // 面板编辑立即生效），见下方"viewport toolbar toggle → Pipeline"段。
        mpScenePipeline->SetShadowConfig(mShadowConfig);

        // v1.3.0 grid 真迁出：编辑器自家 EditorGridAuxPassProvider 实现
        // IAuxPassProvider，通过 Pipeline::SetAuxPassProvider 注册到 engine
        // 主 pass 之后的 aux hook 上。Initialize 失败时（shader spv 缺失等）
        // silent skip 注册，让 viewport 仍能起 —— grid 不显示 ≠ 编辑器不可用。
        mpEditorGridProvider = std::make_unique<EditorGridAuxPassProvider>();
        if (mpEditorGridProvider->Initialize(mRenderDevice))
        {
            mpEditorGridProvider->SetEnabled(mHost.settings.viewportGridEnabled);
            mpScenePipeline->SetAuxPassProvider(mpEditorGridProvider.get());
        }
        else
        {
            ORANGE_LOG_WARN("[OrangeEditor] EditorGridAuxPassProvider Initialize 失败 —— "
                            "viewport grid 不可用，编辑器其余功能正常");
            mpEditorGridProvider.reset();
        }

        mScenePanelWidth  = width;
        mScenePanelHeight = height;

        // 缩略图服务复用 viewport 的同一个 Pipeline 实例渲材质球。Pipeline 刚
        // InitializeOffscreen 成功 → 注入；此后每帧 EnsureScenePipeline 走复用
        // 路径不重复 SetPipeline（指针稳定）。FlushPending 由 EditorRenderLayer
        // 在帧外安全点驱动。
        if (mHost.thumbnails)
        {
            mHost.thumbnails->SetPipeline(mpScenePipeline.get());
        }

        // PIE 游戏模块注册期扇出（ADR-021 / M2.2）—— viewport Pipeline 刚
        // InitializeOffscreen 成功，此刻一次性 RegisterRenderPasses 把各模块的
        // 自定义 IRenderPass（典型 SlimeMetaballPass 走 AfterMainPass，靠 M1 离屏
        // InsertPass 接通）常驻插入。Edit 态就注册、pass 无数据时自己 Execute 早退。
        // 原版 OrangeEditor gameModules 空 → no-op。放在 lazy 创建块内只调一次；
        // 后续 resize 走下面复用分支，不重复注册（pass 生命周期随 Pipeline）。
        mHost.gameModules.RegisterRenderPasses(*mpScenePipeline);
    }
    else if (width != mScenePanelWidth || height != mScenePanelHeight)
    {
        mpScenePipeline->ResizeOffscreen(width, height);
        mScenePanelWidth  = width;
        mScenePanelHeight = height;
        // 旧 viewportColor view 在下一次 Pipeline.Render 内重建后失效，
        // descriptor set 也要相应失效；标记下来，等本帧 Render 后重绑。
        mSceneDescSetDirty = true;
    }

    return true;
}

// 在 EnsureScenePipeline + Pipeline.Render 之后调，确保 mSceneDescSet 指
// 向最新 viewportColor view。首帧或 resize 后会经 RemoveTexture(旧) +
// AddTexture(新) 路径轮换；其余情况复用。
void EditorRenderLayer::RebindSceneDescriptorSetIfNeeded()
{
    if (mpScenePipeline == nullptr)
    {
        return;
    }
    if (mSceneSampler == VK_NULL_HANDLE)
    {
        return;
    }

    const auto* tex = mpScenePipeline->GetOffscreenColor();
    if (tex == nullptr)
    {
        return;
    }

    if (mSceneDescSet != VK_NULL_HANDLE && !mSceneDescSetDirty)
    {
        return; // 复用
    }

    // 旧 set 释放 —— Pipeline.RenderOffscreen 末尾的 WaitIdle 已经把上
    // 一帧 ImGui 采样旧 view 的 GPU 工作排空，free 安全。
    if (mSceneDescSet != VK_NULL_HANDLE)
    {
        ImGui_ImplVulkan_RemoveTexture(mSceneDescSet);
        mSceneDescSet = VK_NULL_HANDLE;
    }

    auto* rawView = Orange::Renderer::Interop::GetVulkanImageView(*tex);
    if (rawView == nullptr)
    {
        return;
    }
    mSceneDescSet = ImGui_ImplVulkan_AddTexture(
        mSceneSampler,
        static_cast<VkImageView>(rawView),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    mSceneDescSetDirty = false;
}

// ===========================================================================
// M9.3 Game 面板（第二离屏 pipeline，从 World 的 Camera 组件出画）
// ===========================================================================

// 按 Game 面板 content region 尺寸初始化 / 重建第二个离屏 pipeline。与
// EnsureScenePipeline 平行，差异：**不**挂 EditorGridAuxPassProvider（Game 视口
// 无编辑器网格）、**不**注入 mHost.thumbnails（缩略图只走 scene pipeline）。
// 返回 true 时 mpGamePipeline 可用。
bool EditorRenderLayer::EnsureGamePipeline(std::uint32_t width, std::uint32_t height)
{
    if (mGamePipelineFailed)
    {
        return false;
    }
    if (width == 0 || height == 0)
    {
        return false;
    }
    if (mHost.assets.pAssets == nullptr)
    {
        return false;
    }
    if (mSceneSampler == VK_NULL_HANDLE)
    {
        return false;
    }

    // lazy init
    if (mpGamePipeline == nullptr)
    {
        mpGamePipeline = std::make_unique<Orange::Engine::Render::Pipeline>();

        // 与 scene pipeline 同款中性化 override（暗面 ~50% baseColor + 中性灰
        // clear），Initialize 之前一次到位（之后调要走 cmd 重填多一次 GPU stall）。
        mpGamePipeline->SetDummyIblAmbient(0.5f, 0.5f, 0.5f);
        mpGamePipeline->SetSceneClearColor(0.12f, 0.12f, 0.13f);

        auto r = mpGamePipeline->InitializeOffscreen(
            mRenderDevice, *mHost.assets.pAssets, width, height);
        if (r.IsErr())
        {
            ORANGE_LOG_ERROR("[OrangeEditor] Pipeline::InitializeOffscreen 失败 (code={}) —— "
                             "Game 视口退化为占位文案",
                             static_cast<unsigned>(r.Error()));
            mpGamePipeline.reset();
            mGamePipelineFailed = true;
            return false;
        }

        // 默认 PostProcessChain（HDR + Bloom + GodRays(disabled) + Tonemap + LUT），
        // 与 scene 同款——漏接会让 emissive 硬 clamp + PBR 偏暗。
        mpGamePostProcessChain = std::make_unique<
            Orange::Engine::Render::PostProcessChain>(
            Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault());
        mpGamePipeline->SetPostProcessChain(mpGamePostProcessChain.get());

        // 首帧 push 一次编辑器档 shadow 配置，让 initial shadow target 按分辨率建。
        mpGamePipeline->SetShadowConfig(mShadowConfig);

        // Game 视口刻意**不**挂 EditorGridAuxPassProvider（无编辑器网格），
        // 也**不**注入 mHost.thumbnails（缩略图只用 scene pipeline）。

        mGamePanelWidth  = width;
        mGamePanelHeight = height;

        // PIE 游戏模块 pass 注册（ADR-021 / M2.2）—— 与 scene pipeline 同款，
        // 让 SlimeMetaballPass 等自定义 pass 在 Game 视口也渲染。lazy 块内只调
        // 一次，后续 resize 走下面复用分支不重复注册（pass 生命周期随 Pipeline）。
        mHost.gameModules.RegisterRenderPasses(*mpGamePipeline);
    }
    else if (width != mGamePanelWidth || height != mGamePanelHeight)
    {
        mpGamePipeline->ResizeOffscreen(width, height);
        mGamePanelWidth  = width;
        mGamePanelHeight = height;
        // 旧 viewportColor view 在下一次 Pipeline.Render 内重建后失效，
        // descriptor set 也要相应失效；标记下来，等本帧 Render 后重绑。
        mGameDescSetDirty = true;
    }

    return true;
}

// 与 RebindSceneDescriptorSetIfNeeded 平行：在 EnsureGamePipeline + Pipeline.Render
// 之后调，确保 mGameDescSet 指向最新 game viewportColor view，复用 mSceneSampler。
void EditorRenderLayer::RebindGameDescriptorSetIfNeeded()
{
    if (mpGamePipeline == nullptr)
    {
        return;
    }
    if (mSceneSampler == VK_NULL_HANDLE)
    {
        return;
    }

    const auto* tex = mpGamePipeline->GetOffscreenColor();
    if (tex == nullptr)
    {
        return;
    }

    if (mGameDescSet != VK_NULL_HANDLE && !mGameDescSetDirty)
    {
        return; // 复用
    }

    // 旧 set 释放 —— Pipeline.RenderOffscreen 末尾 WaitIdle 已把上一帧 ImGui
    // 采样旧 view 的 GPU 工作排空，free 安全。
    if (mGameDescSet != VK_NULL_HANDLE)
    {
        ImGui_ImplVulkan_RemoveTexture(mGameDescSet);
        mGameDescSet = VK_NULL_HANDLE;
    }

    auto* rawView = Orange::Renderer::Interop::GetVulkanImageView(*tex);
    if (rawView == nullptr)
    {
        return;
    }
    mGameDescSet = ImGui_ImplVulkan_AddTexture(
        mSceneSampler,
        static_cast<VkImageView>(rawView),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    mGameDescSetDirty = false;
}

// Game 面板：从 World 的 Camera 组件（游戏相机）渲染同一 World。纯展示——
// 无工具栏 / gizmo / picking / DnD。Scene 面板自由飞观察，本面板看真实游戏机位。
void EditorRenderLayer::DrawGamePanel()
{
    ImGui::Begin("Game");

    ImGui::TextDisabled("游戏相机视图（World 的 Camera 组件）");

    // World 是否有游戏相机 —— 首个挂 Camera 组件的实体即主相机（见
    // RenderScene::Collect 用 view<Camera>().front()）。无相机时 Pipeline
    // HasCamera()==false → 把离屏 color 清黑，渲染仍安全，只是画面为空。
    const bool hasGameCam =
        mHost.scene.pWorld != nullptr &&
        !mHost.scene.pWorld->Registry().view<Orange::Engine::Render::Camera>().empty();
    if (!hasGameCam)
    {
        ImGui::TextColored(
            Orange::Editor::Theme::Color::GetAlertWarn(),
            "场景无 Camera 组件 —— Game 视图为空。给一个实体加 Camera 组件即为游戏相机。");
    }

    const ImVec2        region = ImGui::GetContentRegionAvail();
    const std::uint32_t panelW =
        (region.x > 1.0f) ? static_cast<std::uint32_t>(region.x) : 0u;
    const std::uint32_t panelH =
        (region.y > 1.0f) ? static_cast<std::uint32_t>(region.y) : 0u;

    if (EnsureGamePipeline(panelW, panelH) && mHost.scene.pWorld != nullptr)
    {
        // 每帧 wire partition（同 scene，非拥有指针与 EditorSceneContext 同生命周期）。
        mpGamePipeline->SetWorldPartition(&mHost.scene.partition);
        // Game 视口不覆写相机 → 用 World 的 Camera 组件（游戏相机）。
        mpGamePipeline->SetEditorCameraOverride(nullptr);
        // sky / shadow 与 scene 保持一致；跳过 debugview / grid / debugdraw /
        // colliders —— 那些是编辑器 overlay，Game 视口是游戏画面。
        mpGamePipeline->SetSkyEnabled(mHost.settings.viewportSkyEnabled);
        mpGamePipeline->SetShadowConfig(mShadowConfig);

        // Pipeline.Render 内部 WaitIdle —— 返回时 GPU 已空，RemoveTexture(旧) +
        // AddTexture(新) 才安全。
        mpGamePipeline->Render(*mHost.scene.pWorld);
        RebindGameDescriptorSetIfNeeded();
        if (mGameDescSet != VK_NULL_HANDLE)
        {
            ImGui::Image(reinterpret_cast<ImTextureID>(mGameDescSet),
                         ImVec2(static_cast<float>(panelW),
                                static_cast<float>(panelH)));
        }
    }

    ImGui::End();
}
