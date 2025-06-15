#include "orgpch.h"
#include "VulkanDevice.h"
#include "VulkanContext.h"
#include "vk_mem_alloc.h"

#define ORG_HAS_AFTERMATH 0

#ifdef TODO
#include "Debug/NsightAftermathGpuCrashTracker.h"
#endif

namespace Orange
{
    ////////////////////////////////////////////////////////////////////////////////////
    // Vulkan Physical Device - 物理设备管理
    ////////////////////////////////////////////////////////////////////////////////////

    /**
     * @brief VulkanPhysicalDevice构造函数
     * @details 枚举并选择最适合的物理设备，配置队列族和扩展支持
     *
     * 主要工作流程：
     * 1. 枚举系统中所有可用的物理设备
     * 2. 优先选择独立显卡（DISCRETE_GPU）
     * 3. 查询设备属性、特性和内存属性
     * 4. 枚举队列族并配置队列创建信息
     * 5. 查询支持的扩展列表
     * 6. 查找合适的深度缓冲格式
     */
    VulkanPhysicalDevice::VulkanPhysicalDevice()
    {
        // 获取Vulkan实例句柄
        auto vkInstance = VulkanContext::GetInstance();

        // 第一步：枚举物理设备
        uint32_t gpuCount = 0;
        // 获取可用物理设备数量
        vkEnumeratePhysicalDevices(vkInstance, &gpuCount, nullptr);
        ORG_CORE_ASSERT(gpuCount > 0, "");

        // 枚举所有物理设备
        std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
        VK_CHECK_RESULT(vkEnumeratePhysicalDevices(vkInstance, &gpuCount, physicalDevices.data()));

        // 第二步：选择最佳物理设备
        // 优先选择独立显卡，因为它们通常具有更好的性能
        VkPhysicalDevice selectedPhysicalDevice = nullptr;
        for (VkPhysicalDevice physicalDevice : physicalDevices)
        {
            vkGetPhysicalDeviceProperties(physicalDevice, &m_Properties);
            // 独立显卡优先级最高（相对于集成显卡）
            if (m_Properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                selectedPhysicalDevice = physicalDevice;
                break;
            }
        }

        // 如果没有找到独立显卡，使用最后一个可用设备
        if (!selectedPhysicalDevice)
        {
            ORG_CORE_INFO_TAG("Renderer", "Could not find discrete GPU.");
            selectedPhysicalDevice = physicalDevices.back();
        }

        ORG_CORE_ASSERT(selectedPhysicalDevice, "Could not find any physical devices!");
        m_PhysicalDevice = selectedPhysicalDevice;

        // 第三步：查询设备属性和特性
        // 获取设备支持的特性（如几何着色器、曲面细分等）
        vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &m_Features);
        // 获取设备内存属性（内存类型、堆大小等）
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &m_MemoryProperties);

        // 第四步：查询队列族属性
        // 队列族定义了设备支持的不同类型的操作（图形、计算、传输等）
        uint32_t queueFamilyCount;
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, nullptr);
        ORG_CORE_ASSERT(queueFamilyCount > 0, "");
        m_QueueFamilyProperties.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, m_QueueFamilyProperties.data());

        // 第五步：枚举设备扩展
        // 扩展提供额外的功能，如交换链支持、调试标记等
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extCount, nullptr);
        if (extCount > 0)
        {
            std::vector<VkExtensionProperties> extensions(extCount);
            if (vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extCount, &extensions.front()) == VK_SUCCESS)
            {
                ORG_CORE_INFO_TAG("Renderer", "Selected physical device has {0} extensions", extensions.size());
                for (const auto &ext : extensions)
                {
                    // 将支持的扩展名存储到集合中，便于后续查询
                    m_SupportedExtensions.emplace(ext.extensionName);
                    ORG_CORE_INFO_TAG("Renderer", "  {0}", ext.extensionName);
                }
            }
        }

        // 第六步：配置队列族
        // 队列族配置是创建逻辑设备时的关键步骤
        // 不同的队列族支持不同类型的操作，需要根据应用需求选择合适的队列族

        // 默认队列优先级（0.0-1.0范围，1.0为最高优先级）
        static const float defaultQueuePriority(0.0f);

        // 请求的队列类型：图形、计算、传输
        int requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
        m_QueueFamilyIndices = GetQueueFamilyIndices(requestedQueueTypes);

        // 配置图形队列
        // 图形队列用于渲染操作（顶点处理、光栅化、片段着色等）
        if (requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT)
        {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = m_QueueFamilyIndices.Graphics;
            queueInfo.queueCount = 1; // 只创建一个图形队列
            queueInfo.pQueuePriorities = &defaultQueuePriority;
            m_QueueCreateInfos.push_back(queueInfo);
        }

        // 配置专用计算队列
        // 如果计算队列族与图形队列族不同，需要单独创建
        // 专用计算队列可以与图形操作并行执行，提高性能
        if (requestedQueueTypes & VK_QUEUE_COMPUTE_BIT)
        {
            if (m_QueueFamilyIndices.Compute != m_QueueFamilyIndices.Graphics)
            {
                // 计算队列族索引不同，需要额外的队列创建信息
                VkDeviceQueueCreateInfo queueInfo{};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueFamilyIndex = m_QueueFamilyIndices.Compute;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &defaultQueuePriority;
                m_QueueCreateInfos.push_back(queueInfo);
            }
        }

        // 配置专用传输队列
        // 专用传输队列可以在后台执行数据传输，不阻塞图形和计算操作
        if (requestedQueueTypes & VK_QUEUE_TRANSFER_BIT)
        {
            if ((m_QueueFamilyIndices.Transfer != m_QueueFamilyIndices.Graphics) &&
                (m_QueueFamilyIndices.Transfer != m_QueueFamilyIndices.Compute))
            {
                // 传输队列族索引独立，需要额外的队列创建信息
                VkDeviceQueueCreateInfo queueInfo{};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueFamilyIndex = m_QueueFamilyIndices.Transfer;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &defaultQueuePriority;
                m_QueueCreateInfos.push_back(queueInfo);
            }
        }

        // 第七步：查找深度缓冲格式
        // 深度缓冲用于深度测试，确保正确的前后关系渲染
        m_DepthFormat = FindDepthFormat();
        ORG_CORE_ASSERT(m_DepthFormat);
    }

    VulkanPhysicalDevice::~VulkanPhysicalDevice()
    {
        // 物理设备不需要显式销毁，由Vulkan实例管理
    }

    /**
     * @brief 查找合适的深度缓冲格式
     * @return 支持的深度缓冲格式
     * @details 按优先级顺序尝试不同的深度格式，选择设备支持的最佳格式
     *
     * 深度格式优先级（从高到低）：
     * 1. D32_SFLOAT_S8_UINT - 32位浮点深度 + 8位模板
     * 2. D32_SFLOAT - 32位浮点深度
     * 3. D24_UNORM_S8_UINT - 24位深度 + 8位模板
     * 4. D16_UNORM_S8_UINT - 16位深度 + 8位模板
     * 5. D16_UNORM - 16位深度
     */
    VkFormat VulkanPhysicalDevice::FindDepthFormat() const
    {
        // 由于所有深度格式都是可选的，需要找到合适的深度格式
        // 从最高精度的打包格式开始尝试
        std::vector<VkFormat> depthFormats = {
            VK_FORMAT_D32_SFLOAT_S8_UINT, // 32位浮点深度 + 8位模板（最佳质量）
            VK_FORMAT_D32_SFLOAT,         // 32位浮点深度（高精度）
            VK_FORMAT_D24_UNORM_S8_UINT,  // 24位深度 + 8位模板（常用格式）
            VK_FORMAT_D16_UNORM_S8_UINT,  // 16位深度 + 8位模板（节省内存）
            VK_FORMAT_D16_UNORM           // 16位深度（最小内存占用）
        };

        // TODO: 将此功能移动到VulkanPhysicalDevice类中
        for (auto &format : depthFormats)
        {
            VkFormatProperties formatProps;
            vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &formatProps);
            // 格式必须支持深度模板附件的最优平铺
            // 最优平铺提供最佳的GPU访问性能
            if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                return format;
        }
        return VK_FORMAT_UNDEFINED;
    }

    /**
     * @brief 检查设备是否支持指定扩展
     * @param extensionName 扩展名称
     * @return 是否支持该扩展
     */
    bool VulkanPhysicalDevice::IsExtensionSupported(const std::string &extensionName) const
    {
        return m_SupportedExtensions.find(extensionName) != m_SupportedExtensions.end();
    }

    /**
     * @brief 获取队列族索引
     * @param flags 请求的队列类型标志
     * @return 队列族索引结构
     * @details 根据请求的队列类型查找合适的队列族索引
     *
     * 查找策略：
     * 1. 优先查找专用队列（只支持特定操作类型）
     * 2. 如果没有专用队列，使用通用队列
     * 3. 图形队列通常支持所有操作类型
     */
    VulkanPhysicalDevice::QueueFamilyIndices VulkanPhysicalDevice::GetQueueFamilyIndices(int flags)
    {
        QueueFamilyIndices indices;

        // 查找专用计算队列
        // 尝试找到支持计算但不支持图形的队列族（专用计算队列）
        // 专用计算队列可以与图形操作并行执行，提高整体性能
        if (flags & VK_QUEUE_COMPUTE_BIT)
        {
            for (uint32_t i = 0; i < m_QueueFamilyProperties.size(); i++)
            {
                auto &queueFamilyProperties = m_QueueFamilyProperties[i];
                // 支持计算但不支持图形的队列族
                if ((queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                    ((queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0))
                {
                    indices.Compute = i;
                    break;
                }
            }
        }

        // 查找专用传输队列
        // 尝试找到只支持传输操作的队列族（专用传输队列）
        // 专用传输队列可以在后台执行数据传输，不影响渲染性能
        if (flags & VK_QUEUE_TRANSFER_BIT)
        {
            for (uint32_t i = 0; i < m_QueueFamilyProperties.size(); i++)
            {
                auto &queueFamilyProperties = m_QueueFamilyProperties[i];
                // 支持传输但不支持图形和计算的队列族
                if ((queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                    ((queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) &&
                    ((queueFamilyProperties.queueFlags & VK_QUEUE_COMPUTE_BIT) == 0))
                {
                    indices.Transfer = i;
                    break;
                }
            }
        }

        // 对于其他队列类型或如果没有找到专用队列，返回第一个支持请求标志的队列族
        // 这是回退策略，确保所有请求的队列类型都有对应的队列族
        for (uint32_t i = 0; i < m_QueueFamilyProperties.size(); i++)
        {
            // 如果还没有找到传输队列，查找支持传输的队列族
            if ((flags & VK_QUEUE_TRANSFER_BIT) && indices.Transfer == -1)
            {
                if (m_QueueFamilyProperties[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
                    indices.Transfer = i;
            }

            // 如果还没有找到计算队列，查找支持计算的队列族
            if ((flags & VK_QUEUE_COMPUTE_BIT) && indices.Compute == -1)
            {
                if (m_QueueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
                    indices.Compute = i;
            }

            // 查找图形队列（通常支持所有操作类型）
            if (flags & VK_QUEUE_GRAPHICS_BIT)
            {
                if (m_QueueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                    indices.Graphics = i;
            }
        }

        return indices;
    }

    /**
     * @brief 获取内存类型索引
     * @param typeBits 内存类型位掩码
     * @param properties 所需的内存属性
     * @return 合适的内存类型索引
     * @details 根据内存需求查找合适的内存类型
     *
     * Vulkan内存管理：
     * - 不同的内存类型具有不同的属性（设备本地、主机可见等）
     * - 需要根据用途选择合适的内存类型
     * - typeBits指定了资源可以使用的内存类型
     */
    uint32_t VulkanPhysicalDevice::GetMemoryTypeIndex(uint32_t typeBits, VkMemoryPropertyFlags properties) const
    {
        // 遍历设备可用的所有内存类型
        for (uint32_t i = 0; i < m_MemoryProperties.memoryTypeCount; i++)
        {
            // 检查当前内存类型是否在允许的类型位掩码中
            if ((typeBits & 1) == 1)
            {
                // 检查内存类型是否具有所需的属性
                if ((m_MemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
                    return i;
            }
            // 移动到下一个内存类型位
            typeBits >>= 1;
        }

        ORG_CORE_ASSERT(false, "Could not find a suitable memory type!");
        return UINT32_MAX;
    }

    /**
     * @brief 选择物理设备
     * @return 选中的物理设备引用
     * @details 静态工厂方法，创建并返回物理设备实例
     */
    Ref<VulkanPhysicalDevice> VulkanPhysicalDevice::Select()
    {
        return Ref<VulkanPhysicalDevice>::Create();
    }

    ////////////////////////////////////////////////////////////////////////////////////
    // Vulkan Device - 逻辑设备管理
    ////////////////////////////////////////////////////////////////////////////////////

    /**
     * @brief VulkanDevice构造函数
     * @param physicalDevice 物理设备引用
     * @param enabledFeatures 启用的设备特性
     * @details 创建Vulkan逻辑设备，配置扩展和调试功能
     *
     * 逻辑设备创建流程：
     * 1. 配置设备扩展（交换链、调试标记等）
     * 2. 配置Aftermath崩溃诊断（如果支持）
     * 3. 创建逻辑设备
     * 4. 获取队列句柄
     */
    VulkanDevice::VulkanDevice(const Ref<VulkanPhysicalDevice> &physicalDevice, VkPhysicalDeviceFeatures enabledFeatures)
        : m_PhysicalDevice(physicalDevice), m_EnabledFeatures(enabledFeatures)
    {
        // 是否启用NVIDIA Aftermath崩溃诊断
        const bool enableAftermath = true;

        // 配置设备扩展
        // 扩展提供额外功能，如交换链支持、调试工具等
        std::vector<const char *> deviceExtensions;

        // 交换链扩展：用于将渲染结果呈现到屏幕
        // 这是显示渲染结果的必需扩展
        ORG_CORE_ASSERT(m_PhysicalDevice->IsExtensionSupported(VK_KHR_SWAPCHAIN_EXTENSION_NAME));
        deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        // NVIDIA诊断扩展：用于GPU崩溃分析
        if (m_PhysicalDevice->IsExtensionSupported(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME))
            deviceExtensions.push_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
        if (m_PhysicalDevice->IsExtensionSupported(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME))
            deviceExtensions.push_back(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);

#if ORG_HAS_AFTERMATH
        // 配置NVIDIA Aftermath GPU崩溃跟踪
        // Aftermath提供详细的GPU崩溃信息，有助于调试复杂的渲染问题
        VkDeviceDiagnosticsConfigCreateInfoNV aftermathInfo = {};
        bool canEnableAftermath = enableAftermath &&
                                  m_PhysicalDevice->IsExtensionSupported(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME) &&
                                  m_PhysicalDevice->IsExtensionSupported(VK_NV_DEVICE_DIAGNOSTICS_CONFIG_EXTENSION_NAME);

        if (canEnableAftermath)
        {
            // 必须在设备创建之前初始化崩溃跟踪器
            GpuCrashTracker *gpuCrashTracker = hnew GpuCrashTracker();
            gpuCrashTracker->Initialize();

            // 配置Aftermath标志
            VkDeviceDiagnosticsConfigFlagBitsNV aftermathFlags = (VkDeviceDiagnosticsConfigFlagBitsNV)(VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_RESOURCE_TRACKING_BIT_NV |     // 资源跟踪
                                                                                                       VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_AUTOMATIC_CHECKPOINTS_BIT_NV | // 自动检查点
                                                                                                       VK_DEVICE_DIAGNOSTICS_CONFIG_ENABLE_SHADER_DEBUG_INFO_BIT_NV);     // 着色器调试信息

            aftermathInfo.sType = VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV;
            aftermathInfo.flags = aftermathFlags;
        }
#endif

        // 配置设备创建信息
        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
#if ORG_HAS_AFTERMATH
        // 如果启用了Aftermath，将其配置信息链接到设备创建信息
        if (canEnableAftermath)
            deviceCreateInfo.pNext = &aftermathInfo;
#endif
        // 队列创建信息（从物理设备获取）
        deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(physicalDevice->m_QueueCreateInfos.size());
        ;
        deviceCreateInfo.pQueueCreateInfos = physicalDevice->m_QueueCreateInfos.data();
        // 启用的设备特性
        deviceCreateInfo.pEnabledFeatures = &enabledFeatures;

        // 物理设备特性2结构（用于扩展特性）
        VkPhysicalDeviceFeatures2 physicalDeviceFeatures2{};

        // 调试标记扩展：用于在调试工具中标记Vulkan对象
        // 这对于调试和性能分析非常有用
        if (m_PhysicalDevice->IsExtensionSupported(VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
        {
            deviceExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
            m_EnableDebugMarkers = true;
        }

        // 设置扩展列表
        if (deviceExtensions.size() > 0)
        {
            deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
            deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
        }

        // 创建逻辑设备
        // 这是Vulkan设备初始化的关键步骤
        VkResult result = vkCreateDevice(m_PhysicalDevice->GetVulkanPhysicalDevice(), &deviceCreateInfo, nullptr, &m_LogicalDevice);
        ORG_CORE_ASSERT(result == VK_SUCCESS);

        // 获取队列句柄
        // 队列是提交命令缓冲区的接口
        vkGetDeviceQueue(m_LogicalDevice, m_PhysicalDevice->m_QueueFamilyIndices.Graphics, 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_LogicalDevice, m_PhysicalDevice->m_QueueFamilyIndices.Compute, 0, &m_ComputeQueue);
    }

    VulkanDevice::~VulkanDevice()
    {
        // 析构函数中不执行清理，由Destroy()方法处理
    }

    /**
     * @brief 销毁设备
     * @details 清理所有资源并销毁逻辑设备
     *
     * 销毁顺序很重要：
     * 1. 清理命令池（会自动清理相关的命令缓冲区）
     * 2. 等待设备空闲
     * 3. 销毁逻辑设备
     */
    void VulkanDevice::Destroy()
    {
        // 清理所有线程本地命令池
        // 这会自动释放相关的命令缓冲区
        m_CommandPools.clear();

        // 等待设备完成所有操作
        // 确保没有正在执行的命令
        vkDeviceWaitIdle(m_LogicalDevice);

        // 销毁逻辑设备
        vkDestroyDevice(m_LogicalDevice, nullptr);
    }

    /**
     * @brief 锁定队列
     * @param compute 是否锁定计算队列，默认锁定图形队列
     * @details 用于多线程环境下的队列同步
     *
     * 队列同步的重要性：
     * - Vulkan队列不是线程安全的
     * - 多个线程同时访问同一队列会导致未定义行为
     * - 必须使用互斥锁保护队列访问
     */
    void VulkanDevice::LockQueue(bool compute)
    {
        if (compute)
            m_ComputeQueueMutex.lock();
        else
            m_GraphicsQueueMutex.lock();
    }

    /**
     * @brief 解锁队列
     * @param compute 是否解锁计算队列，默认解锁图形队列
     */
    void VulkanDevice::UnlockQueue(bool compute)
    {
        if (compute)
            m_ComputeQueueMutex.unlock();
        else
            m_GraphicsQueueMutex.unlock();
    }

    /**
     * @brief 获取命令缓冲区
     * @param begin 是否立即开始记录
     * @param compute 是否为计算命令缓冲区
     * @return 命令缓冲区句柄
     * @details 从线程本地命令池分配命令缓冲区
     */
    VkCommandBuffer VulkanDevice::GetCommandBuffer(bool begin, bool compute)
    {
        return GetOrCreateThreadLocalCommandPool()->AllocateCommandBuffer(begin, compute);
    }

    /**
     * @brief 刷新命令缓冲区
     * @param commandBuffer 要刷新的命令缓冲区
     * @details 结束记录并提交命令缓冲区到默认队列
     */
    void VulkanDevice::FlushCommandBuffer(VkCommandBuffer commandBuffer)
    {
        GetThreadLocalCommandPool()->FlushCommandBuffer(commandBuffer);
    }

    /**
     * @brief 刷新命令缓冲区到指定队列
     * @param commandBuffer 要刷新的命令缓冲区
     * @param queue 目标队列
     */
    void VulkanDevice::FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue)
    {
        GetThreadLocalCommandPool()->FlushCommandBuffer(commandBuffer);
    }

    /**
     * @brief 创建辅助命令缓冲区
     * @param debugName 调试名称
     * @return 辅助命令缓冲区句柄
     * @details 创建二级命令缓冲区，用于在渲染通道内执行
     *
     * 辅助命令缓冲区的用途：
     * - 可以在渲染通道内执行
     * - 支持命令缓冲区继承
     * - 适用于多线程渲染
     */
    VkCommandBuffer VulkanDevice::CreateSecondaryCommandBuffer(const char *debugName)
    {
        VkCommandBuffer cmdBuffer;

        // 配置命令缓冲区分配信息
        VkCommandBufferAllocateInfo cmdBufAllocateInfo = {};
        cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufAllocateInfo.commandPool = GetOrCreateThreadLocalCommandPool()->GetGraphicsCommandPool();
        cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY; // 辅助级别
        cmdBufAllocateInfo.commandBufferCount = 1;

        // 分配命令缓冲区
        VK_CHECK_RESULT(vkAllocateCommandBuffers(m_LogicalDevice, &cmdBufAllocateInfo, &cmdBuffer));

        // 设置调试名称（如果支持调试标记）
        VKUtils::SetDebugUtilsObjectName(m_LogicalDevice, VK_OBJECT_TYPE_COMMAND_BUFFER, debugName, cmdBuffer);
        return cmdBuffer;
    }

    /**
     * @brief 获取线程本地命令池
     * @return 命令池引用，如果不存在则断言失败
     * @details 获取当前线程的命令池，要求命令池已存在
     */
    Ref<VulkanCommandPool> VulkanDevice::GetThreadLocalCommandPool()
    {
        auto threadID = std::this_thread::get_id();
        ORG_CORE_VERIFY(m_CommandPools.find(threadID) != m_CommandPools.end());

        return m_CommandPools.at(threadID);
    }

    /**
     * @brief 获取或创建线程本地命令池
     * @return 命令池引用
     * @details 获取当前线程的命令池，如果不存在则创建新的
     *
     * 线程本地命令池的优势：
     * - 避免多线程竞争
     * - 每个线程有独立的命令缓冲区分配器
     * - 提高多线程渲染性能
     */
    Ref<VulkanCommandPool> VulkanDevice::GetOrCreateThreadLocalCommandPool()
    {
        auto threadID = std::this_thread::get_id();
        auto commandPoolIt = m_CommandPools.find(threadID);
        if (commandPoolIt != m_CommandPools.end())
            return commandPoolIt->second;

        // 创建新的线程本地命令池
        Ref<VulkanCommandPool> commandPool = Ref<VulkanCommandPool>::Create();
        m_CommandPools[threadID] = commandPool;
        return commandPool;
    }

    ////////////////////////////////////////////////////////////////////////////////////
    // Vulkan Command Pool - 命令池管理
    ////////////////////////////////////////////////////////////////////////////////////

    /**
     * @brief VulkanCommandPool构造函数
     * @details 创建图形和计算命令池
     *
     * 命令池的作用：
     * - 管理命令缓冲区的内存分配
     * - 提供命令缓冲区的重置和回收机制
     * - 每个线程应该有独立的命令池
     */
    VulkanCommandPool::VulkanCommandPool()
    {
        auto device = VulkanContext::GetCurrentDevice();
        auto vulkanDevice = device->GetVulkanDevice();

        // 创建图形命令池
        VkCommandPoolCreateInfo cmdPoolInfo = {};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.queueFamilyIndex = device->GetPhysicalDevice()->GetQueueFamilyIndices().Graphics;
        // 允许重置单个命令缓冲区，提高性能
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK_RESULT(vkCreateCommandPool(vulkanDevice, &cmdPoolInfo, nullptr, &m_GraphicsCommandPool));

        // 创建计算命令池
        cmdPoolInfo.queueFamilyIndex = device->GetPhysicalDevice()->GetQueueFamilyIndices().Compute;
        VK_CHECK_RESULT(vkCreateCommandPool(vulkanDevice, &cmdPoolInfo, nullptr, &m_ComputeCommandPool));
    }

    /**
     * @brief VulkanCommandPool析构函数
     * @details 销毁图形和计算命令池
     */
    VulkanCommandPool::~VulkanCommandPool()
    {
        auto device = VulkanContext::GetCurrentDevice();
        auto vulkanDevice = device->GetVulkanDevice();

        // 销毁命令池会自动释放所有相关的命令缓冲区
        vkDestroyCommandPool(vulkanDevice, m_GraphicsCommandPool, nullptr);
        vkDestroyCommandPool(vulkanDevice, m_ComputeCommandPool, nullptr);
    }

    /**
     * @brief 分配命令缓冲区
     * @param begin 是否立即开始记录
     * @param compute 是否为计算命令缓冲区
     * @return 分配的命令缓冲区句柄
     * @details 从相应的命令池分配主级命令缓冲区
     */
    VkCommandBuffer VulkanCommandPool::AllocateCommandBuffer(bool begin, bool compute)
    {
        auto device = VulkanContext::GetCurrentDevice();
        auto vulkanDevice = device->GetVulkanDevice();

        VkCommandBuffer cmdBuffer;

        // 配置命令缓冲区分配信息
        VkCommandBufferAllocateInfo cmdBufAllocateInfo = {};
        cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        // 根据类型选择相应的命令池
        cmdBufAllocateInfo.commandPool = compute ? m_ComputeCommandPool : m_GraphicsCommandPool;
        cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // 主级命令缓冲区
        cmdBufAllocateInfo.commandBufferCount = 1;

        // 分配命令缓冲区
        VK_CHECK_RESULT(vkAllocateCommandBuffers(vulkanDevice, &cmdBufAllocateInfo, &cmdBuffer));

        // 如果请求，立即开始命令记录
        if (begin)
        {
            VkCommandBufferBeginInfo cmdBufferBeginInfo{};
            cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufferBeginInfo));
        }

        return cmdBuffer;
    }

    /**
     * @brief 刷新命令缓冲区到默认队列
     * @param commandBuffer 要刷新的命令缓冲区
     */
    void VulkanCommandPool::FlushCommandBuffer(VkCommandBuffer commandBuffer)
    {
        auto device = VulkanContext::GetCurrentDevice();
        FlushCommandBuffer(commandBuffer, device->GetGraphicsQueue());
    }

    /**
     * @brief 刷新命令缓冲区到指定队列
     * @param commandBuffer 要刷新的命令缓冲区
     * @param queue 目标队列
     * @details 结束记录、提交执行、等待完成、清理资源
     *
     * 刷新流程：
     * 1. 结束命令缓冲区记录
     * 2. 创建提交信息
     * 3. 创建栅栏用于同步
     * 4. 提交到队列（加锁保护）
     * 5. 等待执行完成
     * 6. 清理栅栏和命令缓冲区
     */
    void VulkanCommandPool::FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue)
    {
        auto device = VulkanContext::GetCurrentDevice();
        ORG_CORE_VERIFY(queue == device->GetGraphicsQueue());
        auto vulkanDevice = device->GetVulkanDevice();

        // 栅栏超时时间（100秒）
        const uint64_t DEFAULT_FENCE_TIMEOUT = 100000000000;

        ORG_CORE_ASSERT(commandBuffer != VK_NULL_HANDLE);

        // 结束命令缓冲区记录
        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

        // 配置提交信息
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        // 创建栅栏确保命令缓冲区执行完成
        // 栅栏用于CPU-GPU同步
        VkFenceCreateInfo fenceCreateInfo = {};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags = 0;
        VkFence fence;
        VK_CHECK_RESULT(vkCreateFence(vulkanDevice, &fenceCreateInfo, nullptr, &fence));

        {
            // 锁定队列，防止多线程竞争
            device->LockQueue();

            // 提交到队列
            VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));

            device->UnlockQueue();
        }

        // 等待栅栏信号，表示命令缓冲区执行完成
        VK_CHECK_RESULT(vkWaitForFences(vulkanDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

        // 清理资源
        vkDestroyFence(vulkanDevice, fence, nullptr);
        // 释放命令缓冲区回命令池
        vkFreeCommandBuffers(vulkanDevice, m_GraphicsCommandPool, 1, &commandBuffer);
    }
}