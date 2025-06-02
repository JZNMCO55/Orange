#ifndef ORANGE_VULKAN_VULKANGRAPHICSSYSTEM_H
#define ORANGE_VULKAN_VULKANGRAPHICSSYSTEM_H

#include "../../GraphicsInterface/IGraphicsSystem.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>
#include <array>
#include <string>

// 顶点数据结构
struct Vertex
{
    std::array<float, 2> pos;
    std::array<float, 3> color;

    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        return attributeDescriptions;
    }
};

// 调试扩展函数声明
VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
                                      const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger);

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks *pAllocator);

// 调试回调函数声明
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData);

namespace Orange::Graphics::Vulkan
{

    // 前向声明
    class VulkanRenderDevice;
    class VulkanShaderCompiler;

    /**
     * @brief Vulkan图形系统实现
     */
    class VulkanGraphicsSystem : public IGraphicsSystem
    {
    public:
        VulkanGraphicsSystem();
        ~VulkanGraphicsSystem() override;

        // IGraphicsSystem接口实现
        bool Initialize() override;
        void Shutdown() override;
        void BeginFrame() override;
        void EndFrame() override;
        void Present() override;
        IRenderDevice *GetRenderDevice() override;
        IShaderCompiler *GetShaderCompiler() override;
        uint32_t GetBackbufferWidth() const override;
        uint32_t GetBackbufferHeight() const override;
        void SetClearColor(float r, float g, float b, float a = 1.0f) override;
        void Clear() override;

        // Vulkan特定接口
        VkInstance GetVulkanInstance() const { return m_instance; }
        VkDevice GetVulkanDevice() const { return m_device; }
        VkPhysicalDevice GetVulkanPhysicalDevice() const { return m_physicalDevice; }
        VkSurfaceKHR GetVulkanSurface() const { return m_surface; }
        GLFWwindow *GetWindow() const { return m_window; }
        bool IsInitialized() const { return m_initialized; }

        // 三角形渲染
        void DrawTriangle();

    private:
        // 初始化步骤
        bool CreateWindow();
        bool CreateInstance();
        bool SetupDebugMessenger();
        bool CreateSurface();
        bool PickPhysicalDevice();
        bool CreateLogicalDevice();
        bool CreateSwapchain();
        bool CreateImageViews();
        bool CreateRenderPass();
        bool CreateDescriptorSetLayout();
        bool CreateGraphicsPipeline();
        bool CreateFramebuffers();
        bool CreateCommandPool();
        bool CreateVertexBuffer();
        bool CreateCommandBuffers();
        bool CreateSyncObjects();

        // 三角形相关
        bool CreateTriangleResources();
        VkShaderModule CreateShaderModule(const std::vector<uint32_t> &code);

        // 辅助函数
        bool CheckValidationLayerSupport();
        std::vector<const char *> GetRequiredExtensions();
        bool IsDeviceSuitable(VkPhysicalDevice device);
        bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
        void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo);

        // 队列族查找
        struct QueueFamilyIndices
        {
            uint32_t graphicsFamily = UINT32_MAX;
            uint32_t presentFamily = UINT32_MAX;
            bool IsComplete() const
            {
                return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
            }
        };
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);

        // 交换链支持查询
        struct SwapchainSupportDetails
        {
            VkSurfaceCapabilitiesKHR capabilities;
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
        };
        SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device);

        VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);
        VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes);
        VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities);
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

        // 成员变量
        bool m_initialized = false;

        // GLFW窗口
        GLFWwindow *m_window = nullptr;
        uint32_t m_windowWidth = 800;
        uint32_t m_windowHeight = 600;

        // Vulkan核心对象
        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_graphicsQueue = VK_NULL_HANDLE;
        VkQueue m_presentQueue = VK_NULL_HANDLE;

        // 交换链相关
        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        std::vector<VkImage> m_swapchainImages;
        VkFormat m_swapchainImageFormat;
        VkExtent2D m_swapchainExtent;
        std::vector<VkImageView> m_swapchainImageViews;
        std::vector<VkFramebuffer> m_swapchainFramebuffers;

        // 渲染通道
        VkRenderPass m_renderPass = VK_NULL_HANDLE;

        // 图形管线
        VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

        // 顶点缓冲区
        VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;

        // 命令相关
        VkCommandPool m_commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_commandBuffers;

        // 同步对象
        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
        std::vector<VkSemaphore> m_imageAvailableSemaphores;
        std::vector<VkSemaphore> m_renderFinishedSemaphores;
        std::vector<VkFence> m_inFlightFences;
        uint32_t m_currentFrame = 0;
        uint32_t m_currentSwapchainImageIndex = 0;

        // 清除颜色
        VkClearColorValue m_clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};

        // 子系统
        std::unique_ptr<VulkanRenderDevice> m_renderDevice;
        std::unique_ptr<VulkanShaderCompiler> m_shaderCompiler;

        // 着色器SPIR-V数据
        std::vector<uint32_t> m_vertexShaderSpirv;
        std::vector<uint32_t> m_fragmentShaderSpirv;

        // 验证层和扩展
        const std::vector<const char *> m_validationLayers = {
            "VK_LAYER_KHRONOS_validation"};

        const std::vector<const char *> m_deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef ORANGE_DEBUG
        const bool m_enableValidationLayers = true;
#else
        const bool m_enableValidationLayers = false;
#endif
    };

} // namespace Orange::Graphics::Vulkan

#endif // ORANGE_VULKAN_VULKANGRAPHICSSYSTEM_H