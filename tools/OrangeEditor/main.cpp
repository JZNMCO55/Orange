// OrangeEditor —— ImGui dock space + multi-viewport 起步骨架。
//
// 架构选择：
//   * AppHost / LayerStack —— 来自 OrangeEngine，提供窗口 + 事件分发 +
//     主循环
//   * RenderDevice + IRenderer —— 编辑器自管，**不**用 engine Pipeline。
//     原因：编辑器当前只渲染 ImGui，不渲染 3D scene；engine Pipeline 是
//     给 game 渲染场景用的，强行套上反而要做"自定义 RenderPass 注入 +
//     overlay 回调"两层中转。后续若组件检视器需要 scene viewport 预览，
//     再桥接 Pipeline → off-screen RT → ImGui::Image
//   * ImGui Vulkan / GLFW backend —— vendored via FetchContent (docking
//     branch v1.91.5)
//   * Vulkan loader 路径统一 —— ImGui 静态库以 IMGUI_IMPL_VULKAN_NO_PROTOTYPES
//     编译，启动期通过 `Interop::GetVulkanGetInstanceProcAddr()` 取
//     OrangeRender 内 volk 已加载的 loader fn 喂给 ImGui_ImplVulkan_LoadFunctions；
//     编辑器自身需要的 vk* 解析（descriptor pool 创建 / 销毁）也走这条
//     loader。原因详见 OrangeRender `docs/api_guide.md §6.8.1` 与
//     FEATURE-2026-05-10-vulkan-loader-export 的 CHANGELOG 条目。所有
//     Vulkan handle 仍由 `Interop::GetVulkanDeviceHandles` /
//     `Interop::GetVulkanSwapchainInfo` 提供
//   * 多视口（窗口可拖拽悬停成独立 native window）—— 启用 ImGuiConfigFlags
//     _DockingEnable + ViewportsEnable；ImGui 自带的 multi-viewport
//     platform / renderer interface 接管额外 viewport 的窗口 / swapchain
//     创建 + 渲染
//
// 当前 UI 内容：dock space 上五个固定占位面板 —— Scene / Entity Tree /
// Inspector / Assets / Console。前四个仅 TextDisabled 占位，由 Task 06-03
// 起逐个填实；Console 当前放帧统计 + Esc 退出按钮，等接 Core::Log 时换
// 成日志流。默认 dock 布局首帧通过 DockBuilder* 编程式建立，之后用户调
// 整由 imgui.ini 持久化。

// NOMINMAX / WIN32_LEAN_AND_MEAN 必须在**任何**可能传染 windows.h 的头之
// 前 define —— GLFW_EXPOSE_NATIVE_WIN32 + GLFW/glfw3native.h 会拉 windows.h，
// 后续 std::min / std::numeric_limits::max 会被 min/max 宏污染（实测 build
// 报 C2589 / C2737）。
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/platform/Window.h>
#include <orange/engine/platform/WindowEvent.h>
#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/gtc/matrix_transform.hpp>  // glm::lookAt（编辑器相机用）
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <orange/renderer/RenderDevice.h>
#include <orange/renderer/Renderer.h>
#include <orange/renderer/VulkanInterop.h>
#include <orange/rhi/RHICommandList.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <vulkan/vulkan.h>

// Windows IFileDialog —— 编辑器 File 菜单的 Open / Save As 走 native
// Common Item Dialog（COM）。NOMINMAX / WIN32_LEAN_AND_MEAN 已在文件顶
// 部 define（GLFW native header 早于此处 include），这里直接拉 windows.h
// + shobjidl 就行。
#include <windows.h>
#include <shobjidl.h>

#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder* API（仅在编辑器侧首帧建默认布局用）
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace
{

constexpr std::int32_t kEscapeKeyRaw = 256;  // GLFW_KEY_ESCAPE，与 Input::KeyCode::Escape 同值

// 编辑器默认 UI 字体 size（像素）。ImGui 内嵌 ProggyClean 默认 13px，在
// 1080p+ 屏上对编辑器使用偏小；这里拉到 18px。后续要做的扩展点：把这个
// 数值抽到一个"编辑器 Settings"面板里让用户运行时调整 —— 改后重建
// io.Fonts atlas 并触发 ImGui_ImplVulkan_CreateFontsTexture 重传到 GPU。
// 现在硬编码即可，后续 task 真做 Settings 面板时再抽。
constexpr float kDefaultFontSizePx = 28.0f;

// ImGui 在 NO_PROTOTYPES 编译下不再 extern 引用 vulkan-1.lib 的静态 vkXxx
// 符号，启动期通过 `ImGui_ImplVulkan_LoadFunctions(loader, userData)` 让
// loader 把它内部需要的 ~30 个 vk 函数指针逐个 resolve 出来。loader 必须
// 拿"真 VkInstance"才能解析 instance/device 级函数（passing NULL 仅对
// 4 个 global 函数有保证）。打包 (pfn, instance) 为 user_data。
//
// pfn 取自 `Interop::GetVulkanGetInstanceProcAddr()`——OrangeRender 内
// volk 已加载的 loader entry；VkInstance 取自 `Interop::GetVulkanDeviceHandles`。
// 这样 ImGui 与 OrangeRender 共用同一个 loader 解析路径，避免两条独立路径
// 让 instance dispatch 状态错位。
struct ImguiVulkanLoaderCtx
{
    PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr   pfnGetDeviceProcAddr;  // 二级回退用，可为 null
    VkInstance                vkInstance;
    VkDevice                  vkDevice;              // 二级回退用，可为 VK_NULL_HANDLE
};

PFN_vkVoidFunction ImguiVulkanLoader(const char* funcName, void* userData)
{
    const auto* ctx = static_cast<const ImguiVulkanLoaderCtx*>(userData);

    // ImGui docking v1.91.5 的 imgui_impl_vulkan.cpp（line ~1100）硬编码用
    // KHR 后缀名解析 dynamic rendering 两个命令：
    //     ImGuiImplVulkanFuncs_vkCmdBeginRenderingKHR =
    //         loader_func("vkCmdBeginRenderingKHR", user_data);
    //     ImGuiImplVulkanFuncs_vkCmdEndRenderingKHR =
    //         loader_func("vkCmdEndRenderingKHR", user_data);
    //
    // 这里我们**必须**拦截这两个名字并改用 vkGetDeviceProcAddr 解析到 core
    // 名字（无 KHR 后缀），原因是：
    //
    // [trampoline 陷阱] 1.3 SDK 的 Vulkan loader 在 `vkGetInstanceProcAddr(
    // inst, "vkCmdBeginRenderingKHR")` 上**总是**返回一个非 null 的 loader
    // trampoline —— 不管 device 有没有 enable VK_KHR_dynamic_rendering 扩展。
    // 调用时 trampoline 才去查 device dispatch 表里 KHR 槽位；OrangeRender
    // 启用的是 Vulkan 1.3 core 的 `dynamicRendering` feature，不是 KHR 扩
    // 展，KHR 槽位在 dispatch 表里是 null —— trampoline 一旦被调用就跳
    // 0x0000_0000_0000_0000 访问冲突。
    //
    // 实测现象：watch 窗口里 ImGuiImplVulkanFuncs_vkCmdBeginRenderingKHR
    // 显示 vulkan-1.dll!0x...3790（非 null，是 loader trampoline），但抛
    // 异常 0xC0000005 在 0x0000_0000_0000_0000 —— 进了 trampoline、查不到
    // dispatch、空跳。
    //
    // 主视口看不出问题：overlay callback 在 OrangeRender 外层 begin/end
    // rendering scope 里调 RenderDrawData，ImGui 不会自己 call KHR 入口；
    // OrangeRender 自己的 vkCmdBeginRendering 走的是 volk → vkGetDeviceProcAddr
    // 拿到的驱动直接函数指针，绕开 loader trampoline 那层。
    //
    // 出路就是这里 —— 解析 KHR 名时改用 vkGetDeviceProcAddr 拿 core 名字。
    // vkGetDeviceProcAddr 直接落到驱动 ICD，没有 loader trampoline 这层，
    // dispatch 不依赖扩展启用状态、只看 feature；core `dynamicRendering`
    // feature 已 enable，驱动会返回有效函数指针。Vulkan 1.3 promote 这两
    // 个 KHR 命令时是纯名字 promotion、签名完全一致，强制 cast 安全。
    //
    // 注意：仅对这两个**被 promote 的**命令做替换。其它 KHR 命令（如
    // vkAcquireNextImageKHR、vkCreateSwapchainKHR）是真扩展、未被 promote，
    // 不能做同样替换。
    if (ctx->pfnGetDeviceProcAddr != nullptr && ctx->vkDevice != VK_NULL_HANDLE) {
        const char* coreName = nullptr;
        if (std::strcmp(funcName, "vkCmdBeginRenderingKHR") == 0) {
            coreName = "vkCmdBeginRendering";
        } else if (std::strcmp(funcName, "vkCmdEndRenderingKHR") == 0) {
            coreName = "vkCmdEndRendering";
        }
        if (coreName != nullptr) {
            PFN_vkVoidFunction core =
                ctx->pfnGetDeviceProcAddr(ctx->vkDevice, coreName);
            if (core != nullptr) { return core; }
            // 兜底：万一驱动只导出 KHR 名字（极不常见），最后再回 instance
            // proc addr 试一次。
        }
    }

    return ctx->pfnGetInstanceProcAddr(ctx->vkInstance, funcName);
}

// EditorState —— 跨面板共享的编辑器状态：world 引用、当前选中实体。
//
// 持有指针 / 引用而非值，理由：
//   * World 体量比较大（包 entt::registry），按值带容易拖累 EditorRenderLayer
//     的构造期；
//   * main 拥有 world，layer 析构后 world 还要存活（关停时 world 先于
//     ImGui shutdown 安全析构）；
//   * 多个面板（Entity Tree / Inspector / Scene viewport）将读写同一份
//     selectedEntity —— 引用传递天然让所有面板看到同一份状态。
//
// 后续 task 加 rename buffer / clipboard / undo stack 等 UI-side 状态时，
// 全部往这个结构里追加；不应进 engine 公共 API。
// Windows native file dialog 包装（IFileOpenDialog / IFileSaveDialog）。
//
// 设计：
// * isSave 决定调 FileSave 还是 FileOpen dialog；
// * 过滤器固定 ".scene.json"（编辑器场景文件后缀；与引擎 Scene::Save/Load
//   约定一致）；
// * 路径以 UTF-8 写回 outPath —— 引擎 Scene::Save / Load 接受 string_view，
//   传 UTF-8 即可（std::filesystem::path 在 Windows 上构造 UTF-8 string
//   会自动转 wide，本身对编辑器调用方透明）；
// * CoInitializeEx STA 模式 —— ComDlg 要求；CoUninitialize 仅在本帧
//   真正初始化（hr == S_OK）时才调，避免误关掉调用方更高层的 COM 上下文；
// * 失败 / 用户取消 → 返回 false，outPath 保持原状；
// * parentHwnd 用主窗口的 HWND，让 dialog 作为 modal child 居中 / 抢焦点。
//   GLFW 的 HWND 通过 glfwGetWin32Window（GLFW_EXPOSE_NATIVE_WIN32）取。
inline bool ShowSceneFileDialog(bool isSave, HWND parentHwnd, std::string& outPath)
{
    const HRESULT hrCo = CoInitializeEx(nullptr,
                                        COINIT_APARTMENTTHREADED
                                        | COINIT_DISABLE_OLE1DDE);
    if (hrCo != S_OK && hrCo != S_FALSE) { return false; }

    bool ok = false;
    IFileDialog* pDialog = nullptr;
    HRESULT hr = CoCreateInstance(
        isSave ? CLSID_FileSaveDialog : CLSID_FileOpenDialog,
        nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(&pDialog));
    if (SUCCEEDED(hr)) {
        const COMDLG_FILTERSPEC filterSpec[] = {
            { L"Scene Files (*.scene.json)", L"*.scene.json" },
            { L"All Files (*.*)",            L"*.*" },
        };
        pDialog->SetFileTypes(2, filterSpec);
        pDialog->SetFileTypeIndex(1);
        pDialog->SetDefaultExtension(L"scene.json");
        pDialog->SetTitle(isSave ? L"Save Scene As" : L"Open Scene");

        hr = pDialog->Show(parentHwnd);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pDialog->GetResult(&pItem);
            if (SUCCEEDED(hr)) {
                PWSTR pszPath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
                if (SUCCEEDED(hr)) {
                    const int u8len = WideCharToMultiByte(
                        CP_UTF8, 0, pszPath, -1,
                        nullptr, 0, nullptr, nullptr);
                    if (u8len > 1) {
                        outPath.resize(static_cast<std::size_t>(u8len - 1));
                        WideCharToMultiByte(
                            CP_UTF8, 0, pszPath, -1,
                            outPath.data(), u8len, nullptr, nullptr);
                        ok = true;
                    }
                    CoTaskMemFree(pszPath);
                }
                pItem->Release();
            }
        }
        pDialog->Release();
    }
    if (hrCo == S_OK) { CoUninitialize(); }
    return ok;
}

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
    // 编辑器持有 World 所有权 —— 06-07 起场景 Open / New 需要在 OnUpdate
    // 内整体 swap world，必须放在 state 里让 layer 能直接 reset / replace。
    // 之前是 main() 拥有 + state 持裸指针，重构理由见 Task 06-07 提交。
    std::unique_ptr<Orange::Engine::World> pWorld;
    Orange::Engine::Entity selectedEntity = Orange::Engine::Entity::Invalid();

    // 当前 scene 文件路径（绝对路径，UTF-8）；空 = 尚未保存过 / "Untitled"。
    // Save 走 currentScenePath；空时回退到 SaveAs 流程。
    std::string currentScenePath;

    // 帧末统一执行的场景级操作。把"用户从菜单点了 New / Open / ..."与
    // 模态文件对话框 + 真正 swap world 的执行分开，避免在 ImGui frame 中
    // 间或 EnTT view 迭代中触发模态阻塞 / mutate registry。
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
    struct PendingCreate
    {
        Orange::Engine::Entity parent;  // Invalid = 创建为 root；否则挂为该 parent 末子
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
    // + textured material，让 S4 接通 Scene 视口后立刻能看到几何。AssetRegistry
    // 与 MaterialSystem 由编辑器持有所有权 —— 它们的生命周期必须长于任何
    // 引用其中 mesh handle / material instance 的 World，因此放进 EditorState。
    //
    // 注意：场景 Save / Load（06-07）当前不串联 AssetRegistry，存盘的 scene
    // JSON 里的 mesh / material 引用对应的是 *本次启动* 创建的内置 handle，
    // 跨进程加载语义还需要后续 task 把 AssetRegistry 也参与序列化；S2 不
    // 做这件事，新开场景 / load 老存档时 RenderableComponent 的 mesh 会
    // 变成 Invalid（与 06-07 之前同语义），不引入新回归。
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
};

