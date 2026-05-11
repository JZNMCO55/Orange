// Entity Tree 面板（递归绘制 + DnD + rename 状态机）。
// 与 EditorRenderLayer.cpp 主 TU 共享同一类声明 EditorRenderLayer。

#include "../EditorRenderLayer.h"

#include "../EditorHierarchy.h"

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
    if (mState.pWorld == nullptr) {
        ImGui::TextDisabled("(no world bound)");
        ImGui::End();
        return;
    }

    // 全局快捷键：F2 重命名选中、Del 删除选中。重命名进行中不响应
    // —— 否则 InputText 里按 Del 删字符会同时触发实体删除。
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused && !mState.renamingEntity.IsValid() && mState.selectedEntity.IsValid()) {
        if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
            BeginRename(mState.selectedEntity);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            mState.pendingDelete = mState.selectedEntity;
        }
    }

    // 列出所有 root 实体（无 HierarchyComponent 或 parent invalid），
    // 然后递归画子树。EnTT view 遍历的是组件存储不是创建顺序 —— 编
    // 辑器侧不关心顺序稳定性（同根实体在两帧之间显示位置可能不同），
    // 后续 task 真要稳定排序时再加 SortIndex 之类。
    auto& reg = mState.pWorld->Registry();
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
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload(kEntityPayload)) {
                Orange::Engine::Entity src{};
                std::memcpy(&src, p->Data, sizeof(src));
                mState.pendingReparent = {src,
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
        if (ImGui::MenuItem("Create Entity (root)")) {
            mState.pendingCreate = {Orange::Engine::Entity::Invalid(), true};
        }
        ImGui::EndPopup();
    }

    ImGui::End();

    // 帧末统一 apply pending 结构性操作 ——
    // 这两步必须在 tree 递归画完之后执行，否则会破坏当前帧的 sibling
    // 链遍历。同帧内 delete + reparent 同时发生时 delete 优先（被
    // delete 的实体即使有 pendingReparent 也失效）。
    if (mState.pendingDelete.IsValid()) {
        if (mState.selectedEntity == mState.pendingDelete) {
            mState.selectedEntity = Orange::Engine::Entity::Invalid();
        }
        if (mState.renamingEntity == mState.pendingDelete) {
            CancelRename();
        }
        EditorHierarchy::DestroySubtree(*mState.pWorld, mState.pendingDelete);
        mState.pendingDelete = Orange::Engine::Entity::Invalid();
        mState.pendingReparent.valid = false;  // 同帧 reparent 已无意义
    }
    if (mState.pendingReparent.valid) {
        const Orange::Engine::Entity src = mState.pendingReparent.child;
        const Orange::Engine::Entity dst = mState.pendingReparent.newParent;
        mState.pendingReparent.valid = false;
        // 防环 + 防自挂自 + 防"挂到当前父亲"重复操作
        if (src.IsValid() && src != dst
            && !EditorHierarchy::IsAncestorOf(*mState.pWorld, src, dst))
        {
            EditorHierarchy::ReparentTo(*mState.pWorld, src, dst);
        }
    }
    if (mState.pendingCreate.valid) {
        const Orange::Engine::Entity parent = mState.pendingCreate.parent;
        mState.pendingCreate.valid = false;
        Orange::Engine::Entity e = mState.pWorld->CreateEntity();
        // 默认 component：Name + Transform —— 跟 SeedDemoWorld 里
        // make() lambda 行为一致，让新创建实体在 Inspector 里至少
        // 有这两段可看。
        mState.pWorld->AddComponent<Orange::Engine::Scene::NameComponent>(
            e, Orange::Engine::Scene::NameComponent{"New Entity"});
        mState.pWorld->AddComponent<Orange::Engine::Scene::TransformComponent>(
            e, Orange::Engine::Scene::TransformComponent{});
        if (parent.IsValid()) {
            EditorHierarchy::LinkAsLastChild(*mState.pWorld, parent, e);
        }
        mState.selectedEntity = e;
        // 自动进入 rename 模式：刚建出来用户最有可能想做的下一步是命
        // 名，省一次 F2。
        BeginRename(e);
    }
}

