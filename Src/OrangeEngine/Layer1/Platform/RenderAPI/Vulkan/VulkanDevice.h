#ifndef VULKAN_DEVICE_H
#define VULKAN_DEVICE_H

#include "Core/Base/Ref.h"
#include "Vulkan.h"
#include <unordered_set>

namespace Orange
{
    /**
     * @class VulkanPhysicalDevice
     * @brief Vulkan物理设备类
     * @details 封装Vulkan物理设备，提供设备属性查询和队列族管理
     *
     * 该类负责：
     * - 物理设备的选择和属性查询
     * - 队列族索引的管理
     * - 扩展支持的检查
     * - 内存类型的查询
     */
    class VulkanPhysicalDevice : public RefCounted
    {
    public:
        /**
         * @struct QueueFamilyIndices
         * @brief 队列族索引结构
         * @details 存储不同类型队列的族索引
         */
        struct QueueFamilyIndices
        {
            int32_t Graphics = -1; ///< 图形队列族索引
            int32_t Compute = -1;  ///< 计算队列族索引
            int32_t Transfer = -1; ///< 传输队列族索引
        };

    public:
        /**
         * @brief 构造函数
         */
        VulkanPhysicalDevice();
        ~VulkanPhysicalDevice();

        /**
         * @brief 检查扩展是否支持
         * @param extensionName 扩展名称
         * @return 是否支持该扩展
         */
        bool IsExtensionSupported(const std::string &extensionName) const;
        uint32_t GetMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

        /**
         * @brief 获取Vulkan物理设备句柄
         * @return VkPhysicalDevice句柄
         */
        VkPhysicalDevice GetVulkanPhysicalDevice() const { return m_PhysicalDevice; }
        const QueueFamilyIndices &GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }

        /**
         * @brief 获取设备属性
         * @return 设备属性结构的引用
         */
        const VkPhysicalDeviceProperties &GetProperties() const { return m_Properties; }
        const VkPhysicalDeviceLimits &GetLimits() const { return m_Properties.limits; }
        const VkPhysicalDeviceMemoryProperties &GetMemoryProperties() const { return m_MemoryProperties; }

        /**
         * @brief 获取深度缓冲格式
         * @return 深度缓冲的VkFormat
         */
        VkFormat GetDepthFormat() const { return m_DepthFormat; }

        /**
         * @brief 选择最佳物理设备
         * @return 选中的物理设备引用
         * @details 从可用的物理设备中选择最适合的设备
         */
        static Ref<VulkanPhysicalDevice> Select();

    private:
        /**
         * @brief 查找合适的深度格式
         * @return 深度缓冲格式
         */
        VkFormat FindDepthFormat() const;
        QueueFamilyIndices GetQueueFamilyIndices(int queueFlags);

    private:
        QueueFamilyIndices m_QueueFamilyIndices;

        VkPhysicalDevice m_PhysicalDevice = nullptr;
        VkPhysicalDeviceProperties m_Properties;
        VkPhysicalDeviceFeatures m_Features;
        VkPhysicalDeviceMemoryProperties m_MemoryProperties;

        VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED;

        std::vector<VkQueueFamilyProperties> m_QueueFamilyProperties;
        std::unordered_set<std::string> m_SupportedExtensions;
        std::vector<VkDeviceQueueCreateInfo> m_QueueCreateInfos;

        friend class VulkanDevice;
    };

    /**
     * @class VulkanCommandPool
     * @brief Vulkan命令池类
     * @details 管理Vulkan命令缓冲区的分配和回收
     *
     * 该类负责：
     * - 图形和计算命令池的管理
     * - 命令缓冲区的分配
     * - 命令缓冲区的提交和刷新
     */
    class VulkanCommandPool : public RefCounted
    {
    public:
        /**
         * @brief 构造函数
         */
        VulkanCommandPool();
        virtual ~VulkanCommandPool();

        /**
         * @brief 分配命令缓冲区
         * @param begin 是否立即开始记录
         * @param compute 是否为计算命令缓冲区
         * @return 分配的命令缓冲区句柄
         */
        VkCommandBuffer AllocateCommandBuffer(bool begin, bool compute = false);
        void FlushCommandBuffer(VkCommandBuffer commandBuffer);
        void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue);

        /**
         * @brief 获取图形命令池
         * @return 图形命令池句柄
         */
        VkCommandPool GetGraphicsCommandPool() const { return m_GraphicsCommandPool; }
        VkCommandPool GetComputeCommandPool() const { return m_ComputeCommandPool; }

    private:
        VkCommandPool m_GraphicsCommandPool, m_ComputeCommandPool;
    };

    // Represents a logical device
    /**
     * @class VulkanDevice
     * @brief Vulkan逻辑设备类
     * @details 表示Vulkan逻辑设备，管理队列和命令缓冲区
     *
     * 该类负责：
     * - 逻辑设备的创建和销毁
     * - 队列的管理和同步
     * - 命令缓冲区的分配和管理
     * - 线程本地命令池的管理
     */
    class VulkanDevice : public RefCounted
    {
    public:
        /**
         * @brief 构造函数
         * @param physicalDevice 物理设备引用
         * @param enabledFeatures 启用的设备特性
         */
        VulkanDevice(const Ref<VulkanPhysicalDevice> &physicalDevice, VkPhysicalDeviceFeatures enabledFeatures);
        ~VulkanDevice();

        /**
         * @brief 销毁设备
         * @details 清理所有资源并销毁逻辑设备
         */
        void Destroy();

        /**
         * @brief 锁定队列
         * @param compute 是否锁定计算队列，默认锁定图形队列
         * @details 用于多线程环境下的队列同步
         */
        void LockQueue(bool compute = false);
        void UnlockQueue(bool compute = false);
        VkQueue GetGraphicsQueue() { return m_GraphicsQueue; }
        VkQueue GetComputeQueue() { return m_ComputeQueue; }

        /**
         * @brief 获取命令缓冲区
         * @param begin 是否立即开始记录
         * @param compute 是否为计算命令缓冲区
         * @return 命令缓冲区句柄
         */
        VkCommandBuffer GetCommandBuffer(bool begin, bool compute = false);
        void FlushCommandBuffer(VkCommandBuffer commandBuffer);
        void FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue);

        /**
         * @brief 创建辅助命令缓冲区
         * @param debugName 调试名称
         * @return 辅助命令缓冲区句柄
         */
        VkCommandBuffer CreateSecondaryCommandBuffer(const char *debugName);

        /**
         * @brief 获取物理设备
         * @return 物理设备引用
         */
        const Ref<VulkanPhysicalDevice> &GetPhysicalDevice() const { return m_PhysicalDevice; }
        VkDevice GetVulkanDevice() const { return m_LogicalDevice; }

    private:
        /**
         * @brief 获取线程本地命令池
         * @return 命令池引用，如果不存在则返回nullptr
         */
        Ref<VulkanCommandPool> GetThreadLocalCommandPool();
        Ref<VulkanCommandPool> GetOrCreateThreadLocalCommandPool();

    private:
        VkDevice m_LogicalDevice = nullptr;
        Ref<VulkanPhysicalDevice> m_PhysicalDevice;
        VkPhysicalDeviceFeatures m_EnabledFeatures;

        VkQueue m_GraphicsQueue;
        VkQueue m_ComputeQueue;

        std::map<std::thread::id, Ref<VulkanCommandPool>> m_CommandPools;
        bool m_EnableDebugMarkers = false;

        std::mutex m_GraphicsQueueMutex, m_ComputeQueueMutex;
    };
}

#endif