// ---- Hierarchy 维护工具 ------------------------------------------------
//
// 引擎层 HierarchyComponent 是裸数据（parent + 双向 sibling chain），刻
// 意不提供 reparent / link / unlink helper —— 那些是"图操作"语义，引擎
// 自己只有序列化等数据流场景，运行时父子关系变动是编辑器才有的需求。
// 这一组函数放在编辑器本地（不进引擎 include/），Task 06-03 描述里明确
// 说"不引入新公共 API"。
//
// 同时该组也是后续 Task 06-07 场景加载后编辑器侧能"鼠标拖拽 reparent"
// 的底座。Task 06-03 步骤 (a)~(c) 阶段先实现 LinkAsLastChild —— 种子实
// 体需要它；步骤 (f) 真做 DnD 时再补 Detach + ReparentTo。

namespace EditorHierarchy
{

using ::Orange::Engine::Entity;
using ::Orange::Engine::World;
using HC = ::Orange::Engine::Scene::HierarchyComponent;

inline HC& GetOrAdd(World& world, Entity e)
{
    if (auto* p = world.GetComponent<HC>(e)) { return *p; }
    return world.AddComponent<HC>(e, HC{});
}

// 把 child 挂到 parent 的子链末尾。child 进入时假定为 detached（parent
// 为 Invalid）。种子构造期顺序调用即可保证；运行时调用前先 Detach。
inline void LinkAsLastChild(World& world, Entity parent, Entity child)
{
    HC& pc = GetOrAdd(world, parent);
    HC& cc = GetOrAdd(world, child);
    cc.parent = parent;
    if (!pc.firstChild.IsValid()) {
        pc.firstChild = child;
        return;
    }
    // 走到尾兄弟
    Entity cur = pc.firstChild;
    while (true) {
        HC* h = world.GetComponent<HC>(cur);
        if (h == nullptr || !h->nextSibling.IsValid()) { break; }
        cur = h->nextSibling;
    }
    HC* tail = world.GetComponent<HC>(cur);
    tail->nextSibling = child;
    cc.prevSibling    = cur;
}

// 把实体从其父亲的子链上摘下来，使其变为 root。不销毁实体本身、不动
// firstChild —— 整个子树仍然挂在该实体下，只是它从父链脱离。
inline void Detach(World& world, Entity e)
{
    HC* h = world.GetComponent<HC>(e);
    if (h == nullptr) { return; }
    if (!h->parent.IsValid()) { return; }  // 已经是 root

    Entity parent = h->parent;
    Entity prev   = h->prevSibling;
    Entity next   = h->nextSibling;

    // 修兄弟链
    if (prev.IsValid()) {
        if (HC* ph = world.GetComponent<HC>(prev)) { ph->nextSibling = next; }
    } else {
        // 自己是 firstChild —— 让父亲的 firstChild 指向 next
        if (HC* pp = world.GetComponent<HC>(parent)) { pp->firstChild = next; }
    }
    if (next.IsValid()) {
        if (HC* nh = world.GetComponent<HC>(next)) { nh->prevSibling = prev; }
    }

    h->parent      = Entity::Invalid();
    h->prevSibling = Entity::Invalid();
    h->nextSibling = Entity::Invalid();
}

// `ancestor` 是不是 `descendant` 的祖先（含本身）。DnD 防环用。
inline bool IsAncestorOf(World& world, Entity ancestor, Entity descendant)
{
    if (!ancestor.IsValid() || !descendant.IsValid()) { return false; }
    Entity cur = descendant;
    while (cur.IsValid()) {
        if (cur == ancestor) { return true; }
        const HC* h = world.GetComponent<HC>(cur);
        if (h == nullptr) { return false; }
        cur = h->parent;
    }
    return false;
}

// child 改挂到 newParent 下；newParent == Invalid 时把 child 提到 root。
// 调用方负责防环（IsAncestorOf 检查），本函数不再二次校验。
inline void ReparentTo(World& world, Entity child, Entity newParent)
{
    Detach(world, child);
    if (newParent.IsValid()) {
        LinkAsLastChild(world, newParent, child);
    }
}

// 递归销毁 e 及其整个子树。先收集 child 列表（不能边遍历兄弟链边
// destroy，destroy 会把组件抽走 sibling 字段失效），再依次递归销毁，最
// 后把 e 自己从父链摘下并销毁。
inline void DestroySubtree(World& world, Entity e)
{
    if (!e.IsValid()) { return; }
    if (HC* h = world.GetComponent<HC>(e)) {
        // 收集 children 快照（不能边遍历边 destroy —— DestroyEntity 会让
        // 后续 GetComponent 返回 null，sibling 字段失效）。这里用 vector
        // 而非定长数组：编辑器允许任意 fan-out，没必要硬上限。
        std::vector<Entity> kids;
        Entity child = h->firstChild;
        while (child.IsValid()) {
            kids.push_back(child);
            const HC* ch = world.GetComponent<HC>(child);
            child = (ch != nullptr) ? ch->nextSibling : Entity::Invalid();
        }
        for (Entity k : kids) {
            DestroySubtree(world, k);
        }
    }
    Detach(world, e);
    world.DestroyEntity(e);
}

}  // namespace EditorHierarchy

// 种子 demo 世界：让 Task 06-03 阶段的 Entity Tree 面板能立刻显示一棵
// 有意义的层级结构。后续 Task 06-07 接入真实场景加载后，这个种子函数
// 退化为"打开编辑器但没加载 scene 时"的占位 fallback，或直接删除。
//
// 拓扑：
//   Root
//   ├── Camera
//   ├── Light
//   └── Geometry
//       ├── Floor
//       └── Wall
//   Misc Sibling      （第二棵根，验证多根显示）
//
// 内置 plane mesh（XZ 平面 4 顶点 / 2 三角形，朝上）—— SeedDemoWorld 给
// Floor 实体用。halfSize = 2.5 → 边长 5。与 samples/07_full_pipeline 内同
// 名 helper 字段顺序一致，方便比对。
inline std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakePlaneMesh(float halfSize)
{
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Asset::VertexPosition3;
    using ::Orange::Engine::Asset::VertexUV2;

    std::vector<VertexPosition3> positions = {
        {-halfSize, 0.0f, -halfSize},
        { halfSize, 0.0f, -halfSize},
        { halfSize, 0.0f,  halfSize},
        {-halfSize, 0.0f,  halfSize},
    };
    std::vector<VertexUV2> uvs = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
    };
    std::vector<std::uint32_t> indices = {0, 2, 1, 0, 3, 2};
    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(indices));
}

