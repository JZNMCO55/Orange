// PipelineImGui —— 引擎托管的消费者 ImGui debug-UI overlay 实现。
//
// 背景（GAP-2026-05-27-consumer-imgui-tuning-hook）：游戏侧是独立
// find_package(OrangeEngine) 的 exe，编辑器又只编辑数据不跑玩法，所以
// 「边跑边调参 / 调任意效果」在游戏侧没有抓手。本 TU 把 ImGui 的 context +
// GLFW/Vulkan backend + descriptor pool 上提为引擎可复用件：消费者经
// `Pipeline::EnableImGui()` 一次性接通、经 `Layer::OnImGui()` 提交 widget，
// Pipeline 每帧自动 NewFrame / Render / 把 draw data 录进 swap-chain image。
// 与 play-in-editor 正交（PIE = 在哪跑；本 hook = 跑起来后游戏自己的即时
// 模式调试视图）。
//
// 本 TU 是引擎内**唯一**直接 #include <imgui.h> / <vulkan/vulkan.h> 的位置
// （header isolation：vulkan 头不得出现在公共 include/，src/ 内允许；imgui
// 头不在隔离名单内）。所有 imgui / vulkan 类型都藏在这里，`Pipeline::Impl`
// 仅持一个不完整类型的 `unique_ptr<PipelineImGuiState>`。
//
// ImGui Vulkan backend 的 loader 走 OrangeRender 暴露的 vkGetInstanceProcAddr
// （volk 已加载的那一份），与编辑器侧 VulkanLoaderShim 同款 KHR-trampoline
// 补丁——详见下方 ImguiVulkanLoader 注释。

#include "PipelineImpl.h"

#include "orange/engine/core/Log.h"
#include "orange/engine/platform/Window.h"

#include "orange/core/Result.h" // Orange::Failed
#include "orange/renderer/RenderDevice.h"
#include "orange/renderer/Renderer.h"
#include "orange/renderer/VulkanInterop.h"
#include "orange/rhi/RHICommandList.h"

#include <vulkan/vulkan.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cstring>

namespace Orange::Engine::Render
{
    namespace
    {

        // ImGui_ImplVulkan_LoadFunctions 的 loader callback 的 user-data。与
        // 编辑器 VulkanLoaderShim 的 ImguiVulkanLoaderCtx 同结构。
        struct EngineImguiLoaderCtx
        {
            PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr{nullptr};
            PFN_vkGetDeviceProcAddr   pfnGetDeviceProcAddr{nullptr};
            VkInstance                vkInstance{VK_NULL_HANDLE};
            VkDevice                  vkDevice{VK_NULL_HANDLE};
        };

        // ImGui_ImplVulkan_LoadFunctions 的 loader_func：
        //     PFN_vkVoidFunction (*)(const char* name, void* user_data)
        //
        // [KHR-trampoline 陷阱] ImGui docking v1.91.5 的 imgui_impl_vulkan.cpp 硬编码
        // 用 KHR 后缀名解析 dynamic rendering 两个命令（vkCmdBeginRenderingKHR /
        // vkCmdEndRenderingKHR）。1.3 SDK 的 loader 在
        // `vkGetInstanceProcAddr(inst, "vkCmdBeginRenderingKHR")` 上**总是**返回非
        // null 的 loader trampoline——不管 device 有没有 enable VK_KHR_dynamic_rendering
        // 扩展。OrangeRender 启用的是 Vulkan 1.3 core 的 `dynamicRendering` feature
        // 而非 KHR 扩展，KHR 槽位在 device dispatch 表里是 null，trampoline 一旦被
        // 调用就空跳访问冲突（0xC0000005 @ 0x0）。
        //
        // 出路：解析这两个 KHR 名时改用 vkGetDeviceProcAddr 拿 core 名字（无 KHR
        // 后缀）。vkGetDeviceProcAddr 直接落 ICD、不走 loader trampoline、dispatch
        // 只看 feature；core dynamicRendering 已 enable，驱动返回有效函数指针。1.3
        // promote 这两个命令是纯名字 promotion、签名一致，强制 cast 安全。仅对这两
        // 个被 promote 的命令做替换（vkAcquireNextImageKHR 等真扩展不能这么干）。
        PFN_vkVoidFunction EngineImguiVulkanLoader(const char* funcName, void* userData)
        {
            const auto* ctx = static_cast<const EngineImguiLoaderCtx*>(userData);
            if (ctx->pfnGetDeviceProcAddr != nullptr && ctx->vkDevice != VK_NULL_HANDLE)
            {
                const char* coreName = nullptr;
                if (std::strcmp(funcName, "vkCmdBeginRenderingKHR") == 0)
                {
                    coreName = "vkCmdBeginRendering";
                }
                else if (std::strcmp(funcName, "vkCmdEndRenderingKHR") == 0)
                {
                    coreName = "vkCmdEndRendering";
                }
                if (coreName != nullptr)
                {
                    PFN_vkVoidFunction core = ctx->pfnGetDeviceProcAddr(ctx->vkDevice, coreName);
                    if (core != nullptr)
                    {
                        return core;
                    }
                }
            }
            return ctx->pfnGetInstanceProcAddr(ctx->vkInstance, funcName);
        }

