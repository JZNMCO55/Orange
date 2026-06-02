// Entity Tree 面板（递归绘制 + DnD + rename 状态机）。
// 与 EditorRenderLayer.cpp 主 TU 共享同一类声明 EditorRenderLayer。

#include "../EditorRenderLayer.h"

#include "../EditorAssetDropHandler.h"  // v1.2.3 patch · ORANGE_ASSET DnD
#include "../EditorCameraControl.h"  // FrameSelectedCamera（右键 Focus）
#include "../EditorHierarchy.h"
#include "../EditorPrefabActions.h"  // Create Prefab... 右键入口（跨帧请求 modal）
#include "../EditorTextUtil.h"  // Util::ContainsCaseInsensitive（Entity Tree 名称过滤）
#include "../theme/codicons/IconsCodicons.h"  // 节点类型图标前缀
#include "../command/EntityCommands.h"
#include "../command/LambdaCommand.h"

#include <orange/engine/render/EnvironmentComponent.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/PostProcessComponent.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/EntityGuid.h>  // clone 后身份分离（GUID + prefab instanceId）
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/LayerComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>  // 子树 clone（Ctrl+D Duplicate）
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldPartition.h>

#include <entt/entity/registry.hpp>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{

// 递归判断 entity 子树是否含名字匹配 needle 的节点 —— Entity Tree 名称过滤的
// "保留祖先链"语义：节点显示 ⟺ 自身名字匹配 或 任一后代匹配（否则整子树隐藏）。
bool SubtreeMatchesName(Orange::Engine::World&  world,
                        Orange::Engine::Entity  entity,
                        std::string_view        needle)
{
    using HC            = Orange::Engine::Scene::HierarchyComponent;
    using NameComponent = Orange::Engine::Scene::NameComponent;
    if (!world.IsValid(entity)) { return false; }
    const auto* name = world.GetComponent<NameComponent>(entity);
    const std::string_view nm = (name != nullptr && !name->name.empty())
                                    ? std::string_view{name->name}
                                    : std::string_view{};
    if (Orange::Editor::Util::ContainsCaseInsensitive(nm, needle)) { return true; }
    const auto* h     = world.GetComponent<HC>(entity);
    Orange::Engine::Entity child =
        (h != nullptr) ? h->firstChild : Orange::Engine::Entity::Invalid();
    while (child.IsValid())
    {
        if (SubtreeMatchesName(world, child, needle)) { return true; }
        const auto* ch = world.GetComponent<HC>(child);
        child = (ch != nullptr) ? ch->nextSibling : Orange::Engine::Entity::Invalid();
    }
    return false;
}

// entity + 其全部后代追加到 out（Isolate 的 keep-set 用）。按 firstChild→
// nextSibling 递归，与序列化 / 删除子树同款遍历。
void CollectSubtree(Orange::Engine::World&               world,
                    Orange::Engine::Entity               entity,
                    std::vector<Orange::Engine::Entity>& out)
{
    using HC = Orange::Engine::Scene::HierarchyComponent;
    if (!world.IsValid(entity)) { return; }
    out.push_back(entity);
    const auto* h = world.GetComponent<HC>(entity);
    Orange::Engine::Entity child =
        (h != nullptr) ? h->firstChild : Orange::Engine::Entity::Invalid();
    while (child.IsValid())
    {
        CollectSubtree(world, child, out);
        const auto* ch = world.GetComponent<HC>(child);
        child = (ch != nullptr) ? ch->nextSibling : Orange::Engine::Entity::Invalid();
    }
}

}  // namespace