// 内置 cube mesh（6 面 × 4 顶点，共 24 vertices / 12 triangles）。每面单独
// 一组顶点是为了让 UV 在 face 边界不连续 —— textured material 在 face 间
// 看起来才正常（共享 8 顶点的方案 UV 必然拉伸 / 接缝错位）。
inline std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakeCubeMesh(float halfSize)
{
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Asset::VertexPosition3;
    using ::Orange::Engine::Asset::VertexUV2;

    const float h = halfSize;
    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<std::uint32_t>   indices;
    positions.reserve(24);
    uvs.reserve(24);
    indices.reserve(36);

    auto addFace = [&](VertexPosition3 a, VertexPosition3 b,
                       VertexPosition3 c, VertexPosition3 d) {
        const std::uint32_t base = static_cast<std::uint32_t>(positions.size());
        positions.push_back(a); positions.push_back(b);
        positions.push_back(c); positions.push_back(d);
        uvs.push_back({0.0f, 0.0f}); uvs.push_back({1.0f, 0.0f});
        uvs.push_back({1.0f, 1.0f}); uvs.push_back({0.0f, 1.0f});
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 1);
        indices.push_back(base + 0); indices.push_back(base + 3); indices.push_back(base + 2);
    };

    // +X / -X / +Y / -Y / +Z / -Z；winding 与既有 sample 的 plane 同顺
    // （CCW 朝外），避免与 shadow caster / 主 pass 的 CullMode 假设打架。
    addFace({ h,-h, h}, { h,-h,-h}, { h, h,-h}, { h, h, h});  // +X
    addFace({-h,-h,-h}, {-h,-h, h}, {-h, h, h}, {-h, h,-h});  // -X
    addFace({-h, h, h}, { h, h, h}, { h, h,-h}, {-h, h,-h});  // +Y (top)
    addFace({-h,-h,-h}, { h,-h,-h}, { h,-h, h}, {-h,-h, h});  // -Y (bottom)
    addFace({-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h});  // +Z
    addFace({ h,-h,-h}, {-h,-h,-h}, {-h, h,-h}, { h, h,-h});  // -Z

    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(indices));
}

// 编辑器侧的"AssetRegistry / MaterialSystem 一次性建好"——main() 在
// SeedDemoWorld 首次调用之前调一次。失败会让 SeedDemoWorld 仍能工作
// （RenderableComponent 退化到无 mesh / nullptr material 状态），只是 Scene
// 面板视口（S4）看不到几何 —— 编辑器本身仍正常运转。
inline void InitializeEditorAssets(EditorState& state)
{
    using Orange::Engine::Asset::AssetRegistry;
    using Orange::Engine::Asset::MeshAsset;
    using Orange::Engine::Asset::ShaderAsset;
    using Orange::Engine::Asset::ShaderLoader;
    using Orange::Engine::Render::MaterialSystem;

    state.pAssets = std::make_unique<AssetRegistry>();
    if (auto reg = state.pAssets->RegisterLoader<ShaderAsset>(
            std::make_unique<ShaderLoader>());
        reg.IsErr())
    {
        std::fprintf(stderr,
                     "[OrangeEditor] AssetRegistry::RegisterLoader<ShaderAsset> 失败 "
                     "(code=%u)\n",
                     static_cast<unsigned>(reg.Error()));
    }

    if (auto h = state.pAssets->Insert<MeshAsset>("editor/cube", MakeCubeMesh(0.5f));
        h.IsOk())
    {
        state.cubeMeshHandle = h.Value();
    }
    if (auto h = state.pAssets->Insert<MeshAsset>("editor/plane", MakePlaneMesh(2.5f));
        h.IsOk())
    {
        state.planeMeshHandle = h.Value();
    }

    state.pMaterials = std::make_unique<MaterialSystem>(*state.pAssets);
    if (auto rb = state.pMaterials->RegisterBuiltins(); rb.IsErr())
    {
        // 通常意味着 shaders/orange_engine/*.spv 不在 .exe 同目录 —— in-tree
        // build 由 CMake 把 SPV 拷到 build/bin/$<CONFIG>/shaders/orange_engine/，
        // standalone install 还没有官方流程时这里会报，但不阻止编辑器启动。
        std::fprintf(stderr,
                     "[OrangeEditor] MaterialSystem::RegisterBuiltins 失败 "
                     "(code=%u) —— Scene 视口稍后可能不显示几何\n",
                     static_cast<unsigned>(rb.Error()));
    }

    state.pFloorMaterial = state.pMaterials->CreateInstance("textured");
    state.pWallMaterial  = state.pMaterials->CreateInstance("textured");
}