        // 通过 OrangeRender 暴露的 loader fn 级联解 vkGetDeviceProcAddr →
        // vkCreateDescriptorPool，与 ImGui 共用同一条 loader 解析路径（不调静态
        // vulkan-1.lib stub）。失败返回 VK_NULL_HANDLE 并 log。
        VkDescriptorPool MakeImguiDescriptorPool(PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr,
                                                 VkInstance                instance,
                                                 VkDevice                  device)
        {
            auto vkGetDeviceProcAddrFn = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                pfnGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
            if (vkGetDeviceProcAddrFn == nullptr)
            {
                ORANGE_LOG_ERROR("Pipeline::EnableImGui: resolve vkGetDeviceProcAddr failed");
                return VK_NULL_HANDLE;
            }
            auto vkCreateDescriptorPoolFn = reinterpret_cast<PFN_vkCreateDescriptorPool>(
                vkGetDeviceProcAddrFn(device, "vkCreateDescriptorPool"));
            if (vkCreateDescriptorPoolFn == nullptr)
            {
                ORANGE_LOG_ERROR("Pipeline::EnableImGui: resolve vkCreateDescriptorPool failed");
                return VK_NULL_HANDLE;
            }

            constexpr VkDescriptorPoolSize sizes[] = {
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            };
            VkDescriptorPoolCreateInfo desc{};
            desc.sType            = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            desc.flags            = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            desc.maxSets          = 1000;
            desc.poolSizeCount    = sizeof(sizes) / sizeof(sizes[0]);
            desc.pPoolSizes       = sizes;
            VkDescriptorPool pool = VK_NULL_HANDLE;
            if (vkCreateDescriptorPoolFn(device, &desc, nullptr, &pool) != VK_SUCCESS)
            {
                ORANGE_LOG_ERROR("Pipeline::EnableImGui: vkCreateDescriptorPool failed");
            }
            return pool;
        }

        void DestroyImguiDescriptorPool(PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr,
                                        VkInstance                instance,
                                        VkDevice                  device,
                                        VkDescriptorPool          pool)
        {
            if (pool == VK_NULL_HANDLE)
            {
                return;
            }
            auto vkGetDeviceProcAddrFn = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                pfnGetInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
            if (vkGetDeviceProcAddrFn == nullptr)
            {
                return;
            }
            auto vkDestroyDescriptorPoolFn = reinterpret_cast<PFN_vkDestroyDescriptorPool>(
                vkGetDeviceProcAddrFn(device, "vkDestroyDescriptorPool"));
            if (vkDestroyDescriptorPoolFn != nullptr)
            {
                vkDestroyDescriptorPoolFn(device, pool, nullptr);
            }
        }

    } // namespace

    // 引擎托管 ImGui overlay 的全部状态（teardown 所需的句柄 + descriptor pool）。
    struct PipelineImGuiState
    {
        PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr{nullptr};
        VkInstance                vkInstance{VK_NULL_HANDLE};
        VkDevice                  vkDevice{VK_NULL_HANDLE};
        VkDescriptorPool          descriptorPool{VK_NULL_HANDLE};
    };

    // Impl 的构造 + 析构 out-of-line —— 在本 TU 里 PipelineImGuiState 是完整
    // 类型，故 unique_ptr<PipelineImGuiState> 的构造 / 销毁都在这里实例化（持有
    // + 构造 Impl 的 Pipeline.cpp 不需要看到完整定义）。
    Pipeline::Impl::Impl()  = default;
    Pipeline::Impl::~Impl() = default;

