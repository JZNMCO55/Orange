#ifndef ORANGE_EDITOR_CONTEXT_EDITOR_SELECTION_H
#define ORANGE_EDITOR_CONTEXT_EDITOR_SELECTION_H

// EditorSelection —— 选中 entity + 帧末延迟操作 + Inspector 编辑相关 UI 缓存。
//
// v0.2.5 整骨：从原 19 字段的 god struct EditorState 拆出 selection 子域。
//
// 字段归属 rationale：
//   * selectedEntity / pendingDelete / pendingReparent / pendingCreate 都是
//     selection 域："当前选了谁" + "帧末对选中实体或 hierarchy 做什么"
//   * rename 三件套属 selection——rename 永远绑当前选中 entity
//   * transformEulerCache 属 selection——它与 selectedEntity 联动（切实体
//     时缓存失效需要从 quat 重算 Euler）。editor-roadmap.md v0.2.5 deliverable
//     里写 "EditorCameraState（轨道相机 + Euler cache）" 字眼让人误以为
//     Euler 是相机角度——实际是 Transform 段 DragFloat3 的编辑缓存，与
//     camera 完全无关，归 selection 才是语义对的位置。
//
// 树状结构上的破坏性 / 增加操作（DestroySubtree / Reparent / Create）
// 不能在递归 draw 中即时执行 —— 会破坏当前遍历的 sibling 链 / EnTT view
// 迭代器。先在面板里记下"本帧应执行什么"，draw 结束后统一 apply。

#include <orange/engine/scene/Entity.h>

#include <glm/vec3.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

struct EditorSelection
{
    Orange::Engine::Entity selectedEntity = Orange::Engine::Entity::Invalid();

    // v0.8 多选实体：除 selectedEntity（primary）外的其它选中 entity 集合。
    // Ctrl-click 在 EntityTree 内 toggle 添加 / 移除。Shift-click 范围选择
    // 在 v0.8 范围外（需要 tree 节点扁平化排序）。
    // 语义：
    //   * 单选场景：selectedEntity = X / additionalSelectedEntities 空
    //   * 多选场景：selectedEntity = primary（最后一次点击的，gizmo
    //     pivot / Inspector 主体） / additionalSelectedEntities = 其余
    //   * 任何切 primary 路径（regular click / right-click context）都应
    //     清空 additionalSelectedEntities（Lumix / Unity 同款）
    std::vector<Orange::Engine::Entity> additionalSelectedEntities;

    bool IsSelected(Orange::Engine::Entity e) const noexcept
    {
        if (e == selectedEntity)
        {
            return true;
        }
        for (const auto& a : additionalSelectedEntities)
        {
            if (a == e)
            {
                return true;
            }
        }
        return false;
    }

    std::size_t SelectedCount() const noexcept
    {
        return selectedEntity.IsValid() ? (1 + additionalSelectedEntities.size()) : 0;
    }

    // Toggle entity in additional set。Ctrl-click primary 自身 = 取消它：
    // promote 最后一个 additional 为新 primary（无 additional 则清空选中）——
    // 与 Unity/Unreal "Ctrl 点已选项取消选择"一致（hierarchy gap §4 quick-win #1）。
    void ToggleAdditional(Orange::Engine::Entity e)
    {
        if (e == selectedEntity)
        {
            if (!additionalSelectedEntities.empty())
            {
                selectedEntity = additionalSelectedEntities.back();
                additionalSelectedEntities.pop_back();
            }
            else
            {
                selectedEntity = Orange::Engine::Entity::Invalid();
            }
            return;
        }
        auto it = std::find(additionalSelectedEntities.begin(),
                            additionalSelectedEntities.end(), e);
        if (it != additionalSelectedEntities.end())
        {
            additionalSelectedEntities.erase(it);
        }
        else
        {
            additionalSelectedEntities.push_back(e);
        }
    }

    void ClearAdditional() noexcept
    {
        additionalSelectedEntities.clear();
    }

    Orange::Engine::Entity pendingDelete = Orange::Engine::Entity::Invalid();

    // Ctrl+D：帧末复制 primary 的子树（序列化 + 重映射内部引用，见
    // SceneSerialization::SaveSubtreeToString / LoadFromString）。
    bool pendingDuplicate = false;

    // Ctrl+V：帧末从 EditorRenderLayer::mEntityClipboard 粘贴一份子树。
    bool pendingPaste = false;

    struct PendingReparent
    {
        // drop 落点：Into = 挂为 newParent 末子（newParent Invalid = 提到 root）；
        // Before/After = 插到 refSibling 同父链的前 / 后（兄弟重排，或跨父定位）。
        enum class Where : std::uint8_t
        {
            IntoAsLastChild = 0,
            BeforeSibling   = 1,
            AfterSibling    = 2,
        };
        Orange::Engine::Entity child;
        Orange::Engine::Entity newParent  = Orange::Engine::Entity::Invalid(); // Into 用
        Orange::Engine::Entity refSibling = Orange::Engine::Entity::Invalid(); // Before/After 用
        Where                  where      = Where::IntoAsLastChild;
        bool                   valid      = false;
    } pendingReparent;

    enum class PendingCreateKind : std::uint8_t
    {
        Empty = 0, // Name + Transform，用户后续手动 + Add Component
        Light,     // Name + Transform + DirectionalLight + Renderable(cube + emissive)
                   // —— 一键搭出"看得见的发光物体"
        // 基本体（"3D Object" 子菜单，Unity/Godot 同款一键可见几何）：Name +
        // Transform + Renderable(对应内置 mesh + pbr 材质)。省去"Create Entity →
        // Add Renderable → 选 mesh"三步，灰盒搭场景高频入口。
        Cube,   // cubeMeshHandle
        Sphere, // sphereMeshHandle（lat/lon UV sphere）
        Plane,  // planeMeshHandle（地面 / 墙面）
    };

    struct PendingCreate
    {
        Orange::Engine::Entity parent; // Invalid = 创建为 root；否则挂为该 parent 末子
        PendingCreateKind      kind  = PendingCreateKind::Empty;
        bool                   valid = false;
    } pendingCreate;

    // 内联重命名状态：renamingEntity 标记当前正在重命名哪个 entity，
    // renameBuffer 是 InputText 编辑缓冲。renameJustStarted 让首帧自动
    // 抢键盘焦点（SetKeyboardFocusHere），之后归 false 让用户能正常点击
    // 撤销编辑。
    Orange::Engine::Entity renamingEntity    = Orange::Engine::Entity::Invalid();
    char                   renameBuffer[256] = {};
    bool                   renameJustStarted = false;

    // Transform Euler 编辑缓存（degrees）。UI 用 Euler 输入比 quat 4 字段
    // 直观，但 quat→Euler 在 gimbal lock 附近不连续，用户在 DragFloat3 上
    // 滑动时显示值会跳。所以编辑期把 Euler 缓存到 selection；切 entity 时
    // 才从 quat 重算一次，DragFloat3 写 cache，cache 改了再把 quat 重算回
    // component。
    Orange::Engine::Entity transformEulerCacheEntity =
        Orange::Engine::Entity::Invalid();
    glm::vec3 transformEulerCache{0.0f, 0.0f, 0.0f};
};

#endif // ORANGE_EDITOR_CONTEXT_EDITOR_SELECTION_H