void EditorRenderLayer::DrawEntityTreePanel()
{
    ImGui::Begin("Entity Tree");
    if (mHost.scene.pWorld == nullptr) {
        ImGui::TextDisabled("(no world bound)");
        ImGui::End();
        return;
    }

    // Shift 范围选用：帧首把上一帧建好的可见节点 DFS 扁平序换入 mTreeFlatOrder，
    // 开新一帧的累积。DrawEntityNodeRecursive 渲染每个可见节点时 push 进 building。
    mTreeFlatOrder.swap(mTreeFlatOrderBuilding);
    mTreeFlatOrderBuilding.clear();

    // Play / Paused 期间禁止结构性编辑；Edit 态才允许。
    // 这里统一给"一帧内所有结构性操作的入口"加防护；帧末 apply 处不
    // 再重复检查 —— 保证 pendingXxx 只在 canEdit 为 true 时被写入。
    const bool canEdit = (mHost.scene.playState == PlayState::Edit);

    // 全局快捷键：F2 重命名选中、Del 删除选中。重命名进行中不响应
    // —— 否则 InputText 里按 Del 删字符会同时触发实体删除。
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    // Ctrl+A：全选所有用户实体（带 Name）。与下面 per-selection 快捷键不同，
    // **不要求已有选中**，故单独成块。多选 infra（additionalSelectedEntities）
    // 承接，便于批量变换 / 删除。对齐 Unity/Lumix 层级 Ctrl+A。
    if (canEdit && focused
        && !mHost.selection.renamingEntity.IsValid()
        && mHost.scene.pWorld != nullptr
        && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))
    {
        std::vector<Orange::Engine::Entity> all;
        auto view = mHost.scene.pWorld->Registry()
            .view<Orange::Engine::Scene::NameComponent>();
        for (auto e : view) { all.push_back(Orange::Engine::World::FromEntt(e)); }
        if (!all.empty())
        {
            mHost.selection.selectedEntity = all[0];
            mHost.selection.additionalSelectedEntities.assign(
                all.begin() + 1, all.end());
            mHost.assets.selectedAssetPath.clear();
        }
    }

    // Entity::IsValid() 只检测哨兵 null；Undo 可能已销毁实体，补一次
    // World::IsValid 以防操作死实体触发 EnTT assert / UB。
    if (canEdit && focused
        && !mHost.selection.renamingEntity.IsValid()
        && mHost.selection.selectedEntity.IsValid()
        && mHost.scene.pWorld->IsValid(mHost.selection.selectedEntity))
    {
        // v0.8 keybinding：从 EditorKeybindings 读绑定的 key（默认 F2 /
        // Delete，可在 Settings 面板内 rebind）。
        const auto& kb = mHost.keybindings;
        if (ImGui::IsKeyPressed(kb.renameEntity)) {
            BeginRename(mHost.selection.selectedEntity);
        }
        if (ImGui::IsKeyPressed(kb.deleteEntity)) {
            mHost.selection.pendingDelete = mHost.selection.selectedEntity;
        }
        // Ctrl+D：复制 primary 子树（hierarchy gap §3 P2，消费子树序列化基建）。
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
            mHost.selection.pendingDuplicate = true;
        }
        // Ctrl+C：把 primary 子树 SaveSubtreeToString 存进进程内剪贴板。
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
            Orange::Engine::Scene::SaveOptions cpOpts;
            cpOpts.assetRegistry          = mHost.assets.pAssets.get();
            cpOpts.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
            cpOpts.extraSerializers       = mHost.extraSerializers;
            const std::vector<Orange::Engine::Entity> cpRoots{mHost.selection.selectedEntity};
            auto blobRes = Orange::Engine::Scene::SaveSubtreeToString(
                *mHost.scene.pWorld, cpRoots, cpOpts);
            if (blobRes.IsOk()) { mEntityClipboard = blobRes.Value(); }
        }
        // Ctrl+V：剪贴板非空时帧末粘贴一份（作 primary 的 sibling）。
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)
            && !mEntityClipboard.empty()) {
            mHost.selection.pendingPaste = true;
        }
        // Ctrl+X：剪切 = 拷进剪贴板（同 Ctrl+C）+ 帧末删除（走可撤销删除路径，
        // 见 pendingDelete 处理）。仅在拷贝成功时才删，避免"剪了但没进剪贴板"。
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X)
            && mHost.selection.selectedEntity.IsValid()) {
            Orange::Engine::Scene::SaveOptions cutOpts;
            cutOpts.assetRegistry          = mHost.assets.pAssets.get();
            cutOpts.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
            cutOpts.extraSerializers       = mHost.extraSerializers;
            const std::vector<Orange::Engine::Entity> cutRoots{mHost.selection.selectedEntity};
            auto cutRes = Orange::Engine::Scene::SaveSubtreeToString(
                *mHost.scene.pWorld, cutRoots, cutOpts);
            if (cutRes.IsOk()) {
                mEntityClipboard               = cutRes.Value();
                mHost.selection.pendingDelete  = mHost.selection.selectedEntity;
            }
        }
    }

    // v1.0.1 c3：每帧 build "singleton-style component overflow" 集合 —— Pipeline
    // 对 DirectionalLight / EnvironmentComponent / PostProcess(Global) 走 first-found
    // 路径，多余实例静默忽略。DrawEntityNodeRecursive 查表后在 entity 行尾画 ⚠ chip
    // + tooltip，让用户立即看见"这条不生效"。first-found 序定义：与 Pipeline
    // RenderScene / SyncPostProcessFromWorld 走的 `view` 同款 EnTT 顺序，保证 UI
    // 标注与渲染端取舍一致。
    mSingletonOverflowDirLight.clear();
    mSingletonOverflowEnvironment.clear();
    mSingletonOverflowPostProcess.clear();
    {
        auto& regForOverflow = mHost.scene.pWorld->Registry();
        using DL = Orange::Engine::Render::DirectionalLight;
        using EC = Orange::Engine::Render::EnvironmentComponent;
        using PP = Orange::Engine::Render::PostProcessComponent;
        bool firstDirLightSeen = false;
        for (auto e : regForOverflow.view<DL>()) {
            if (!firstDirLightSeen) { firstDirLightSeen = true; continue; }
            mSingletonOverflowDirLight.push_back(
                Orange::Engine::World::FromEntt(e));
        }
        bool firstEnvSeen = false;
        for (auto e : regForOverflow.view<EC>()) {
            if (!firstEnvSeen) { firstEnvSeen = true; continue; }
            mSingletonOverflowEnvironment.push_back(
                Orange::Engine::World::FromEntt(e));
        }
        // PostProcess 只有 Global 模式是 first-found 单例：Pipeline 取第一个 Global
        // 作 base 底，第 2+ 个 Global 被静默丢弃。Local volume 按相机位置混合、各自
        // 都可能生效，不算 overflow（与 SyncPostProcessFromWorld 的 collect 循环同款
        // 取舍 —— 那里也是 `if mode==Global && globalBase==null` 才认 base）。
        auto ppView = regForOverflow.view<PP>();
        bool firstGlobalPostSeen = false;
        for (auto e : ppView) {
            if (ppView.get<PP>(e).mode != PP::Mode::Global) { continue; }
            if (!firstGlobalPostSeen) { firstGlobalPostSeen = true; continue; }
            mSingletonOverflowPostProcess.push_back(
                Orange::Engine::World::FromEntt(e));
        }
    }

    // 列出所有 root 实体（无 HierarchyComponent 或 parent invalid），按根序
    // （HierarchyComponent.sortIndex，ADR-014）升序递归画子树。sortIndex 相同
    // （含旧场景全 0）退化到 entity id 序——稳定、近似创建序，与历史行为兼容。
    // 根序可经根节点右键 "Move Up / Move Down" 调整并持久化（GAP-2026-05-29
    // 落地）。子节点顺序由兄弟链决定（DrawEntityNodeRecursive 按 firstChild→
    // nextSibling 画），可经 DnD 重排。
    auto& reg = mHost.scene.pWorld->Registry();
    using HC = Orange::Engine::Scene::HierarchyComponent;
    std::vector<entt::entity> roots;
    for (auto e : reg.view<entt::entity>()) {
        const auto* h = reg.try_get<HC>(e);
        if (h == nullptr || !h->parent.IsValid()) {
            roots.push_back(e);
        }
    }
    std::sort(roots.begin(), roots.end(),
              [&reg](entt::entity a, entt::entity b) {
                  const auto* ha = reg.try_get<HC>(a);
                  const auto* hb = reg.try_get<HC>(b);
                  const int sa = (ha != nullptr) ? ha->sortIndex : 0;
                  const int sb = (hb != nullptr) ? hb->sortIndex : 0;
                  if (sa != sb) { return sa < sb; }
                  return entt::to_integral(a) < entt::to_integral(b);
              });

    // 名称过滤框（hierarchy gap §4 quick-win #5）：大小写不敏感子串，空 = 不
    // 过滤。非空时 DrawEntityNodeRecursive 跳过"自身及子树均不匹配"的节点，
    // 命中节点的祖先链因 SubtreeMatchesName 自然保留。
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##entity_filter", "filter entities...",
                             mEntityTreeFilterBuf, sizeof(mEntityTreeFilterBuf));

    for (entt::entity e : roots) {
        DrawEntityNodeRecursive(Orange::Engine::World::FromEntt(e));
    }

    // 面板剩余空白区域 = "drop here to unparent" 区。Dummy 占满残余
    // ContentRegion，作为 drop target —— 把一个 entity 拖到这片空白
    // 上等同把它提到 root（detach from parent）。
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.y > 0.0f) {
        ImGui::Dummy(avail);
        if (canEdit && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload(kEntityPayload)) {
                Orange::Engine::Entity src{};
                std::memcpy(&src, p->Data, sizeof(src));
                EditorSelection::PendingReparent pr;
                pr.child     = src;
                pr.newParent = Orange::Engine::Entity::Invalid();   // 提到 root
                pr.where     = EditorSelection::PendingReparent::Where::IntoAsLastChild;
                pr.valid     = true;
                mHost.selection.pendingReparent = pr;
            }
            ImGui::EndDragDropTarget();
        }
    }

    // 面板背景右键菜单 —— 空白处 RMB 弹出"Create Entity (root)"。
    // NoOpenOverItems：避免与 TreeNode 上的右键菜单（DrawEntityNodeRecursive
    // 内 BeginPopupContextItem）打架。
    if (ImGui::BeginPopupContextWindow(
            "##tree_bg_ctx",
              ImGuiPopupFlags_MouseButtonRight
            | ImGuiPopupFlags_NoOpenOverItems)) {
        ImGui::BeginDisabled(!canEdit);
        if (ImGui::MenuItem("Create Entity (root)")) {
            mHost.selection.pendingCreate = {Orange::Engine::Entity::Invalid(),
                                    EditorSelection::PendingCreateKind::Empty, true};
        }
        if (ImGui::MenuItem("Create Light Object (root)")) {
            mHost.selection.pendingCreate = {Orange::Engine::Entity::Invalid(),
                                    EditorSelection::PendingCreateKind::Light, true};
        }
        // 基本体（"3D Object" 子菜单）—— 一键创建带 Renderable 的可见几何，
        // root 级。参 Unity GameObject → 3D Object / Godot 节点创建。
        if (ImGui::BeginMenu("Create 3D Object (root)")) {
            const Orange::Engine::Entity rootParent =
                Orange::Engine::Entity::Invalid();
            if (ImGui::MenuItem("Cube")) {
                mHost.selection.pendingCreate =
                    {rootParent, EditorSelection::PendingCreateKind::Cube, true};
            }
            if (ImGui::MenuItem("Sphere")) {
                mHost.selection.pendingCreate =
                    {rootParent, EditorSelection::PendingCreateKind::Sphere, true};
            }
            if (ImGui::MenuItem("Plane")) {
                mHost.selection.pendingCreate =
                    {rootParent, EditorSelection::PendingCreateKind::Plane, true};
            }
            ImGui::EndMenu();
        }
        ImGui::EndDisabled();
        // Unhide All 逃生口：批量 Hide 后一键全部显示。N==0 disable。
        // 不受 canEdit 约束——显隐是 view 态，只读场景也该能恢复可见性。
        {
            const std::size_t hiddenN = mHost.scene.partition.HiddenEntityCount();
            ImGui::Separator();
            ImGui::BeginDisabled(hiddenN == 0);
            char unhideLabel[48];
            std::snprintf(unhideLabel, sizeof(unhideLabel), "Unhide All (%zu)", hiddenN);
            if (ImGui::MenuItem(unhideLabel)) {
                mHost.scene.partition.ClearEntityHidden();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }

    ImGui::End();

    // 帧末统一 apply pending 结构性操作 ——
    // 这两步必须在 tree 递归画完之后执行，否则会破坏当前帧的 sibling
    // 链遍历。同帧内 delete + reparent 同时发生时 delete 优先（被
    // delete 的实体即使有 pendingReparent 也失效）。
    if (mHost.selection.pendingDelete.IsValid()) {
        using HCd = Orange::Engine::Scene::HierarchyComponent;
        auto& sel = mHost.selection;
        auto* pW  = mHost.scene.pWorld.get();
        // 批量删除（消费 additional set）：删 primary 且多选 → 连同选区一起删。
        std::vector<Orange::Engine::Entity> toDelete;
        toDelete.push_back(sel.pendingDelete);
        if (sel.pendingDelete == sel.selectedEntity) {
            for (const auto a : sel.additionalSelectedEntities) {
                if (a != sel.pendingDelete) { toDelete.push_back(a); }
            }
        }
        // 顶层过滤：祖先也在删除集的实体随祖先子树一起删，不单独序列化/恢复。
        auto ancestorInDelete = [&](Orange::Engine::Entity e) {
            if (pW == nullptr) { return false; }
            const auto* h0 = pW->GetComponent<HCd>(e);
            Orange::Engine::Entity p =
                (h0 != nullptr) ? h0->parent : Orange::Engine::Entity::Invalid();
            while (p.IsValid()) {
                if (std::find(toDelete.begin(), toDelete.end(), p) != toDelete.end()) {
                    return true;
                }
                const auto* ph = pW->GetComponent<HCd>(p);
                p = (ph != nullptr) ? ph->parent : Orange::Engine::Entity::Invalid();
            }
            return false;
        };

        // 删除现在**可 Undo**（消费子树序列化基建 + RemoveComponent-undo 同款
        // no-Clear 安全性）：每个顶层 root 删前 SaveSubtreeToString + 记位置
        // （parent/prevSibling），do=DestroySubtree、undo=LoadFromString 恢复 +
        // MoveToPosition 精确复位。rootPtr 追踪"当前实体"——undo 重建是新 id，
        // redo 须删新 id（捕获原 id 会失效→no-op→redo 漏删）。grouped 一次 Undo。
        // 不再 Clear()：旧字段编辑命令对已删 entity 走 schema.get nullptr-guard
        // 安全（delete 命令在栈顶，undo 先恢复实体；与 RemoveComponent-undo 一致）。
        Orange::Engine::Scene::SaveOptions delSaveOpts;
        delSaveOpts.assetRegistry          = mHost.assets.pAssets.get();
        delSaveOpts.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
        delSaveOpts.extraSerializers       = mHost.extraSerializers;

        std::vector<Orange::Engine::Entity> delRoots;
        for (const auto e : toDelete) {
            if (pW != nullptr && pW->IsValid(e) && !ancestorInDelete(e)) {
                delRoots.push_back(e);
            }
        }

        const bool delGrouped = delRoots.size() > 1;
        if (delGrouped) { mHost.cmdStack.BeginGroup("Delete (batch)", MergeMode::Disable); }
        bool anyDeleted = false;
        for (const auto root : delRoots) {
            const auto* rh = pW->GetComponent<HCd>(root);
            const Orange::Engine::Entity capParent =
                (rh != nullptr) ? rh->parent : Orange::Engine::Entity::Invalid();
            const Orange::Engine::Entity capPrev =
                (rh != nullptr) ? rh->prevSibling : Orange::Engine::Entity::Invalid();

            if (sel.selectedEntity == root) {
                sel.selectedEntity = Orange::Engine::Entity::Invalid();
            }
            if (sel.renamingEntity == root) { CancelRename(); }

            const std::vector<Orange::Engine::Entity> oneRoot{root};
            auto blobRes = Orange::Engine::Scene::SaveSubtreeToString(*pW, oneRoot, delSaveOpts);
            if (blobRes.IsErr()) {
                // 序列化失败兜底：直接销毁（本条不可 undo），不阻塞删除。
                EditorHierarchy::DestroySubtree(*pW, root);
                anyDeleted = true;
                continue;
            }
            const std::string blob = blobRes.Value();
            auto*             pH   = &mHost;
            auto rootPtr = std::make_shared<Orange::Engine::Entity>(root);
            mHost.cmdStack.Push(std::make_unique<LambdaCommand>(
                "delete_entity",
                [pH, rootPtr]() {
                    auto* w = pH->scene.pWorld.get();
                    if (w == nullptr) { return; }
                    if (rootPtr->IsValid() && w->IsValid(*rootPtr)) {
                        EditorHierarchy::DestroySubtree(*w, *rootPtr);
                    }
                },
                [pH, rootPtr, blob, capParent, capPrev]() {
                    auto* w = pH->scene.pWorld.get();
                    if (w == nullptr) { return; }
                    Orange::Engine::Scene::LoadOptions lo;
                    lo.assetRegistry          = pH->assets.pAssets.get();
                    lo.animatorRegistry       = pH->assets.pAnimators.get();
                    lo.namedMaterialInstances = &pH->assets.namedMaterialInstances;
                    lo.extraSerializers       = pH->extraSerializers;
                    std::vector<Orange::Engine::Entity> created;
                    if (Orange::Engine::Scene::LoadFromString(blob, *w, lo, &created).IsErr()) {
                        return;
                    }
                    for (const auto ce : created) {
                        const auto* eh = w->GetComponent<HCd>(ce);
                        if (eh == nullptr || !eh->parent.IsValid()) {
                            *rootPtr = ce;  // 追踪重建实体，供 redo 删对
                            EditorHierarchy::MoveToPosition(*w, ce, capParent, capPrev);
                            break;
                        }
                    }
                }
            ));
            anyDeleted = true;
        }
        if (delGrouped) { mHost.cmdStack.EndGroup(); }
        if (anyDeleted) { sel.ClearAdditional(); }
        sel.pendingDelete            = Orange::Engine::Entity::Invalid();
        sel.pendingReparent.valid    = false;  // 同帧 reparent 已无意义
    }
    if (mHost.selection.pendingReparent.valid) {
        using PR = EditorSelection::PendingReparent;
        using HC = Orange::Engine::Scene::HierarchyComponent;
        const PR pr = mHost.selection.pendingReparent;   // 值拷贝后立刻清标志
        mHost.selection.pendingReparent.valid = false;

        Orange::Engine::World* const pW = mHost.scene.pWorld.get();
        const Orange::Engine::Entity dragged = pr.child;

        // 计算"最终父"（防环 + 跨 layer 基准，批量里每个 src 一致）：Into 取
        // newParent；Before/After 取 refSibling 当前的父。
        Orange::Engine::Entity finalParent = pr.newParent;
        bool                   refOk       = true;
        if (pr.where != PR::Where::IntoAsLastChild) {
            const auto* rh = (pW != nullptr) ? pW->GetComponent<HC>(pr.refSibling) : nullptr;
            finalParent = (rh != nullptr) ? rh->parent : Orange::Engine::Entity::Invalid();
            refOk = (pW != nullptr) && pW->IsValid(pr.refSibling);
        }

        const PR::Where              where   = pr.where;
        const Orange::Engine::Entity dstInto = pr.newParent;
        const Orange::Engine::Entity refSib  = pr.refSibling;

        // 批量 reparent（hierarchy gap §3 P0 三件套最后一件）：拖的是 primary 且
        // 多选 → reparent 整个选区，否则单个。每候选 src 单独校验：存活 + 非
        // finalParent + （before/after 时）非 ref 自身 + 防环（IsAncestorOf）+
        // 非跨 layer（per-layer 序列化会静默丢，见 SceneSerialization SaveImpl，
        // warn + 跳过）。再过滤掉"祖先也在选区内"的实体（随被选中祖先一起移，
        // 避免双移破坏子树）。最终 srcs 整批一个 cmdStack group = 一次 Undo。
        std::vector<Orange::Engine::Entity> srcs;
        if (pW != nullptr && refOk)
        {
            auto inSelection = [&](Orange::Engine::Entity e) {
                if (e == mHost.selection.selectedEntity) { return true; }
                for (const auto a : mHost.selection.additionalSelectedEntities) {
                    if (a == e) { return true; }
                }
                return false;
            };
            auto anyAncestorSelected = [&](Orange::Engine::Entity e) {
                const auto* hh = pW->GetComponent<HC>(e);
                Orange::Engine::Entity p =
                    (hh != nullptr) ? hh->parent : Orange::Engine::Entity::Invalid();
                while (p.IsValid()) {
                    if (inSelection(p)) { return true; }
                    const auto* ph = pW->GetComponent<HC>(p);
                    p = (ph != nullptr) ? ph->parent : Orange::Engine::Entity::Invalid();
                }
                return false;
            };
            auto consider = [&](Orange::Engine::Entity e) {
                if (!pW->IsValid(e) || e == finalParent) { return; }
                if (where != PR::Where::IntoAsLastChild && e == refSib) { return; }
                if (EditorHierarchy::IsAncestorOf(*pW, e, finalParent)) { return; }  // 防环
                if (finalParent.IsValid()) {
                    const auto sL = mHost.scene.partition.GetLayerOf(*pW, e);
                    const auto dL = mHost.scene.partition.GetLayerOf(*pW, finalParent);
                    if (sL != dL) {
                        ORANGE_LOG_WARN("[OrangeEditor] reparent 拒绝：跨 layer 父子"
                                        "（layer '{}' → '{}'）—— per-layer 序列化不会"
                                        "保存；先放到同一 layer 再 reparent",
                                        std::string{sL}, std::string{dL});
                        return;
                    }
                }
                srcs.push_back(e);
            };

            const bool batch = (dragged == mHost.selection.selectedEntity)
                            && !mHost.selection.additionalSelectedEntities.empty();
            if (batch) {
                consider(mHost.selection.selectedEntity);
                for (const auto a : mHost.selection.additionalSelectedEntities) {
                    if (!anyAncestorSelected(a)) { consider(a); }
                }
            } else {
                consider(dragged);
            }
        }

        // 批量（>1）打 group → 一次 Undo；单个不开 group（与原单 reparent 行为
        // 逐字节一致：consider 单跑一次、push 同一条 "reparent" 命令）。
        const bool grouped = srcs.size() > 1;
        if (grouped) {
            mHost.cmdStack.BeginGroup("Reparent (batch)", MergeMode::Disable);
        }
        for (const auto src : srcs)
        {
            // 记录旧**精确位置**（parent + prevSibling）：Undo 用 MoveToPosition 原
            // 位复位，不丢兄弟顺序。
            const auto* hc = pW->GetComponent<HC>(src);
            const Orange::Engine::Entity oldParent =
                (hc != nullptr) ? hc->parent : Orange::Engine::Entity::Invalid();
            const Orange::Engine::Entity oldPrev =
                (hc != nullptr) ? hc->prevSibling : Orange::Engine::Entity::Invalid();

            // c14 解 World* 强耦合：lambda 捕获 EditorHost* 而非裸 World*，调用时
            // 间接解 World——切场景走 nullptr 防御分支 no-op 而非 dangling 崩溃。
            mHost.cmdStack.Push(std::make_unique<LambdaCommand>(
                "reparent",
                [pH = &mHost, src, where, dstInto, refSib]() {
                    auto* w = pH->scene.pWorld.get();
                    if (w == nullptr || !w->IsValid(src)) { return; }
                    switch (where) {
                        case PR::Where::IntoAsLastChild:
                            EditorHierarchy::ReparentTo(*w, src, dstInto);
                            break;
                        case PR::Where::BeforeSibling:
                            if (w->IsValid(refSib)) { EditorHierarchy::MoveBefore(*w, src, refSib); }
                            break;
                        case PR::Where::AfterSibling:
                            if (w->IsValid(refSib)) { EditorHierarchy::MoveAfter(*w, src, refSib); }
                            break;
                    }
                },
                [pH = &mHost, src, oldParent, oldPrev]() {
                    auto* w = pH->scene.pWorld.get();
                    if (w == nullptr) { return; }
                    // Undo 时 src 可能已被其他命令销毁（EnTT version check）
                    if (w->IsValid(src)) {
                        EditorHierarchy::MoveToPosition(*w, src, oldParent, oldPrev);
                    }
                }
            ));
        }
        if (grouped) {
            mHost.cmdStack.EndGroup();
        }
    }
    if (mHost.selection.pendingCreate.valid) {
        const Orange::Engine::Entity         parent         = mHost.selection.pendingCreate.parent;
        const EditorSelection::PendingCreateKind kind           = mHost.selection.pendingCreate.kind;
        const auto cubeMesh   = mHost.assets.cubeMeshHandle;
        const auto sphereMesh = mHost.assets.sphereMeshHandle;
        const auto planeMesh  = mHost.assets.planeMeshHandle;
        auto* const pLightMat = mHost.assets.pLightObjectMaterial.get();
        // 基本体材质：优先 PBR baseline；缺席（理论上 InitializeEditorAssets 后不会）
        // 退化到 default textured（棋盘格 dev-checker，保证可见）。
        auto* const pPrimMat  = mHost.assets.pPbrMaterial
            ? mHost.assets.pPbrMaterial.get()
            : mHost.assets.pDefaultRenderableMaterial.get();
        // 基本体在相机焦点（轨道 pivot ≈ 视野中心）处生成，避免在原点看不见
        // （相机看别处时"新建 Cube 怎么没出现"）。Empty / Light 保持原点（容器 /
        // 方向光位置无关，零行为变化）。
        const glm::vec3 primSpawnPos = mHost.camera.pivot;
        mHost.selection.pendingCreate.valid = false;

        auto cmd = std::make_unique<CreateEntityCommand>(
            mHost,
            [parent, kind, cubeMesh, sphereMesh, planeMesh, pLightMat, pPrimMat,
             primSpawnPos]
            (Orange::Engine::World& w) -> Orange::Engine::Entity
            {
                using PCK = EditorSelection::PendingCreateKind;
                Orange::Engine::Entity e = w.CreateEntity();
                const char* initialName = "New Entity";
                switch (kind) {
                    case PCK::Light:  initialName = "Light Object"; break;
                    case PCK::Cube:   initialName = "Cube";   break;
                    case PCK::Sphere: initialName = "Sphere"; break;
                    case PCK::Plane:  initialName = "Plane";  break;
                    default: break;
                }
                w.AddComponent<Orange::Engine::Scene::NameComponent>(
                    e, Orange::Engine::Scene::NameComponent{initialName});
                // 基本体在相机焦点生成；Empty / Light 在原点（默认 Transform）。
                Orange::Engine::Scene::TransformComponent tc{};
                if (kind == PCK::Cube || kind == PCK::Sphere
                    || kind == PCK::Plane) {
                    tc.position = primSpawnPos;
                }
                w.AddComponent<Orange::Engine::Scene::TransformComponent>(e, tc);

                using ::Orange::Engine::Render::DirectionalLight;
                using ::Orange::Engine::Render::RenderableComponent;
                if (kind == PCK::Light) {
                    // 一键搭出"可见的发光物体" —— DirectionalLight 提供光照贡献 +
                    // Renderable(cube + emissive material) 让灯本身在 Scene 视口
                    // 可见（不然方向光是看不见的）。
                    w.AddComponent<DirectionalLight>(e, DirectionalLight{});
                    RenderableComponent rc{};
                    rc.mesh             = cubeMesh;
                    rc.materialInstance = pLightMat;
                    rc.visible          = true;
                    rc.castsShadow      = false;
                    w.AddComponent<RenderableComponent>(e, rc);
                }
                else if (kind == PCK::Cube || kind == PCK::Sphere
                         || kind == PCK::Plane) {
                    // 基本体：一键带 Renderable 的可见几何（pbr 材质 + 对应内置 mesh）。
                    RenderableComponent rc{};
                    rc.mesh = (kind == PCK::Sphere) ? sphereMesh
                            : (kind == PCK::Plane)  ? planeMesh
                                                    : cubeMesh;
                    rc.materialInstance = pPrimMat;
                    rc.visible          = true;
                    rc.castsShadow      = true;
                    w.AddComponent<RenderableComponent>(e, rc);
                }

                if (parent.IsValid() && w.IsValid(parent)) {
                    EditorHierarchy::LinkAsLastChild(w, parent, e);
                }
                return e;
            }
        );

        // Push 前取 raw 指针；Push 内部 Execute 会填充 mCreated，
        // 随后 unique_ptr move 进栈 —— raw 仍指向栈内对象，生命周期安全。
        auto* rawCmd = cmd.get();
        mHost.cmdStack.Push(std::move(cmd));
        const Orange::Engine::Entity e = rawCmd->CreatedEntity();

        mHost.selection.selectedEntity = e;
        // B3 修：互斥选择 —— Hierarchy / viewport / 新建实体 操作总是把
        // Inspector 焦点拉回实体模式，不让 Material 子模式残留。
        mHost.assets.selectedAssetPath.clear();
        BeginRename(e);
    }

    // 帧末 Duplicate（Ctrl+D）：复制 primary 的子树。复用子树序列化基建
    // （SaveSubtreeToString → LoadFromString，内部引用自动 remap 到克隆）。
    // 可 undo：do = clone + 把克隆根 reparent 到原根的父（作 sibling）+ 选中；
    // undo = DestroySubtree 克隆根（createdPtr 里"父不在 created 集"者）。
    if (mHost.selection.pendingDuplicate) {
        mHost.selection.pendingDuplicate = false;
        using HC2 = Orange::Engine::Scene::HierarchyComponent;
        auto* pW = mHost.scene.pWorld.get();
        const Orange::Engine::Entity root = mHost.selection.selectedEntity;
        if (pW != nullptr && root.IsValid() && pW->IsValid(root)) {
            Orange::Engine::Scene::SaveOptions saveOpts;
            saveOpts.assetRegistry          = mHost.assets.pAssets.get();
            saveOpts.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
            saveOpts.extraSerializers       = mHost.extraSerializers;
            const std::vector<Orange::Engine::Entity> dupRoots{root};
            auto blobRes = Orange::Engine::Scene::SaveSubtreeToString(*pW, dupRoots, saveOpts);
            if (blobRes.IsOk()) {
                const auto* rh = pW->GetComponent<HC2>(root);
                const Orange::Engine::Entity origParent =
                    (rh != nullptr) ? rh->parent : Orange::Engine::Entity::Invalid();
                const std::string blob = blobRes.Value();
                auto* pH = &mHost;
                auto createdPtr = std::make_shared<std::vector<Orange::Engine::Entity>>();
                mHost.cmdStack.Push(std::make_unique<LambdaCommand>(
                    "duplicate",
                    [pH, blob, origParent, createdPtr]() {
                        auto* w = pH->scene.pWorld.get();
                        if (w == nullptr) { return; }
                        Orange::Engine::Scene::LoadOptions lo;
                        lo.assetRegistry          = pH->assets.pAssets.get();
                        lo.animatorRegistry       = pH->assets.pAnimators.get();
                        lo.namedMaterialInstances = &pH->assets.namedMaterialInstances;
                        lo.extraSerializers       = pH->extraSerializers;
                        std::vector<Orange::Engine::Entity> created;
                        auto r = Orange::Engine::Scene::LoadFromString(blob, *w, lo, &created);
                        if (r.IsErr()) { return; }
                        *createdPtr = created;
                        // 身份分离：blob 字节保真复制了源的 GuidComponent +
                        // PrefabInstanceComponent.instanceId。Duplicate 出来的是
                        // 全新实体 / 全新一次实例化，必须换新 per-entity GUID +
                        // 新 instanceId，否则与源碰撞（同 world 两实体共享 GUID /
                        // 被误认为同一次 prefab 实例化）。对无这些组件的普通实体
                        // 无副作用，幂等安全。
                        Orange::Engine::Scene::SeparateClonedIdentities(*w, created);
                        // 克隆根 = created 中父失效者（原父在子树外未序列化）。单根
                        // duplicate 只有一个；reparent 到原根的父 + 选中。
                        for (const auto ce : created) {
                            const auto* eh = w->GetComponent<HC2>(ce);
                            if (eh == nullptr || !eh->parent.IsValid()) {
                                if (origParent.IsValid() && w->IsValid(origParent)) {
                                    EditorHierarchy::ReparentTo(*w, ce, origParent);
                                }
                                pH->selection.selectedEntity = ce;
                                pH->selection.ClearAdditional();
                                pH->assets.selectedAssetPath.clear();
                                break;
                            }
                        }
                    },
                    [pH, createdPtr]() {
                        auto* w = pH->scene.pWorld.get();
                        if (w == nullptr) { return; }
                        // 删克隆：对每个"父不在 created 集内"的 created 实体（克隆子树
                        // 根）DestroySubtree，其后代也在 created、由 DestroySubtree 一并销毁。
                        for (const auto ce : *createdPtr) {
                            if (!w->IsValid(ce)) { continue; }
                            const auto* eh = w->GetComponent<HC2>(ce);
                            const bool isRoot = (eh == nullptr) || !eh->parent.IsValid()
                                || std::find(createdPtr->begin(), createdPtr->end(),
                                             eh->parent) == createdPtr->end();
                            if (isRoot) { EditorHierarchy::DestroySubtree(*w, ce); }
                        }
                    }
                ));
            }
        }
    }

    // 帧末 Paste（Ctrl+V）：从剪贴板 blob 粘贴一份子树，作当前 primary 的
    // sibling（无 primary → root）。LoadFromString 内部引用 remap；可 undo
    // （do=clone+reparent+选中，undo=DestroySubtree 克隆根），同 Duplicate 模式。
    if (mHost.selection.pendingPaste) {
        mHost.selection.pendingPaste = false;
        using HCp = Orange::Engine::Scene::HierarchyComponent;
        auto* pWp = mHost.scene.pWorld.get();
        if (pWp != nullptr && !mEntityClipboard.empty()) {
            Orange::Engine::Entity tgtParent = Orange::Engine::Entity::Invalid();
            const Orange::Engine::Entity primary = mHost.selection.selectedEntity;
            if (primary.IsValid() && pWp->IsValid(primary)) {
                const auto* ph = pWp->GetComponent<HCp>(primary);
                tgtParent = (ph != nullptr) ? ph->parent : Orange::Engine::Entity::Invalid();
            }
            const std::string blob = mEntityClipboard;
            auto* pH = &mHost;
            auto createdPtr = std::make_shared<std::vector<Orange::Engine::Entity>>();
            mHost.cmdStack.Push(std::make_unique<LambdaCommand>(
                "paste",
                [pH, blob, tgtParent, createdPtr]() {
                    auto* w = pH->scene.pWorld.get();
                    if (w == nullptr) { return; }
                    Orange::Engine::Scene::LoadOptions lo;
                    lo.assetRegistry          = pH->assets.pAssets.get();
                    lo.animatorRegistry       = pH->assets.pAnimators.get();
                    lo.namedMaterialInstances = &pH->assets.namedMaterialInstances;
                    lo.extraSerializers       = pH->extraSerializers;
                    std::vector<Orange::Engine::Entity> created;
                    if (Orange::Engine::Scene::LoadFromString(blob, *w, lo, &created).IsErr()) {
                        return;
                    }
                    *createdPtr = created;
                    // 同 Duplicate：剪贴板 blob 也字节保真复制了 GUID + prefab
                    // instanceId，Paste 出来的是新实体 / 新一次实例化，换新身份避
                    // 免与剪贴板源（及之前多次 Paste 出来的副本）碰撞。
                    Orange::Engine::Scene::SeparateClonedIdentities(*w, created);
                    for (const auto ce : created) {
                        const auto* eh = w->GetComponent<HCp>(ce);
                        if (eh == nullptr || !eh->parent.IsValid()) {
                            if (tgtParent.IsValid() && w->IsValid(tgtParent)) {
                                EditorHierarchy::ReparentTo(*w, ce, tgtParent);
                            }
                            pH->selection.selectedEntity = ce;
                            pH->selection.ClearAdditional();
                            pH->assets.selectedAssetPath.clear();
                            break;
                        }
                    }
                },
                [pH, createdPtr]() {
                    auto* w = pH->scene.pWorld.get();
                    if (w == nullptr) { return; }
                    for (const auto ce : *createdPtr) {
                        if (!w->IsValid(ce)) { continue; }
                        const auto* eh = w->GetComponent<HCp>(ce);
                        const bool isRoot = (eh == nullptr) || !eh->parent.IsValid()
                            || std::find(createdPtr->begin(), createdPtr->end(),
                                         eh->parent) == createdPtr->end();
                        if (isRoot) { EditorHierarchy::DestroySubtree(*w, ce); }
                    }
                }
            ));
        }
    }
}

// 递归画一个实体节点 + 其子树。
//
// 用 TreeNodeEx + ImGuiTreeNodeFlags_OpenOnArrow：点叶身体当选中，点
// 三角才展开 —— 跟 Unity / Unreal 编辑器手感一致。Selected 状态从
// mHost.selection.selectedEntity 反映，点击任意节点写回。叶子节点（无 firstChild）
// 用 ImGuiTreeNodeFlags_Leaf 关闭三角并强制不可展开。
//
// Rename：renamingEntity == 当前 entity 时，TreeNode 的 label 用空串
// + AllowOverlap，SameLine 上画 InputText 接管 label 区域。Enter 提
// 交，Esc / 失焦取消。
// DnD：每个节点同时是 drag source 和 drop target；拖一个 entity 放到
// 另一节点 → reparent 进它；放到面板空白 → detach 到 root（见
// DrawEntityTreePanel 末尾）。
void EditorRenderLayer::DrawEntityNodeRecursive(Orange::Engine::Entity entity)
{
    if (!entity.IsValid()) { return; }
    // Entity::IsValid() 只检查哨兵 null；World::IsValid() 才能检出 EnTT
    // version 已被 Undo/Redo 的 DestroyEntity 失效的"死实体"。
    // 正常路径下（DestroySubtree + Detach 已清理兄弟链）死实体不会进到
    // 这里，但防御性 early-out 避免万一出现 ghost 引用时 GetComponent /
    // PushID 对死实体操作导致 EnTT assert / UB。
    if (!mHost.scene.pWorld->IsValid(entity)) { return; }

    // 名称过滤（hierarchy gap §4 quick-win #5）：filter 非空且本子树无名字
    // 匹配 → 整子树隐藏。在 PushID 之前 return 保持 ImGui ID 栈平衡；
    // SubtreeMatchesName 已查过子树，false 即无任何后代匹配，安全全隐。
    if (mEntityTreeFilterBuf[0] != '\0'
        && !SubtreeMatchesName(*mHost.scene.pWorld, entity,
                               std::string_view{mEntityTreeFilterBuf})) {
        return;
    }

    // 本节点确定渲染 → 记入可见节点 DFS 扁平序（Shift 范围选用，按 pre-order
    // 追加；折叠的子节点不会递归到这里，自然不入序）。
    mTreeFlatOrderBuilding.push_back(entity);

    using HC = Orange::Engine::Scene::HierarchyComponent;
    using NameComponent = Orange::Engine::Scene::NameComponent;

    const auto* h     = mHost.scene.pWorld->GetComponent<HC>(entity);
    const auto* name  = mHost.scene.pWorld->GetComponent<NameComponent>(entity);
    const bool  hasKid = (h != nullptr) && h->firstChild.IsValid();
    const bool  selected = (mHost.selection.selectedEntity == entity);
    const bool  renaming = (mHost.selection.renamingEntity == entity);

    // 不挂 OpenOnDoubleClick：双击节点名留给"进入重命名"（见下），展开 / 折叠
    // 只走左侧三角（OpenOnArrow），与 Unreal / Godot 的 scene tree 同款手感 ——
    // 否则双击父节点会被"展开"吃掉、永远进不了重命名。
    ImGuiTreeNodeFlags flags =
          ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth
        | ImGuiTreeNodeFlags_DefaultOpen
        | ImGuiTreeNodeFlags_AllowOverlap;
    if (!hasKid)  { flags |= ImGuiTreeNodeFlags_Leaf; }
    if (selected) { flags |= ImGuiTreeNodeFlags_Selected; }

    // ID 用 entity 数值 —— 不依赖名字（重名/空名也稳定），并满足
    // "同一棵子树里不会重复" 的 ImGui ID 唯一性约束。
    ImGui::PushID(static_cast<int>(static_cast<std::uint32_t>(entity.Value())));

    bool open = false;
    // TreeNodeEx 行 rect —— 紧跟 TreeNodeEx 后捕获（此时它是 last item）。
    // 后面 DnD target 在画完 chips / context menu 之后才求值，那时 last item
    // 已不是 node，故用本对局部变量算 drop 落点 Y 分区，稳定可靠。
    ImVec2 nodeMin{0.0f, 0.0f};
    ImVec2 nodeMax{0.0f, 0.0f};
    // 节点级编辑权限 —— 与 DrawEntityTreePanel 顶部的 canEdit 同逻辑，
    // 但 DrawEntityNodeRecursive 是独立调用栈，所以这里重新取一次。
    const bool canEditNode = (mHost.scene.playState == PlayState::Edit);

    if (renaming) {
        // 空 label + SameLine InputText —— TreeNode 三角仍可用，
        // label 区域被 InputText 接管。
        open = ImGui::TreeNodeEx("##node", flags, "%s", "");
        nodeMin = ImGui::GetItemRectMin();
        nodeMax = ImGui::GetItemRectMax();
        ImGui::SameLine();
        if (mHost.selection.renameJustStarted) {
            ImGui::SetKeyboardFocusHere();
            mHost.selection.renameJustStarted = false;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool entered = ImGui::InputText(
            "##rename", mHost.selection.renameBuffer, sizeof(mHost.selection.renameBuffer),
              ImGuiInputTextFlags_EnterReturnsTrue
            | ImGuiInputTextFlags_AutoSelectAll);
        // 三种触发：
        //   * entered = Enter 键明确确认（EnterReturnsTrue 触发）
        //   * IsItemDeactivatedAfterEdit = 用户编辑过内容后失焦（典型
        //     "改完点别处" 的 UX 期望是 commit，不是 cancel；Unity /
        //     Unreal 的 InputField 也是这个行为）
        //   * IsItemDeactivated 且未 edit = Esc / 点别处但没改动 → 取消
        // 顺序 if-else 保证 Enter 优先；deactivated-after-edit 把"鼠标转
        // 走但已经输完" 这条容易丢的路径也 commit。
        const bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
        const bool deactivated          = ImGui::IsItemDeactivated();
        if (entered || deactivatedAfterEdit) {
            CommitRename(entity);
        } else if (deactivated) {
            CancelRename();
        }
    } else {
        const char* label = (name != nullptr && !name->name.empty())
            ? name->name.c_str()
            : "(unnamed)";
        // 类型图标前缀（hierarchy gap §4 quick-win #4）：按组件分类 ——
        // 有 Directional/Point light → 灯泡；有 Renderable → object；否则
        // generic（空 / Transform-only）。codicon 已并入主字体可直接渲染。
        namespace R = Orange::Engine::Render;
        const bool isLight =
            mHost.scene.pWorld->GetComponent<R::DirectionalLight>(entity) != nullptr
            || mHost.scene.pWorld->GetComponent<R::PointLight>(entity) != nullptr;
        const bool isMesh =
            mHost.scene.pWorld->GetComponent<R::RenderableComponent>(entity) != nullptr;
        const char* typeIcon = isLight ? ICON_CI_LIGHTBULB
                             : isMesh  ? ICON_CI_SYMBOL_OBJECT
                                       : ICON_CI_SYMBOL_NAMESPACE;
        // 锁定指示：locked 时名字前加锁图标（hierarchy gap §3 P1）。
        const char* lockPrefix = IsEntityLocked(entity) ? (ICON_CI_LOCK " ") : "";
        // 隐藏指示：hidden 时加 eye-closed 图标 + 整行文字 dim（Unity/Lumix
        // 同款"隐藏物体灰显"）。dim 色取 ImGuiCol_TextDisabled（style 查询，
        // 非字面 ImVec4，符合 lint）。
        const bool  entityHidden =
            mHost.scene.partition.IsEntityHidden(entity);
        const char* hidePrefix = entityHidden ? (ICON_CI_EYE_CLOSED " ") : "";
        if (entityHidden) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        open = ImGui::TreeNodeEx("##node", flags, "%s%s%s %s", hidePrefix, lockPrefix,
                                 typeIcon, label);
        if (entityHidden) {
            ImGui::PopStyleColor();
        }
        nodeMin = ImGui::GetItemRectMin();
        nodeMax = ImGui::GetItemRectMax();
        // 锁定实体不可 tree-click 选中（防误编辑，hierarchy gap §3 P1）；解锁
        // 经右键 context menu Unlock（不依赖选中）。
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()
            && !IsEntityLocked(entity)) {
            // 多选：Shift-click 范围选（hierarchy gap P1）；Ctrl-click toggle
            // 加入/移出 additional；regular click 清空 additional + 切 primary。
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyShift && mHost.selection.selectedEntity.IsValid()
                && entity != mHost.selection.selectedEntity)
            {
                // 用上一帧的可见节点扁平序，选中 anchor（当前 primary）↔ clicked
                // 之间的全部可见节点。primary 保持 anchor 不变（重复 Shift-click
                // 从同一 anchor 伸缩），区间内其余入 additional。
                const auto& flat = mTreeFlatOrder;
                int ai = -1;
                int ci = -1;
                for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
                    if (flat[i] == mHost.selection.selectedEntity) { ai = i; }
                    if (flat[i] == entity)                          { ci = i; }
                }
                if (ai >= 0 && ci >= 0) {
                    const int lo = (ai < ci) ? ai : ci;
                    const int hi = (ai < ci) ? ci : ai;
                    mHost.selection.ClearAdditional();
                    for (int i = lo; i <= hi; ++i) {
                        if (flat[i] != mHost.selection.selectedEntity) {
                            mHost.selection.additionalSelectedEntities.push_back(flat[i]);
                        }
                    }
                } else {
                    // anchor/clicked 不在上帧序里（罕见：刚展开/过滤变化）→ 退化单选。
                    mHost.selection.selectedEntity = entity;
                    mHost.selection.ClearAdditional();
                }
            }
            else if (io.KeyCtrl && mHost.selection.selectedEntity.IsValid()
                && entity != mHost.selection.selectedEntity)
            {
                mHost.selection.ToggleAdditional(entity);
            }
            else
            {
                mHost.selection.selectedEntity = entity;
                mHost.selection.ClearAdditional();
            }
            // B3 修：互斥选择，详见 ScenePanel viewport pick 同名注释。
            mHost.assets.selectedAssetPath.clear();
        }

        // 双击节点名进入重命名（Edit 态才允许）。与 DnD 同理，IsItemHovered 必须
        // 在 TreeNode 仍是 last item 时查 —— 放到下面 SameLine chip 之后会锚到 chip，
        // 双击节点名无反应（这正是之前的 bug）。已去掉 OpenOnDoubleClick，双击不再
        // 触发展开；!IsItemToggledOpen 仅在双击三角时为真，保留作保险。
        if (canEditNode
            && ImGui::IsItemHovered()
            && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
            && !ImGui::IsItemToggledOpen()) {
            BeginRename(entity);
        }

        // —— 行级 DnD（拖拽重排 / reparent）必须在 TreeNode 仍是 ImGui
        // "last item" 时建立！下面用 SameLine 画的 layer / warning chip 是无 ID
        // 的 Text item，会把 last-item 锚点偷走：BeginDragDropSource 对无 ID item
        // 走 "hover 该 item 矩形才激活" 路径（imgui.cpp SourceAllowNullID 分支），
        // 于是只能从右侧那个小 chip 起拖、拖节点名无反应（同理 drop target 只认
        // chip 矩形）。故 source / target 前置到 chip 之前，锚定整行 TreeNode（有
        // ID，走 ActiveId 常规路径）。
        if (canEditNode && !IsEntityLocked(entity)
            && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(kEntityPayload, &entity, sizeof(entity));
            ImGui::Text("Move %s",
                        (name != nullptr && !name->name.empty())
                            ? name->name.c_str() : "(unnamed)");
            ImGui::EndDragDropSource();
        }
        // DnD target：entity drop 按鼠标 Y 相对节点 rect 分三区——上 1/4 = 插到
        // 该兄弟之前、下 1/4 = 之后（reorder）、中间 = 挂进该节点（reparent into）。
        // before/after 仅对**有父的子节点**提供（根之间无顺序表示，root 只给
        // into）。ORANGE_ASSET drop 与落点无关，恒按 into 语义 apply 到 entity。
        if (canEditNode && ImGui::BeginDragDropTarget()) {
            using PR = EditorSelection::PendingReparent;
            const bool  targetIsChild = (h != nullptr) && h->parent.IsValid();
            const float rowH = nodeMax.y - nodeMin.y;
            const float t    = (rowH > 0.0f)
                ? (ImGui::GetMousePos().y - nodeMin.y) / rowH : 0.5f;
            PR::Where where = PR::Where::IntoAsLastChild;
            if (targetIsChild && t < 0.25f)      { where = PR::Where::BeforeSibling; }
            else if (targetIsChild && t > 0.75f) { where = PR::Where::AfterSibling; }

            // 插入指示线：before 画节点上沿、after 画下沿（into 用 ImGui 默认
            // 矩形高亮表达，不另画线）。
            if (where != PR::Where::IntoAsLastChild) {
                const float ly = (where == PR::Where::BeforeSibling) ? nodeMin.y : nodeMax.y;
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2{nodeMin.x, ly}, ImVec2{nodeMax.x, ly},
                    ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
            }

            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload(kEntityPayload)) {
                Orange::Engine::Entity src{};
                std::memcpy(&src, p->Data, sizeof(src));
                PR pr;
                pr.child = src;
                pr.where = where;
                if (where == PR::Where::IntoAsLastChild) {
                    pr.newParent = entity;
                } else {
                    pr.refSibling = entity;   // 与 entity 同父，插到其前 / 后
                }
                pr.valid = true;
                mHost.selection.pendingReparent = pr;
            }
            // v1.2.3 patch · ORANGE_ASSET DnD：按文件扩展名 apply 到 entity 对应
            // component 字段（.material → Renderable.materialInstance / .mesh →
            // Renderable.mesh / .wav 等 → AudioSource.sound）。详 EditorAssetDrop
            // Handler.h 调用约定 + 失败语义（宽容口径 silent skip + log）。
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload("ORANGE_ASSET")) {
                const std::size_t len = (p->DataSize > 0)
                    ? static_cast<std::size_t>(p->DataSize) - 1 : 0;
                const std::string path(static_cast<const char*>(p->Data), len);
                if (!path.empty()) {
                    Orange::Editor::ApplyAssetDropToEntity(mHost, entity, path);
                }
            }
            ImGui::EndDragDropTarget();
        }

        // v0.6 c5：行尾 layer chip —— 让用户一眼看到每个 entity 所属
        // layer。chip 显示 LayerComponent.layerId（缺则 "default"），灰色
        // 弱化避免抢主名字。SameLine + 右对齐：用 GetContentRegionAvail
        // 倒推一个 button 位置；hover 弹 tooltip 提示用右键 "Move to
        // layer >" 改归属（避免增加额外的可点击控件冲淡 tree DnD 手感）。
        //
        // v1.0.1 c3：layer chip 左侧追加 ⚠ warning chip ——
        // singleton-style component（DirectionalLight / Environment）overflow
        // 的 entity 在此显示 warning，hover 弹 tooltip 说明"不生效"原因。
        {
            const std::string_view layerId =
                mHost.scene.partition.GetLayerOf(*mHost.scene.pWorld, entity);
            const std::string layerText{layerId};
            const ImVec2 chipSize = ImGui::CalcTextSize(layerText.c_str());

            // overflow 查表 —— linear scan，N ≤ 3 完全可接受。同一 entity
            // 可能同时撞多类（典型：开发者把 DirLight + Environment +
            // PostProcess 都挂到同一辅助 entity 上做测试），tooltip 逐类
            // 拼行合并展示（见下，避免 3 个独立布尔的组合爆炸分支）。
            const bool overflowDirLight = std::find(
                mSingletonOverflowDirLight.begin(),
                mSingletonOverflowDirLight.end(), entity)
                != mSingletonOverflowDirLight.end();
            const bool overflowEnv = std::find(
                mSingletonOverflowEnvironment.begin(),
                mSingletonOverflowEnvironment.end(), entity)
                != mSingletonOverflowEnvironment.end();
            const bool overflowPostProcess = std::find(
                mSingletonOverflowPostProcess.begin(),
                mSingletonOverflowPostProcess.end(), entity)
                != mSingletonOverflowPostProcess.end();
            const bool hasWarning =
                overflowDirLight || overflowEnv || overflowPostProcess;

            const char* warnText = "(!)";
            const ImVec2 warnSize = hasWarning
                ? ImGui::CalcTextSize(warnText) : ImVec2{0.0f, 0.0f};
            const float  spacing  = ImGui::GetStyle().ItemSpacing.x;
            const float  avail    = ImGui::GetContentRegionAvail().x;
            // chips 总宽 = warning（如有）+ inner spacing + layer + outer spacing。
            const float  innerSpacing = hasWarning ? spacing : 0.0f;
            const float  needed = warnSize.x + innerSpacing + chipSize.x + spacing;
            // 仅在右侧确实有空间时画 chip 组，避免极窄面板时与名字重叠
            if (avail > needed + spacing) {
                float cursorX = ImGui::GetCursorPosX() + avail - needed;
                if (hasWarning) {
                    ImGui::SameLine(cursorX);
                    // warning 用黄色而非默认 TextDisabled，让它在一片灰色
                    // 文本中跳出；color (1.0, 0.78, 0.20) 是 Cocos Creator
                    // / Lumix StudioApp 的 warning chip 同色系。
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4{1.00f, 0.78f, 0.20f, 1.0f});
                    ImGui::TextUnformatted(warnText);
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) {
                        // 三类 first-found 单例 overflow 可任意组合命中；逐类拼
                        // 行而非硬编码 2^N 组合分支。SetTooltip 用 "%s" 喂拼好的
                        // 字符串（与下方 layer chip tooltip 同款安全格式）。
                        std::string tip{
                            "此 entity 上的以下组件不生效 —— Pipeline 取 first-found "
                            "实例，多余的被静默忽略：\n"};
                        if (overflowDirLight) {
                            tip += "  • DirectionalLight：场景中已有另一个作为主光；"
                                   "DirLight 为全局单例语义。\n";
                        }
                        if (overflowEnv) {
                            tip += "  • Environment：场景中已有另一个作为全局环境；"
                                   "建议每 scene 至多挂一个。\n";
                        }
                        if (overflowPostProcess) {
                            tip += "  • PostProcess (Global)：场景中已有另一个 Global "
                                   "后处理作 base 底；把本组件 Mode 改为 Local 可按"
                                   "相机位置叠加生效。\n";
                        }
                        tip += "若想生效：删除其他 entity 上的同名组件，或改用支持"
                               "多实例的模式（如 PostProcess 的 Local volume）。";
                        ImGui::SetTooltip("%s", tip.c_str());
                    }
                    cursorX += warnSize.x + innerSpacing;
                }
                ImGui::SameLine(cursorX);
                ImGui::TextDisabled("%s", layerText.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("layer: %s\n右键 → Move to layer 改归属",
                                      layerText.c_str());
                }
            }
        }

    }
    // 节点右键菜单 —— 选中始终允许；结构性操作（Create/Rename/Delete）
    // 受 canEditNode 约束。
    if (!renaming && ImGui::BeginPopupContextItem("##node_ctx")) {
        mHost.selection.selectedEntity = entity;
        // B3 修：互斥选择，右键 context menu 也是"实体选中"入口之一。
        mHost.assets.selectedAssetPath.clear();
        ImGui::BeginDisabled(!canEditNode);
        if (ImGui::MenuItem("Create Child")) {
            mHost.selection.pendingCreate = {entity, EditorSelection::PendingCreateKind::Empty, true};
        }
        if (ImGui::MenuItem("Create Light Object (Child)")) {
            mHost.selection.pendingCreate = {entity, EditorSelection::PendingCreateKind::Light, true};
        }
        // 基本体子菜单（child 级，挂为右键节点的末子）。
        if (ImGui::BeginMenu("Create 3D Object (Child)")) {
            if (ImGui::MenuItem("Cube")) {
                mHost.selection.pendingCreate =
                    {entity, EditorSelection::PendingCreateKind::Cube, true};
            }
            if (ImGui::MenuItem("Sphere")) {
                mHost.selection.pendingCreate =
                    {entity, EditorSelection::PendingCreateKind::Sphere, true};
            }
            if (ImGui::MenuItem("Plane")) {
                mHost.selection.pendingCreate =
                    {entity, EditorSelection::PendingCreateKind::Plane, true};
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        // Focus（聚焦相机到该实体）—— 右键已把选中切到本节点，直接复用
        // FrameSelectedCamera（与 viewport F 键同款）。Entity Tree 里也能聚焦，
        // 不必先切到 viewport 按 F。
        if (ImGui::MenuItem("Focus", "F")) {
            FrameSelectedCamera(mHost);
        }
        if (ImGui::MenuItem("Rename", "F2")) {
            BeginRename(entity);
        }
        // Reset Transform（Unity 标准）—— 把该实体 TransformComponent 重置为本地
        // identity（position 0 / rotation 单位 quat / scale 1）。可 Undo。对导入
        // 模型 transform 异常 / 手滑挪偏后归位有用。defensive 重解引（capE+IsValid）
        // 同 Paste Values；cmdStack.onChanged 自动置 dirty，无需手动标。
        {
            using ::Orange::Engine::Entity;
            using TC = ::Orange::Engine::Scene::TransformComponent;
            auto* pW = mHost.scene.pWorld.get();
            const bool hasTC = pW != nullptr && pW->IsValid(entity)
                            && pW->GetComponent<TC>(entity) != nullptr;
            if (ImGui::MenuItem("Reset Transform", nullptr, false, hasTC)) {
                const TC      oldTc = *pW->GetComponent<TC>(entity);
                auto*         pH    = &mHost;
                const Entity  capE  = entity;
                auto applyTc = [pH, capE](const TC& v) {
                    auto* w = pH->scene.pWorld.get();
                    if (w == nullptr || !w->IsValid(capE)) { return; }
                    auto* t = w->GetComponent<TC>(capE);
                    if (t == nullptr) { return; }
                    *t = v;
                    // 让 Inspector 旋转 euler 缓存重读重置后的值。
                    pH->selection.transformEulerCacheEntity = Entity::Invalid();
                };
                mHost.cmdStack.Push(std::make_unique<LambdaCommand>(
                    "Reset Transform",
                    [applyTc]()        { applyTc(TC{}); },
                    [applyTc, oldTc]() { applyTc(oldTc); }));
            }
        }
        // 剪贴板 / 复制组（hierarchy gap §3）：键盘已有 Ctrl+C/X/V/D，这里补右键
        // 入口（快捷键不可见，菜单提供可发现性）。统一作用于"右键的这个节点"——
        // Copy/Cut 自包含序列化它的子树；Duplicate/Paste 先把选中切到该节点（清
        // additional）再走帧末 pendingDuplicate/pendingPaste，保证"右键谁就对谁"。
        {
            auto serializeSubtree = [&](Orange::Engine::Entity root) {
                Orange::Engine::Scene::SaveOptions o;
                o.assetRegistry          = mHost.assets.pAssets.get();
                o.namedMaterialInstances = &mHost.assets.namedMaterialInstances;
                o.extraSerializers       = mHost.extraSerializers;
                const std::vector<Orange::Engine::Entity> rs{root};
                return Orange::Engine::Scene::SaveSubtreeToString(
                    *mHost.scene.pWorld, rs, o);
            };
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                auto r = serializeSubtree(entity);
                if (r.IsOk()) { mEntityClipboard = r.Value(); }
            }
            if (ImGui::MenuItem("Cut", "Ctrl+X")) {
                auto r = serializeSubtree(entity);
                if (r.IsOk()) {
                    mEntityClipboard              = r.Value();
                    mHost.selection.pendingDelete = entity;
                }
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, !mEntityClipboard.empty())) {
                mHost.selection.selectedEntity = entity;
                mHost.selection.ClearAdditional();
                mHost.selection.pendingPaste = true;
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                mHost.selection.selectedEntity = entity;
                mHost.selection.ClearAdditional();
                mHost.selection.pendingDuplicate = true;
            }
            // Create Prefab...：从右键的这个子树创建 .prefab.json。MVP 单根
            // （多选只取右键的 entity）。仅登记跨帧请求 + 记源根；modal 由
            // DrawAssetsPanel 末尾承接（不在 context popup 内直接 OpenPopup，
            // 同 Create Material 的 sPendingOpenCreateMaterial pattern）。
            if (ImGui::MenuItem("Create Prefab...")) {
                Orange::Editor::Prefab::RequestCreatePrefab(entity);
            }
        }
        // 批量重命名（hierarchy gap §2 / P2）：右键的是 primary 且多选 → 给整个
        // 选区按 "base_NNN" 编号重命名（3 位零填充，primary 起 001，顺序 = primary
        // 后接 additional）。每实体一条 RenameCommand（可 undo），整批一个 group。
        if (entity == mHost.selection.selectedEntity
            && !mHost.selection.additionalSelectedEntities.empty()) {
            const std::size_t selN = 1 + mHost.selection.additionalSelectedEntities.size();
            if (ImGui::BeginMenu("Batch rename")) {
                static char sBatchBaseBuf[96] = "Entity";
                ImGui::SetNextItemWidth(ImGui::CalcTextSize("MMMMMMMMMMMMMM").x);
                ImGui::InputText("##batch_base", sBatchBaseBuf, sizeof(sBatchBaseBuf));
                ImGui::TextDisabled("-> %s_001 .. _%03zu (%zu entities)",
                                    sBatchBaseBuf, selN, selN);
                ImGui::BeginDisabled(sBatchBaseBuf[0] == '\0');
                if (ImGui::Button("Apply")) {
                    std::vector<Orange::Engine::Entity> order;
                    order.push_back(mHost.selection.selectedEntity);
                    for (const auto a : mHost.selection.additionalSelectedEntities) {
                        order.push_back(a);
                    }
                    mHost.cmdStack.BeginGroup("Batch rename", MergeMode::Disable);
                    int idx = 1;
                    for (const auto e : order) {
                        if (mHost.scene.pWorld->IsValid(e)) {
                            const auto* nc = mHost.scene.pWorld->GetComponent<
                                Orange::Engine::Scene::NameComponent>(e);
                            const std::string oldName =
                                (nc != nullptr) ? nc->name : std::string{};
                            char numbered[128];
                            std::snprintf(numbered, sizeof(numbered),
                                          "%s_%03d", sBatchBaseBuf, idx);
                            if (oldName != numbered) {
                                mHost.cmdStack.Push(std::make_unique<RenameCommand>(
                                    mHost, e, oldName, std::string{numbered}));
                            }
                        }
                        ++idx;
                    }
                    mHost.cmdStack.EndGroup();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
                ImGui::EndMenu();
            }
        }
        if (ImGui::MenuItem("Delete", "Del")) {
            mHost.selection.pendingDelete = entity;
        }
        // 根节点专属：Move Up / Move Down 调整根序（HierarchyComponent.sortIndex，
        // ADR-014）。仅根（parent==Invalid）显示——非根顺序由兄弟链 DnD 管。直接
        // 执行（改 sortIndex 不删/加 entity，不破坏本帧 tree 迭代，下帧按新序枚举）；
        // 边界 no-op（MoveRootRelative 返回 false）不记 Undo。
        {
            const auto* nodeHc = mHost.scene.pWorld->GetComponent<
                Orange::Engine::Scene::HierarchyComponent>(entity);
            const bool isRoot = (nodeHc == nullptr || !nodeHc->parent.IsValid());
            if (isRoot) {
                ImGui::Separator();
                const auto rootMove = [&](int delta) {
                    // 用 dryRun 预检"能否移动"——**不在此真执行**；真正的移动只交给
                    // 下面命令栈 Push 的 Execute 跑一次。否则"判断时执行一次 + 命令
                    // 栈 Execute 再执行一次" = 移两位，reorder 直接跳顶/底
                    // （BUG-2026-06-01-root-reorder-double-apply）。
                    if (EditorHierarchy::MoveRootRelative(*mHost.scene.pWorld, entity,
                                                          delta, /*dryRun*/ true)) {
                        Orange::Engine::World* pW = &(*mHost.scene.pWorld);
                        const Orange::Engine::Entity capE = entity;
                        mHost.cmdStack.Push(std::make_unique<LambdaCommand>(
                            "MoveRoot",
                            [pW, capE, delta]() {
                                EditorHierarchy::MoveRootRelative(*pW, capE, delta);
                            },
                            [pW, capE, delta]() {
                                EditorHierarchy::MoveRootRelative(*pW, capE, -delta);
                            }));
                    }
                };
                if (ImGui::MenuItem("Move Up"))   { rootMove(-1); }
                if (ImGui::MenuItem("Move Down")) { rootMove(+1); }
            }
        }
        // Lock / Unlock 切换（hierarchy gap §3 P1）：锁定 = 不可 pick/tree-click
        // 选中、不可拖拽（防误编辑）；session-only，经本菜单解锁。
        {
            const bool locked = IsEntityLocked(entity);
            if (ImGui::MenuItem(locked ? "Unlock" : "Lock")) {
                if (locked) {
                    for (auto it = mLockedEntities.begin(); it != mLockedEntities.end(); ++it) {
                        if (*it == entity) { mLockedEntities.erase(it); break; }
                    }
                } else {
                    mLockedEntities.push_back(entity);
                    // 锁定时若它正被选中，清掉（保持"锁定=不可选"一致）。
                    if (mHost.selection.selectedEntity == entity) {
                        mHost.selection.selectedEntity = Orange::Engine::Entity::Invalid();
                    }
                }
            }
        }
        // Show / Hide 切换（hierarchy gap §3 P1）：隐藏 = render 跳过这一个
        // entity（经 WorldPartition per-entity hidden override，RenderScene
        // 已逐 entity 调 IsEntityVisible）；session-only，不序列化，仍可选中
        // / 编辑（与 Lock 正交：Lock 管"能不能动"，Hide 管"画不画"）。
        {
            const bool hidden =
                mHost.scene.partition.IsEntityHidden(entity);
            if (ImGui::MenuItem(hidden ? "Show" : "Hide")) {
                mHost.scene.partition.SetEntityHidden(entity, !hidden);
            }
        }
        // Isolate Selected（hierarchy gap §3，聚焦工作流，复用 per-entity hidden
        // 基建）：把 target 子树以外的所有实体隐藏，只留 target + 后代可见。
        // target = 右键的是 primary 且多选 → 整个选区；否则 = {entity}（与批删/
        // 批移同款"右键 primary 消费 additional"规则）。可经 Unhide All 还原。
        if (ImGui::MenuItem("Isolate Selected")) {
            std::vector<Orange::Engine::Entity> targets;
            if (entity == mHost.selection.selectedEntity
                && !mHost.selection.additionalSelectedEntities.empty()) {
                targets.push_back(mHost.selection.selectedEntity);
                for (const auto a : mHost.selection.additionalSelectedEntities) {
                    targets.push_back(a);
                }
            } else {
                targets.push_back(entity);
            }
            std::vector<Orange::Engine::Entity> keep;
            for (const auto t : targets) {
                CollectSubtree(*mHost.scene.pWorld, t, keep);
            }
            auto inKeep = [&](Orange::Engine::Entity e) {
                for (const auto k : keep) { if (k == e) { return true; } }
                return false;
            };
            // 枚举全实体（含折叠 / 不在 tree flat order 的），逐个定显隐。
            auto& reg = mHost.scene.pWorld->Registry();
            for (auto raw : reg.view<entt::entity>()) {
                const auto e = Orange::Engine::World::FromEntt(raw);
                mHost.scene.partition.SetEntityHidden(e, !inKeep(e));
            }
        }
        ImGui::Separator();
        // v0.6 c5："Move to layer >" 子菜单 —— 遍历 partition.GetLayers()
        // 列出所有 layer；点击 = SetLayerOf via cmdStack（可 Undo）。
        // 当前归属用 "(current)" 后缀标识，避免无意义的"改归同 layer"
        // 命令污染 undo 栈。
        if (ImGui::BeginMenu("Move to layer")) {
            const std::string_view curId =
                mHost.scene.partition.GetLayerOf(*mHost.scene.pWorld, entity);
            const auto& allLayers = mHost.scene.partition.GetLayers();
            for (const auto& info : allLayers) {
                const bool isCurrent = (info.id == curId);
                const std::string item = info.displayName.empty()
                                             ? info.id
                                             : info.displayName;
                const std::string label = isCurrent
                                              ? (item + "  (current)")
                                              : item;
                if (ImGui::MenuItem(label.c_str(), nullptr, false, !isCurrent)) {
                    const std::string newId = info.id;
                    auto*             pH    = &mHost;
                    // 批量 move-to-layer（hierarchy gap 报告 §3 P0）：右键的是
                    // primary 且多选 → 移整个选区；否则单个。每 entity 记自身
                    // oldId，整批打成一条 LambdaCommand = 一次 Undo。
                    std::vector<std::pair<Orange::Engine::Entity, std::string>> moves;
                    auto addMove = [&](Orange::Engine::Entity e) {
                        if (!mHost.scene.pWorld->IsValid(e)) { return; }
                        std::string old{mHost.scene.partition.GetLayerOf(*mHost.scene.pWorld, e)};
                        if (old != newId) { moves.emplace_back(e, std::move(old)); }
                    };
                    addMove(entity);
                    if (entity == mHost.selection.selectedEntity) {
                        for (const auto a : mHost.selection.additionalSelectedEntities) {
                            addMove(a);
                        }
                    }
                    if (!moves.empty()) {
                        mHost.cmdStack.Push(std::make_unique<LambdaCommand>(
                            "set_entity_layer",
                            [pH, moves, newId]() {
                                if (auto* pW = pH->scene.pWorld.get()) {
                                    for (const auto& [e, oldId] : moves) {
                                        if (pW->IsValid(e)) {
                                            pH->scene.partition.SetLayerOf(*pW, e, newId);
                                        }
                                    }
                                }
                            },
                            [pH, moves]() {
                                if (auto* pW = pH->scene.pWorld.get()) {
                                    for (const auto& [e, oldId] : moves) {
                                        if (pW->IsValid(e)) {
                                            pH->scene.partition.SetLayerOf(*pW, e, oldId);
                                        }
                                    }
                                }
                            }
                        ));
                    }
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    if (open) {
        // 遍历兄弟链，递归
        if (h != nullptr) {
            Orange::Engine::Entity child = h->firstChild;
            while (child.IsValid()) {
                DrawEntityNodeRecursive(child);
                const auto* ch = mHost.scene.pWorld->GetComponent<HC>(child);
                child = (ch != nullptr) ? ch->nextSibling
                                        : Orange::Engine::Entity::Invalid();
            }
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void EditorRenderLayer::BeginRename(Orange::Engine::Entity entity)
{
    const auto* name = mHost.scene.pWorld->GetComponent<
        Orange::Engine::Scene::NameComponent>(entity);
    const std::string& src = (name != nullptr) ? name->name : std::string{};
    const std::size_t  n   = std::min(src.size(), sizeof(mHost.selection.renameBuffer) - 1);
    std::memcpy(mHost.selection.renameBuffer, src.data(), n);
    mHost.selection.renameBuffer[n]   = '\0';
    mHost.selection.renamingEntity    = entity;
    mHost.selection.renameJustStarted = true;
}

void EditorRenderLayer::CommitRename(Orange::Engine::Entity entity)
{
    if (mHost.scene.pWorld == nullptr || !entity.IsValid()) {
        CancelRename();
        return;
    }
    mHost.selection.renameBuffer[sizeof(mHost.selection.renameBuffer) - 1] = '\0';
    const auto* nc = mHost.scene.pWorld->GetComponent<
        Orange::Engine::Scene::NameComponent>(entity);
    const std::string oldName = (nc != nullptr) ? nc->name : std::string{};
    const std::string newName = mHost.selection.renameBuffer;
    if (oldName != newName) {
        mHost.cmdStack.Push(std::make_unique<RenameCommand>(
            mHost, entity, oldName, newName));
    }
    CancelRename();
}

void EditorRenderLayer::CancelRename()
{
    mHost.selection.renamingEntity    = Orange::Engine::Entity::Invalid();
    mHost.selection.renameJustStarted = false;
    mHost.selection.renameBuffer[0]   = '\0';
}