    Result<void, ResultCode> Pipeline::Impl::EnableImGuiImpl()
    {
        if (imguiEnabled)
        {
            return ResultCode::AlreadyInitialized;
        }
        if (!initialized || renderDevice == nullptr || !renderer || window == nullptr)
        {
            ORANGE_LOG_ERROR("Pipeline::EnableImGui: 必须在 window 模式 Initialize 成功之后调用");
            return ResultCode::NotInitialized;
        }
        if (offscreenMode)
        {
            // 编辑器 offscreen 路径自管 ImGui；引擎托管 overlay 仅服务 window 模式。
            ORANGE_LOG_ERROR("Pipeline::EnableImGui: offscreen 模式不支持引擎托管 ImGui overlay");
            return ResultCode::InvalidArgument;
        }

        auto* glfwWindow = static_cast<GLFWwindow*>(window->GetGlfwWindowHandle());
        if (glfwWindow == nullptr)
        {
            ORANGE_LOG_ERROR("Pipeline::EnableImGui: GLFWwindow handle 为空");
            return ResultCode::InvalidArgument;
        }

        // 取 OrangeRender 透出的 Vulkan handle + loader fn。
        const auto handles                = Orange::Renderer::Interop::GetVulkanDeviceHandles(*renderDevice);
        auto       pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            Orange::Renderer::Interop::GetVulkanGetInstanceProcAddr());
        if (pfnGetInstanceProcAddr == nullptr || handles.vkInstance == nullptr || handles.vkDevice == nullptr)
        {
            ORANGE_LOG_ERROR("Pipeline::EnableImGui: 非 Vulkan 后端或 device 句柄不可用");
            return ResultCode::Unsupported;
        }

        auto vkInstance       = static_cast<VkInstance>(handles.vkInstance);
        auto vkPhysicalDevice = static_cast<VkPhysicalDevice>(handles.vkPhysicalDevice);
        auto vkDevice         = static_cast<VkDevice>(handles.vkDevice);
        auto vkQueue          = static_cast<VkQueue>(handles.vkGraphicsQueue);

        // 跑一帧让 swap-chain ready，再取 swap-chain info（min/count + format）。
        // 与编辑器同款节奏：GetVulkanSwapchainInfo 需要 swap-chain 已建好。
        {
            Orange::Renderer::FrameTimeInfo dummy{};
            dummy.mTotalTimeSeconds = 0.0;
            dummy.mDeltaTimeSeconds = 0.0f;
            if (Orange::Failed(renderer->BeginFrame(dummy)) || Orange::Failed(renderer->EndFrame()))
            {
                ORANGE_LOG_ERROR("Pipeline::EnableImGui: swap-chain 预热帧失败");
                return ResultCode::InternalError;
            }
        }
        const auto sci      = Orange::Renderer::Interop::GetVulkanSwapchainInfo(*renderer);
        VkFormat   colorFmt = static_cast<VkFormat>(sci.colorFormat);

        // ImGui context —— 启用键盘导航 + docking（debug 面板可拖拽停靠）；不启
        // 多视口（multi-viewport），保持游戏侧 overlay 的最小复杂度。引擎用 ImGui
        // 默认内置字体（不在引擎里硬编码任何系统字体路径——那属编辑器审美决定）。
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        // GLFW backend：install_callbacks=true 让 ImGui 自动装 GLFW key/mouse/focus
        // 回调，与 AppHost 共享同一 window（ImGui 拦在 AppHost 之前）。
        if (!ImGui_ImplGlfw_InitForVulkan(glfwWindow, true))
        {
            ORANGE_LOG_ERROR("Pipeline::EnableImGui: ImGui_ImplGlfw_InitForVulkan failed");
            ImGui::DestroyContext();
            return ResultCode::InternalError;
        }

