/**
 * @file VulkanSwapChain.cpp
 * @brief Vulkan交换链实现文件
 * @details 实现交换链创建、表面管理、帧缓冲区管理和呈现同步
 * @author Orange Engine
 */

#include "orgpch.h"
#include "VulkanSwapChain.h"

#include "Core/Debug/Profiler.h"


#include <format>

// 基于Vulkan实例获取过程地址的宏
// 用于获取扩展函数指针
#define GET_INSTANCE_PROC_ADDR(inst, entrypoint)                                                              \
    {                                                                                                         \
        fp##entrypoint = reinterpret_cast<PFN_vk##entrypoint>(vkGetInstanceProcAddr(inst, "vk" #entrypoint)); \
        ORG_CORE_ASSERT(fp##entrypoint);                                                                      \
    }

// 基于Vulkan设备获取过程地址的宏
// 用于获取设备级扩展函数指针
#define GET_DEVICE_PROC_ADDR(dev, entrypoint)                                                              \
    {                                                                                                      \
        fp##entrypoint = reinterpret_cast<PFN_vk##entrypoint>(vkGetDeviceProcAddr(dev, "vk" #entrypoint)); \
        ORG_CORE_ASSERT(fp##entrypoint);                                                                   \
    }

// 交换链相关的函数指针
// 这些函数是VK_KHR_swapchain扩展的一部分
static PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
static PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
static PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
static PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
static PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;
static PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
static PFN_vkQueuePresentKHR fpQueuePresentKHR;

// NVIDIA扩展函数指针
// 用于GPU崩溃诊断和性能分析
PFN_vkCmdSetCheckpointNV fpCmdSetCheckpointNV;
PFN_vkGetQueueCheckpointDataNV fpGetQueueCheckpointDataNV;

/**
 * @brief NVIDIA检查点命令包装函数
 * @param commandBuffer 命令缓冲区
 * @param pCheckpointMarker 检查点标记数据
 * @details 在命令缓冲区中插入检查点，用于GPU崩溃诊断
 */
VKAPI_ATTR void VKAPI_CALL vkCmdSetCheckpointNV(
    VkCommandBuffer commandBuffer,
    const void *pCheckpointMarker)
{
    fpCmdSetCheckpointNV(commandBuffer, pCheckpointMarker);
}

/**
 * @brief 获取队列检查点数据
 * @param queue 队列句柄
 * @param pCheckpointDataCount 检查点数据数量指针
 * @param pCheckpointData 检查点数据数组
 * @details 获取队列中的检查点数据，用于分析GPU崩溃位置
 */
VKAPI_ATTR void VKAPI_CALL vkGetQueueCheckpointDataNV(
    VkQueue queue,
    uint32_t *pCheckpointDataCount,
    VkCheckpointDataNV *pCheckpointData)
{
    fpGetQueueCheckpointDataNV(queue, pCheckpointDataCount, pCheckpointData);
}

namespace Orange
{

    /**
     * @brief 初始化交换链
     * @param instance Vulkan实例
     * @param device Vulkan设备引用
     * @details 设置基础的Vulkan实例和设备引用，并加载所需的扩展函数
     *
     * 该函数执行以下操作：
     * 1. 存储实例和设备引用
     * 2. 加载交换链相关的扩展函数
     * 3. 加载NVIDIA诊断扩展函数（如果可用）
     */
    void VulkanSwapChain::Init(VkInstance instance, const Ref<VulkanDevice> &device)
    {
        m_Instance = instance;
        m_Device = device;

        VkDevice vulkanDevice = m_Device->GetVulkanDevice();

        // 加载设备级交换链扩展函数
        // 这些函数用于交换链的创建、销毁和图像获取
        GET_DEVICE_PROC_ADDR(vulkanDevice, CreateSwapchainKHR);
        GET_DEVICE_PROC_ADDR(vulkanDevice, DestroySwapchainKHR);
        GET_DEVICE_PROC_ADDR(vulkanDevice, GetSwapchainImagesKHR);
        GET_DEVICE_PROC_ADDR(vulkanDevice, AcquireNextImageKHR);
        GET_DEVICE_PROC_ADDR(vulkanDevice, QueuePresentKHR);

        // 加载实例级表面扩展函数
        // 这些函数用于查询表面属性和能力
        GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceSupportKHR);
        GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
        GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceFormatsKHR);
        GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfacePresentModesKHR);

        // 加载NVIDIA诊断扩展函数
        // 用于GPU崩溃分析和性能调试
        GET_INSTANCE_PROC_ADDR(instance, CmdSetCheckpointNV);
        GET_INSTANCE_PROC_ADDR(instance, GetQueueCheckpointDataNV);
    }

    /**
     * @brief 初始化表面
     * @param windowHandle GLFW窗口句柄
     * @details 创建与窗口关联的Vulkan表面，并配置队列族支持
     *
     * 表面初始化流程：
     * 1. 通过GLFW创建窗口表面
     * 2. 查询物理设备的队列族属性
     * 3. 查找支持呈现的队列族
     * 4. 配置图形和呈现队列
     * 5. 查找合适的图像格式和颜色空间
     */
    void VulkanSwapChain::InitSurface(GLFWwindow *windowHandle)
    {
        VkPhysicalDevice physicalDevice = m_Device->GetPhysicalDevice()->GetVulkanPhysicalDevice();

        // 通过GLFW创建窗口表面
        // 表面是Vulkan与窗口系统交互的接口
        glfwCreateWindowSurface(m_Instance, windowHandle, nullptr, &m_Surface);

        // 获取可用的队列族属性
        // 队列族定义了设备支持的不同类型操作
        uint32_t queueCount;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, NULL);
        ORG_CORE_ASSERT(queueCount >= 1);

        std::vector<VkQueueFamilyProperties> queueProps(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queueProps.data());

        // 遍历每个队列族，了解其是否支持呈现
        // 查找支持呈现的队列族，用于将交换链图像呈现到窗口系统
        std::vector<VkBool32> supportsPresent(queueCount);
        for (uint32_t i = 0; i < queueCount; i++)
        {
            fpGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, m_Surface, &supportsPresent[i]);
        }

        // 在队列族数组中搜索图形队列和呈现队列
        // 尝试找到同时支持两者的队列族（最优情况）
        uint32_t graphicsQueueNodeIndex = UINT32_MAX;
        uint32_t presentQueueNodeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < queueCount; i++)
        {
            // 查找支持图形操作的队列族
            if ((queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                if (graphicsQueueNodeIndex == UINT32_MAX)
                {
                    graphicsQueueNodeIndex = i;
                }

                // 如果该队列族同时支持呈现，这是最佳选择
                if (supportsPresent[i] == VK_TRUE)
                {
                    graphicsQueueNodeIndex = i;
                    presentQueueNodeIndex = i;
                    break; // 找到同时支持图形和呈现的队列族，退出循环
                }
            }
        }

        // 如果没有找到同时支持图形和呈现的队列族
        // 尝试找到单独的呈现队列族
        if (presentQueueNodeIndex == UINT32_MAX)
        {
            for (uint32_t i = 0; i < queueCount; ++i)
            {
                if (supportsPresent[i] == VK_TRUE)
                {
                    presentQueueNodeIndex = i;
                    break;
                }
            }
        }

        // 确保找到了有效的图形和呈现队列族
        ORG_CORE_ASSERT(graphicsQueueNodeIndex != UINT32_MAX);
        ORG_CORE_ASSERT(presentQueueNodeIndex != UINT32_MAX);

        // 存储队列节点索引（这里使用图形队列索引）
        m_QueueNodeIndex = graphicsQueueNodeIndex;

        // 查找合适的图像格式和颜色空间
        FindImageFormatAndColorSpace();
    }

    /**
     * @brief 创建交换链
     * @param width 交换链宽度指针（可能被修改以匹配表面能力）
     * @param height 交换链高度指针（可能被修改以匹配表面能力）
     * @param vsync 是否启用垂直同步
     * @details 创建交换链、图像视图、命令缓冲区、同步对象、渲染通道和帧缓冲区
     *
     * 交换链创建流程：
     * 1. 查询表面能力和呈现模式
     * 2. 确定交换链尺寸和图像数量
     * 3. 配置表面变换和合成Alpha
     * 4. 创建交换链
     * 5. 获取交换链图像并创建图像视图
     * 6. 创建命令缓冲区
     * 7. 创建同步对象（信号量和栅栏）
     * 8. 创建渲染通道
     * 9. 创建帧缓冲区
     */
    void VulkanSwapChain::Create(uint32_t *width, uint32_t *height, bool vsync)
    {
        m_VSync = vsync;

        VkDevice device = m_Device->GetVulkanDevice();
        VkPhysicalDevice physicalDevice = m_Device->GetPhysicalDevice()->GetVulkanPhysicalDevice();

        // 保存旧交换链句柄（用于重建时的优化）
        VkSwapchainKHR oldSwapchain = m_SwapChain;

        // 第一步：获取物理设备表面属性和格式
        VkSurfaceCapabilitiesKHR surfCaps;
        VK_CHECK_RESULT(fpGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, m_Surface, &surfCaps));

        // 第二步：获取可用的呈现模式
        // 呈现模式决定了图像如何从交换链传输到表面
        uint32_t presentModeCount;
        VK_CHECK_RESULT(fpGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_Surface, &presentModeCount, NULL));
        ORG_CORE_ASSERT(presentModeCount > 0);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        VK_CHECK_RESULT(fpGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_Surface, &presentModeCount, presentModes.data()));

        // 第三步：确定交换链尺寸
        VkExtent2D swapchainExtent = {};
        // 如果宽度（和高度）等于特殊值0xFFFFFFFF，表面大小将由交换链设置
        if (surfCaps.currentExtent.width == (uint32_t)-1)
        {
            // 如果表面大小未定义，大小设置为请求的图像大小
            swapchainExtent.width = *width;
            swapchainExtent.height = *height;
        }
        else
        {
            // 如果表面大小已定义，交换链大小必须匹配
            swapchainExtent = surfCaps.currentExtent;
            *width = surfCaps.currentExtent.width;
            *height = surfCaps.currentExtent.height;
        }

        m_Width = *width;
        m_Height = *height;

        // 如果尺寸为0，直接返回（窗口最小化等情况）
        if (*width == 0 || *height == 0)
            return;

        // 第四步：选择交换链的呈现模式
        // VK_PRESENT_MODE_FIFO_KHR模式必须始终存在（规范要求）
        // 此模式等待垂直空白（"v-sync"）
        VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

        // 如果不需要v-sync，尝试找到邮箱模式
        // 这是可用的最低延迟无撕裂呈现模式
        if (!vsync)
        {
            for (size_t i = 0; i < presentModeCount; i++)
            {
                // 邮箱模式：三重缓冲，低延迟，无撕裂
                if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
                {
                    swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                    break;
                }
                // 立即模式：最低延迟，但可能有撕裂
                if ((swapchainPresentMode != VK_PRESENT_MODE_MAILBOX_KHR) && (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR))
                {
                    swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                }
            }
        }

        // 第五步：确定图像数量
        // 使用最小图像数量+1以提供更好的性能
        uint32_t desiredNumberOfSwapchainImages = surfCaps.minImageCount + 1;
        if ((surfCaps.maxImageCount > 0) && (desiredNumberOfSwapchainImages > surfCaps.maxImageCount))
        {
            desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
        }

        // 第六步：查找表面变换
        VkSurfaceTransformFlagsKHR preTransform;
        if (surfCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        {
            // 我们偏好非旋转变换
            preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        }
        else
        {
            preTransform = surfCaps.currentTransform;
        }

        // 第七步：查找支持的合成Alpha格式（并非所有设备都支持Alpha不透明）
        VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        // 简单选择第一个可用的合成Alpha格式
        std::vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,          // 不透明
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,  // 预乘Alpha
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, // 后乘Alpha
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,         // 继承
        };
        for (auto &compositeAlphaFlag : compositeAlphaFlags)
        {
            if (surfCaps.supportedCompositeAlpha & compositeAlphaFlag)
            {
                compositeAlpha = compositeAlphaFlag;
                break;
            };
        }

        // 第八步：配置交换链创建信息
        VkSwapchainCreateInfoKHR swapchainCI = {};
        swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainCI.pNext = NULL;
        swapchainCI.surface = m_Surface;
        swapchainCI.minImageCount = desiredNumberOfSwapchainImages;
        swapchainCI.imageFormat = m_ColorFormat;
        swapchainCI.imageColorSpace = m_ColorSpace;
        swapchainCI.imageExtent = {swapchainExtent.width, swapchainExtent.height};
        swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // 用作颜色附件
        swapchainCI.preTransform = (VkSurfaceTransformFlagBitsKHR)preTransform;
        swapchainCI.imageArrayLayers = 1;                         // 非立体渲染
        swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // 独占模式
        swapchainCI.queueFamilyIndexCount = 0;
        swapchainCI.pQueueFamilyIndices = NULL;
        swapchainCI.presentMode = swapchainPresentMode;
        swapchainCI.oldSwapchain = oldSwapchain; // 用于优化重建
        // 设置clipped为VK_TRUE允许实现丢弃表面区域外的渲染
        swapchainCI.clipped = VK_TRUE;
        swapchainCI.compositeAlpha = compositeAlpha;

        // 如果支持，在交换链图像上启用传输源
        if (surfCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        {
            swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        // 如果支持，在交换链图像上启用传输目标
        if (surfCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        {
            swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        // 第九步：创建交换链
        VK_CHECK_RESULT(fpCreateSwapchainKHR(device, &swapchainCI, nullptr, &m_SwapChain));

        // 销毁旧交换链（如果存在）
        if (oldSwapchain)
            fpDestroySwapchainKHR(device, oldSwapchain, nullptr);

        // 清理旧的图像视图
        for (auto &image : m_Images)
            vkDestroyImageView(device, image.ImageView, nullptr);
        m_Images.clear();

        // 第十步：获取交换链图像
        VK_CHECK_RESULT(fpGetSwapchainImagesKHR(device, m_SwapChain, &m_ImageCount, NULL));
        // 获取交换链图像
        m_Images.resize(m_ImageCount);
        m_VulkanImages.resize(m_ImageCount);
        VK_CHECK_RESULT(fpGetSwapchainImagesKHR(device, m_SwapChain, &m_ImageCount, m_VulkanImages.data()));

        // 第十一步：创建图像视图
        // 获取包含图像和图像视图的交换链缓冲区
        m_Images.resize(m_ImageCount);
        for (uint32_t i = 0; i < m_ImageCount; i++)
        {
            // 配置颜色附件视图
            VkImageViewCreateInfo colorAttachmentView = {};
            colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            colorAttachmentView.pNext = NULL;
            colorAttachmentView.format = m_ColorFormat;
            colorAttachmentView.image = m_VulkanImages[i];
            // 组件映射（RGBA -> RGBA）
            colorAttachmentView.components = {
                VK_COMPONENT_SWIZZLE_R,
                VK_COMPONENT_SWIZZLE_G,
                VK_COMPONENT_SWIZZLE_B,
                VK_COMPONENT_SWIZZLE_A};
            // 子资源范围（颜色方面，单个mip级别和数组层）
            colorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            colorAttachmentView.subresourceRange.baseMipLevel = 0;
            colorAttachmentView.subresourceRange.levelCount = 1;
            colorAttachmentView.subresourceRange.baseArrayLayer = 0;
            colorAttachmentView.subresourceRange.layerCount = 1;
            colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            colorAttachmentView.flags = 0;

            m_Images[i].Image = m_VulkanImages[i];

            // 创建图像视图
            VK_CHECK_RESULT(vkCreateImageView(device, &colorAttachmentView, nullptr, &m_Images[i].ImageView));
            // 设置调试名称
            VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, std::format("Swapchain ImageView: {}", i), m_Images[i].ImageView);
        }

        // 第十二步：创建命令缓冲区
        // 为每个交换链图像创建专用的命令缓冲区和命令池
        {
            // 清理旧的命令池（如果存在）
            for (auto &commandBuffer : m_CommandBuffers)
                vkDestroyCommandPool(device, commandBuffer.CommandPool, nullptr);

            // 配置命令池创建信息
            VkCommandPoolCreateInfo cmdPoolInfo = {};
            cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cmdPoolInfo.queueFamilyIndex = m_QueueNodeIndex; // 使用图形队列族
            // TRANSIENT标志表示命令缓冲区生命周期短，有助于驱动程序优化
            cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

            // 配置命令缓冲区分配信息
            VkCommandBufferAllocateInfo commandBufferAllocateInfo{};
            commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // 主级命令缓冲区
            commandBufferAllocateInfo.commandBufferCount = 1;                  // 每个命令池分配一个命令缓冲区

            // 为每个交换链图像创建命令池和命令缓冲区
            // 这样可以避免多帧之间的同步问题
            m_CommandBuffers.resize(m_ImageCount);
            for (auto &commandBuffer : m_CommandBuffers)
            {
                // 创建命令池
                VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &commandBuffer.CommandPool));

                // 从命令池分配命令缓冲区
                commandBufferAllocateInfo.commandPool = commandBuffer.CommandPool;
                VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffer.CommandBuffer));
            }
        }

        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // 第十三步：创建同步对象
        // 同步对象用于协调CPU和GPU之间以及不同GPU操作之间的执行顺序
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // 获取飞行帧数配置
        // 飞行帧是指同时在GPU上处理的帧数，通常为2-3帧