// 递归画一个实体节点 + 其子树。
//
// 用 TreeNodeEx + ImGuiTreeNodeFlags_OpenOnArrow：点叶身体当选中，点
// 三角才展开 —— 跟 Unity / Unreal 编辑器手感一致。Selected 状态从
// mState.selectedEntity 反映，点击任意节点写回。叶子节点（无 firstChild）
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
    using HC = Orange::Engine::Scene::HierarchyComponent;
    using NameComponent = Orange::Engine::Scene::NameComponent;

    const auto* h     = mState.pWorld->GetComponent<HC>(entity);
    const auto* name  = mState.pWorld->GetComponent<NameComponent>(entity);
    const bool  hasKid = (h != nullptr) && h->firstChild.IsValid();
    const bool  selected = (mState.selectedEntity == entity);
    const bool  renaming = (mState.renamingEntity == entity);

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
    if (renaming) {
        // 空 label + SameLine InputText —— TreeNode 三角仍可用，
        // label 区域被 InputText 接管。
        open = ImGui::TreeNodeEx("##node", flags, "%s", "");
        ImGui::SameLine();
        if (mState.renameJustStarted) {
            ImGui::SetKeyboardFocusHere();
            mState.renameJustStarted = false;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool entered = ImGui::InputText(
            "##rename", mState.renameBuffer, sizeof(mState.renameBuffer),
              ImGuiInputTextFlags_EnterReturnsTrue
            | ImGuiInputTextFlags_AutoSelectAll);
        if (entered) {
            CommitRename(entity);
        } else if (ImGui::IsItemDeactivated()) {
            // 失焦 = 取消（Esc / 点别处）。EnterReturnsTrue 已经走
            // 上面的分支，所以这里走的是非 Enter 的所有退出路径。
            CancelRename();
        }
    } else {
        const char* label = (name != nullptr && !name->name.empty())
            ? name->name.c_str()
            : "(unnamed)";
        open = ImGui::TreeNodeEx("##node", flags, "%s", label);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            mState.selectedEntity = entity;
        }
        // 双击 entry-body 进入重命名（不是双击三角 —— OpenOnDoubleClick
        // 让三角双击只切换展开）
        if (ImGui::IsItemHovered()
            && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
            && !ImGui::IsItemToggledOpen()) {
            BeginRename(entity);
        }
        // DnD source —— 只有非重命名态才允许拖拽
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(kEntityPayload, &entity, sizeof(entity));
            ImGui::Text("Move %s",
                        (name != nullptr && !name->name.empty())
                            ? name->name.c_str() : "(unnamed)");
            ImGui::EndDragDropSource();
        }
    }
    // 节点上的右键菜单 —— "Create Child" 把新实体挂为本节点末子；
    // Rename / Delete 把 F2 / Del 快捷键的等价入口挂上菜单。
    if (!renaming && ImGui::BeginPopupContextItem("##node_ctx")) {
        mState.selectedEntity = entity;
        if (ImGui::MenuItem("Create Child")) {
            mState.pendingCreate = {entity, true};
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename", "F2")) {
            BeginRename(entity);
        }
        if (ImGui::MenuItem("Delete", "Del")) {
            mState.pendingDelete = entity;
        }
        ImGui::EndPopup();
    }

    // DnD target —— 无论是否重命名都可接受 drop，把别的节点挂到本节点下
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p =
                ImGui::AcceptDragDropPayload(kEntityPayload)) {
            Orange::Engine::Entity src{};
            std::memcpy(&src, p->Data, sizeof(src));
            mState.pendingReparent = {src, entity, true};
        }
        ImGui::EndDragDropTarget();
    }

    if (open) {
        // 遍历兄弟链，递归
        if (h != nullptr) {
            Orange::Engine::Entity child = h->firstChild;
            while (child.IsValid()) {
                DrawEntityNodeRecursive(child);
                const auto* ch = mState.pWorld->GetComponent<HC>(child);
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
    const auto* name = mState.pWorld->GetComponent<
        Orange::Engine::Scene::NameComponent>(entity);
    const std::string& src = (name != nullptr) ? name->name : std::string{};
    const std::size_t  n   = std::min(src.size(), sizeof(mState.renameBuffer) - 1);
    std::memcpy(mState.renameBuffer, src.data(), n);
    mState.renameBuffer[n]   = '\0';
    mState.renamingEntity    = entity;
    mState.renameJustStarted = true;
}

void EditorRenderLayer::CommitRename(Orange::Engine::Entity entity)
{
    if (mState.pWorld == nullptr || !entity.IsValid()) {
        CancelRename();
        return;
    }
    mState.renameBuffer[sizeof(mState.renameBuffer) - 1] = '\0';
    Orange::Engine::Scene::NameComponent nc;
    nc.name = mState.renameBuffer;
    mState.pWorld->AddComponent<Orange::Engine::Scene::NameComponent>(
        entity, std::move(nc));
    CancelRename();
}

void EditorRenderLayer::CancelRename()
{
    mState.renamingEntity    = Orange::Engine::Entity::Invalid();
    mState.renameJustStarted = false;
    mState.renameBuffer[0]   = '\0';
}