inline void SeedDemoWorld(EditorState& state)
{
    using ::Orange::Engine::Entity;
    using ::Orange::Engine::Scene::NameComponent;
    using ::Orange::Engine::Scene::TransformComponent;
    using ::Orange::Engine::Render::Camera;
    using ::Orange::Engine::Render::DirectionalLight;
    using ::Orange::Engine::Render::RenderableComponent;
    using ::Orange::Engine::Physics::BodyType;
    using ::Orange::Engine::Physics::ColliderComponent;
    using ::Orange::Engine::Physics::BoxDesc;
    using ::Orange::Engine::Physics::RigidBodyComponent;

    auto& world = *state.pWorld;
    auto make = [&](const char* name) {
        Entity e = world.CreateEntity();
        world.AddComponent<NameComponent>(e, NameComponent{name});
        world.AddComponent<TransformComponent>(e, TransformComponent{});
        return e;
    };

    Entity root     = make("Root");
    Entity camera   = make("Camera");
    Entity light    = make("Light");
    Entity geometry = make("Geometry");
    Entity floor    = make("Floor");
    Entity wall     = make("Wall");
    Entity misc     = make("Misc Sibling");

    // Camera：透视投影 + lookAt 从前上方看向原点，让 Floor / Wall 都在视
    // 野里。aspect 用 1:1（Scene 视口默认尺寸先按方形），S3 / S4 接通
    // viewport resize 后由编辑器相机系统按实际尺寸覆盖 projection。
    {
        Camera cam = Camera::Perspective(glm::radians(50.0f),
                                         /*aspect=*/1.0f,
                                         /*zNear=*/0.1f,
                                         /*zFar=*/100.0f);
        cam.view = glm::lookAt(glm::vec3(3.0f, 2.5f, 5.0f),
                               glm::vec3(0.0f, 0.5f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent<Camera>(camera, cam);
    }

    // Light：默认朝下方略偏前的方向 —— 让 Wall 在 Floor 上投出可见阴影。
    {
        DirectionalLight dl{};
        // direction 字段语义随引擎默认（DirectionalLight 默认值是合理朝下）；
        // S2 不重写，避免与 Inspector 编辑入口的默认值表现不一致。
        world.AddComponent<DirectionalLight>(light, dl);
    }

    // Floor：plane mesh + textured material；地面通常不投自己阴影。
    {
        // 调整 Floor transform：略下移让 cube 站在地面上（cube 中心在 y=0
        // 时 -Y 面落在 y=-0.5；地面 y=-0.5 与 cube 底齐）。
        auto* floorT = world.GetComponent<TransformComponent>(floor);
        if (floorT != nullptr)
        {
            floorT->position.y = -0.5f;
        }
        RenderableComponent rcFloor{};
        rcFloor.mesh             = state.planeMeshHandle;
        rcFloor.materialInstance = state.pFloorMaterial.get();
        rcFloor.visible          = true;
        rcFloor.castsShadow      = false;
        world.AddComponent<RenderableComponent>(floor, rcFloor);

        RigidBodyComponent rbFloor{};
        rbFloor.type            = BodyType::Static;
        rbFloor.fixedRotation   = true;
        rbFloor.gravityScale    = 0.0f;
        world.AddComponent<RigidBodyComponent>(floor, rbFloor);

        ColliderComponent ccFloor{};
        ccFloor.shape       = BoxDesc{glm::vec2{5.0f, 0.5f}};  // 半宽 / 半高
        ccFloor.density     = 0.0f;
        ccFloor.friction    = 0.5f;
        world.AddComponent<ColliderComponent>(floor, ccFloor);
    }

    // Wall：cube mesh + textured material；偏左一点站在 Floor 上方。
    {
        auto* wallT = world.GetComponent<TransformComponent>(wall);
        if (wallT != nullptr)
        {
            wallT->position = glm::vec3(-1.0f, 0.0f, 0.0f);  // cube 底面正好坐在 Floor 上
        }
        RenderableComponent rcWall{};
        rcWall.mesh             = state.cubeMeshHandle;
        rcWall.materialInstance = state.pWallMaterial.get();
        rcWall.visible          = true;
        rcWall.castsShadow      = true;
        world.AddComponent<RenderableComponent>(wall, rcWall);
    }

    EditorHierarchy::LinkAsLastChild(world, root,     camera);
    EditorHierarchy::LinkAsLastChild(world, root,     light);
    EditorHierarchy::LinkAsLastChild(world, root,     geometry);
    EditorHierarchy::LinkAsLastChild(world, geometry, floor);
    EditorHierarchy::LinkAsLastChild(world, geometry, wall);
    // root 和 misc 自身是 root level —— 不挂任何 parent，HierarchyComponent
    // 也可以不加（树视图按"无 HC 或 parent invalid 视为 root"处理）。
    (void)misc;
}

// 三色 X/Y/Z 标签 + 3 个 DragFloat 的组合控件，对齐 Unity Transform 的
// 配色（X 红 / Y 绿 / Z 蓝）。比裸 DragFloat3 多视觉占用：每分量前一
// 个有色 Button 当 label —— Button 是装饰，点击吃掉但无副作用（不进
// 入键盘焦点队列）。
//
// 用 PushID(label) 隔离三个内部 DragFloat 的 ImGui ID；外层调用方按需
// 再包 PushID（Inspector 同一 Window 内同名字段不出现，目前不必）。
//
// 返回值：任一分量被改 → true，调用方一般写回 component 字段即可。
inline bool DragVec3Colored(const char* label, float v[3],
                            float speed = 0.1f,
                            float vMin  = 0.0f,
                            float vMax  = 0.0f,
                            const char* fmt = "%.3f")
{
    constexpr ImVec4 kRedX  {0.70f, 0.18f, 0.18f, 1.0f};
    constexpr ImVec4 kGrnY  {0.27f, 0.55f, 0.27f, 1.0f};
    constexpr ImVec4 kBluZ  {0.18f, 0.36f, 0.70f, 1.0f};

    bool changed = false;
    ImGui::PushID(label);

    const ImGuiStyle& s = ImGui::GetStyle();
    const float btnH    = ImGui::GetFrameHeight();
    // CalcItemWidth：当前 column 下默认 item 宽度（ImGui 自适应窗口宽）
    const float total   = ImGui::CalcItemWidth();
    const float dragW   = (total - 3.0f * (btnH + s.ItemInnerSpacing.x)) / 3.0f;

    auto axis = [&](int idx, const char* name, ImVec4 color) {
        ImGui::PushStyleColor(ImGuiCol_Button,        color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  color);
        ImGui::Button(name, ImVec2(btnH, btnH));
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(dragW);
        char id[8];
        std::snprintf(id, sizeof(id), "##%s", name);
        if (ImGui::DragFloat(id, &v[idx], speed, vMin, vMax, fmt)) {
            changed = true;
        }
    };

    axis(0, "X", kRedX);
    ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
    axis(1, "Y", kGrnY);
    ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
    axis(2, "Z", kBluZ);
    ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
    ImGui::TextUnformatted(label);

    ImGui::PopID();
    return changed;
}

// 编辑器侧需要在每帧 BeginFrame 之前调 ImGui::NewFrame 等。把这件事
// 封到一个 layer，让 AppHost 主循环按 LayerStack 的 OnUpdate 顺序自动
// 触发。Render layer 在最后 push，确保 ImGui::NewFrame → user UI →
// ImGui::Render 在 BeginFrame 之前完成；BeginFrame / EndFrame 内 overlay
// callback 拿 ImGui 的 DrawData 录制。
class EditorRenderLayer : public Orange::Engine::Layer
{
public:
    EditorRenderLayer(Orange::Engine::AppHost&             host,
                      Orange::Renderer::IRenderer&         renderer,
                      VkDescriptorPool                     descriptorPool,
                      VkDevice                             device,
                      EditorState&                         state)
        : Orange::Engine::Layer("EditorRender")
        , mHost(host)
        , mRenderer(renderer)
        , mDescriptorPool(descriptorPool)
        , mDevice(device)
        , mState(state)
    {
        // 注册 swap-chain overlay callback —— 引擎 EndFrame 内 swap-chain
        // 渲染窗口里调一次 ImGui_ImplVulkan_RenderDrawData，把当前帧 ImGui
        // DrawData 录到主窗口 swap-chain image。
        mRenderer.SetSwapchainOverlayCallback(
            [](Orange::Rhi::RHICommandList& cmd,
               std::uint32_t /*w*/, std::uint32_t /*h*/, std::uint64_t /*frameIndex*/)
            {
                void* rawCmd = Orange::Renderer::Interop::GetVulkanCommandBuffer(cmd);
                if (rawCmd != nullptr) {
                    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                                    static_cast<VkCommandBuffer>(rawCmd));
                }
            });
    }

    ~EditorRenderLayer() override
    {
        // 清 callback 避免捕获已销毁资源
        mRenderer.SetSwapchainOverlayCallback({});
    }

    void OnUpdate(const Orange::Engine::FrameContext& frame) override
    {
        // ---- ImGui 帧开始 ---------------------------------------------
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // dock space —— 占满主 viewport，所有 imgui window 都可以 dock 进来。
        // DockSpaceOverViewport 返回的 ID 在主 viewport 生命周期内稳定，下面
        // DockBuilder 系列 API 用它建默认布局。
        const ImGuiID dockspaceId =
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        BuildDefaultLayoutOnce(dockspaceId);
        DrawMainMenuBar();

        // 五个固定面板（Task 06-02 占位骨架）：
        //   Scene        —— 场景视口预览（Task 06-04 真要画 viewport 时填）
        //   Entity Tree  —— ECS World 实体树（Task 06-03 填）
        //   Inspector    —— 组件检视器（Task 06-04 填）
        //   Assets       —— 资源浏览器（Phase 6 后续填）
        //   Console      —— 编辑器日志 / 帧统计（本 task 已能放调试信息）
        DrawScenePanel();
        DrawEntityTreePanel();
        DrawInspectorPanel();
        DrawAssetsPanel();
        DrawConsolePanel(frame);

        // 帧末统一 apply 场景级操作。放在 panel 绘制完之后、ImGui::Render
        // 之前 —— 文件对话框是模态阻塞窗口，它内部会 pump 一些消息但不
        // 影响本帧的 ImGui DrawData；swap world 之后的 selectedEntity /
        // renamingEntity / euler 缓存清理也在此发生，下一帧才用新状态画。
        ApplyPendingSceneOp();

        ImGui::Render();

        // ---- engine frame：BeginFrame → (overlay callback fires
        //      ImGui_ImplVulkan_RenderDrawData) → EndFrame ----------------
        Orange::Renderer::FrameTimeInfo time{};
        time.mTotalTimeSeconds = frame.time.totalSeconds;
        time.mDeltaTimeSeconds = static_cast<float>(frame.time.deltaSeconds);
        if (Orange::Failed(mRenderer.BeginFrame(time))) {
            std::fprintf(stderr, "[OrangeEditor] BeginFrame failed\n");
            mHost.RequestExit();
            return;
        }
        // 编辑器不渲染任何 SubmitItem 内容 —— 仅靠 overlay callback 内的
        // ImGui draw data。FrameLifecycle 在 hasDraw=false + overlay 已
        // 注册时会强制走 begin/end rendering 路径（FEATURE-2026-05-09 修
        // 复的 overlay-on-empty-frame bug），callback 仍能正常触发。
        if (Orange::Failed(mRenderer.EndFrame())) {
            std::fprintf(stderr, "[OrangeEditor] EndFrame failed\n");
            mHost.RequestExit();
            return;
        }

        // ---- multi-viewport：让 ImGui 渲染所有"已拖出主窗口"的额外
        //      viewport 到它们各自的 native window 上 -------------------
        ImGuiIO& io = ImGui::GetIO();
        if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    bool OnEvent(const Orange::Engine::Platform::WindowEvent& event) override
    {
        // ImGui_ImplGlfw_InitForVulkan(true) 时 install_callbacks=true，
        // ImGui 会自己装 GLFW 回调拿到所有事件 —— 这里**不**再转发
        // KeyEvent，避免双触发。仅把 Esc 作为编辑器的全局退出快捷键拦
        // 截。不检查 WantCaptureKeyboard：NavEnableKeyboard 开启后 ImGui
        // 几乎永远占着键盘焦点（demo / dock 任一可导航 widget 在就会拿
        // WantCaptureKeyboard=true），那样 Esc 永远到不了这里。Esc=quit
        // 是 scaffold 选定的开发期约定，与 ImGui 的常规键盘交互不会冲突。
        const auto* key = std::get_if<Orange::Engine::Platform::KeyEvent>(&event);
        if (key == nullptr) { return false; }
        if (key->action != Orange::Engine::Platform::KeyAction::Press) { return false; }
        if (key->key != kEscapeKeyRaw) { return false; }
        std::fprintf(stdout, "[OrangeEditor] Esc 按下，请求退出\n");
        mHost.RequestExit();
        return true;
    }

private:
    // 首帧（或 imgui.ini 还没存过布局时）建默认 dock 布局。判定条件用
    // DockBuilderGetNode → 子节点为空，这样能兼容两种场景：
    //   * 首次启动 / 删了 imgui.ini —— 节点存在但无子，建布局；
    //   * 已有保存的布局 —— 节点有子，跳过、尊重用户调整。
    // 注意 DockBuilder* 来自 imgui_internal.h，是 ImGui 公开但内部稳定度
    // 比 imgui.h 略低的 API；编辑器侧使用是 ImGui 官方推荐路径。
    static void BuildDefaultLayoutOnce(ImGuiID dockspaceId)
    {
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
        if (node != nullptr && node->IsSplitNode()) { return; }

        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId,
                                  ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId,
                                      ImGui::GetMainViewport()->Size);

        // 布局：左 20% Entity Tree；右 25% Inspector；下 30% Assets/Console
        // tab；剩余中央留给 Scene。比例与 Unity / Unreal 默认 layout 接近，
        // 后续可让用户调；ImGui 会把改动写回 imgui.ini，下次启动恢复。
        ImGuiID center = dockspaceId;
        ImGuiID left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,
                                                    0.20f, nullptr, &center);
        ImGuiID right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right,
                                                    0.25f, nullptr, &center);
        ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,
                                                    0.30f, nullptr, &center);

        ImGui::DockBuilderDockWindow("Entity Tree", left);
        ImGui::DockBuilderDockWindow("Inspector",   right);
        ImGui::DockBuilderDockWindow("Assets",      bottom);
        ImGui::DockBuilderDockWindow("Console",     bottom);  // 同节点 = tab
        ImGui::DockBuilderDockWindow("Scene",       center);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    // 主菜单栏（File / View / Help ...）。BeginMainMenuBar 创建一个固定
    // 顶部的浮动 bar，与 DockSpaceOverViewport 共存 —— ImGui 自动把
    // dockspace 下移留出 menu bar 高度。文件操作不在此立即执行：菜单点
    // 击仅设置 pendingSceneOp，真正的 dialog + Save/Load 调用走帧末
    // ApplyPendingSceneOp。
    //
    // 快捷键 Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S 在菜单 label 处只
    // 是显示文本，真要响应快捷键需要在 OnUpdate 里检 IsKeyPressed +
    // ModCtrl。本 task 范围内菜单点击足以验收 Save/Load 流程；快捷键留
    // 给后续微调（同时也避免与 Entity Tree 面板的 F2/Del 冲突）。
    void DrawMainMenuBar()
    {
        if (!ImGui::BeginMainMenuBar()) { return; }
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene")) {
                mState.pendingSceneOp = SceneOp::New;
            }
            if (ImGui::MenuItem("Open Scene...")) {
                mState.pendingSceneOp = SceneOp::Open;
            }
            ImGui::Separator();
            const bool canQuickSave = !mState.currentScenePath.empty();
            if (ImGui::MenuItem("Save", nullptr, false, canQuickSave)) {
                mState.pendingSceneOp = SceneOp::Save;
            }
            if (ImGui::MenuItem("Save Scene As...")) {
                mState.pendingSceneOp = SceneOp::SaveAs;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                mHost.RequestExit();
            }
            ImGui::EndMenu();
        }
        // 当前 scene 路径作为只读 indicator 显示在菜单栏右侧 —— OS 窗口
        // 标题这一层目前没有动态修改入口，先放这里让用户清楚自己在编辑哪个
        // 文件 / 是不是 Untitled。
        const std::string& path = mState.currentScenePath;
        const char* sceneLabel  = path.empty() ? "[Untitled]" : path.c_str();
        const float bbWidth =
            ImGui::CalcTextSize(sceneLabel).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - bbWidth);
        ImGui::TextDisabled("%s", sceneLabel);
        ImGui::EndMainMenuBar();
    }

    // 把 EditorState 内与"被编辑 world 实体身份强相关"的状态全清空。
    // Open / New 切 world 后必须调；不调的话 selectedEntity 会指向新 world
    // 里不存在的 entity，Inspector 看到野指针。
    void ResetEntityLocalState()
    {
        mState.selectedEntity            = Orange::Engine::Entity::Invalid();
        mState.renamingEntity            = Orange::Engine::Entity::Invalid();
        mState.renameBuffer[0]           = '\0';
        mState.renameJustStarted         = false;
        mState.pendingDelete             = Orange::Engine::Entity::Invalid();
        mState.pendingReparent.valid     = false;
        mState.pendingCreate.valid       = false;
        mState.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
    }

    // 帧末统一 apply 用户菜单点击的场景操作。dialog 阻塞期 ImGui 主循环
    // 等待，可接受 —— 编辑器无实时帧率要求。失败 / 取消都仅 stderr 记
    // 录，不弹 modal，与项目"日志走 stderr，等 Core::Log 接入再改"的过
    // 渡期惯例一致。
    void ApplyPendingSceneOp()
    {
        const SceneOp op = mState.pendingSceneOp;
        if (op == SceneOp::None) { return; }
        mState.pendingSceneOp = SceneOp::None;

        // 拿主窗口 HWND 给 dialog 当 parent，确保 dialog 居中 + 抢焦点。
        auto* gw = static_cast<GLFWwindow*>(
            mHost.GetWindow().GetGlfwWindowHandle());
        const HWND hwnd = (gw != nullptr) ? glfwGetWin32Window(gw) : nullptr;

        switch (op) {
            case SceneOp::New: {
                mState.pWorld = std::make_unique<Orange::Engine::World>();
                SeedDemoWorld(mState);  // 与启动期一致；后续真要"空场景"再做"New Empty"
                mState.currentScenePath.clear();
                ResetEntityLocalState();
                std::fprintf(stdout, "[OrangeEditor] new scene (seeded demo world)\n");
                break;
            }
            case SceneOp::Open: {
                std::string path;
                if (!ShowSceneFileDialog(/*isSave=*/false, hwnd, path)) { break; }
                auto pNew = std::make_unique<Orange::Engine::World>();
                auto rc = Orange::Engine::Scene::Load(path, *pNew);
                if (rc.IsErr()) {
                    std::fprintf(stderr,
                                 "[OrangeEditor] Scene::Load failed: %s (code=%u)\n",
                                 path.c_str(),
                                 static_cast<unsigned>(rc.Error()));
                    break;  // 保留原 world
                }
                mState.pWorld = std::move(pNew);
                mState.currentScenePath = path;
                ResetEntityLocalState();
                std::fprintf(stdout, "[OrangeEditor] opened scene: %s\n", path.c_str());
                break;
            }
            case SceneOp::Save: {
                if (mState.currentScenePath.empty()) {
                    // 没保存过 → 转 SaveAs。下一帧 menu 不再可见，但
                    // 我们直接走 SaveAs 流程也行。
                    std::string path;
                    if (!ShowSceneFileDialog(/*isSave=*/true, hwnd, path)) { break; }
                    mState.currentScenePath = std::move(path);
                }
                auto rc = Orange::Engine::Scene::Save(
                    *mState.pWorld, mState.currentScenePath);
                if (rc.IsErr()) {
                    std::fprintf(stderr,
                                 "[OrangeEditor] Scene::Save failed: %s (code=%u)\n",
                                 mState.currentScenePath.c_str(),
                                 static_cast<unsigned>(rc.Error()));
                } else {
                    std::fprintf(stdout, "[OrangeEditor] saved scene: %s\n",
                                 mState.currentScenePath.c_str());
                }
                break;
            }
            case SceneOp::SaveAs: {
                std::string path;
                if (!ShowSceneFileDialog(/*isSave=*/true, hwnd, path)) { break; }
                auto rc = Orange::Engine::Scene::Save(*mState.pWorld, path);
                if (rc.IsErr()) {
                    std::fprintf(stderr,
                                 "[OrangeEditor] Scene::Save failed: %s (code=%u)\n",
                                 path.c_str(),
                                 static_cast<unsigned>(rc.Error()));
                    break;
                }
                mState.currentScenePath = std::move(path);
                std::fprintf(stdout, "[OrangeEditor] saved scene as: %s\n",
                             mState.currentScenePath.c_str());
                break;
            }
            case SceneOp::None:
                break;  // unreachable, 上面已 early return
        }
    }

    // 占位面板：仅一行 placeholder 文案。每个面板的实际内容由后续 task 填
    // —— Entity Tree 由 06-03、Inspector 由 06-04、Scene viewport 由 06-04、
    // Assets 由 Phase 6 后续 task。这里只保证默认 dock 布局里这些名字真的
    // 存在，dock layout 才能建得起来。
    static void DrawScenePanel()
    {
        ImGui::Begin("Scene");
        ImGui::TextDisabled("scene viewport — Task 06-08");
        ImGui::End();
    }

    void DrawEntityTreePanel()
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
        // EnTT 3.13：registry.each() 已删除，遍历所有实体走 view<entt::entity>。
        // 引擎序列化层（src/scene/SceneSerialization.cpp）用的也是这个 idiom。
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
            // 有这两段可看。其它组件（Renderable / RigidBody / Light）
            // 等用户主动需要时加 —— Task 06-04 不提供 "+ Add Component"
            // 按钮，留给后续 task。
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
    void DrawEntityNodeRecursive(Orange::Engine::Entity entity)
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
        // Rename / Delete 把 F2 / Del 快捷键的等价入口挂上菜单。打开菜
        // 单会先让节点 "becomes hovered/clicked"，所以同时也会写
        // selectedEntity（统一通过 ImGui::IsItemClicked 路径处理）。
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

    void BeginRename(Orange::Engine::Entity entity)
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

    void CommitRename(Orange::Engine::Entity entity)
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

    void CancelRename()
    {
        mState.renamingEntity    = Orange::Engine::Entity::Invalid();
        mState.renameJustStarted = false;
        mState.renameBuffer[0]   = '\0';
    }

    // DnD payload type 标识。ImGui 用这个字符串区分不同类型的 drag payload。
    static constexpr const char* kEntityPayload = "ORANGE_EDITOR_ENTITY";

    // Inspector 入口：选中实体的 entity id + 所有"已挂着的内置 component"
    // 各起一个 CollapsingHeader 段。每段 if HasComponent → DrawXxx。
    //
    // 组件类型表是**硬编码**的（task 描述就是"内置组件"列表）。引擎仍处
    // 在 Phase 1–6 阶段禁止 entt::meta / 反射，所以游戏侧自定义 component
    // 暂时只能不显示 —— Phase 6 后续真要扩展时走"编辑器扩展点 API 让游
    // 戏注册自己的 inspector callback"路径，不在本 task 范围内。
    void DrawInspectorPanel()
    {
        ImGui::Begin("Inspector");
        if (mState.pWorld == nullptr || !mState.selectedEntity.IsValid()) {
            ImGui::TextDisabled("(select an entity)");
            ImGui::End();
            return;
        }

        const Orange::Engine::Entity e = mState.selectedEntity;
        ImGui::Text("Entity #%u",
                    static_cast<unsigned>(static_cast<std::uint32_t>(e.Value())));
        ImGui::Separator();

        DrawInspectorName(e);
        DrawInspectorTransform(e);
        DrawInspectorHierarchy(e);
        DrawInspectorDirectionalLight(e);
        DrawInspectorRenderable(e);
        DrawInspectorRigidBody(e);
        DrawInspectorCollider(e);
        DrawInspectorParticleEmitter(e);
        DrawInspectorAnimator(e);

        // ---- + Add Component -----------------------------------------
        // 列出尚未挂在本实体上的内置可添加组件。Animator 跳过 —— 需要具
        // 体 IAnimator 子类实例，不能用空 unique_ptr 默认构造。Hierarchy
        // 跳过 —— DnD 管理，手动 add 会出现 "孤立 HC"（parent invalid
        // 且不挂在任何父链上）。
        ImGui::Separator();
        if (ImGui::Button("+ Add Component")) {
            ImGui::OpenPopup("##add_component");
        }
        if (ImGui::BeginPopup("##add_component")) {
            using namespace Orange::Engine::Scene;
            using namespace Orange::Engine::Render;
            using namespace Orange::Engine::Physics;
            auto& w = *mState.pWorld;
            if (!w.HasComponent<TransformComponent>(e)
                && ImGui::MenuItem("Transform")) {
                w.AddComponent<TransformComponent>(e, TransformComponent{});
            }
            if (!w.HasComponent<DirectionalLight>(e)
                && ImGui::MenuItem("Directional Light")) {
                w.AddComponent<DirectionalLight>(e, DirectionalLight{});
            }
            if (!w.HasComponent<RenderableComponent>(e)
                && ImGui::MenuItem("Renderable")) {
                w.AddComponent<RenderableComponent>(e, RenderableComponent{});
            }
            if (!w.HasComponent<RigidBodyComponent>(e)
                && ImGui::MenuItem("RigidBody")) {
                w.AddComponent<RigidBodyComponent>(e, RigidBodyComponent{});
            }
            if (!w.HasComponent<ColliderComponent>(e)
                && ImGui::MenuItem("Collider")) {
                w.AddComponent<ColliderComponent>(e, ColliderComponent{});
            }
            if (!w.HasComponent<ParticleEmitterComponent>(e)
                && ImGui::MenuItem("Particle Emitter")) {
                w.AddComponent<ParticleEmitterComponent>(e,
                    ParticleEmitterComponent{});
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    // CollapsingHeader 包装 —— 多一个 "右键 → Remove Component" 上下文菜
    // 单。outRemove 表示用户本帧请求了移除；调用方在 fields 渲染完后据
    // 此调 RemoveComponent。把 remove 写在 fields 之后是为了让该帧的
    // field 控件仍正常渲染，不会因为半途 remove 而 GetComponent 拿到野
    // 指针。
    //
    // 不暴露 Name / Hierarchy 的 remove —— Name 总在让 Entity Tree 有
    // 名字显示；Hierarchy 是 DnD 维护的结构性数据，手动 remove 会让自
    // 身脱离父链且子节点变成孤儿。这两段调用方直接用裸 CollapsingHeader。
    static bool ComponentHeader(const char* label, bool* outRemove,
                                bool defaultOpen = true)
    {
        *outRemove = false;
        const bool open = ImGui::CollapsingHeader(
            label, defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Remove Component")) { *outRemove = true; }
            ImGui::EndPopup();
        }
        return open;
    }

    // ---- 各 component 段 -------------------------------------------------
    //
    // 每个 DrawInspectorXxx 的统一模式：
    //   1. 先 HasComponent 检查 —— 不挂就整段不显示
    //   2. CollapsingHeader（默认展开），点 header 可折叠
    //   3. ImGui::DragFloat / Checkbox / Combo 等控件直接读写 component 字段
    //   4. 编辑修改后不需要显式 commit，下一帧就会反映到 ECS（组件就是这
    //      行数据）
    //
    // 没必要在每段中包 PushID —— ImGui 的控件 label（"##xxx"）+ 当前 ID
    // stack（DrawInspectorPanel 在一个 Window 内，没多重并发同名 entity）
    // 已经足够区分。

    void DrawInspectorName(Orange::Engine::Entity e)
    {
        using NameComponent = Orange::Engine::Scene::NameComponent;
        if (!mState.pWorld->HasComponent<NameComponent>(e)) { return; }
        if (!ImGui::CollapsingHeader("Name", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }
        auto* nc = mState.pWorld->GetComponent<NameComponent>(e);
        // 直接复用 mState.renameBuffer 容量大小的本地缓冲，避免对
        // std::string 内存的实时 resize。每帧从 component 拷贝进 buf，
        // 编辑后写回 —— 这样多个面板（树 InputText / Inspector InputText）
        // 同时观察一份 NameComponent 时不会跟 mState.renameBuffer 串味。
        char buf[256];
        const std::size_t n = std::min(nc->name.size(), sizeof(buf) - 1);
        std::memcpy(buf, nc->name.data(), n);
        buf[n] = '\0';
        if (ImGui::InputText("##name", buf, sizeof(buf))) {
            nc->name = buf;
        }
    }

    void DrawInspectorTransform(Orange::Engine::Entity e)
    {
        using TransformComponent = Orange::Engine::Scene::TransformComponent;
        if (!mState.pWorld->HasComponent<TransformComponent>(e)) { return; }
        bool remove = false;
        const bool open = ComponentHeader("Transform", &remove);
        if (!open) {
            if (remove) {
                mState.pWorld->RemoveComponent<TransformComponent>(e);
                mState.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
            }
            return;
        }
        auto* t = mState.pWorld->GetComponent<TransformComponent>(e);

        DragVec3Colored("Position", &t->position.x, 0.05f);

        // Euler 缓存：换实体了 → 重置 cache（从 quat 推 Euler）；同一实体
        // 持续编辑 → 用 cache 保证 DragFloat3 在 gimbal lock 附近不抖。
        if (mState.transformEulerCacheEntity != e) {
            const glm::vec3 eulerRad = glm::eulerAngles(t->rotation);
            mState.transformEulerCache       = glm::degrees(eulerRad);
            mState.transformEulerCacheEntity = e;
        }
        if (DragVec3Colored("Rotation (°)", &mState.transformEulerCache.x, 0.5f)) {
            t->rotation = glm::quat(glm::radians(mState.transformEulerCache));
        }

        DragVec3Colored("Scale", &t->scale.x, 0.05f);

        if (remove) {
            mState.pWorld->RemoveComponent<TransformComponent>(e);
            mState.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
        }
    }

    void DrawInspectorHierarchy(Orange::Engine::Entity e)
    {
        using HC = Orange::Engine::Scene::HierarchyComponent;
        if (!mState.pWorld->HasComponent<HC>(e)) { return; }
        if (!ImGui::CollapsingHeader("Hierarchy")) { return; }
        const auto* h = mState.pWorld->GetComponent<HC>(e);

        auto idTextOf = [](Orange::Engine::Entity x) -> std::string {
            if (!x.IsValid()) { return "(none)"; }
            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "#%u",
                          static_cast<unsigned>(
                              static_cast<std::uint32_t>(x.Value())));
            return tmp;
        };
        // 全只读 —— 父子关系的编辑入口是 Entity Tree 面板的 DnD（Task 06-03）。
        // 在这里再加一遍 reparent 控件会让两套修改路径竞争状态。
        ImGui::Text("Parent       : %s", idTextOf(h->parent).c_str());
        ImGui::Text("First child  : %s", idTextOf(h->firstChild).c_str());
        ImGui::Text("Prev sibling : %s", idTextOf(h->prevSibling).c_str());
        ImGui::Text("Next sibling : %s", idTextOf(h->nextSibling).c_str());
        ImGui::TextDisabled("(edit by drag-drop in Entity Tree)");
    }

    void DrawInspectorDirectionalLight(Orange::Engine::Entity e)
    {
        using DirectionalLight = Orange::Engine::Render::DirectionalLight;
        if (!mState.pWorld->HasComponent<DirectionalLight>(e)) { return; }
        bool remove = false;
        const bool open = ComponentHeader("Directional Light", &remove);
        if (!open) {
            if (remove) { mState.pWorld->RemoveComponent<DirectionalLight>(e); }
            return;
        }
        auto* l = mState.pWorld->GetComponent<DirectionalLight>(e);
        // direction 约定为单位向量；UI 不强制 normalize（用户拖中间态可能
        // 临时变长度），但 Pipeline 自己在着色阶段会按需 normalize。这里
        // 加一个 "Normalize" 按钮让用户随时归一化。
        DragVec3Colored("Direction", &l->direction.x, 0.01f);
        if (ImGui::SmallButton("Normalize Direction")) {
            const float len = glm::length(l->direction);
            if (len > 0.0f) { l->direction /= len; }
        }
        ImGui::ColorEdit3("Color", &l->color.x);
        ImGui::DragFloat("Intensity", &l->intensity, 0.05f, 0.0f, 1000.0f);
        ImGui::Checkbox("Casts Shadow", &l->castsShadow);

        if (remove) { mState.pWorld->RemoveComponent<DirectionalLight>(e); }
    }

    void DrawInspectorRenderable(Orange::Engine::Entity e)
    {
        using RC = Orange::Engine::Render::RenderableComponent;
        if (!mState.pWorld->HasComponent<RC>(e)) { return; }
        bool remove = false;
        const bool open = ComponentHeader("Renderable", &remove);
        if (!open) {
            if (remove) { mState.pWorld->RemoveComponent<RC>(e); }
            return;
        }
        auto* r = mState.pWorld->GetComponent<RC>(e);
        // mesh / materialInstance 是 handle / 裸指针 —— 编辑得通过 Asset
        // 浏览器（Phase 6 后续 task）才有意义。这里只读显示。
        ImGui::Text("Mesh handle      : %llu",
                    static_cast<unsigned long long>(r->mesh.Value()));
        ImGui::Text("MaterialInstance : %p",
                    reinterpret_cast<void*>(r->materialInstance));
        ImGui::Checkbox("Visible",      &r->visible);
        ImGui::Checkbox("Casts Shadow", &r->castsShadow);

        if (remove) { mState.pWorld->RemoveComponent<RC>(e); }
    }

    void DrawInspectorRigidBody(Orange::Engine::Entity e)
    {
        using RB = Orange::Engine::Physics::RigidBodyComponent;
        using BT = Orange::Engine::Physics::BodyType;
        if (!mState.pWorld->HasComponent<RB>(e)) { return; }
        bool remove = false;
        const bool open = ComponentHeader("RigidBody", &remove);
        if (!open) {
            if (remove) { mState.pWorld->RemoveComponent<RB>(e); }
            return;
        }
        auto* b = mState.pWorld->GetComponent<RB>(e);

        const char* kBodyTypeNames[] = {"Static", "Kinematic", "Dynamic"};
        int typeIdx = static_cast<int>(b->type);
        if (ImGui::Combo("Type", &typeIdx, kBodyTypeNames, 3)) {
            b->type = static_cast<BT>(typeIdx);
        }
        ImGui::DragFloat2("Initial Position", &b->initialPosition.x, 0.05f);
        ImGui::DragFloat("Initial Angle (rad)", &b->initialAngle, 0.01f);
        ImGui::DragFloat2("Linear Velocity",  &b->linearVelocity.x,  0.05f);
        ImGui::DragFloat("Angular Velocity",  &b->angularVelocity,   0.05f);
        ImGui::DragFloat("Linear Damping",    &b->linearDamping,     0.01f, 0.0f, 100.0f);
        ImGui::DragFloat("Angular Damping",   &b->angularDamping,    0.01f, 0.0f, 100.0f);
        ImGui::Checkbox("Fixed Rotation",     &b->fixedRotation);
        ImGui::DragFloat("Gravity Scale",     &b->gravityScale,      0.05f);
        // handle 是 PhysicsWorld::AddBody 反写的运行时引用，编辑器不该动；
        // 但显示一下让用户知道 body 是否已注册。
        ImGui::Separator();
        ImGui::TextDisabled("handle (runtime) : %llu",
                            static_cast<unsigned long long>(b->handle.Value()));

        if (remove) { mState.pWorld->RemoveComponent<RB>(e); }
    }

    void DrawInspectorCollider(Orange::Engine::Entity e)
    {
        using CC = Orange::Engine::Physics::ColliderComponent;
        using ::Orange::Engine::Physics::CircleDesc;
        using ::Orange::Engine::Physics::BoxDesc;
        using ::Orange::Engine::Physics::PolygonDesc;
        using ::Orange::Engine::Physics::EdgeChainDesc;
        if (!mState.pWorld->HasComponent<CC>(e)) { return; }
        bool remove = false;
        const bool open = ComponentHeader("Collider", &remove);
        if (!open) {
            if (remove) { mState.pWorld->RemoveComponent<CC>(e); }
            return;
        }
        auto* c = mState.pWorld->GetComponent<CC>(e);

        // shape 是 std::variant —— 显示 shape 类型 + 各自的简单数值。
        // 切换 shape 类型（assign 一个不同 alternative）会重置数据，
        // 比起 Inspector 一行 Combo 误操作风险大，这里**不**提供切换
        // 控件，留给 Task 06-05 / 后续 collider 编辑专用 UI。
        if (std::holds_alternative<CircleDesc>(c->shape)) {
            auto& s = std::get<CircleDesc>(c->shape);
            ImGui::Text("Shape: Circle");
            ImGui::DragFloat("Radius", &s.radius, 0.01f, 0.0f, 0.0f);
            ImGui::DragFloat2("Center", &s.center.x, 0.01f);
        } else if (std::holds_alternative<BoxDesc>(c->shape)) {
            auto& s = std::get<BoxDesc>(c->shape);
            ImGui::Text("Shape: Box");
            ImGui::DragFloat2("Half Extents", &s.halfExtents.x, 0.01f);
            ImGui::DragFloat2("Center",       &s.center.x,      0.01f);
        } else if (std::holds_alternative<PolygonDesc>(c->shape)) {
            const auto& s = std::get<PolygonDesc>(c->shape);
            ImGui::Text("Shape: Polygon (%u verts)",
                        static_cast<unsigned>(s.count));
            ImGui::TextDisabled("(polygon vertex editing — later task)");
        } else if (std::holds_alternative<EdgeChainDesc>(c->shape)) {
            const auto& s = std::get<EdgeChainDesc>(c->shape);
            ImGui::Text("Shape: EdgeChain (%u verts, loop=%s)",
                        static_cast<unsigned>(s.count),
                        s.isLoop ? "yes" : "no");
            ImGui::TextDisabled("(edge chain editing — later task)");
        }
        ImGui::Separator();
        ImGui::DragFloat("Density",     &c->density,     0.01f, 0.0f, 0.0f);
        ImGui::DragFloat("Friction",    &c->friction,    0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Restitution", &c->restitution, 0.01f, 0.0f, 1.0f);
        ImGui::Checkbox("Is Sensor",    &c->isSensor);

        if (remove) { mState.pWorld->RemoveComponent<CC>(e); }
    }

    void DrawInspectorParticleEmitter(Orange::Engine::Entity e)
    {
        using PEC = Orange::Engine::Render::ParticleEmitterComponent;
        if (!mState.pWorld->HasComponent<PEC>(e)) { return; }
        bool remove = false;
        const bool open = ComponentHeader("Particle Emitter", &remove);
        if (!open) {
            if (remove) { mState.pWorld->RemoveComponent<PEC>(e); }
            return;
        }
        auto* p = mState.pWorld->GetComponent<PEC>(e);
        auto& d = p->desc;

        ImGui::Checkbox("Emitting", &p->emitting);
        ImGui::DragFloat("Emission Rate (/s)", &d.emissionRate, 0.5f, 0.0f, 0.0f);

        ImGui::SeparatorText("Lifetime");
        ImGui::DragFloat("Lifetime Min (s)", &d.lifetimeMin, 0.01f, 0.0f, 0.0f);
        ImGui::DragFloat("Lifetime Max (s)", &d.lifetimeMax, 0.01f, 0.0f, 0.0f);

        ImGui::SeparatorText("Spawn Offset (entity local)");
        ImGui::DragFloat2("Offset Min", &d.spawnOffsetMin.x, 0.01f);
        ImGui::DragFloat2("Offset Max", &d.spawnOffsetMax.x, 0.01f);

        ImGui::SeparatorText("Initial Velocity (m/s, worldspace)");
        ImGui::DragFloat2("Velocity Min", &d.initialVelocityMin.x, 0.05f);
        ImGui::DragFloat2("Velocity Max", &d.initialVelocityMax.x, 0.05f);

        ImGui::SeparatorText("Forces");
        ImGui::DragFloat2("Gravity (m/s²)", &d.gravity.x, 0.05f);

        ImGui::SeparatorText("Color curve (linear lerp start→end by age01)");
        // 颜色 RGB + alpha 分开 —— alpha > 1 触发 bloom 拾取，需要 DragFloat
        // 而非 ColorEdit 的 [0,1] clamp。所以 RGB 给 ColorEdit3，alpha 单独
        // DragFloat。
        ImGui::ColorEdit3("Color Start RGB", &d.colorStart.x);
        ImGui::DragFloat("Color Start Alpha", &d.colorStart.w, 0.01f, 0.0f, 0.0f);
        ImGui::ColorEdit3("Color End RGB",   &d.colorEnd.x);
        ImGui::DragFloat("Color End Alpha",   &d.colorEnd.w,   0.01f, 0.0f, 0.0f);

        ImGui::SeparatorText("Size curve");
        ImGui::DragFloat("Size Start", &d.sizeStart, 0.005f, 0.0f, 0.0f);
        ImGui::DragFloat("Size End",   &d.sizeEnd,   0.005f, 0.0f, 0.0f);

        ImGui::SeparatorText("Pool");
        int maxP = static_cast<int>(d.maxParticles);
        if (ImGui::DragInt("Max Particles", &maxP, 1.0f, 0, 65536)) {
            d.maxParticles = static_cast<std::uint32_t>(std::max(0, maxP));
        }

        ImGui::TextDisabled("(real-time preview pending Task 06-08 viewport)");

        if (remove) { mState.pWorld->RemoveComponent<PEC>(e); }
    }

    void DrawInspectorAnimator(Orange::Engine::Entity e)
    {
        using AC = Orange::Engine::Animation::AnimatorComponent;
        if (!mState.pWorld->HasComponent<AC>(e)) { return; }
        if (!ImGui::CollapsingHeader("Animator")) { return; }
        const auto* a = mState.pWorld->GetComponent<AC>(e);
        // AnimatorComponent 持 unique_ptr<IAnimator>，是 move-only 抽象类指
        // 针，运行时 "换 backend" 不是 inspector 一行 combo 能搞定的。这里
        // 仅显示是否挂着 + 指针地址；详细参数交给 Task 06-04 之后的动画
        // 子模式。
        ImGui::Text("Animator (runtime) : %p",
                    reinterpret_cast<const void*>(a->animator.get()));
        ImGui::TextDisabled("(animator backend editing — later task)");
    }

    static void DrawAssetsPanel()
    {
        ImGui::Begin("Assets");
        ImGui::TextDisabled("asset browser — Phase 6 后续");
        ImGui::End();
    }

    // Console 面板放调试信息：帧 index、deltaTime、Esc 退出按钮、Vulkan
    // multi-viewport 提示。Task 06-02 阶段编辑器没有日志系统，先把这些
    // 当作 "console" 的内容，等真接 Core::Log 时换成日志流。
    void DrawConsolePanel(const Orange::Engine::FrameContext& frame)
    {
        ImGui::Begin("Console");
        ImGui::Text("OrangeEditor v0.0.3 (Task 06-02)");
        ImGui::Separator();
        ImGui::Text("frame index: %llu",
                    static_cast<unsigned long long>(frame.time.frameIndex));
        ImGui::Text("delta: %.3f ms",
                    frame.time.deltaSeconds * 1000.0);
        ImGui::Separator();
        ImGui::TextWrapped(
            "Drag any panel's tab OUT of the main window to detach it as a "
            "floating native OS window (ImGui multi-viewport).");
        ImGui::Separator();
        if (ImGui::Button("Quit (or press Esc)")) {
            mHost.RequestExit();
        }
        ImGui::End();
    }

    Orange::Engine::AppHost&      mHost;
    Orange::Renderer::IRenderer&  mRenderer;
    VkDescriptorPool              mDescriptorPool;  // owned by main, not by layer
    VkDevice                      mDevice;
    EditorState&                  mState;           // owned by main, not by layer
};

// 创建 ImGui Vulkan backend 用的 descriptor pool。
//
// 通过 OrangeRender 暴露的 loader fn (`Interop::GetVulkanGetInstanceProcAddr`)
// 级联解 `vkGetDeviceProcAddr` → `vkCreateDescriptorPool`，与 ImGui 共用同
// 一条 loader 解析路径；不再调静态 vulkan-1.lib stub 的 `vkGetInstanceProcAddr`。
VkDescriptorPool MakeImguiDescriptorPool(PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr,
                                          VkInstance               instance,
                                          VkDevice                 device)
{
    auto vkGetDeviceProcAddrFn =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            pfnGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
    if (vkGetDeviceProcAddrFn == nullptr) {
        std::fprintf(stderr, "[OrangeEditor] resolve vkGetDeviceProcAddr failed\n");
        return VK_NULL_HANDLE;
    }
    auto vkCreateDescriptorPoolFn =
        reinterpret_cast<PFN_vkCreateDescriptorPool>(
            vkGetDeviceProcAddrFn(device, "vkCreateDescriptorPool"));
    if (vkCreateDescriptorPoolFn == nullptr) {
        std::fprintf(stderr, "[OrangeEditor] resolve vkCreateDescriptorPool failed\n");
        return VK_NULL_HANDLE;
    }

    constexpr VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
    };
    VkDescriptorPoolCreateInfo desc{};
    desc.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    desc.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    desc.maxSets       = 1000;
    desc.poolSizeCount = sizeof(sizes) / sizeof(sizes[0]);
    desc.pPoolSizes    = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPoolFn(device, &desc, nullptr, &pool) != VK_SUCCESS) {
        std::fprintf(stderr, "[OrangeEditor] vkCreateDescriptorPool failed\n");
    }
    return pool;
}

// 对应 MakeImguiDescriptorPool 的关停期清理；同样走 OrangeRender 的 loader。
void DestroyImguiDescriptorPool(PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr,
                                 VkInstance               instance,
                                 VkDevice                 device,
                                 VkDescriptorPool         pool)
{
    if (pool == VK_NULL_HANDLE) { return; }
    auto vkGetDeviceProcAddrFn =
        reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            pfnGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
    if (vkGetDeviceProcAddrFn == nullptr) { return; }
    auto vkDestroyDescriptorPoolFn =
        reinterpret_cast<PFN_vkDestroyDescriptorPool>(
            vkGetDeviceProcAddrFn(device, "vkDestroyDescriptorPool"));
    if (vkDestroyDescriptorPoolFn != nullptr) {
        vkDestroyDescriptorPoolFn(device, pool, nullptr);
    }
}

}  // namespace

