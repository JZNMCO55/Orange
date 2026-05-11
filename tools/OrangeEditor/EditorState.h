#ifndef ORANGE_EDITOR_EDITOR_STATE_H
#define ORANGE_EDITOR_EDITOR_STATE_H

// EditorState —— 跨面板共享的编辑器状态：World 所有权 + 选中 entity +
// 文件路径 + pending 操作 + 内联重命名 + Transform Euler 缓存 + 编辑器
// 自管 AssetRegistry/MaterialSystem + 内置 mesh 句柄 + Floor/Wall material
// instance + viewport-local EditorCamera 状态。
//
// 持有指针 / 引用而非值，理由：
//   * World 体量比较大（包 entt::registry），按值带容易拖累 EditorRenderLayer
//     的构造期；
//   * 多个面板（Entity Tree / Inspector / Scene viewport）将读写同一份
//     selectedEntity —— 引用传递天然让所有面板看到同一份状态。
//
// 后续 task 加 rename buffer / clipboard / undo stack 等 UI-side 状态时，
// 全部往这个结构里追加；不应进 engine 公共 API。
//
// 本头仅供 tools/OrangeEditor/ 内部 TU 包含（CMake target_include_directories
// 不暴露 tools/，外部代码不会撞到）。

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/World.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <string>

// 帧末统一执行的场景级操作。把"用户从菜单点了 New / Open / ..."与模态
// 文件对话框 + 真正 swap world 的执行分开，避免在 ImGui frame 中间或
// EnTT view 迭代中触发模态阻塞 / mutate registry。
enum class SceneOp : std::uint8_t
{
    None = 0,
    New,
    Open,
    Save,
    SaveAs,
};

struct EditorState
{
    // 编辑器持有 World 所有权 —— Task 06-07 起场景 Open / New 需要在
    // OnUpdate 内整体 swap world，必须放在 state 里让 layer 能直接
    // reset / replace。之前是 main() 拥有 + state 持裸指针，重构理由见
    // Task 06-07 提交。
    std::unique_ptr<Orange::Engine::World> pWorld;
    Orange::Engine::Entity selectedEntity = Orange::Engine::Entity::Invalid();

    // 当前 scene 文件路径（绝对路径，UTF-8）；空 = 尚未保存过 / "Untitled"。
    // Save 走 currentScenePath；空时回退到 SaveAs 流程。
    std::string currentScenePath;

    SceneOp pendingSceneOp = SceneOp::None;

    // 内联重命名状态：renamingEntity 标记当前正在重命名哪个 entity，
    // renameBuffer 是 InputText 编辑缓冲。renameJustStarted 让首帧自动
    // 抢键盘焦点（SetKeyboardFocusHere），之后归 false 让用户能正常点击
    // 撤销编辑。
    Orange::Engine::Entity renamingEntity    = Orange::Engine::Entity::Invalid();
    char                   renameBuffer[256] = {};
    bool                   renameJustStarted = false;

    // 树状结构上的破坏性 / 增加操作不能在递归 draw 中即时执行 —— 会破
    // 坏当前遍历的 sibling 链 / EnTT view 迭代器（CreateEntity 会修改
    // entity storage）。先在面板里记下"本帧应执行什么"，draw 结束后统
    // 一 apply。
    Orange::Engine::Entity pendingDelete = Orange::Engine::Entity::Invalid();
    struct PendingReparent
    {
        Orange::Engine::Entity child;
        Orange::Engine::Entity newParent;  // Invalid 表示提到 root
        bool                   valid = false;
    } pendingReparent;
    enum class PendingCreateKind : std::uint8_t
    {
        Empty = 0,    // Name + Transform，用户后续手动 + Add Component
        Light,        // Name + Transform + DirectionalLight + Renderable(cube + emissive)
                      // —— 一键搭出"看得见的发光物体"
    };
    struct PendingCreate
    {
        Orange::Engine::Entity parent;  // Invalid = 创建为 root；否则挂为该 parent 末子
        PendingCreateKind      kind  = PendingCreateKind::Empty;
        bool                   valid = false;
    } pendingCreate;

    // Transform rotation 编辑的 Euler 角缓存（degrees）。原因：UI 用 Euler
    // 输入比 quat 4 字段直观，但 quat→Euler 在 gimbal lock 附近不连续，
    // 用户在 DragFloat3 上滑动时显示值会跳。所以编辑期把 Euler 缓存到
    // EditorState，每次 selectedEntity 切换才从 quat 重新算一次；DragFloat3
    // 写 cache，cache 改了再把 quat 重算回 component。
    Orange::Engine::Entity transformEulerCacheEntity =
        Orange::Engine::Entity::Invalid();
    glm::vec3              transformEulerCache{0.0f, 0.0f, 0.0f};

