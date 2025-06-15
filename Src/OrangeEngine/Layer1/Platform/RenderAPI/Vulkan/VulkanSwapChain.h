#ifndef VULKAN_SWAP_CHAIN_H
#define VULKAN_SWAP_CHAIN_H

#include "Core/Base/Base.h"
#include "Platform/RenderInterface/RenderCommandBuffer.h"

#include "Vulkan.h"
#include "VulkanDevice.h"
#include "VulkanAllocator.h"
#include <GLFW/glfw3.h>

#include <vector>

namespace Orange
{

    /**
     * @class VulkanSwapChain
     * @brief Vulkan交换链类
     * @details 管理Vulkan交换链，处理图像呈现和帧缓冲区管理
     *
     * 该类负责：
     * - 交换链的创建和销毁
     * - 表面格式和颜色空间的选择
     * - 帧缓冲区和深度缓冲区的管理
     * - 命令缓冲区的分配和管理
     * - 同步对象（信号量、栅栏）的管理
     * - 垂直同步控制
     */
    class VulkanSwapChain
    {
    public:
        /**
         * @brief 默认构造函数
         */
        VulkanSwapChain() = default;

        /**
         * @brief 初始化交换链
         * @param instance Vulkan实例
         * @param device Vulkan设备引用
         * @details 设置基础的Vulkan实例和设备引用
         */
        void Init(VkInstance instance, const Ref<VulkanDevice> &device);

        /**
         * @brief 初始化表面
         * @param windowHandle GLFW窗口句柄
         * @details 创建与窗口关联的Vulkan表面
         */
        void InitSurface(GLFWwindow *windowHandle);

        /**
         * @brief 创建交换链
         * @param width 交换链宽度指针（可能被修改）
         * @param height 交换链高度指针（可能被修改）
         * @param vsync 是否启用垂直同步
         * @details 创建交换链、图像视图、深度缓冲区、渲染通道和帧缓冲区
         */
        void Create(uint32_t *width, uint32_t *height, bool vsync);

        /**
         * @brief 销毁交换链
         * @details 清理所有相关资源
         */
        void Destroy();

        /**
         * @brief 处理窗口大小调整
         * @param width 新宽度
         * @param height 新高度
         * @details 重新创建交换链以适应新的窗口大小
         */
        void OnResize(uint32_t width, uint32_t height);

        /**
         * @brief 开始新帧
         * @details 获取下一个可用图像并准备渲染
         */
        void BeginFrame();

        /**
         * @brief 呈现当前帧
         * @details 将渲染结果提交到交换链进行显示
         */
        void Present();

        /**
         * @brief 获取交换链图像数量
         * @return 图像数量
         */
        uint32_t GetImageCount() const { return m_ImageCount; }

        /**
         * @brief 获取交换链宽度
         * @return 宽度像素值
         */
        uint32_t GetWidth() const { return m_Width; }

        /**
         * @brief 获取交换链高度
         * @return 高度像素值
         */
        uint32_t GetHeight() const { return m_Height; }

        /**
         * @brief 获取渲染通道
         * @return VkRenderPass句柄
         */
        VkRenderPass GetRenderPass() { return m_RenderPass; }

        /**
         * @brief 获取当前帧缓冲区
         * @return 当前帧缓冲区句柄
         */
        VkFramebuffer GetCurrentFramebuffer() { return GetFramebuffer(m_CurrentImageIndex); }

        /**
         * @brief 获取当前绘制命令缓冲区
         * @return 当前命令缓冲区句柄
         */
        VkCommandBuffer GetCurrentDrawCommandBuffer() { return GetDrawCommandBuffer(m_CurrentFrameIndex); }

        /**
         * @brief 获取颜色格式
         * @return VkFormat颜色格式
         */
        VkFormat GetColorFormat() { return m_ColorFormat; }

        /**
         * @brief 获取当前缓冲区索引
         * @return 当前帧索引
         */
        uint32_t GetCurrentBufferIndex() const { return m_CurrentFrameIndex; }