#ifdef TODO
        auto framesInFlight = Renderer::GetConfig().FramesInFlight;
#else
        auto framesInFlight = 2;
#endif

        // 创建信号量：用于GPU-GPU同步
        if (m_ImageAvailableSemaphores.size() != framesInFlight)
        {
            m_ImageAvailableSemaphores.resize(framesInFlight);
            m_RenderFinishedSemaphores.resize(framesInFlight);

            VkSemaphoreCreateInfo semaphoreCreateInfo{};
            semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            for (size_t i = 0; i < framesInFlight; i++)
            {
                // 图像可用信号量：当交换链图像可用于渲染时发出信号
                VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_ImageAvailableSemaphores[i]));
                VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_SEMAPHORE, std::format("Swapchain Semaphore ImageAvailable {0}", i), m_ImageAvailableSemaphores[i]);

                // 渲染完成信号量：当渲染完成可以呈现时发出信号
                VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_RenderFinishedSemaphores[i]));
                VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_SEMAPHORE, std::format("Swapchain Semaphore RenderFinished {0}", i), m_RenderFinishedSemaphores[i]);
            }
        }

        // 创建栅栏：用于CPU-GPU同步
        if (m_WaitFences.size() != framesInFlight)
        {
            VkFenceCreateInfo fenceCreateInfo{};
            fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            // 创建时处于已发信号状态，避免第一帧等待
            fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            m_WaitFences.resize(framesInFlight);
            for (auto &fence : m_WaitFences)
            {
                VK_CHECK_RESULT(vkCreateFence(m_Device->GetVulkanDevice(), &fenceCreateInfo, nullptr, &fence));
                VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_FENCE, "Swapchain Fence", fence);
            }
        }

        // 管线阶段标志：指定同步点
        VkPipelineStageFlags pipelineStageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        // 获取深度格式（虽然这里没有使用深度缓冲区）
        VkFormat depthFormat = m_Device->GetPhysicalDevice()->GetDepthFormat();

        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // 第十四步：创建渲染通道
        // 渲染通道定义了渲染操作的框架，包括附件格式、加载/存储操作等
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // 配置颜色附件描述
        VkAttachmentDescription colorAttachmentDesc = {};
        colorAttachmentDesc.format = m_ColorFormat;                            // 使用交换链的颜色格式
        colorAttachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;                   // 无多重采样
        colorAttachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;              // 渲染前清除
        colorAttachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;            // 渲染后保存
        colorAttachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;   // 不关心模板
        colorAttachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // 不关心模板
        colorAttachmentDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;         // 初始布局未定义
        colorAttachmentDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;     // 最终用于呈现

        // 配置颜色附件引用
        VkAttachmentReference colorReference = {};
        colorReference.attachment = 0;                                    // 附件索引
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // 渲染时的最优布局

        // 配置深度附件引用（虽然这里没有使用）
        VkAttachmentReference depthReference = {};
        depthReference.attachment = 1;
        depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // 配置子通道描述
        VkSubpassDescription subpassDescription = {};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; // 图形管线绑定点
        subpassDescription.colorAttachmentCount = 1;                            // 一个颜色附件
        subpassDescription.pColorAttachments = &colorReference;
        subpassDescription.inputAttachmentCount = 0; // 无输入附件
        subpassDescription.pInputAttachments = nullptr;
        subpassDescription.preserveAttachmentCount = 0; // 无保留附件
        subpassDescription.pPreserveAttachments = nullptr;
        subpassDescription.pResolveAttachments = nullptr; // 无解析附件（多重采样用）

        // 配置子通道依赖
        // 确保颜色附件输出在呈现之前完成
        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;                             // 外部子通道（呈现）
        dependency.dstSubpass = 0;                                               // 我们的子通道
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // 源阶段
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // 目标阶段
        dependency.srcAccessMask = 0;                                            // 源访问掩码
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;         // 目标访问掩码

        // 配置渲染通道创建信息
        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1; // 一个附件（颜色）
        renderPassInfo.pAttachments = &colorAttachmentDesc;
        renderPassInfo.subpassCount = 1; // 一个子通道
        renderPassInfo.pSubpasses = &subpassDescription;
        renderPassInfo.dependencyCount = 1; // 一个依赖
        renderPassInfo.pDependencies = &dependency;

        // 创建渲染通道
        VK_CHECK_RESULT(vkCreateRenderPass(m_Device->GetVulkanDevice(), &renderPassInfo, nullptr, &m_RenderPass));
        VKUtils::SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_RENDER_PASS, "Swapchain render pass", m_RenderPass);

        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // 第十五步：创建帧缓冲区
        // 为每个交换链图像创建对应的帧缓冲区
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        {
            // 清理旧的帧缓冲区
            for (auto &framebuffer : m_Framebuffers)
                vkDestroyFramebuffer(device, framebuffer, nullptr);

            // 配置帧缓冲区创建信息
            VkFramebufferCreateInfo frameBufferCreateInfo = {};
            frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            frameBufferCreateInfo.renderPass = m_RenderPass; // 关联的渲染通道
            frameBufferCreateInfo.attachmentCount = 1;       // 一个附件（颜色）
            frameBufferCreateInfo.width = m_Width;           // 帧缓冲区宽度
            frameBufferCreateInfo.height = m_Height;         // 帧缓冲区高度
            frameBufferCreateInfo.layers = 1;                // 单层（非数组纹理）

            // 为每个交换链图像创建帧缓冲区
            m_Framebuffers.resize(m_ImageCount);
            for (uint32_t i = 0; i < m_Framebuffers.size(); i++)
            {
                // 设置当前图像视图作为颜色附件
                frameBufferCreateInfo.pAttachments = &m_Images[i].ImageView;
                VK_CHECK_RESULT(vkCreateFramebuffer(m_Device->GetVulkanDevice(), &frameBufferCreateInfo, nullptr, &m_Framebuffers[i]));
                // 设置调试名称，便于调试时识别
                VKUtils::SetDebugUtilsObjectName(m_Device->GetVulkanDevice(), VK_OBJECT_TYPE_FRAMEBUFFER, std::format("Swapchain framebuffer (Frame in flight: {})", i), m_Framebuffers[i]);
            }
        }
    }

    /**
     * @brief 销毁交换链
     * @details 清理所有相关资源，包括交换链、图像视图、命令缓冲区、同步对象等
     *
     * 销毁顺序很重要，需要确保：
     * 1. 等待设备空闲，确保没有正在使用的资源
     * 2. 按照依赖关系的逆序销毁资源
     * 3. 最后再次等待设备空闲确保清理完成
     */
    void VulkanSwapChain::Destroy()
    {
        ORG_CORE_WARN_TAG("Renderer", "VulkanSwapChain::OnDestroy");

        auto device = m_Device->GetVulkanDevice();
        // 等待设备完成所有操作，确保没有资源正在使用
        vkDeviceWaitIdle(device);

        // 销毁交换链
        if (m_SwapChain)
            fpDestroySwapchainKHR(device, m_SwapChain, nullptr);

        // 销毁图像视图
        for (auto &image : m_Images)
            vkDestroyImageView(device, image.ImageView, nullptr);
        m_Images.clear();

        // 销毁命令池（会自动释放相关的命令缓冲区）
        for (auto &commandBuffer : m_CommandBuffers)
            vkDestroyCommandPool(device, commandBuffer.CommandPool, nullptr);
        m_CommandBuffers.clear();

        // 销毁渲染通道
        if (m_RenderPass)
            vkDestroyRenderPass(device, m_RenderPass, nullptr);

        // 销毁帧缓冲区
        for (auto framebuffer : m_Framebuffers)
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        m_Framebuffers.clear();

        // 销毁同步对象
        // 图像可用信号量
        for (auto &semaphore : m_ImageAvailableSemaphores)
            vkDestroySemaphore(device, semaphore, nullptr);
        m_ImageAvailableSemaphores.clear();

        // 渲染完成信号量
        for (auto &semaphore : m_RenderFinishedSemaphores)
            vkDestroySemaphore(device, semaphore, nullptr);
        m_RenderFinishedSemaphores.clear();

        // 等待栅栏
        for (auto &fence : m_WaitFences)
            vkDestroyFence(device, fence, nullptr);
        m_WaitFences.clear();

        // 最终等待设备空闲，确保所有清理操作完成
        vkDeviceWaitIdle(device);
    }

    /**
     * @brief 处理窗口大小调整
     * @param width 新的窗口宽度
     * @param height 新的窗口高度
     * @details 重新创建交换链以适应新的窗口大小
     *
     * 窗口大小调整流程：
     * 1. 等待设备空闲，确保没有正在进行的渲染操作
     * 2. 使用新尺寸重新创建交换链
     * 3. 再次等待设备空闲，确保重建完成
     */
    void VulkanSwapChain::OnResize(uint32_t width, uint32_t height)
    {
        ORG_CORE_WARN_TAG("Renderer", "VulkanSwapChain::OnResize");

        auto device = m_Device->GetVulkanDevice();
        // 等待所有GPU操作完成
        vkDeviceWaitIdle(device);
        // 使用新尺寸重新创建交换链
        Create(&width, &height, m_VSync);
        // 确保重建操作完成
        vkDeviceWaitIdle(device);
    }

    /**
     * @brief 开始新帧
     * @details 准备渲染新帧，包括资源清理、获取下一个图像、重置命令池
     *
     * 帧开始流程：
     * 1. 执行资源释放队列，清理上一帧的临时资源
     * 2. 获取下一个可用的交换链图像
     * 3. 重置当前帧的命令池，准备记录新的命令
     */
    void VulkanSwapChain::BeginFrame()
    {
#ifdef TODO // From Renderer and Application
        ORG_SCOPE_PERF("VulkanSwapChain::BeginFrame");

        // 执行资源释放队列
        // 清理当前帧索引对应的延迟释放资源
        auto &queue = Renderer::GetRenderResourceReleaseQueue(m_CurrentFrameIndex);
        queue.Execute();
#endif

        // 获取下一个可用的交换链图像索引
        m_CurrentImageIndex = AcquireNextImage();

        // 重置当前帧的命令池
        // 这会重置池中的所有命令缓冲区，准备记录新的命令
        VK_CHECK_RESULT(vkResetCommandPool(m_Device->GetVulkanDevice(), m_CommandBuffers[m_CurrentFrameIndex].CommandPool, 0));
    }

    /**
     * @brief 呈现当前帧
     * @details 提交渲染命令并将结果呈现到屏幕
     *
     * 呈现流程：
     * 1. 配置提交信息，包括等待和信号信号量
     * 2. 重置栅栏并提交命令缓冲区到图形队列
     * 3. 配置呈现信息并提交到呈现队列
     * 4. 处理可能的交换链过期错误
     */
    void VulkanSwapChain::Present()
    {
        ORG_PROFILE_FUNC();
#ifdef TODO // From Application
        ORG_SCOPE_PERF("VulkanSwapChain::Present");
#endif
        // 栅栏超时时间（100秒）
        const uint64_t DEFAULT_FENCE_TIMEOUT = 100000000000;

        // 等待阶段掩码：等待颜色附件输出阶段
        VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        // 配置提交信息
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pWaitDstStageMask = &waitStageMask;                                 // 等待阶段
        submitInfo.pWaitSemaphores = &m_ImageAvailableSemaphores[m_CurrentFrameIndex]; // 等待图像可用
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &m_RenderFinishedSemaphores[m_CurrentFrameIndex]; // 渲染完成时发信号
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffers[m_CurrentFrameIndex].CommandBuffer; // 要执行的命令缓冲区
        submitInfo.commandBufferCount = 1;

        // 重置栅栏，准备等待新的提交
        VK_CHECK_RESULT(vkResetFences(m_Device->GetVulkanDevice(), 1, &m_WaitFences[m_CurrentFrameIndex]));

        // 提交命令缓冲区到图形队列
        m_Device->LockQueue(); // 锁定队列，确保线程安全
        VK_CHECK_RESULT(vkQueueSubmit(m_Device->GetGraphicsQueue(), 1, &submitInfo, m_WaitFences[m_CurrentFrameIndex]));

        // 将当前缓冲区呈现到交换链
        // 传递从提交信息中的命令缓冲区提交发出信号的信号量作为交换链呈现的等待信号量
        // 这确保图像在所有命令提交完成之前不会呈现到窗口系统
        VkResult result;
        {
#ifdef TODO // From Application
            ORG_SCOPE_PERF("VulkanSwapChain::Present - QueuePresent");
#endif

            // 配置呈现信息
            VkPresentInfoKHR presentInfo = {};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.pNext = NULL;
            presentInfo.swapchainCount = 1;                   // 呈现的交换链数量
            presentInfo.pSwapchains = &m_SwapChain;           // 交换链句柄
            presentInfo.pImageIndices = &m_CurrentImageIndex; // 要呈现的图像索引

            // 等待渲染完成信号量
            presentInfo.pWaitSemaphores = &m_RenderFinishedSemaphores[m_CurrentFrameIndex];
            presentInfo.waitSemaphoreCount = 1;

            // 提交呈现请求
            result = fpQueuePresentKHR(m_Device->GetGraphicsQueue(), &presentInfo);
        }

        m_Device->UnlockQueue(); // 解锁队列

        // 处理呈现结果
        if (result != VK_SUCCESS)
        {
            // 如果交换链过期或次优，需要重新创建
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            {
                OnResize(m_Width, m_Height);
            }
            else
            {
                // 其他错误，检查结果
                VK_CHECK_RESULT(result);
            }
        }
    }

    /**
     * @brief 获取下一个可用图像
     * @return 可用图像的索引
     * @details 从交换链获取下一个可用于渲染的图像
     *
     * 图像获取流程：
     * 1. 更新当前帧索引（环形缓冲）
     * 2. 等待当前帧的栅栏，确保上一次使用该帧已完成
     * 3. 从交换链获取下一个可用图像
     * 4. 处理可能的交换链过期错误
     */
    uint32_t VulkanSwapChain::AcquireNextImage()
    {
        // 更新当前帧索引，使用环形缓冲
        // 这确保我们在多个飞行帧之间循环
#ifdef TODO // From Renderer
        m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % Renderer::GetConfig().FramesInFlight;

        // 确保我们请求的帧已完成渲染（来自之前的迭代）
        {
            ORG_PROFILE_SCOPE("VulkanSwapChain::AcquireNextImage - WaitForFences");
            auto &performanceTimers = Application::Get().GetPerformanceTimers();
            Timer gpuWaitTimer;

            // 等待当前帧的栅栏，确保GPU已完成该帧的所有操作
            VK_CHECK_RESULT(vkWaitForFences(m_Device->GetVulkanDevice(), 1, &m_WaitFences[m_CurrentFrameIndex], VK_TRUE, UINT64_MAX));

            // 记录GPU等待时间，用于性能分析
            performanceTimers.RenderThreadGPUWaitTime = gpuWaitTimer.ElapsedMillis();
        }
#endif

        uint32_t imageIndex;
        // 从交换链获取下一个可用图像
        // 当图像可用时，会发出m_ImageAvailableSemaphores[m_CurrentFrameIndex]信号
        VkResult result = fpAcquireNextImageKHR(m_Device->GetVulkanDevice(), m_SwapChain, UINT64_MAX, m_ImageAvailableSemaphores[m_CurrentFrameIndex], (VkFence) nullptr, &imageIndex);

        if (result != VK_SUCCESS)
        {
            // 如果交换链过期或次优，重新创建交换链
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            {
                OnResize(m_Width, m_Height);
                // 重新尝试获取图像
                VK_CHECK_RESULT(fpAcquireNextImageKHR(m_Device->GetVulkanDevice(), m_SwapChain, UINT64_MAX, m_ImageAvailableSemaphores[m_CurrentFrameIndex], (VkFence) nullptr, &imageIndex));
            }
        }

        return imageIndex;
    }

    /**
     * @brief 查找图像格式和颜色空间
     * @details 从支持的表面格式中选择最适合的格式和颜色空间
     *
     * 格式选择策略：
     * 1. 如果只有一个未定义格式，默认使用B8G8R8A8_UNORM
     * 2. 优先选择B8G8R8A8_UNORM格式（最常用）
     * 3. 如果不可用，选择第一个可用格式
     */
    void VulkanSwapChain::FindImageFormatAndColorSpace()
    {
        VkPhysicalDevice physicalDevice = m_Device->GetPhysicalDevice()->GetVulkanPhysicalDevice();

        // 获取支持的表面格式列表
        uint32_t formatCount;
        VK_CHECK_RESULT(fpGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_Surface, &formatCount, NULL));
        ORG_CORE_ASSERT(formatCount > 0);

        std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
        VK_CHECK_RESULT(fpGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_Surface, &formatCount, surfaceFormats.data()));

        // 如果表面格式列表只包含一个VK_FORMAT_UNDEFINED条目，
        // 表示没有首选格式，所以我们假设VK_FORMAT_B8G8R8A8_UNORM
        if ((formatCount == 1) && (surfaceFormats[0].format == VK_FORMAT_UNDEFINED))
        {
            m_ColorFormat = VK_FORMAT_B8G8R8A8_UNORM;
            m_ColorSpace = surfaceFormats[0].colorSpace;
        }
        else
        {
            // 遍历可用表面格式列表，
            // 检查是否存在VK_FORMAT_B8G8R8A8_UNORM
            bool found_B8G8R8A8_UNORM = false;
            for (auto &&surfaceFormat : surfaceFormats)
            {
                if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
                {
                    m_ColorFormat = surfaceFormat.format;
                    m_ColorSpace = surfaceFormat.colorSpace;
                    found_B8G8R8A8_UNORM = true;
                    break;
                }
            }

            // 如果VK_FORMAT_B8G8R8A8_UNORM不可用，
            // 选择第一个可用的颜色格式
            if (!found_B8G8R8A8_UNORM)
            {
                m_ColorFormat = surfaceFormats[0].format;
                m_ColorSpace = surfaceFormats[0].colorSpace;
            }
        }
    }

}
