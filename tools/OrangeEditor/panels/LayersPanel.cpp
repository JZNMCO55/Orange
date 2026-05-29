// Layers 面板 —— layer manifest 编辑 UI（v0.6 c5）。
//
// 功能：
//   * 顶部 [+] 按钮新增 layer（弹小 popup 输入 id + displayName）
//   * 列出每条 layer：visibility checkbox / id 文本 / [X] 删除按钮
//   * default layer：visibility 仍可切（hide default 的合法场景：临时
//     只显示某一 work-in-progress layer），但 [X] disabled —— 与
//     WorldPartition::RemoveLayer("default") 拒绝删除的行为对齐
//   * 所有 mutate 走 cmdStack（Add/SetVisible 可 Undo；Remove 由于会
//     重写 N 个 entity 的 LayerComponent，按 EntityTreePanel
//     pendingDelete 同款约定：直接 mutate + Clear cmdStack）
//
// per-layer entity count 显示：2026-05-29 落地（[N] chip，gap 报告 §2.7 Layer 子项）。
// 仍不在范围（后续）：
//   * 重命名 layer（displayName 编辑）—— 需 per-layer rename 输入状态
//   * 拖动调整 layer 顺序 —— 需 WorldPartition 加 reorder 方法（引擎侧）
//   * Drag & drop entity → layer chip 一步改归属
//
// 设计参考：
//   * vendor/LumixEngine/src/editor/world_editor.cpp 的 layers UI
//   * vendor/godot/editor/scene_tree_editor.cpp 同款"右键 Move to..." 模式

#include "../EditorRenderLayer.h"

#include "../command/LambdaCommand.h"
#include "../theme/EditorTheme.h"

#include <orange/engine/core/Log.h>
#include <orange/engine/scene/LayerComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldPartition.h>

#include <entt/entity/registry.hpp>

#include <imgui.h>

#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{

// 默认 layer id —— 与 WorldPartition::DefaultLayerId() 同款常量；放在本
// TU 内 fileScope 避免每次 hit 都做 string_view 比较的不必要分配。
constexpr std::string_view kDefaultLayerId = "default";

// 删 layer 时把所有挂这个 layer 的 entity 归到 default（与 partition
// RemoveLayer 接口注释里 "RemoveLayer 不动 World 内任何 LayerComponent，
// 调用方若需让被删 layer 的 entity 改归 default，应在调用后自行遍历
// World 重写" 的契约对应）。返回写改了几个 entity，便于 log。
std::size_t ReparentEntitiesToDefault(Orange::Engine::World&                   world,
                                      Orange::Engine::Scene::WorldPartition&   partition,
                                      std::string_view                         removedLayerId)
{
    using LC = Orange::Engine::Scene::LayerComponent;
    std::size_t rewritten = 0;
    auto& reg = world.Registry();
    for (auto e : reg.view<LC>()) {
        auto& lc = reg.get<LC>(e);
        if (lc.layerId == removedLayerId) {
            partition.SetLayerOf(world, Orange::Engine::World::FromEntt(e),
                                 Orange::Engine::Scene::WorldPartition::DefaultLayerId());
            ++rewritten;
        }
    }
    return rewritten;
}

}  // namespace

