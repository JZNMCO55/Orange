#include "VulkanGraphicsSystem.h"
#include "VulkanRenderDevice.h"
#include "VulkanShaderCompiler.h"
#include "Orange.h"
#include <iostream>
#include <fstream>
#include <set>
#include <algorithm>
#include <stdexcept>

// 调试回调函数
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData)
{

    std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

// 动态加载调试扩展函数
VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
                                      const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else
    {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks *pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
    {
        func(instance, debugMessenger, pAllocator);
    }
}

namespace Orange::Graphics::Vulkan
{

    VulkanGraphicsSystem::VulkanGraphicsSystem()
    {
        // 初始化GLFW
        if (!glfwInit())
        {
            throw std::runtime_error("Failed to initialize GLFW");
        }
    }

    VulkanGraphicsSystem::~VulkanGraphicsSystem()
    {
        if (m_initialized)
        {
            Shutdown();
        }
        glfwTerminate();
    }

    bool VulkanGraphicsSystem::Initialize()
    {
        if (m_initialized)
        {
            return true;
        }

        try
        {
            if (!CreateWindow())
                return false;
            if (!CreateInstance())
                return false;
            if (!SetupDebugMessenger())
                return false;
            if (!CreateSurface())
                return false;
            if (!PickPhysicalDevice())
                return false;
            if (!CreateLogicalDevice())
                return false;
            if (!CreateSwapchain())
                return false;
            if (!CreateImageViews())
                return false;
            if (!CreateRenderPass())
                return false;
            if (!CreateDescriptorSetLayout())
                return false;
            if (!CreateGraphicsPipeline())
                return false;
            if (!CreateFramebuffers())
                return false;
            if (!CreateCommandPool())
                return false;
            if (!CreateVertexBuffer())
                return false;
            if (!CreateCommandBuffers())
                return false;
            if (!CreateSyncObjects())
                return false;

            // 创建子系统
            m_renderDevice = std::make_unique<VulkanRenderDevice>(this);
            m_shaderCompiler = std::make_unique<VulkanShaderCompiler>();

            // 初始化立方体渲染
            if (!InitializeCubeRendering())
            {
                ORG_LOG_WARN("Failed to initialize cube rendering, but continuing...");
            }

            m_initialized = true;
            ORG_LOG_INFO("Vulkan Graphics System initialized successfully");
            return true;
        }
        catch (const std::exception &e)
        {
            ORG_LOG_ERROR("Failed to initialize Vulkan Graphics System: {}", e.what());
            return false;
        }
    }

    bool VulkanGraphicsSystem::Initialize(void *windowHandle)
    {
        if (m_initialized)
        {
            return true;
        }

        // 使用外部窗口句柄
        m_window = static_cast<GLFWwindow *>(windowHandle);
        if (!m_window)
        {
            ORG_LOG_ERROR("Invalid window handle provided to VulkanGraphicsSystem");
            return false;
        }

        // 获取窗口尺寸
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);
        m_windowWidth = static_cast<uint32_t>(width);
        m_windowHeight = static_cast<uint32_t>(height);

        try
        {
            // 跳过CreateWindow()，直接从CreateInstance()开始
            if (!CreateInstance())
                return false;
            if (!SetupDebugMessenger())
                return false;
            if (!CreateSurface())
                return false;
            if (!PickPhysicalDevice())
                return false;
            if (!CreateLogicalDevice())
                return false;
            if (!CreateSwapchain())
                return false;
            if (!CreateImageViews())
                return false;
            if (!CreateRenderPass())
                return false;
            if (!CreateDescriptorSetLayout())
                return false;
            if (!CreateGraphicsPipeline())
                return false;
            if (!CreateFramebuffers())
                return false;
            if (!CreateCommandPool())
                return false;
            if (!CreateVertexBuffer())
                return false;
            if (!CreateCommandBuffers())
                return false;
            if (!CreateSyncObjects())
                return false;

            // 创建子系统
            m_renderDevice = std::make_unique<VulkanRenderDevice>(this);
            m_shaderCompiler = std::make_unique<VulkanShaderCompiler>();

            // 初始化立方体渲染
            if (!InitializeCubeRendering())
            {
                ORG_LOG_WARN("Failed to initialize cube rendering, but continuing...");
            }

            m_initialized = true;
            ORG_LOG_INFO("Vulkan Graphics System initialized successfully");
            return true;
        }
        catch (const std::exception &e)
        {
            ORG_LOG_ERROR("Failed to initialize Vulkan Graphics System: {}", e.what());
            return false;
        }
    }

    void VulkanGraphicsSystem::Shutdown()
    {
        if (!m_initialized)
        {
            return;
        }

        vkDeviceWaitIdle(m_device);

        // 销毁子系统
        m_renderDevice.reset();
        m_shaderCompiler.reset();

        // 销毁同步对象
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
        }

        // 清理立方体渲染资源
        CleanupCubeRendering();

        // 销毁顶点缓冲区
        vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
        vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);

        // 销毁命令池
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);

        // 销毁帧缓冲区
        for (auto framebuffer : m_swapchainFramebuffers)
        {
            vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        }

        // 销毁图形管线
        vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);

        // 销毁渲染通道
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);

        // 销毁图像视图
        for (auto imageView : m_swapchainImageViews)
        {
            vkDestroyImageView(m_device, imageView, nullptr);
        }

        // 销毁交换链
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);

        // 销毁逻辑设备
        vkDestroyDevice(m_device, nullptr);

        // 销毁调试信使
        if (m_enableValidationLayers)
        {
            DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        }

        // 销毁表面
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

        // 销毁实例
        vkDestroyInstance(m_instance, nullptr);

        // 不要销毁窗口，由Application的Window管理
        // glfwDestroyWindow(m_window);
        m_window = nullptr;

        m_initialized = false;
        ORG_LOG_INFO("Vulkan Graphics System shutdown complete");
    }

    void VulkanGraphicsSystem::BeginFrame()
    {
        // 等待前一帧完成
        vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

        // 获取下一个可用的交换链图像
        VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                                m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &m_currentSwapchainImageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            // 需要重建交换链
            // TODO: 实现交换链重建
            return;
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            throw std::runtime_error("Failed to acquire swap chain image");
        }

        // 重置围栏
        vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]);

        // 重置命令缓冲区
        vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);

        // 开始记录命令缓冲区
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = 0;
        beginInfo.pInheritanceInfo = nullptr;

        if (vkBeginCommandBuffer(m_commandBuffers[m_currentFrame], &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to begin recording command buffer");
        }

        // 开始渲染通道
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_renderPass;
        renderPassInfo.framebuffer = m_swapchainFramebuffers[m_currentSwapchainImageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = m_swapchainExtent;

        VkClearValue clearValue{};
        clearValue.color = m_clearColor;
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(m_commandBuffers[m_currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        // 绘制立方体 (替代三角形)
        DrawCube();
    }

    void VulkanGraphicsSystem::EndFrame()
    {
        // 结束渲染通道
        vkCmdEndRenderPass(m_commandBuffers[m_currentFrame]);

        // 结束命令缓冲区记录
        if (vkEndCommandBuffer(m_commandBuffers[m_currentFrame]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to record command buffer");
        }
    }

    void VulkanGraphicsSystem::Present()
    {
        // 提交命令缓冲区
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

        VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to submit draw command buffer");
        }

        // 呈现结果
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapchains[] = {m_swapchain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &m_currentSwapchainImageIndex;
        presentInfo.pResults = nullptr;

        VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            // 需要重建交换链
            // TODO: 实现交换链重建
        }
        else if (result != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to present swap chain image");
        }

        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    RenderDevice *VulkanGraphicsSystem::GetRenderDevice()
    {
        return m_renderDevice.get();
    }

    ShaderCompiler *VulkanGraphicsSystem::GetShaderCompiler()
    {
        return m_shaderCompiler.get();
    }

    uint32_t VulkanGraphicsSystem::GetBackbufferWidth() const
    {
        return m_swapchainExtent.width;
    }

    uint32_t VulkanGraphicsSystem::GetBackbufferHeight() const
    {
        return m_swapchainExtent.height;
    }

    void VulkanGraphicsSystem::SetClearColor(float r, float g, float b, float a)
    {
        m_clearColor.float32[0] = r;
        m_clearColor.float32[1] = g;
        m_clearColor.float32[2] = b;
        m_clearColor.float32[3] = a;
    }

    void VulkanGraphicsSystem::Clear()
    {
        // 清除操作在BeginFrame中的渲染通道开始时自动执行
        // 这里可以留空或者实现立即清除的逻辑
    }

    bool VulkanGraphicsSystem::CreateWindow()
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        m_window = glfwCreateWindow(m_windowWidth, m_windowHeight, "Orange Engine", nullptr, nullptr);
        if (!m_window)
        {
            std::cerr << "Failed to create GLFW window" << std::endl;
            return false;
        }

        return true;
    }

    bool VulkanGraphicsSystem::CreateInstance()
    {
        if (m_enableValidationLayers && !CheckValidationLayerSupport())
        {
            throw std::runtime_error("Validation layers requested, but not available");
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Orange Engine";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Orange Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        auto extensions = GetRequiredExtensions();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        if (m_enableValidationLayers)
        {
            createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
            createInfo.ppEnabledLayerNames = m_validationLayers.data();

            PopulateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
        }
        else
        {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
        }

        if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan instance");
        }

        return true;
    }

    // 三角形渲染实现
    void VulkanGraphicsSystem::DrawTriangle()
    {
        VkBuffer vertexBuffers[] = {m_vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindPipeline(m_commandBuffers[m_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
        vkCmdBindVertexBuffers(m_commandBuffers[m_currentFrame], 0, 1, vertexBuffers, offsets);
        vkCmdDraw(m_commandBuffers[m_currentFrame], 3, 1, 0, 0);
    }

    // 立方体渲染实现
    void VulkanGraphicsSystem::DrawCube()
    {
        if (!m_cubeRenderingInitialized)
        {
            return;
        }

        VkBuffer vertexBuffers[] = {m_cubeVertexBuffer};
        VkDeviceSize offsets[] = {0};

        vkCmdBindPipeline(m_commandBuffers[m_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_cubePipeline);
        vkCmdBindVertexBuffers(m_commandBuffers[m_currentFrame], 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(m_commandBuffers[m_currentFrame], m_cubeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

        // 绘制立方体 (36个索引，12个三角形)
        vkCmdDrawIndexed(m_commandBuffers[m_currentFrame], 36, 1, 0, 0, 0);
    }

    bool VulkanGraphicsSystem::InitializeCubeRendering()
    {
        if (m_cubeRenderingInitialized)
        {
            return true;
        }

        ORG_LOG_INFO("Initializing cube rendering...");

        // 创建立方体顶点数据 (8个顶点，6种颜色)
        const std::vector<CubeVertex> cubeVertices = {
            // 前面 (红色系)
            {{-0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}}, // 0: 左下前
            {{0.5f, -0.5f, 0.5f}, {1.0f, 0.5f, 0.0f}},  // 1: 右下前
            {{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}},   // 2: 右上前
            {{-0.5f, 0.5f, 0.5f}, {0.5f, 1.0f, 0.0f}},  // 3: 左上前

            // 后面 (蓝色系)
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}}, // 4: 左下后
            {{0.5f, -0.5f, -0.5f}, {0.0f, 0.5f, 1.0f}},  // 5: 右下后
            {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}},   // 6: 右上后
            {{-0.5f, 0.5f, -0.5f}, {0.5f, 0.0f, 1.0f}}   // 7: 左上后
        };

        // 立方体索引数据 (12个三角形)
        const std::vector<uint32_t> cubeIndices = {
            // 前面
            0, 1, 2, 2, 3, 0,
            // 后面
            4, 6, 5, 6, 4, 7,
            // 左面
            4, 0, 3, 3, 7, 4,
            // 右面
            1, 5, 6, 6, 2, 1,
            // 底面
            4, 5, 1, 1, 0, 4,
            // 顶面
            3, 2, 6, 6, 7, 3};

        // 创建顶点缓冲区
        VkBufferCreateInfo vertexBufferInfo{};
        vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vertexBufferInfo.size = sizeof(cubeVertices[0]) * cubeVertices.size();
        vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vertexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device, &vertexBufferInfo, nullptr, &m_cubeVertexBuffer) != VK_SUCCESS)
        {
            ORG_LOG_ERROR("Failed to create cube vertex buffer");
            return false;
        }

        VkMemoryRequirements vertexMemRequirements;
        vkGetBufferMemoryRequirements(m_device, m_cubeVertexBuffer, &vertexMemRequirements);

        VkMemoryAllocateInfo vertexAllocInfo{};
        vertexAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        vertexAllocInfo.allocationSize = vertexMemRequirements.size;
        vertexAllocInfo.memoryTypeIndex = FindMemoryType(vertexMemRequirements.memoryTypeBits,
                                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_device, &vertexAllocInfo, nullptr, &m_cubeVertexBufferMemory) != VK_SUCCESS)
        {
            ORG_LOG_ERROR("Failed to allocate cube vertex buffer memory");
            return false;
        }

        vkBindBufferMemory(m_device, m_cubeVertexBuffer, m_cubeVertexBufferMemory, 0);

        // 上传顶点数据
        void *vertexData;
        vkMapMemory(m_device, m_cubeVertexBufferMemory, 0, vertexBufferInfo.size, 0, &vertexData);
        memcpy(vertexData, cubeVertices.data(), (size_t)vertexBufferInfo.size);
        vkUnmapMemory(m_device, m_cubeVertexBufferMemory);

        // 创建索引缓冲区
        VkBufferCreateInfo indexBufferInfo{};
        indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        indexBufferInfo.size = sizeof(cubeIndices[0]) * cubeIndices.size();
        indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        indexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device, &indexBufferInfo, nullptr, &m_cubeIndexBuffer) != VK_SUCCESS)
        {
            ORG_LOG_ERROR("Failed to create cube index buffer");
            return false;
        }

        VkMemoryRequirements indexMemRequirements;
        vkGetBufferMemoryRequirements(m_device, m_cubeIndexBuffer, &indexMemRequirements);

        VkMemoryAllocateInfo indexAllocInfo{};
        indexAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        indexAllocInfo.allocationSize = indexMemRequirements.size;
        indexAllocInfo.memoryTypeIndex = FindMemoryType(indexMemRequirements.memoryTypeBits,
                                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_device, &indexAllocInfo, nullptr, &m_cubeIndexBufferMemory) != VK_SUCCESS)
        {
            ORG_LOG_ERROR("Failed to allocate cube index buffer memory");
            return false;
        }

        vkBindBufferMemory(m_device, m_cubeIndexBuffer, m_cubeIndexBufferMemory, 0);

        // 上传索引数据
        void *indexData;
        vkMapMemory(m_device, m_cubeIndexBufferMemory, 0, indexBufferInfo.size, 0, &indexData);
        memcpy(indexData, cubeIndices.data(), (size_t)indexBufferInfo.size);
        vkUnmapMemory(m_device, m_cubeIndexBufferMemory);

        // 创建立方体渲染管线 (暂时复用现有的管线布局)
        // TODO: 实际应该创建专门的立方体管线，支持MVP矩阵等
        m_cubePipeline = m_graphicsPipeline; // 暂时复用三角形管线

        m_cubeRenderingInitialized = true;
        ORG_LOG_INFO("Cube rendering initialized successfully");
        return true;
    }

    void VulkanGraphicsSystem::CleanupCubeRendering()
    {
        if (!m_cubeRenderingInitialized)
        {
            return;
        }

        if (m_cubeVertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_device, m_cubeVertexBuffer, nullptr);
            m_cubeVertexBuffer = VK_NULL_HANDLE;
        }

        if (m_cubeVertexBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_device, m_cubeVertexBufferMemory, nullptr);
            m_cubeVertexBufferMemory = VK_NULL_HANDLE;
        }

        if (m_cubeIndexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_device, m_cubeIndexBuffer, nullptr);
            m_cubeIndexBuffer = VK_NULL_HANDLE;
        }

        if (m_cubeIndexBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_device, m_cubeIndexBufferMemory, nullptr);
            m_cubeIndexBufferMemory = VK_NULL_HANDLE;
        }

        m_cubeRenderingInitialized = false;
        ORG_LOG_INFO("Cube rendering resources cleaned up");
    }

    bool VulkanGraphicsSystem::CreateDescriptorSetLayout()
    {
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 0;
        layoutInfo.pBindings = nullptr;

        if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor set layout");
        }

        return true;
    }

    bool VulkanGraphicsSystem::CreateGraphicsPipeline()
    {
        ORG_LOG_INFO("Loading pre-compiled SPIR-V shaders...");

        try
        {
            // 尝试加载预编译的着色器
            auto vertResult = m_shaderCompiler->LoadSPIRV("Resources/Shaders/triangle.vert.spv");
            auto fragResult = m_shaderCompiler->LoadSPIRV("Resources/Shaders/triangle.frag.spv");

            if (!vertResult.success || !fragResult.success)
            {
                ORG_LOG_WARN("Failed to load pre-compiled shaders, trying dynamic compilation...");

                // 尝试动态编译
                vertResult = m_shaderCompiler->CompileGLSLFileToSPIRV("Resources/Shaders/triangle.vert", Graphics::ShaderStage::Vertex);
                fragResult = m_shaderCompiler->CompileGLSLFileToSPIRV("Resources/Shaders/triangle.frag", Graphics::ShaderStage::Fragment);

                if (!vertResult.success || !fragResult.success)
                {
                    ORG_LOG_ERROR("Failed to compile shaders");
                    return false;
                }
            }

            m_vertexShaderSpirv = std::move(vertResult.spirvBytecode);
            m_fragmentShaderSpirv = std::move(fragResult.spirvBytecode);

            ORG_LOG_INFO("Successfully loaded shaders!");

            // 创建着色器模块
            VkShaderModule vertShaderModule = CreateShaderModule(m_vertexShaderSpirv);
            VkShaderModule fragShaderModule = CreateShaderModule(m_fragmentShaderSpirv);

            VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
            vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
            vertShaderStageInfo.module = vertShaderModule;
            vertShaderStageInfo.pName = "main";

            VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
            fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragShaderStageInfo.module = fragShaderModule;
            fragShaderStageInfo.pName = "main";

            VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

            auto bindingDescription = Vertex::getBindingDescription();
            auto attributeDescriptions = Vertex::getAttributeDescriptions();

            VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
            vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
            vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssembly.primitiveRestartEnable = VK_FALSE;

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = (float)m_swapchainExtent.width;
            viewport.height = (float)m_swapchainExtent.height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = m_swapchainExtent;

            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
            rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.sampleShadingEnable = VK_FALSE;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.logicOpEnable = VK_FALSE;
            colorBlending.logicOp = VK_LOGIC_OP_COPY;
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;
            colorBlending.blendConstants[0] = 0.0f;
            colorBlending.blendConstants[1] = 0.0f;
            colorBlending.blendConstants[2] = 0.0f;
            colorBlending.blendConstants[3] = 0.0f;

            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;

            if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create pipeline layout");
            }

            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = shaderStages;
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.layout = m_pipelineLayout;
            pipelineInfo.renderPass = m_renderPass;
            pipelineInfo.subpass = 0;
            pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

            if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create graphics pipeline");
            }

            vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
            vkDestroyShaderModule(m_device, vertShaderModule, nullptr);

            return true;
        }
        catch (const std::exception &e)
        {
            ORG_LOG_ERROR("Exception while creating graphics pipeline: {}", e.what());
            return false;
        }
    }

    bool VulkanGraphicsSystem::CreateVertexBuffer()
    {
        // 定义三角形顶点（带颜色的渐变三角形）
        const std::vector<Vertex> vertices = {
            {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}}, // 顶部 - 红色
            {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},  // 右下 - 绿色
            {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}  // 左下 - 蓝色
        };

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(vertices[0]) * vertices.size();
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_vertexBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create vertex buffer");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(m_device, m_vertexBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_vertexBufferMemory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate vertex buffer memory");
        }

        vkBindBufferMemory(m_device, m_vertexBuffer, m_vertexBufferMemory, 0);

        void *data;
        vkMapMemory(m_device, m_vertexBufferMemory, 0, bufferInfo.size, 0, &data);
        memcpy(data, vertices.data(), (size_t)bufferInfo.size);
        vkUnmapMemory(m_device, m_vertexBufferMemory);

        return true;
    }

    VkShaderModule VulkanGraphicsSystem::CreateShaderModule(const std::vector<uint32_t> &code)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size() * sizeof(uint32_t);
        createInfo.pCode = code.data();

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create shader module");
        }

        return shaderModule;
    }

    uint32_t VulkanGraphicsSystem::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        throw std::runtime_error("Failed to find suitable memory type");
    }

    // 添加缺失的初始化方法实现
    bool VulkanGraphicsSystem::SetupDebugMessenger()
    {
        if (!m_enableValidationLayers)
            return true;

        VkDebugUtilsMessengerCreateInfoEXT createInfo;
        PopulateDebugMessengerCreateInfo(createInfo);

        if (CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to set up debug messenger");
        }

        return true;
    }

    bool VulkanGraphicsSystem::CreateSurface()
    {
        if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create window surface");
        }
        return true;
    }

    bool VulkanGraphicsSystem::PickPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

        if (deviceCount == 0)
        {
            throw std::runtime_error("Failed to find GPUs with Vulkan support");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

        for (const auto &device : devices)
        {
            if (IsDeviceSuitable(device))
            {
                m_physicalDevice = device;
                break;
            }
        }

        if (m_physicalDevice == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Failed to find a suitable GPU");
        }

        return true;
    }

    bool VulkanGraphicsSystem::CreateLogicalDevice()
    {
        QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily, indices.presentFamily};

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

        if (m_enableValidationLayers)
        {
            createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
            createInfo.ppEnabledLayerNames = m_validationLayers.data();
        }
        else
        {
            createInfo.enabledLayerCount = 0;
        }

        if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create logical device");
        }

        vkGetDeviceQueue(m_device, indices.graphicsFamily, 0, &m_graphicsQueue);
        vkGetDeviceQueue(m_device, indices.presentFamily, 0, &m_presentQueue);

        return true;
    }

    // 添加缺失的辅助方法
    bool VulkanGraphicsSystem::CheckValidationLayerSupport()
    {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char *layerName : m_validationLayers)
        {
            bool layerFound = false;

            for (const auto &layerProperties : availableLayers)
            {
                if (strcmp(layerName, layerProperties.layerName) == 0)
                {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound)
            {
                return false;
            }
        }

        return true;
    }

    std::vector<const char *> VulkanGraphicsSystem::GetRequiredExtensions()
    {
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char *> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        if (m_enableValidationLayers)
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    bool VulkanGraphicsSystem::IsDeviceSuitable(VkPhysicalDevice device)
    {
        QueueFamilyIndices indices = FindQueueFamilies(device);

        bool extensionsSupported = CheckDeviceExtensionSupport(device);

        bool swapchainAdequate = false;
        if (extensionsSupported)
        {
            SwapchainSupportDetails swapchainSupport = QuerySwapchainSupport(device);
            swapchainAdequate = !swapchainSupport.formats.empty() && !swapchainSupport.presentModes.empty();
        }

        return indices.IsComplete() && extensionsSupported && swapchainAdequate;
    }

    bool VulkanGraphicsSystem::CheckDeviceExtensionSupport(VkPhysicalDevice device)
    {
        uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());

        for (const auto &extension : availableExtensions)
        {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    void VulkanGraphicsSystem::PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo)
    {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = DebugCallback;
    }

    VulkanGraphicsSystem::QueueFamilyIndices VulkanGraphicsSystem::FindQueueFamilies(VkPhysicalDevice device)
    {
        QueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto &queueFamily : queueFamilies)
        {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                indices.graphicsFamily = i;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);

            if (presentSupport)
            {
                indices.presentFamily = i;
            }

            if (indices.IsComplete())
            {
                break;
            }

            i++;
        }

        return indices;
    }

    VulkanGraphicsSystem::SwapchainSupportDetails VulkanGraphicsSystem::QuerySwapchainSupport(VkPhysicalDevice device)
    {
        SwapchainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);

        if (formatCount != 0)
        {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);

        if (presentModeCount != 0)
        {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR VulkanGraphicsSystem::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats)
    {
        for (const auto &availableFormat : availableFormats)
        {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return availableFormat;
            }
        }

        return availableFormats[0];
    }

    VkPresentModeKHR VulkanGraphicsSystem::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes)
    {
        for (const auto &availablePresentMode : availablePresentModes)
        {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return availablePresentMode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D VulkanGraphicsSystem::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities)
    {
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            return capabilities.currentExtent;
        }
        else
        {
            int width, height;
            glfwGetFramebufferSize(m_window, &width, &height);

            VkExtent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)};

            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    bool VulkanGraphicsSystem::CreateSwapchain()
    {
        SwapchainSupportDetails swapchainSupport = QuerySwapchainSupport(m_physicalDevice);

        VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapchainSupport.formats);
        VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapchainSupport.presentModes);
        VkExtent2D extent = ChooseSwapExtent(swapchainSupport.capabilities);

        uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;

        if (swapchainSupport.capabilities.maxImageCount > 0 && imageCount > swapchainSupport.capabilities.maxImageCount)
        {
            imageCount = swapchainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
        uint32_t queueFamilyIndices[] = {indices.graphicsFamily, indices.presentFamily};

        if (indices.graphicsFamily != indices.presentFamily)
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
        }

        createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create swap chain");
        }

        vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
        m_swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

        m_swapchainImageFormat = surfaceFormat.format;
        m_swapchainExtent = extent;

        return true;
    }

    bool VulkanGraphicsSystem::CreateImageViews()
    {
        m_swapchainImageViews.resize(m_swapchainImages.size());

        for (size_t i = 0; i < m_swapchainImages.size(); i++)
        {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = m_swapchainImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_swapchainImageFormat;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(m_device, &createInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create image views");
            }
        }

        return true;
    }

    bool VulkanGraphicsSystem::CreateRenderPass()
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = m_swapchainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create render pass");
        }

        return true;
    }

    bool VulkanGraphicsSystem::CreateFramebuffers()
    {
        m_swapchainFramebuffers.resize(m_swapchainImageViews.size());

        for (size_t i = 0; i < m_swapchainImageViews.size(); i++)
        {
            VkImageView attachments[] = {m_swapchainImageViews[i]};

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = m_swapchainExtent.width;
            framebufferInfo.height = m_swapchainExtent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create framebuffer");
            }
        }

        return true;
    }

    bool VulkanGraphicsSystem::CreateCommandPool()
    {
        QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_physicalDevice);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;

        if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create command pool");
        }

        return true;
    }

    bool VulkanGraphicsSystem::CreateCommandBuffers()
    {
        m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();

        if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate command buffers");
        }

        return true;
    }

    bool VulkanGraphicsSystem::CreateSyncObjects()
    {
        m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create synchronization objects for a frame");
            }
        }

        return true;
    }

} // namespace Orange::Graphics::Vulkan