int main()
{
    using namespace Orange::Engine;

    // ---- AppHost（窗口 + 主循环）---------------------------------------
    AppConfig cfg{};
    cfg.window.title  = "OrangeEditor v0.0.3 (Task 06-02 default dock layout)";
    cfg.window.width  = 1600;
    cfg.window.height = 900;
    auto hostRes = AppHost::Create(cfg);
    if (hostRes.IsErr()) {
        std::fprintf(stderr, "[OrangeEditor] AppHost::Create failed (code=%u)\n",
                     static_cast<unsigned>(hostRes.Error()));
        return 1;
    }
    auto host = std::move(hostRes).Value();
    auto* glfwWindow = static_cast<GLFWwindow*>(host->GetWindow().GetGlfwWindowHandle());

    // ---- 编辑器自管 RenderDevice + IRenderer ---------------------------
    Orange::Renderer::RenderDeviceDesc rdDesc{};
    rdDesc.mBackend          = Orange::Renderer::BackendType::Default;
    rdDesc.mEnableValidation = true;
    auto pRenderDevice = Orange::Renderer::RenderDevice::Create(rdDesc);
    if (pRenderDevice == nullptr) {
        std::fprintf(stderr, "[OrangeEditor] RenderDevice::Create failed\n");
        return 1;
    }

    auto pRenderer = Orange::Renderer::CreateRenderer();
    Orange::Renderer::RendererDesc rendererDesc{};
    rendererDesc.mpDevice             = &pRenderDevice->GetRhiDevice();
    rendererDesc.mpNativeWindowHandle = glfwWindow;
    rendererDesc.mFramesInFlight      = 2;
    if (Orange::Failed(pRenderer->Initialize(rendererDesc))) {
        std::fprintf(stderr, "[OrangeEditor] Renderer::Initialize failed\n");
        return 1;
    }

    // ---- 取 OrangeRender 透出的 Vulkan handle + loader fn ---------------
    // handles 来自 FEATURE-2026-05-09；loader fn 来自 FEATURE-2026-05-10。
    // loader fn 是 OrangeRender 内 volk 已加载的 vkGetInstanceProcAddr，
    // 编辑器 ImGui + descriptor pool 创建全部走这一份 loader，与 OrangeRender
    // 共用 instance dispatch 状态。
    const auto handles = Orange::Renderer::Interop::GetVulkanDeviceHandles(*pRenderDevice);
    auto pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        Orange::Renderer::Interop::GetVulkanGetInstanceProcAddr());
    if (pfnGetInstanceProcAddr == nullptr) {
        std::fprintf(stderr,
                     "[OrangeEditor] Interop::GetVulkanGetInstanceProcAddr 返回 null —— "
                     "非 Vulkan 后端或 RenderDevice 尚未 Initialize\n");
        return 1;
    }
    // 跑一帧让 swap-chain ready，再取 swap-chain info（min/count + format）
    {
        Orange::Renderer::FrameTimeInfo dummy{};
        dummy.mTotalTimeSeconds = 0.0;
        dummy.mDeltaTimeSeconds = 0.0f;
        if (Orange::Failed(pRenderer->BeginFrame(dummy))) {
            std::fprintf(stderr, "[OrangeEditor] dummy BeginFrame failed\n");
            return 1;
        }
        if (Orange::Failed(pRenderer->EndFrame())) {
            std::fprintf(stderr, "[OrangeEditor] dummy EndFrame failed\n");
            return 1;
        }
    }
    const auto sci = Orange::Renderer::Interop::GetVulkanSwapchainInfo(*pRenderer);
    std::fprintf(stdout,
                 "[OrangeEditor] Vulkan handles: instance=%p device=%p qFamily=%u\n"
                 "[OrangeEditor] swap-chain: min=%u count=%u format=%d %ux%u\n",
                 handles.vkInstance, handles.vkDevice, handles.graphicsQueueFamilyIndex,
                 sci.minImageCount, sci.imageCount, sci.colorFormat, sci.imageWidth, sci.imageHeight);

    auto vkInstance       = static_cast<VkInstance>(handles.vkInstance);
    auto vkPhysicalDevice = static_cast<VkPhysicalDevice>(handles.vkPhysicalDevice);
    auto vkDevice         = static_cast<VkDevice>(handles.vkDevice);
    auto vkQueue          = static_cast<VkQueue>(handles.vkGraphicsQueue);
    auto colorFmt         = static_cast<VkFormat>(sci.colorFormat);

    // ---- ImGui Init -----------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();
    // 多视口模式下让"detached" window 看起来跟主窗口风格一致
    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // 默认 UI 字体加大：优先用 Windows 系统 Segoe UI（TrueType，任意 size
    // 都清晰），找不到回退到 ImGui 内嵌 ProggyClean 拉大 SizePixels（位图
    // 字体非原生 size 略糊但保底可用）。**必须**在 ImGui_ImplVulkan_Init
    // 之前完成 —— Vulkan backend 在 Init 阶段从 io.Fonts atlas 创建 font
    // texture，后改动 atlas 需要重建 + 重上传。
    {
        ImFont* fontMain = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\segoeui.ttf", kDefaultFontSizePx);
        if (fontMain == nullptr) {
            ImFontConfig fontCfg;
            fontCfg.SizePixels = kDefaultFontSizePx;
            io.Fonts->AddFontDefault(&fontCfg);
            std::fprintf(stdout,
                         "[OrangeEditor] Segoe UI 加载失败，回退 ImGui 默认字体 @%.0fpx\n",
                         kDefaultFontSizePx);
        }
    }

    // GLFW backend —— install_callbacks=true 让 ImGui 自动装 GLFW key /
    // mouse / focus 回调；与 AppHost 共享同一 window，事件分发上 ImGui
    // 拦在 AppHost 之前（GLFW 回调链顺序）
    if (!ImGui_ImplGlfw_InitForVulkan(glfwWindow, true)) {
        std::fprintf(stderr, "[OrangeEditor] ImGui_ImplGlfw_InitForVulkan failed\n");
        return 1;
    }

    // ImGui Vulkan backend —— ImGui 在 NO_PROTOTYPES 下需要先用 LoadFunctions
    // 把内部 ~30 个 vkXxx 指针逐个 resolve；user_data 必须同时携带 loader fn
    // 与一个真 VkInstance，否则 instance/device 级函数无法解析。LoadFunctions
    // 仅在调用期间读 user_data，本地栈对象生命周期足够。
    // 预解析 vkGetDeviceProcAddr 作为 loader 的二级回退（仅 KHR→core alias
    // 路径用得到）。device proc addr 比 instance proc addr 对 device 级
    // 命令的解析更可靠 —— 后者在某些 loader 实现里对 core 1.3 device
    // 命令有 quirk。
    auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        pfnGetInstanceProcAddr(vkInstance, "vkGetDeviceProcAddr"));

    ImguiVulkanLoaderCtx loaderCtx{pfnGetInstanceProcAddr, pfnGetDeviceProcAddr, vkInstance, vkDevice};
    if (!ImGui_ImplVulkan_LoadFunctions(&ImguiVulkanLoader, &loaderCtx)) {
        std::fprintf(stderr, "[OrangeEditor] ImGui_ImplVulkan_LoadFunctions failed\n");
        return 1;
    }

    VkDescriptorPool imguiDescPool =
        MakeImguiDescriptorPool(pfnGetInstanceProcAddr, vkInstance, vkDevice);
    if (imguiDescPool == VK_NULL_HANDLE) { return 1; }

    ImGui_ImplVulkan_InitInfo vkInfo{};
    vkInfo.Instance        = vkInstance;
    vkInfo.PhysicalDevice  = vkPhysicalDevice;
    vkInfo.Device          = vkDevice;
    vkInfo.QueueFamily     = handles.graphicsQueueFamilyIndex;
    vkInfo.Queue           = vkQueue;
    vkInfo.DescriptorPool  = imguiDescPool;
    vkInfo.MinImageCount   = sci.minImageCount;
    vkInfo.ImageCount      = sci.imageCount;
    vkInfo.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
    vkInfo.UseDynamicRendering = true;
    vkInfo.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    vkInfo.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
    vkInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFmt;
    if (!ImGui_ImplVulkan_Init(&vkInfo)) {
        std::fprintf(stderr, "[OrangeEditor] ImGui_ImplVulkan_Init failed\n");
        DestroyImguiDescriptorPool(pfnGetInstanceProcAddr, vkInstance, vkDevice, imguiDescPool);
        return 1;
    }

    // ---- 编辑器侧 EditorState + 启动期种子 World ------------------------
    //
    // EditorState 拥有 World（unique_ptr 字段）—— Task 06-07 起 File →
    // Open / New 要在 OnUpdate 内整体 swap world，所有权放 state 内最自
    // 然。生命周期：editorState 与 host 在同一 scope；host.reset() 已
    // 在关停段手工提前调，保证 layer 析构时 state（含 world）仍存活。
    //
    // 启动期种 demo 实体（Root → Camera/Light/Geometry → Floor/Wall +
    // Misc Sibling）让 Entity Tree / Inspector 立刻有东西可看。File →
    // New 会重新执行同样的 seed —— 真要"空场景"等后续 task 加 "New Empty"
    // 入口再分。
    //
    // 资产初始化必须发生在 SeedDemoWorld 之前 —— 否则 Floor / Wall 的
    // RenderableComponent.mesh 会拿到 Invalid handle，Scene 视口（S4）
    // 接通后就什么都画不出来。
    EditorState editorState;
    editorState.pWorld = std::make_unique<Orange::Engine::World>();
    InitializeEditorAssets(editorState);
    SeedDemoWorld(editorState);

    // ---- Layer 注入 -----------------------------------------------------
    host->PushLayer(std::make_unique<EditorRenderLayer>(*host, *pRenderer,
                                                        imguiDescPool, vkDevice,
                                                        editorState));

    std::fprintf(stdout,
                 "[OrangeEditor] ImGui dock + multi-viewport ready. world entities=%zu. Esc 退出。\n",
                 editorState.pWorld->Size());

    const int rc = host->Run();

    // ---- 关停 ---------------------------------------------------------
    // 关键约束：ImGui_ImplGlfw_Shutdown 会销毁 multi-viewport 期间 ImGui
    // 自己开的额外 GLFWwindow + 标准 cursor，必须发生在 AppHost dtor
    // （主 GLFWwindow 销毁 + glfwTerminate）**之前**，否则 GLFW 已经
    // 被 terminate，所有 glfwDestroy* 调用会丢 17 行
    // GLFW_NOT_INITIALIZED。
    //
    // 同时还要先 EditorRenderLayer 析构（dtor 清 overlay callback），
    // 避免 renderer Shutdown 时调到捕获 ImGui 已 dead 状态的 callback。
    // LayerStack 由 host 拥有，要逼析构必须先 host.reset() —— 这跟上一
    // 段冲突：layer 想先 reset，window 想后 reset。出路是手工先把
    // overlay callback 清空，再做 ImGui shutdown 与 host.reset。
    pRenderDevice->WaitIdle();
    pRenderer->SetSwapchainOverlayCallback({});  // layer dtor 之外手工提前清
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    host.reset();   // → AppHost dtor → LayerStack dtor → EditorRenderLayer dtor
                    //   （overlay callback 已提前清，dtor 再清一次是幂等的）
    DestroyImguiDescriptorPool(pfnGetInstanceProcAddr, vkInstance, vkDevice, imguiDescPool);
    pRenderer->Shutdown();
    pRenderer.reset();
    pRenderDevice.reset();

    std::fprintf(stdout, "[OrangeEditor] clean shutdown.\n");
    return rc;
}
