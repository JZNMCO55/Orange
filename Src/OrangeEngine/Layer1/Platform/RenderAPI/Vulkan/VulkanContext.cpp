/**
 * @file VulkanContext.cpp
 * @brief Vulkan渲染上下文实现文件
 * @details 实现Vulkan实例创建、验证层配置、调试回调和设备初始化
 * @author Orange Engine
 */

#include "orgpch.h"
#include "VulkanContext.h"

#include "Vulkan.h"
#include "VulkanImage.h"

#include <GLFW/glfw3.h>

#ifdef ORG_PLATFORM_WINDOWS
#include <Windows.h>
#endif

#include <format>

// #ifndef VK_API_VERSION_1_2
// #error Wrong Vulkan SDK! Please run scripts/Setup.bat
// #endif

namespace Orange
{

    // 验证层配置：在Debug和Release模式下启用，Distribution模式下禁用
#if defined(ORG_DEBUG) || defined(ORG_RELEASE)
    static bool s_Validation = true;
#else
    static bool s_Validation = false; // Let's leave this on for now...
#endif

#if 0
	/**
	 * @brief 旧版调试报告回调函数（已废弃）
	 * @details 这是VK_EXT_debug_report扩展的回调函数，现已被VK_EXT_debug_utils替代
	 */
	static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugReportCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData)
	{
		(void)flags; (void)object; (void)location; (void)messageCode; (void)pUserData; (void)pLayerPrefix; // Unused arguments
		ORG_CORE_WARN_TAG("Renderer", "VulkanDebugCallback:\n  Object Type: {0}\n  Message: {1}", objectType, pMessage);

		const auto& imageRefs = VulkanImage2D::GetImageRefs();
		if (strstr(pMessage, "CoreValidation-DrawState-InvalidImageLayout"))
			ORG_CORE_ASSERT(false);

		return VK_FALSE;
	}