    // ---- 编辑器自管 AssetRegistry + MaterialSystem ----------------------
    // Phase 6 / Task 06-08 S2：SeedDemoWorld 给 Floor / Wall 实体挂真 mesh
    // + textured material，让 Scene 视口立刻能看到几何。AssetRegistry
    // 与 MaterialSystem 由编辑器持有所有权 —— 它们的生命周期必须长于任何
    // 引用其中 mesh handle / material instance 的 World，因此放进 EditorState。
    //
    // 注意：场景 Save / Load 当前不串联 AssetRegistry，存盘的 scene JSON
    // 里的 mesh / material 引用对应的是 *本次启动* 创建的内置 handle，
    // 跨进程加载语义还需要后续 task 把 AssetRegistry 也参与序列化。
    std::unique_ptr<Orange::Engine::Asset::AssetRegistry>   pAssets;
    std::unique_ptr<Orange::Engine::Render::MaterialSystem> pMaterials;

    // 内置 mesh handle —— SeedDemoWorld 给 Floor 用 plane / Wall 用 cube。
    Orange::Engine::Asset::AssetHandle<Orange::Engine::Asset::MeshAsset>
        cubeMeshHandle  {};
    Orange::Engine::Asset::AssetHandle<Orange::Engine::Asset::MeshAsset>
        planeMeshHandle {};

    // SeedDemoWorld 用的 textured material 实例 —— Floor / Wall 各一个
    // （地址要稳定供 RenderableComponent::materialInstance 持有），生命周期
    // 跟着 EditorState 走。
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pFloorMaterial;
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pWallMaterial;

    // "Create Light Object" / "Add Renderable Component" 等编辑器创建路径
    // 共用的默认 material instance：
    //   * pDefaultRenderableMaterial = textured ——"+ Add Component → Renderable"
    //     时默认绑这个，让新挂的 Renderable 立刻能看到（而非 mesh=Invalid /
    //     material=nullptr 的空挂）；
    //   * pLightObjectMaterial = emissive ——"Create Light Object" 把灯做
    //     成发光的可见物体（cube + emissive material）。
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pDefaultRenderableMaterial;
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pLightObjectMaterial;

    // ---- 编辑器相机（Phase 6 / Task 06-08 S3）---------------------------
    // viewport-local 相机状态：position + yaw/pitch + 投影参数。每帧由
    // DrawScenePanel 读 ImGui 输入更新本结构，再 Compute 出 Camera 写到
    // World 里 Camera 组件第一个出现的实体上 —— Camera 组件不在
    // SceneSerialization 路径上，本写入对 Save / Load 透明，不会"污染"用
    // 户场景。
    //
    // 控制约定（与 DrawScenePanel 内的输入捕获保持一致）：
    //   * 鼠标右键拖动（hover Scene 面板时按下）—— 旋转 yaw / pitch
    //   * 滚轮（hover Scene 面板时）—— 沿 forward 方向距离 + / -
    //   * WASD（focus Scene 面板时）—— 相对相机朝向水平移动
    //   * Q / E（focus Scene 面板时）—— 世界 Y 上 / 下
    struct EditorCamera
    {
        glm::vec3 position{3.0f, 2.5f, 5.0f};
        float     yaw           = -0.541f;   // 弧度；与 SeedDemoWorld 旧 lookAt 一致
        float     pitch         = -0.330f;   // 弧度
        float     fovYDegrees   = 50.0f;
        float     zNear         = 0.1f;
        float     zFar          = 100.0f;
        // 操作灵敏度（编辑器经验值，未来可暴露给 Preferences）
        float     moveSpeed       = 4.0f;     // units / sec
        float     lookSensitivity = 0.0035f;  // 弧度 / pixel
        float     zoomSensitivity = 0.6f;     // units / wheel notch

        // RMB 拖动状态机：按下时（且鼠标在 Scene 面板内）置 true，进入"无
        // 论鼠标是否仍 hover 都吃 MouseDelta"模式；释放时清零。修复"拖动
        // 快了鼠标滑出面板 → IsWindowHovered 返回 false → 旋转中断"的体感
        // 问题，与 Unity SceneView / Unreal viewport 通行的 capture-on-press
        // 模式一致。
        bool      dragging = false;
    } editorCamera;
};

#endif  // ORANGE_EDITOR_EDITOR_STATE_H
