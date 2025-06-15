#ifndef VULKAN_CONTEXT_H
#define VULKAN_CONTEXT_H

#include "Platform/RenderInterface/RendererContext.h"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"

struct GLFWwindow;

namespace Orange
{
    /**
     * @class VulkanContext
     * @brief Vulkan渲染上下文类
     * @details 继承自RendererContext，管理Vulkan实例、设备和交换链
     *
     * 该类负责：
     * - Vulkan实例的创建和管理
     * - 物理设备的选择和逻辑设备的创建
     * - 调试层和验证层的设置
     * - 交换链的管理
     * - 管线缓存的维护
     */
    class VulkanContext : public RendererContext
    {
    public:
        /**
         * @brief 构造函数
         */
        VulkanContext();

        /**
         * @brief 析构函数
         */
        virtual ~VulkanContext();

        /**
         * @brief 初始化Vulkan上下文
         * @details 创建Vulkan实例、选择物理设备、创建逻辑设备等
         */
        virtual void Init() override;

        /**
         * @brief 获取Vulkan设备
         * @return Vulkan设备的引用
         */
        Ref<VulkanDevice> GetDevice() { return m_Device; }

        /**
         * @brief 获取Vulkan实例
         * @return Vulkan实例句柄
         * @details 静态方法，可以在任何地方获取全局Vulkan实例
         */
        static VkInstance GetInstance() { return s_VulkanInstance; }

        /**
         * @brief 获取当前Vulkan上下文
         * @return 当前VulkanContext的引用
         */
        static Ref<VulkanContext> Get() 
        { 
#ifdef TODO // 后边再来处理，先保证编译通过
            return Ref<VulkanContext>(Renderer::GetContext()); 
#endif
            return nullptr;
        }

        /**
         * @brief 获取当前Vulkan设备
         * @return 当前VulkanDevice的引用
         * @details 便捷方法，直接获取当前上下文的设备
         */
        static Ref<VulkanDevice> GetCurrentDevice() { return Get()->GetDevice(); }

    private:
        // 设备相关
        Ref<VulkanPhysicalDevice> m_PhysicalDevice; ///< 物理设备
        Ref<VulkanDevice> m_Device;                 ///< 逻辑设备

        // Vulkan实例
        inline static VkInstance s_VulkanInstance; ///< 全局Vulkan实例

#if 0
		VkDebugReportCallbackEXT m_DebugReportCallback = VK_NULL_HANDLE; ///< 调试报告回调（已废弃）
#endif
        VkDebugUtilsMessengerEXT m_DebugUtilsMessenger = VK_NULL_HANDLE; ///< 调试工具信使
        VkPipelineCache m_PipelineCache = nullptr;                       ///< 管线缓存

        VulkanSwapChain m_SwapChain; ///< 交换链
    };
}

#endif