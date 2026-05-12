// Entity Tree 面板（递归绘制 + DnD + rename 状态机）。
// 与 EditorRenderLayer.cpp 主 TU 共享同一类声明 EditorRenderLayer。

#include "../EditorRenderLayer.h"

#include "../EditorHierarchy.h"
#include "../command/EntityCommands.h"
#include "../command/LambdaCommand.h"

#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <entt/entity/registry.hpp>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>

void EditorRenderLayer::DrawEntityTreePanel()
{
    ImGui::Begin("Entity Tree");
    if (mHost.scene.pWorld == nullptr) {
        ImGui::TextDisabled("(no world bound)");
        ImGui::End();
        return;
    }

    // Play / Paused 期间禁止结构性编辑；Edit 态才允许。
    // 这里统一给"一帧内所有结构性操作的入口"加防护；帧末 apply 处不
    // 再重复检查 —— 保证 pendingXxx 只在 canEdit 为 true 时被写入。
    const bool canEdit = (mHost.scene.playState == PlayState::Edit);

    // 全局快捷键：F2 重命名选中、Del 删除选中。重命名进行中不响应
    // —— 否则 InputText 里按 Del 删字符会同时触发实体删除。
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    // Entity::IsValid() 只检测哨兵 null；Undo 可能已销毁实体，补一次
    // World::IsValid 以防操作死实体触发 EnTT assert / UB。
    if (canEdit && focused
        && !mHost.selection.renamingEntity.IsValid()
        && mHost.selection.selectedEntity.IsValid()
        && mHost.scene.pWorld->IsValid(mHost.selection.selectedEntity))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
            BeginRename(mHost.selection.selectedEntity);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            mHost.selection.pendingDelete = mHost.selection.selectedEntity;
        }
    }

    // 列出所有 root 实体（无 HierarchyComponent 或 parent invalid），
    // 然后递归画子树。EnTT view 遍历的是组件存储不是创建顺序 —— 编
    // 辑器侧不关心顺序稳定性（同根实体在两帧之间显示位置可能不同），
    // 后续 task 真要稳定排序时再加 SortIndex 之类。
    auto& reg = mHost.scene.pWorld->Registry();
    using HC = Orange::Engine::Scene::HierarchyComponent;
    for (auto e : reg.view<entt::entity>()) {
        const auto* h = reg.try_get<HC>(e);
        const bool isRoot = (h == nullptr) || !h->parent.IsValid();
        if (isRoot) {
            DrawEntityNodeRecursive(Orange::Engine::World::FromEntt(e));
        }
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
                mHost.selection.pendingReparent = {src,
                                          Orange::Engine::Entity::Invalid(),
                                          true};
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
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    ImGui::End();

    // 帧末统一 apply pending 结构性操作 ——
    // 这两步必须在 tree 递归画完之后执行，否则会破坏当前帧的 sibling
    // 链遍历。同帧内 delete + reparent 同时发生时 delete 优先（被
    // delete 的实体即使有 pendingReparent 也失效）。
    if (mHost.selection.pendingDelete.IsValid()) {
        // 额外检查实体是否仍在 registry 中 —— Undo 可能已销毁它，此时
        // pendingDelete 持有的是死实体句柄，DestroySubtree 会崩溃。
        if (mHost.scene.pWorld->IsValid(mHost.selection.pendingDelete)) {
            if (mHost.selection.selectedEntity == mHost.selection.pendingDelete) {
                mHost.selection.selectedEntity = Orange::Engine::Entity::Invalid();
            }
            if (mHost.selection.renamingEntity == mHost.selection.pendingDelete) {
                CancelRename();
            }
            EditorHierarchy::DestroySubtree(*mHost.scene.pWorld, mHost.selection.pendingDelete);
            // 删除操作不可撤销（子树已析构）—— 清掉 undo 历史，防止后续 Undo
            // 尝试访问已销毁 entity 的 SetFieldValueCommand lambda。
            mHost.cmdStack.Clear();
        }
        mHost.selection.pendingDelete = Orange::Engine::Entity::Invalid();
        mHost.selection.pendingReparent.valid = false;  // 同帧 reparent 已无意义
    }
    if (mHost.selection.pendingReparent.valid) {
        const Orange::Engine::Entity src = mHost.selection.pendingReparent.child;
        const Orange::Engine::Entity dst = mHost.selection.pendingReparent.newParent;
        mHost.selection.pendingReparent.valid = false;
        // 防环 + 防自挂自 + 防"挂到当前父亲"重复操作
        if (src.IsValid() && mHost.scene.pWorld->IsValid(src) && src != dst
            && !EditorHierarchy::IsAncestorOf(*mHost.scene.pWorld, src, dst))
        {
            // 记录旧 parent，用于 Undo 还原层级关系。
            using HC = Orange::Engine::Scene::HierarchyComponent;
            const auto*                  hc        = mHost.scene.pWorld->GetComponent<HC>(src);
            const Orange::Engine::Entity oldParent = (hc != nullptr)
                ? hc->parent
                : Orange::Engine::Entity::Invalid();
            mHost.cmdStack.Push(std::make_unique<LambdaCommand>(
                "reparent",
                [pW = mHost.scene.pWorld.get(), src, dst]() {
                    EditorHierarchy::ReparentTo(*pW, src, dst);
                },
                [pW = mHost.scene.pWorld.get(), src, oldParent]() {
                    // Undo 时 src 可能已被其他命令销毁（EnTT version check）
                    if (pW->IsValid(src)) {
                        EditorHierarchy::ReparentTo(*pW, src, oldParent);
                    }
                }
            ));
        }
    }
    if (mHost.selection.pendingCreate.valid) {
        const Orange::Engine::Entity         parent         = mHost.selection.pendingCreate.parent;
        const EditorSelection::PendingCreateKind kind           = mHost.selection.pendingCreate.kind;
        const auto cubeMesh  = mHost.assets.cubeMeshHandle;
        auto* const pLightMat = mHost.assets.pLightObjectMaterial.get();
        mHost.selection.pendingCreate.valid = false;

        auto cmd = std::make_unique<CreateEntityCommand>(
            *mHost.scene.pWorld,
            [parent, kind, cubeMesh, pLightMat]
            (Orange::Engine::World& w) -> Orange::Engine::Entity
            {
                Orange::Engine::Entity e = w.CreateEntity();
                const char* initialName = (kind == EditorSelection::PendingCreateKind::Light)
                    ? "Light Object" : "New Entity";
                w.AddComponent<Orange::Engine::Scene::NameComponent>(
                    e, Orange::Engine::Scene::NameComponent{initialName});
                w.AddComponent<Orange::Engine::Scene::TransformComponent>(
                    e, Orange::Engine::Scene::TransformComponent{});

                if (kind == EditorSelection::PendingCreateKind::Light) {
                    // 一键搭出"可见的发光物体" —— DirectionalLight 提供光照贡献 +
                    // Renderable(cube + emissive material) 让灯本身在 Scene 视口
                    // 可见（不然方向光是看不见的）。
                    using ::Orange::Engine::Render::DirectionalLight;
                    using ::Orange::Engine::Render::RenderableComponent;
                    w.AddComponent<DirectionalLight>(e, DirectionalLight{});
                    RenderableComponent rc{};
                    rc.mesh             = cubeMesh;
                    rc.materialInstance = pLightMat;
                    rc.visible          = true;
                    rc.castsShadow      = false;
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
        BeginRename(e);
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
    using HC = Orange::Engine::Scene::HierarchyComponent;
    using NameComponent = Orange::Engine::Scene::NameComponent;

    const auto* h     = mHost.scene.pWorld->GetComponent<HC>(entity);
    const auto* name  = mHost.scene.pWorld->GetComponent<NameComponent>(entity);
    const bool  hasKid = (h != nullptr) && h->firstChild.IsValid();
    const bool  selected = (mHost.selection.selectedEntity == entity);
    const bool  renaming = (mHost.selection.renamingEntity == entity);

    ImGuiTreeNodeFlags flags =
          ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_OpenOnDoubleClick
        | ImGuiTreeNodeFlags_SpanAvailWidth
        | ImGuiTreeNodeFlags_DefaultOpen
        | ImGuiTreeNodeFlags_AllowOverlap;
    if (!hasKid)  { flags |= ImGuiTreeNodeFlags_Leaf; }
    if (selected) { flags |= ImGuiTreeNodeFlags_Selected; }

    // ID 用 entity 数值 —— 不依赖名字（重名/空名也稳定），并满足
    // "同一棵子树里不会重复" 的 ImGui ID 唯一性约束。
    ImGui::PushID(static_cast<int>(static_cast<std::uint32_t>(entity.Value())));

    bool open = false;
    // 节点级编辑权限 —— 与 DrawEntityTreePanel 顶部的 canEdit 同逻辑，
    // 但 DrawEntityNodeRecursive 是独立调用栈，所以这里重新取一次。
    const bool canEditNode = (mHost.scene.playState == PlayState::Edit);

    if (renaming) {
        // 空 label + SameLine InputText —— TreeNode 三角仍可用，
        // label 区域被 InputText 接管。
        open = ImGui::TreeNodeEx("##node", flags, "%s", "");
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
        open = ImGui::TreeNodeEx("##node", flags, "%s", label);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            mHost.selection.selectedEntity = entity;
        }
        // 双击 entry-body 进入重命名（Edit 态才允许）
        if (canEditNode
            && ImGui::IsItemHovered()
            && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
            && !ImGui::IsItemToggledOpen()) {
            BeginRename(entity);
        }
        // DnD source —— Play / Paused 期间禁止拖拽（防止触发 reparent）
        if (canEditNode
            && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(kEntityPayload, &entity, sizeof(entity));
            ImGui::Text("Move %s",
                        (name != nullptr && !name->name.empty())
                            ? name->name.c_str() : "(unnamed)");
            ImGui::EndDragDropSource();
        }
    }
    // 节点右键菜单 —— 选中始终允许；结构性操作（Create/Rename/Delete）
    // 受 canEditNode 约束。
    if (!renaming && ImGui::BeginPopupContextItem("##node_ctx")) {
        mHost.selection.selectedEntity = entity;
        ImGui::BeginDisabled(!canEditNode);
        if (ImGui::MenuItem("Create Child")) {
            mHost.selection.pendingCreate = {entity, EditorSelection::PendingCreateKind::Empty, true};
        }
        if (ImGui::MenuItem("Create Light Object (Child)")) {
            mHost.selection.pendingCreate = {entity, EditorSelection::PendingCreateKind::Light, true};
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename", "F2")) {
            BeginRename(entity);
        }
        if (ImGui::MenuItem("Delete", "Del")) {
            mHost.selection.pendingDelete = entity;
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    // DnD target —— Play / Paused 期间不接受 drop
    if (canEditNode && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p =
                ImGui::AcceptDragDropPayload(kEntityPayload)) {
            Orange::Engine::Entity src{};
            std::memcpy(&src, p->Data, sizeof(src));
            mHost.selection.pendingReparent = {src, entity, true};
        }
        ImGui::EndDragDropTarget();
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
            *mHost.scene.pWorld, entity, oldName, newName));
    }
    CancelRename();
}

void EditorRenderLayer::CancelRename()
{
    mHost.selection.renamingEntity    = Orange::Engine::Entity::Invalid();
    mHost.selection.renameJustStarted = false;
    mHost.selection.renameBuffer[0]   = '\0';
}