void EditorRenderLayer::DrawLayersPanel()
{
    ImGui::Begin("Layers");
    if (mHost.scene.pWorld == nullptr) {
        ImGui::TextDisabled("(no world bound)");
        ImGui::End();
        return;
    }

    // canEdit 与 EntityTreePanel 同款 —— Play / Paused 期间禁止结构性
    // 编辑，避免 simulation 中途 mutate partition 让 ApplyLayerVisibility
    // 撞到旧 manifest。
    const bool canEdit = (mHost.scene.playState == PlayState::Edit);

    // ---- 顶部工具栏：[+ icon] Add Layer ----
    ImGui::BeginDisabled(!canEdit);
    const std::string addLayerLabel =
        std::string(Orange::Editor::Theme::Icon::GetAdd()) + " Add Layer";
    if (ImGui::Button(addLayerLabel.c_str())) {
        ImGui::OpenPopup("##add_layer_popup");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu layers", mHost.scene.partition.LayerCount());

    if (ImGui::BeginPopup("##add_layer_popup")) {
        static char idBuf[64]   = "";
        static char nameBuf[64] = "";
        ImGui::TextUnformatted("New layer");
        ImGui::Separator();
        // 控件宽度按"最长 hint 文本宽度 + 一段缓冲"派生 —— 与
        // editor-roadmap v0.4.5 红线 "禁止字面量像素列宽" 对齐。
        // hint "e.g. background" ~ 110px on Segoe UI 16；CalcTextSize
        // 自带 DPI scale 兜底，多机器一致。
        const float inputWidth = ImGui::CalcTextSize("e.g. background").x
                               + ImGui::GetStyle().FramePadding.x * 6.0f;
        ImGui::SetNextItemWidth(inputWidth);
        ImGui::InputTextWithHint("id",          "e.g. background", idBuf,   sizeof(idBuf));
        ImGui::SetNextItemWidth(inputWidth);
        ImGui::InputTextWithHint("displayName", "(optional)",      nameBuf, sizeof(nameBuf));
        const bool idEmpty     = (idBuf[0] == '\0');
        const bool idCollision = !idEmpty && mHost.scene.partition.HasLayer(idBuf);
        if (idCollision) {
            // v0.6.5 c7：错误提示用 EditorTheme AlertError 红 token
            // 替换字面量 RGBA（与 lint editor-literal-rgba 规则对齐）。
            ImGui::TextColored(Orange::Editor::Theme::Color::GetAlertError(),
                               "id '%s' already exists", idBuf);
        }
        ImGui::BeginDisabled(idEmpty || idCollision);
        if (ImGui::Button("Create")) {
            Orange::Engine::Scene::LayerInfo info;
            info.id          = idBuf;
            info.displayName = (nameBuf[0] != '\0') ? std::string{nameBuf} : std::string{idBuf};
            info.visible     = true;

            auto* pH = &mHost;
            mHost.cmdStack.Push(std::make_unique<LambdaCommand>(
                "add_layer",
                [pH, info]() { pH->scene.partition.AddLayer(info); },
                [pH, idStr = info.id]() { pH->scene.partition.RemoveLayer(idStr); }
            ));

            idBuf[0]   = '\0';
            nameBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            idBuf[0]   = '\0';
            nameBuf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // ---- Layer 列表 ----
    // GetLayers() 返回 const& 到 manifest 内部 vector；遍历期间不能调
    // RemoveLayer 否则迭代器失效 —— 用 pendingRemoveId 延迟到帧末执行。
    std::string pendingRemoveId;

    // per-layer entity count（gap 报告 §2.7 Layer 子项）：按 LayerComponent.layerId
    // 计数（持有该组件的实体）。每帧重算（实体数通常不大，O(N) 可忽略）。
    std::unordered_map<std::string, std::size_t> layerCounts;
    {
        using LC  = Orange::Engine::Scene::LayerComponent;
        auto& reg = mHost.scene.pWorld->Registry();
        for (auto e : reg.view<LC>()) {
            ++layerCounts[reg.get<LC>(e).layerId];
        }
    }

    const auto& layers = mHost.scene.partition.GetLayers();
    for (const auto& layer : layers) {
        ImGui::PushID(layer.id.c_str());

        // visibility checkbox：勾掉 → SetLayerVisible(false)。允许 default
        // 也切（隐藏 default 是合法的"只看某一 work-in-progress layer"用法）。
        bool visible = layer.visible;
        ImGui::BeginDisabled(!canEdit);
        if (ImGui::Checkbox("##vis", &visible)) {
            const bool prev = layer.visible;
            const std::string idStr = layer.id;
            auto* pH = &mHost;
            mHost.cmdStack.Push(std::make_unique<LambdaCommand>(
                "set_layer_visible",
                [pH, idStr, visible]() {
                    pH->scene.partition.SetLayerVisible(idStr, visible);
                },
                [pH, idStr, prev]() {
                    pH->scene.partition.SetLayerVisible(idStr, prev);
                }
            ));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        // displayName 优先；空则显示 id。后跟灰色的 id 让 user 知道实际
        // partition lookup 用的 key。
        if (!layer.displayName.empty() && layer.displayName != layer.id) {
            ImGui::Text("%s", layer.displayName.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", layer.id.c_str());
        } else {
            ImGui::Text("%s", layer.id.c_str());
        }

        // entity count chip —— 该 layer 下挂 LayerComponent 的实体数。
        ImGui::SameLine();
        const auto cit = layerCounts.find(layer.id);
        ImGui::TextDisabled("[%zu]",
                            cit != layerCounts.end() ? cit->second : std::size_t{0});

        // [×] 删除按钮（Codicons CLOSE）—— default 禁掉。
        const bool isDefault = (layer.id == kDefaultLayerId);
        const char* closeIcon = Orange::Editor::Theme::Icon::GetClose();
        const float buttonW   = ImGui::CalcTextSize(closeIcon).x
                              + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - buttonW);
        ImGui::BeginDisabled(!canEdit || isDefault);
        if (ImGui::SmallButton(closeIcon)) {
            pendingRemoveId = layer.id;
        }
        ImGui::EndDisabled();
        if (isDefault && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("default layer 不可删除");
        }

        ImGui::PopID();
    }

    ImGui::End();

    // 帧末执行删除 —— 与 EntityTreePanel pendingDelete 同款。Remove 会
    // 重写多个 entity 的 LayerComponent + RemoveLayer manifest，不走
    // cmdStack（实现成本不成比例；用户期望也接受"删 layer 不可 Undo，
    // 与 DestroySubtree 同档"），同步 Clear cmdStack 让旧命令不悬挂。
    if (!pendingRemoveId.empty()) {
        const std::size_t rewritten = ReparentEntitiesToDefault(
            *mHost.scene.pWorld, mHost.scene.partition, pendingRemoveId);
        if (mHost.scene.partition.RemoveLayer(pendingRemoveId)) {
            ORANGE_LOG_INFO("[OrangeEditor] removed layer '{}' ({} entities moved to default)",
                            pendingRemoveId, rewritten);
            mHost.cmdStack.Clear();
            mHost.scene.dirty = true;
        }
    }
}
