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
// 2026-05-29 落地（gap 报告 §2.7 Layer 子项）：
//   * per-layer entity count 显示（名后 [N] chip）
//   * 重命名 layer（双击名进 InputText 编辑 displayName，走 cmdStack 可 Undo）
//   * 拖动调整 layer 顺序（[↑][↓] 按钮 → WorldPartition::MoveLayer，可 Undo）
// 仍不在范围（后续）：
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

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace
{

// 默认 layer id —— 与 WorldPartition::DefaultLayerId() 同款常量；放在本
// TU 内 fileScope 避免每次 hit 都做 string_view 比较的不必要分配。
constexpr std::string_view kDefaultLayerId = "default";

// 重命名 layer 的 in-flight 状态（per-panel file-scope；同时只能 rename
// 一条）。空串 = 当前没在重命名；双击某 layer 名进入，Enter / 失焦提交、
// Esc 取消。sRenameFocusPending 让进入重命名的首帧把键盘焦点打到 InputText。
std::string sRenamingLayerId;
char        sRenameBuf[64]      = "";
bool        sRenameFocusPending = false;

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
    // reorder 同理延迟：MoveLayer 会 rotate mLayers，迭代途中改它会让
    // range-for 迭代器失效。pendingMoveDelta = -1 上移 / +1 下移。
    std::string pendingMoveId;
    int         pendingMoveDelta = 0;
    // DnD：从 Entity Tree 拖实体落到 layer 名 → 改该实体归属本 layer。延迟到
    // 帧末经 cmdStack 执行（可 Undo），与 reorder/remove 同款"先记下、帧末做"。
    Orange::Engine::Entity pendingAssignEntity = Orange::Engine::Entity::Invalid();
    std::string            pendingAssignLayer;

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
    std::size_t layerIdx = 0;
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
        // partition lookup 用的 key。双击名字进入重命名（编辑 displayName，
        // 不动 id —— id 是 LayerComponent 引用的稳定 key）。
        const bool renamingThis = (sRenamingLayerId == layer.id);
        if (renamingThis) {
            if (sRenameFocusPending) {
                ImGui::SetKeyboardFocusHere();
                sRenameFocusPending = false;
            }
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("MMMMMMMMMMMMMMMM").x);
            const bool entered = ImGui::InputText(
                "##rename_layer", sRenameBuf, sizeof(sRenameBuf),
                ImGuiInputTextFlags_EnterReturnsTrue);
            const bool deactivated = ImGui::IsItemDeactivated();
            // Esc 优先：ImGui 此时已把 buffer 还原到进入时的值，直接取消不提交。
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                sRenamingLayerId.clear();
            } else if (entered || deactivated) {
                const std::string newName = sRenameBuf;
                const std::string oldName = layer.displayName;
                if (!newName.empty() && newName != oldName) {
                    const std::string idStr = layer.id;
                    auto* pH = &mHost;
                    mHost.cmdStack.Push(std::make_unique<LambdaCommand>(
                        "rename_layer",
                        [pH, idStr, newName]() {
                            if (auto* L = pH->scene.partition.GetLayer(idStr)) {
                                L->displayName = newName;
                            }
                        },
                        [pH, idStr, oldName]() {
                            if (auto* L = pH->scene.partition.GetLayer(idStr)) {
                                L->displayName = oldName;
                            }
                        }
                    ));
                }
                sRenamingLayerId.clear();
            }
        } else {
            if (!layer.displayName.empty() && layer.displayName != layer.id) {
                ImGui::Text("%s", layer.displayName.c_str());
            } else {
                ImGui::Text("%s", layer.id.c_str());
            }
            // 双击名字 → 进入重命名（用 displayName 作种子，空则用 id）。
            if (canEdit && ImGui::IsItemHovered()
                && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                sRenamingLayerId    = layer.id;
                sRenameFocusPending = true;
                const std::string& seed =
                    !layer.displayName.empty() ? layer.displayName : layer.id;
                std::snprintf(sRenameBuf, sizeof(sRenameBuf), "%s", seed.c_str());
            }
            // DnD drop target：锚在刚画的名字 Text（带 label 的中行 item，
            // 非行尾 chip——避开本仓 trailing-chip anchor-bug 前科）。接受
            // Entity Tree 的 kEntityPayload，帧末改该实体归属本 layer。
            if (canEdit && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl =
                        ImGui::AcceptDragDropPayload(kEntityPayload)) {
                    if (pl->Data != nullptr
                        && pl->DataSize == static_cast<int>(sizeof(Orange::Engine::Entity))) {
                        Orange::Engine::Entity dropped{};
                        std::memcpy(&dropped, pl->Data, sizeof(dropped));
                        pendingAssignEntity = dropped;
                        pendingAssignLayer  = layer.id;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (!layer.displayName.empty() && layer.displayName != layer.id) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", layer.id.c_str());
            }
        }

        // entity count chip —— 该 layer 下挂 LayerComponent 的实体数。
        ImGui::SameLine();
        const auto cit = layerCounts.find(layer.id);
        ImGui::TextDisabled("[%zu]",
                            cit != layerCounts.end() ? cit->second : std::size_t{0});

        // ---- 右对齐按钮簇：[↑ 上移] [↓ 下移] [× 删除] ----
        // 边界禁用：第一条无法上移、最后一条无法下移；default 不可删。
        const bool  isDefault = (layer.id == kDefaultLayerId);
        const bool  isFirst   = (layerIdx == 0);
        const bool  isLast    = (layerIdx + 1 == layers.size());
        const char* upIcon    = Orange::Editor::Theme::Icon::GetArrowUp();
        const char* downIcon  = Orange::Editor::Theme::Icon::GetArrowDown();
        const char* closeIcon = Orange::Editor::Theme::Icon::GetClose();
        const float pad        = ImGui::GetStyle().FramePadding.x * 2.0f;
        const float spacing    = ImGui::GetStyle().ItemSpacing.x;
        const float clusterW   = ImGui::CalcTextSize(upIcon).x + pad
                               + ImGui::CalcTextSize(downIcon).x + pad
                               + ImGui::CalcTextSize(closeIcon).x + pad
                               + spacing * 2.0f;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - clusterW);

        ImGui::BeginDisabled(!canEdit || isFirst);
        if (ImGui::SmallButton(upIcon)) {
            pendingMoveId    = layer.id;
            pendingMoveDelta = -1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        ImGui::BeginDisabled(!canEdit || isLast);
        if (ImGui::SmallButton(downIcon)) {
            pendingMoveId    = layer.id;
            pendingMoveDelta = +1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        ImGui::BeginDisabled(!canEdit || isDefault);
        if (ImGui::SmallButton(closeIcon)) {
            pendingRemoveId = layer.id;
        }
        ImGui::EndDisabled();
        if (isDefault && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("default layer 不可删除");
        }

        ImGui::PopID();
        ++layerIdx;
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

    // 帧末执行 reorder —— 走 cmdStack 可 Undo（纯顺序变更，逆操作 = 反向
    // MoveLayer）。上/下按钮只发 delta = ±1 且边界已禁用，所以 forward 必
    // 然成功移动 1 步、undo 反移 1 步精确还原。dirty 由 cmdStack onChanged
    // 钩子自动置（layer 顺序进 manifest 序列化）。
    if (!pendingMoveId.empty() && pendingMoveDelta != 0) {
        auto*             pH    = &mHost;
        const std::string idStr = pendingMoveId;
        const int         delta = pendingMoveDelta;
        mHost.cmdStack.Push(std::make_unique<LambdaCommand>(
            "move_layer",
            [pH, idStr, delta]() { pH->scene.partition.MoveLayer(idStr, delta); },
            [pH, idStr, delta]() { pH->scene.partition.MoveLayer(idStr, -delta); }
        ));
    }

    // 帧末执行 DnD 改归属 —— 走 cmdStack 可 Undo（逆操作 = SetLayerOf 回旧
    // layer）。SetLayerOf 改的是 entity 的 LayerComponent，不动 mLayers，故不
    // 撞 layer 列表迭代；延迟仍为统一"UI 期间不 mutate world"风格。lambda 内
    // 守 pWorld 非空（scene swap 后 Clear 不会让旧命令悬挂，仍防御）。
    if (pendingAssignEntity.IsValid() && !pendingAssignLayer.empty()) {
        auto& world = *mHost.scene.pWorld;
        if (world.IsValid(pendingAssignEntity)) {
            const std::string oldLayer{mHost.scene.partition.GetLayerOf(world, pendingAssignEntity)};
            const std::string newLayer = pendingAssignLayer;
            if (oldLayer != newLayer) {
                auto*                        pH  = &mHost;
                const Orange::Engine::Entity ent = pendingAssignEntity;
                mHost.cmdStack.Push(std::make_unique<LambdaCommand>(
                    "assign_layer",
                    [pH, ent, newLayer]() {
                        if (pH->scene.pWorld) {
                            pH->scene.partition.SetLayerOf(*pH->scene.pWorld, ent, newLayer);
                        }
                    },
                    [pH, ent, oldLayer]() {
                        if (pH->scene.pWorld) {
                            pH->scene.partition.SetLayerOf(*pH->scene.pWorld, ent, oldLayer);
                        }
                    }
                ));
                ORANGE_LOG_INFO("[OrangeEditor] entity assigned to layer '{}'", newLayer);
            }
        }
    }
}
