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

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/platform/Window.h>
#include <orange/engine/platform/WindowEvent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <orange/renderer/RenderDevice.h>
#include <orange/renderer/Renderer.h>
#include <orange/renderer/VulkanInterop.h>
#include <orange/rhi/RHICommandList.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <vulkan/vulkan.h>

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
struct EditorState
{
    Orange::Engine::World* pWorld         = nullptr;
    Orange::Engine::Entity selectedEntity = Orange::Engine::Entity::Invalid();

    // 内联重命名状态：renamingEntity 标记当前正在重命名哪个 entity，
    // renameBuffer 是 InputText 编辑缓冲。renameJustStarted 让首帧自动
    // 抢键盘焦点（SetKeyboardFocusHere），之后归 false 让用户能正常点击
    // 撤销编辑。
    Orange::Engine::Entity renamingEntity    = Orange::Engine::Entity::Invalid();
    char                   renameBuffer[256] = {};
    bool                   renameJustStarted = false;

    // 树状结构上的破坏性操作（destroy / reparent）不能在递归 draw 中即
    // 时执行 —— 会破坏当前遍历的 sibling 链。先在面板里记下"本帧应执行
    // 什么"，draw 结束后统一 apply。
    Orange::Engine::Entity pendingDelete = Orange::Engine::Entity::Invalid();
    struct PendingReparent
    {
        Orange::Engine::Entity child;
        Orange::Engine::Entity newParent;  // Invalid 表示提到 root
        bool                   valid = false;
    } pendingReparent;
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
inline void SeedDemoWorld(Orange::Engine::World& world)
{
    using ::Orange::Engine::Entity;
    using ::Orange::Engine::Scene::NameComponent;
    using ::Orange::Engine::Scene::TransformComponent;

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

    EditorHierarchy::LinkAsLastChild(world, root,     camera);
    EditorHierarchy::LinkAsLastChild(world, root,     light);
    EditorHierarchy::LinkAsLastChild(world, root,     geometry);
    EditorHierarchy::LinkAsLastChild(world, geometry, floor);
    EditorHierarchy::LinkAsLastChild(world, geometry, wall);
    // root 和 misc 自身是 root level —— 不挂任何 parent，HierarchyComponent
    // 也可以不加（树视图按"无 HC 或 parent invalid 视为 root"处理）。
    (void)misc;
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

    // 占位面板：仅一行 placeholder 文案。每个面板的实际内容由后续 task 填
    // —— Entity Tree 由 06-03、Inspector 由 06-04、Scene viewport 由 06-04、
    // Assets 由 Phase 6 后续 task。这里只保证默认 dock 布局里这些名字真的
    // 存在，dock layout 才能建得起来。
    static void DrawScenePanel()
    {
        ImGui::Begin("Scene");
        ImGui::TextDisabled("scene viewport — Task 06-04");
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

    void DrawInspectorPanel()
    {
        ImGui::Begin("Inspector");
        if (mState.pWorld == nullptr || !mState.selectedEntity.IsValid()) {
            ImGui::TextDisabled("(select an entity)");
            ImGui::End();
            return;
        }
        // Task 06-03 阶段 Inspector 仅显示 entity id + name —— 真组件检
        // 视器是 Task 06-04。这里现在就显示一行是为了"选中态"在 UI 上
        // 可见，便于验证 Entity Tree 的 select 行为。
        const auto* name = mState.pWorld->GetComponent<
            Orange::Engine::Scene::NameComponent>(mState.selectedEntity);
        ImGui::Text("Entity #%u",
                    static_cast<unsigned>(static_cast<std::uint32_t>(
                        mState.selectedEntity.Value())));
        ImGui::Text("Name: %s",
                    (name != nullptr && !name->name.empty())
                        ? name->name.c_str() : "(unnamed)");
        ImGui::Separator();
        ImGui::TextDisabled("component inspector — Task 06-04");
        ImGui::End();
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

    // ---- 编辑器侧 World + EditorState -----------------------------------
    //
    // Task 06-03 阶段：用代码种一棵 demo 层级（Root → Camera/Light/Geometry
    // (→Floor/Wall) + Misc Sibling 第二棵根）让 Entity Tree 面板能立刻看
    // 到东西。Task 06-07 接真实场景加载后，这段退化成"未加载任何场景时"
    // 的占位 fallback（或直接删）。
    //
    // World 由 main 拥有，layer 通过 EditorState 引用读写 —— 生命周期：
    // host.reset() 走 layer dtor 之前 world 必须存活，所以 world / state
    // 声明在 layer push 之前、host.reset() 之后才析构（与 host 在同一
    // scope，且声明顺序在 host 之后保证析构先于 host 的反过来 OK 因为
    // host.reset() 被手动提前调，见关停段）。
    auto pWorld = std::make_unique<Orange::Engine::World>();
    SeedDemoWorld(*pWorld);
    EditorState editorState{};
    editorState.pWorld = pWorld.get();

    // ---- Layer 注入 -----------------------------------------------------
    host->PushLayer(std::make_unique<EditorRenderLayer>(*host, *pRenderer,
                                                        imguiDescPool, vkDevice,
                                                        editorState));

    std::fprintf(stdout,
                 "[OrangeEditor] ImGui dock + multi-viewport ready. world entities=%zu. Esc 退出。\n",
                 pWorld->Size());

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