        // Vulkan backend：NO_PROTOTYPES 下先 LoadFunctions 解析 ~30 个 vk* 指针，
        // 与 OrangeRender 共用同一 loader（含 KHR→core 补丁）。
        auto pfnGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            pfnGetInstanceProcAddr(vkInstance, "vkGetDeviceProcAddr"));
        EngineImguiLoaderCtx loaderCtx{pfnGetInstanceProcAddr, pfnGetDeviceProcAddr, vkInstance, vkDevice};
        if (!ImGui_ImplVulkan_LoadFunctions(&EngineImguiVulkanLoader, &loaderCtx))
        {
            ORANGE_LOG_ERROR("Pipeline::EnableImGui: ImGui_ImplVulkan_LoadFunctions failed");
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            return ResultCode::InternalError;
        }

        VkDescriptorPool descPool = MakeImguiDescriptorPool(pfnGetInstanceProcAddr, vkInstance, vkDevice);
        if (descPool == VK_NULL_HANDLE)
        {
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            return ResultCode::InternalError;
        }

        ImGui_ImplVulkan_InitInfo vkInfo{};
        vkInfo.Instance                                            = vkInstance;
        vkInfo.PhysicalDevice                                      = vkPhysicalDevice;
        vkInfo.Device                                              = vkDevice;
        vkInfo.QueueFamily                                         = handles.graphicsQueueFamilyIndex;
        vkInfo.Queue                                               = vkQueue;
        vkInfo.DescriptorPool                                      = descPool;
        vkInfo.MinImageCount                                       = sci.minImageCount;
        vkInfo.ImageCount                                          = sci.imageCount;
        vkInfo.MSAASamples                                         = VK_SAMPLE_COUNT_1_BIT;
        vkInfo.UseDynamicRendering                                 = true;
        vkInfo.PipelineRenderingCreateInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        vkInfo.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
        vkInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFmt;
        if (!ImGui_ImplVulkan_Init(&vkInfo))
        {
            ORANGE_LOG_ERROR("Pipeline::EnableImGui: ImGui_ImplVulkan_Init failed");
            DestroyImguiDescriptorPool(pfnGetInstanceProcAddr, vkInstance, vkDevice, descPool);
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            return ResultCode::InternalError;
        }

        // 注册 swap-chain overlay callback —— renderer EndFrame 内 swap-chain
        // image 的 rendering scope 里调一次 RenderDrawData，把当前帧 ImGui
        // DrawData 录到主窗口 swap-chain image。
        renderer->SetSwapchainOverlayCallback(
            [](Orange::Rhi::RHICommandList& cmd,
               std::uint32_t /*w*/, std::uint32_t /*h*/, std::uint64_t /*frameIndex*/)
            {
                ImDrawData* drawData = ImGui::GetDrawData();
                if (drawData == nullptr)
                {
                    return;
                }
                void* rawCmd = Orange::Renderer::Interop::GetVulkanCommandBuffer(cmd);
                if (rawCmd != nullptr)
                {
                    ImGui_ImplVulkan_RenderDrawData(drawData, static_cast<VkCommandBuffer>(rawCmd));
                }
            });

        imguiState                         = std::make_unique<PipelineImGuiState>();
        imguiState->pfnGetInstanceProcAddr = pfnGetInstanceProcAddr;
        imguiState->vkInstance             = vkInstance;
        imguiState->vkDevice               = vkDevice;
        imguiState->descriptorPool         = descPool;
        imguiEnabled                       = true;

        ORANGE_LOG_INFO("Pipeline::EnableImGui: 引擎托管 ImGui overlay 已接通 "
                        "(swap-chain min={} count={})",
                        sci.minImageCount, sci.imageCount);
        return {};
    }

    void Pipeline::Impl::ShutdownImGuiImpl()
    {
        if (!imguiEnabled)
        {
            return;
        }
        // 先 WaitIdle —— ImGui backend 资源可能仍在飞行中的 cmd buffer 里被引用。
        if (renderDevice != nullptr)
        {
            renderDevice->WaitIdle();
        }
        // 清 overlay callback，防止后续（如有）EndFrame 调到已 dead 的 ImGui。
        if (renderer)
        {
            renderer->SetSwapchainOverlayCallback({});
        }
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        if (imguiState)
        {
            DestroyImguiDescriptorPool(imguiState->pfnGetInstanceProcAddr, imguiState->vkInstance,
                                       imguiState->vkDevice, imguiState->descriptorPool);
            imguiState.reset();
        }
        imguiEnabled = false;
    }

    void Pipeline::Impl::ImGuiBeginFrameAndSubmit()
    {
        if (!imguiEnabled)
        {
            return;
        }
        // NewFrame / submit / Render 全是 CPU 端构 draw list，不依赖 swap-chain
        // 就绪——即使本帧 Render 后续因窗口最小化等早退，draw data 也只是被丢弃，
        // 不破坏 ImGui 帧配对（下一帧 NewFrame 仍平衡）。
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (imguiSubmit)
        {
            imguiSubmit();
        }
        ImGui::Render();
    }

    // ---- Pipeline 公共面转发 ---------------------------------------------------

    Result<void, ResultCode> Pipeline::EnableImGui()
    {
        return mpImpl->EnableImGuiImpl();
    }

    bool Pipeline::IsImGuiEnabled() const noexcept
    {
        return mpImpl->imguiEnabled;
    }

    void Pipeline::SetImGuiSubmit(std::function<void()> submit)
    {
        mpImpl->imguiSubmit = std::move(submit);
    }

} // namespace Orange::Engine::Render