        /**
         * @brief 获取指定索引的帧缓冲区
         * @param index 帧缓冲区索引
         * @return 帧缓冲区句柄
         */
        VkFramebuffer GetFramebuffer(uint32_t index)
        {
            ORG_CORE_ASSERT(index < m_Framebuffers.size());
            return m_Framebuffers[index];
        }

        /**
         * @brief 获取指定索引的绘制命令缓冲区
         * @param index 命令缓冲区索引
         * @return 命令缓冲区句柄
         */
        VkCommandBuffer GetDrawCommandBuffer(uint32_t index)
        {
            ORG_CORE_ASSERT(index < m_CommandBuffers.size());
            return m_CommandBuffers[index].CommandBuffer;
        }

        /**
         * @brief 设置垂直同步
         * @param enabled 是否启用垂直同步
         */
        void SetVSync(const bool enabled) { m_VSync = enabled; }

    private:
        /**
         * @brief 获取下一个可用图像
         * @return 图像索引
         * @details 从交换链获取下一个可用于渲染的图像
         */
        uint32_t AcquireNextImage();

        /**
         * @brief 查找图像格式和颜色空间
         * @details 选择最适合的表面格式和颜色空间
         */
        void FindImageFormatAndColorSpace();

    private:
        VkInstance m_Instance = nullptr; ///< Vulkan实例
        Ref<VulkanDevice> m_Device;      ///< Vulkan设备引用
        bool m_VSync = false;            ///< 垂直同步标志

        VkFormat m_ColorFormat;       ///< 颜色格式
        VkColorSpaceKHR m_ColorSpace; ///< 颜色空间

        VkSwapchainKHR m_SwapChain = nullptr; ///< 交换链句柄
        uint32_t m_ImageCount = 0;            ///< 交换链图像数量
        std::vector<VkImage> m_VulkanImages;  ///< Vulkan图像列表

        /**
         * @struct SwapchainImage
         * @brief 交换链图像结构
         * @details 包含图像和图像视图
         */
        struct SwapchainImage
        {
            VkImage Image = nullptr;         ///< 图像句柄
            VkImageView ImageView = nullptr; ///< 图像视图句柄
        };
        std::vector<SwapchainImage> m_Images; ///< 交换链图像列表

        /**
         * @brief 深度模板缓冲区结构
         */
        struct
        {
            VkImage Image = nullptr;             ///< 深度图像
            VmaAllocation MemoryAlloc = nullptr; ///< 内存分配
            VkImageView ImageView = nullptr;     ///< 深度图像视图
        } m_DepthStencil;

        std::vector<VkFramebuffer> m_Framebuffers; ///< 帧缓冲区列表

        /**
         * @struct SwapchainCommandBuffer
         * @brief 交换链命令缓冲区结构
         * @details 包含命令池和命令缓冲区
         */
        struct SwapchainCommandBuffer
        {
            VkCommandPool CommandPool = nullptr;     ///< 命令池
            VkCommandBuffer CommandBuffer = nullptr; ///< 命令缓冲区
        };
        std::vector<SwapchainCommandBuffer> m_CommandBuffers; ///< 命令缓冲区列表

        // 信号量：用于标识图像可用于渲染和渲染完成（每个飞行帧一对）
        std::vector<VkSemaphore> m_ImageAvailableSemaphores; ///< 图像可用信号量
        std::vector<VkSemaphore> m_RenderFinishedSemaphores; ///< 渲染完成信号量

        // 栅栏：用于标识命令缓冲区可以重用（每个飞行帧一个）
        std::vector<VkFence> m_WaitFences; ///< 等待栅栏

        VkRenderPass m_RenderPass = nullptr; ///< 渲染通道
        uint32_t m_CurrentFrameIndex = 0;    ///< 当前帧索引（正在处理的帧，最多到最大飞行帧数）
        uint32_t m_CurrentImageIndex = 0;    ///< 当前交换链图像索引（可能与帧索引不同）

        uint32_t m_QueueNodeIndex = UINT32_MAX; ///< 队列节点索引
        uint32_t m_Width = 0, m_Height = 0;     ///< 交换链尺寸

        VkSurfaceKHR m_Surface; ///< Vulkan表面

        friend class VulkanContext;
    };
}
#endif