#endif

    /**
     * @brief 将Vulkan调试消息类型转换为字符串
     * @param type 消息类型标志
     * @return 消息类型的字符串表示
     */
    constexpr const char *VkDebugUtilsMessageType(const VkDebugUtilsMessageTypeFlagsEXT type)
    {
        switch (type)
        {
        case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
            return "General"; // 一般信息
        case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
            return "Validation"; // 验证错误
        case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
            return "Performance"; // 性能警告
        default:
            return "Unknown"; // 未知类型
        }
    }

    /**
     * @brief 将Vulkan调试消息严重程度转换为字符串
     * @param severity 消息严重程度
     * @return 严重程度的字符串表示
     */
    constexpr const char *VkDebugUtilsMessageSeverity(const VkDebugUtilsMessageSeverityFlagBitsEXT severity)
    {
        switch (severity)
        {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            return "error"; // 错误
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            return "warning"; // 警告
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            return "info"; // 信息
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            return "verbose"; // 详细信息
        default:
            return "unknown"; // 未知
        }
    }

    /**
     * @brief Vulkan调试工具信使回调函数
     * @param messageSeverity 消息严重程度
     * @param messageType 消息类型
     * @param pCallbackData 回调数据，包含详细的调试信息
     * @param pUserData 用户数据（未使用）
     * @return VK_FALSE表示不中断Vulkan调用
     *
     * @details 这是现代Vulkan调试的核心回调函数，提供比旧版debug_report更丰富的信息：
     * - 支持命令缓冲区标签（用于标识渲染通道）
     * - 支持对象信息（显示相关的Vulkan对象）
     * - 更详细的错误分类和上下文信息
     *
     * 回调信息包括：
     * - 消息严重程度（错误、警告、信息、详细）
     * - 消息类型（一般、验证、性能）
     * - 命令缓冲区标签（如果有）
     * - 相关Vulkan对象信息
     */
    static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugUtilsMessengerCallback(const VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, const VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData)
    {
        (void)pUserData; // 未使用的参数

        // 性能警告控制：可以选择性地禁用性能相关的警告
        const bool performanceWarnings = false;
        if (!performanceWarnings)
        {
            if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
                return VK_FALSE;
        }

        // 构建命令缓冲区标签信息字符串
        // 标签用于标识当前正在执行的渲染通道或操作
        std::string labels, objects;
        if (pCallbackData->cmdBufLabelCount)
        {
            labels = std::format("\tLabels({}): \n", pCallbackData->cmdBufLabelCount);
            for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; ++i)
            {
                const auto &label = pCallbackData->pCmdBufLabels[i];
                // 格式化颜色信息（RGBA值）
                const std::string colorStr = std::format("[ {}, {}, {}, {} ]", label.color[0], label.color[1], label.color[2], label.color[3]);
                labels.append(std::format("\t\t- Command Buffer Label[{0}]: name: {1}, color: {2}\n", i, label.pLabelName ? label.pLabelName : "NULL", colorStr));
            }
        }

        // 构建相关Vulkan对象信息字符串
        // 显示与错误相关的Vulkan对象（缓冲区、图像、管线等）
        if (pCallbackData->objectCount)
        {
            objects = std::format("\tObjects({}): \n", pCallbackData->objectCount);
            for (uint32_t i = 0; i < pCallbackData->objectCount; ++i)
            {
                const auto &object = pCallbackData->pObjects[i];
                objects.append(std::format("\t\t- Object[{0}] name: {1}, type: {2}, handle: {3:#x}\n", i, object.pObjectName ? object.pObjectName : "NULL", Utils::VkObjectTypeToString(object.objectType), object.objectHandle));
            }
        }

        // 输出完整的调试信息
        // 包括消息类型、严重程度、具体消息、标签和对象信息
        ORG_CORE_WARN("{0} {1} message: \n\t{2}\n {3} {4}", VkDebugUtilsMessageType(messageType), VkDebugUtilsMessageSeverity(messageSeverity), pCallbackData->pMessage, labels, objects);

        // 获取图像引用（用于特定的调试场景）
        [[maybe_unused]] const auto &imageRefs = VulkanImage2D::GetImageRefs();

        return VK_FALSE; // 不中断Vulkan调用的执行
    }

    /**
     * @brief 检查驱动程序API版本支持
     * @param minimumSupportedVersion 最低支持的API版本
     * @return 是否支持所需的API版本
     *
     * @details 验证系统安装的Vulkan驱动程序是否支持应用程序所需的最低API版本。
     * 这对于确保应用程序能够正常运行非常重要，因为不同版本的Vulkan API
     * 具有不同的功能和扩展支持。
     */
    static bool CheckDriverAPIVersionSupport(uint32_t minimumSupportedVersion)
    {
        uint32_t instanceVersion;
        vkEnumerateInstanceVersion(&instanceVersion);

        if (instanceVersion < minimumSupportedVersion)
        {
            // 输出详细的版本信息，帮助用户了解问题
            ORG_CORE_CRITICAL("Incompatible Vulkan driver version!");
            ORG_CORE_CRITICAL("  You have {}.{}.{}", VK_API_VERSION_MAJOR(instanceVersion), VK_API_VERSION_MINOR(instanceVersion), VK_API_VERSION_PATCH(instanceVersion));
            ORG_CORE_CRITICAL("  You need at least {}.{}.{}", VK_API_VERSION_MAJOR(minimumSupportedVersion), VK_API_VERSION_MINOR(minimumSupportedVersion), VK_API_VERSION_PATCH(minimumSupportedVersion));

            return false;
        }

        return true;
    }

    /**
     * @brief VulkanContext构造函数
     * @details 初始化Vulkan上下文的基本状态
     */
    VulkanContext::VulkanContext()
    {
    }

    /**
     * @brief VulkanContext析构函数
     * @details 清理Vulkan实例和相关资源
     *
     * 注意：设备的销毁在这里已经太晚了，因为Destroy()方法需要访问上下文
     * （而我们正在析构上下文）。设备在GLFWWindow::Shutdown()中销毁。
     */
    VulkanContext::~VulkanContext()
    {
        // 销毁Vulkan实例
        vkDestroyInstance(s_VulkanInstance, nullptr);
        s_VulkanInstance = nullptr;
    }

    /**
     * @brief 初始化Vulkan上下文
     * @details 创建Vulkan实例、配置验证层、选择物理设备、创建逻辑设备
     *
     * 初始化流程：
     * 1. 检查GLFW的Vulkan支持
     * 2. 验证驱动程序API版本
     * 3. 配置应用程序信息
     * 4. 配置实例扩展和验证层
     * 5. 创建Vulkan实例
     * 6. 设置调试回调
     * 7. 选择物理设备
     * 8. 创建逻辑设备
     * 9. 初始化内存分配器
     * 10. 创建管线缓存
     */
    void VulkanContext::Init()
    {
        ORG_CORE_INFO_TAG("Renderer", "VulkanContext::Create");

        // 第一步：检查GLFW的Vulkan支持
        // GLFW必须支持Vulkan才能创建窗口表面
        ORG_CORE_ASSERT(glfwVulkanSupported(), "GLFW must support Vulkan!");

        // 第二步：检查驱动程序API版本支持
        // 确保系统支持Vulkan 1.2或更高版本
        if (!CheckDriverAPIVersionSupport(VK_API_VERSION_1_2))
        {
#ifdef ORG_PLATFORM_WINDOWS
            // Windows平台显示消息框
            MessageBox(nullptr, "Incompatible Vulkan driver version.\nUpdate your GPU drivers!", "Orange Error", MB_OK | MB_ICONERROR);
#else
            // 其他平台输出错误信息
            ORG_CORE_ERROR("Incompatible Vulkan driver version.\nUpdate your GPU drivers!");
#endif
            ORG_CORE_VERIFY(false);
        }

        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // 第三步：配置应用程序信息
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Orange";     // 应用程序名称
        appInfo.pEngineName = "Orange";          // 引擎名称
        appInfo.apiVersion = VK_API_VERSION_1_2; // 使用的Vulkan API版本

        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // 第四步：配置扩展和验证层
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // 平台特定的表面扩展配置
        // TODO(Emily): GLFW可以为我们处理这个
#ifdef ORG_PLATFORM_WINDOWS
#define VK_KHR_WIN32_SURFACE_EXTENSION_NAME "VK_KHR_win32_surface"
#elif defined(ORG_PLATFORM_LINUX)
#define VK_KHR_WIN32_SURFACE_EXTENSION_NAME "VK_KHR_xcb_surface"
#endif

        // 实例扩展列表
        // 这些扩展提供了与窗口系统交互和调试的能力
        std::vector<const char *> instanceExtensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,      // 表面扩展（跨平台）
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME // 平台特定表面扩展
        };

        // 调试工具扩展：性能影响很小，可以在Release版本中使用
        instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        if (s_Validation)
        {
            // 旧版调试报告扩展（已废弃，但某些工具可能需要）
            instanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
            // 物理设备属性2扩展（用于查询扩展属性）
            instanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        }

        // 验证特性配置
        // 启用最佳实践验证，帮助发现潜在的性能问题
        VkValidationFeatureEnableEXT enables[] = {VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT};
        VkValidationFeaturesEXT features = {};
        features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        features.enabledValidationFeatureCount = 1;
        features.pEnabledValidationFeatures = enables;

        // 实例创建信息配置
        VkInstanceCreateInfo instanceCreateInfo = {};
        instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceCreateInfo.pNext = nullptr; // &features; // 可以启用验证特性
        instanceCreateInfo.pApplicationInfo = &appInfo;
        instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
        instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();

        // TODO: 将所有验证相关代码提取到单独的类中
        if (s_Validation)
        {
            // 验证层配置
            // Khronos验证层提供全面的API使用验证
            const char *validationLayerName = "VK_LAYER_KHRONOS_validation";

            // 检查验证层是否在实例级别可用
            uint32_t instanceLayerCount;
            vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr);
            std::vector<VkLayerProperties> instanceLayerProperties(instanceLayerCount);
            vkEnumerateInstanceLayerProperties(&instanceLayerCount, instanceLayerProperties.data());

            bool validationLayerPresent = false;
            ORG_CORE_INFO_TAG("Renderer", "Vulkan Instance Layers:");
            for (const VkLayerProperties &layer : instanceLayerProperties)
            {
                ORG_CORE_INFO_TAG("Renderer", "  {0}", layer.layerName);
                if (strcmp(layer.layerName, validationLayerName) == 0)
                {
                    validationLayerPresent = true;
                    break;
                }
            }

            // 如果验证层可用，启用它
            if (validationLayerPresent)
            {
                instanceCreateInfo.ppEnabledLayerNames = &validationLayerName;
                instanceCreateInfo.enabledLayerCount = 1;
            }
            else
            {
                ORG_CORE_ERROR_TAG("Renderer", "Validation layer VK_LAYER_KHRONOS_validation not present, validation is disabled");
            }
        }

        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // 第五步：创建实例和加载调试扩展
        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // 创建Vulkan实例
        // 这是Vulkan应用程序的入口点
        VK_CHECK_RESULT(vkCreateInstance(&instanceCreateInfo, nullptr, &s_VulkanInstance));

        // 加载调试工具扩展函数
        // 这些函数用于设置对象名称和插入调试标记
        Utils::VulkanLoadDebugUtilsExtensions(s_VulkanInstance);

        // 第六步：设置调试回调
        if (s_Validation)
        {
#if 0
			// 旧版调试报告回调设置（已废弃）
			auto vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(s_VulkanInstance, "vkCreateDebugReportCallbackEXT");
			ORG_CORE_ASSERT(vkCreateDebugReportCallbackEXT != NULL, "");
			VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
			debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
			debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
			debug_report_ci.pfnCallback = VulkanDebugReportCallback;
			debug_report_ci.pUserData = VK_NULL_HANDLE;
			VK_CHECK_RESULT(vkCreateDebugReportCallbackEXT(s_VulkanInstance, &debug_report_ci, nullptr, &m_DebugReportCallback));
#endif

            // 现代调试工具信使设置
            // 获取创建调试信使的函数指针
            auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s_VulkanInstance, "vkCreateDebugUtilsMessengerEXT");
            ORG_CORE_ASSERT(vkCreateDebugUtilsMessengerEXT != NULL, "");

            // 配置调试信使创建信息
            VkDebugUtilsMessengerCreateInfoEXT debugUtilsCreateInfo{};
            debugUtilsCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            // 监听的消息类型：一般、验证、性能
            debugUtilsCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            // 回调函数
            debugUtilsCreateInfo.pfnUserCallback = VulkanDebugUtilsMessengerCallback;
            // 监听的消息严重程度：警告和错误（可以添加信息和详细级别）
            debugUtilsCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT /*  | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT*/
                ;

            // 创建调试信使
            VK_CHECK_RESULT(vkCreateDebugUtilsMessengerEXT(s_VulkanInstance, &debugUtilsCreateInfo, nullptr, &m_DebugUtilsMessenger));
        }

        // 第七步：选择物理设备
        // 从系统中可用的GPU中选择最适合的设备
        m_PhysicalDevice = VulkanPhysicalDevice::Select();

        // 第八步：配置设备特性并创建逻辑设备
        // 启用应用程序需要的设备特性
        VkPhysicalDeviceFeatures enabledFeatures;
        memset(&enabledFeatures, 0, sizeof(VkPhysicalDeviceFeatures));
        enabledFeatures.samplerAnisotropy = true;                   // 各向异性过滤
        enabledFeatures.wideLines = true;                           // 宽线条支持
        enabledFeatures.fillModeNonSolid = true;                    // 非实体填充模式（线框、点）
        enabledFeatures.independentBlend = true;                    // 独立混合
        enabledFeatures.pipelineStatisticsQuery = true;             // 管线统计查询
        enabledFeatures.shaderStorageImageReadWithoutFormat = true; // 着色器存储图像读取（无格式）

        // 创建逻辑设备
        m_Device = Ref<VulkanDevice>::Create(m_PhysicalDevice, enabledFeatures);

        // 第九步：初始化内存分配器
        // VMA（Vulkan Memory Allocator）简化了Vulkan内存管理
        VulkanAllocator::Init(m_Device);

        // 第十步：创建管线缓存
        // 管线缓存可以加速管线创建，特别是在应用程序重启时
        VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
        pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VK_CHECK_RESULT(vkCreatePipelineCache(m_Device->GetVulkanDevice(), &pipelineCacheCreateInfo, nullptr, &m_PipelineCache));
    }

}
