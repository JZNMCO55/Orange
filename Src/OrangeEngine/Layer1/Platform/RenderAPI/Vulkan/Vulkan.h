#ifndef ORANGE_VULKAN_H
#define ORANGE_VULKAN_H

#include <vulkan/vulkan.h>
#include "Core/Base/Assert.h"

// Vulkan调试工具扩展函数指针
// 注意：使用inline而不是static，因为static会随机设置为nullptr
inline PFN_vkSetDebugUtilsObjectNameEXT fpSetDebugUtilsObjectNameEXT;   ///< 设置Vulkan对象名称的函数指针
inline PFN_vkCmdBeginDebugUtilsLabelEXT fpCmdBeginDebugUtilsLabelEXT;   ///< 开始调试标签的函数指针
inline PFN_vkCmdEndDebugUtilsLabelEXT fpCmdEndDebugUtilsLabelEXT;       ///< 结束调试标签的函数指针
inline PFN_vkCmdInsertDebugUtilsLabelEXT fpCmdInsertDebugUtilsLabelEXT; ///< 插入调试标签的函数指针

namespace Orange::Utils
{
    /**
     * @brief 加载Vulkan调试工具扩展
     * @param instance Vulkan实例
     * @details 加载调试相关的扩展函数指针，用于调试和性能分析
     */
    void VulkanLoadDebugUtilsExtensions(VkInstance instance);

    /**
     * @brief 将VkResult枚举值转换为字符串
     * @param result Vulkan操作结果
     * @return 对应的字符串描述
     * @details 用于错误处理和日志记录，提供可读的错误信息
     */
    inline const char *VKResultToString(VkResult result)
    {
        switch (result)
        {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_EVENT_SET:
            return "VK_EVENT_SET";
        case VK_EVENT_RESET:
            return "VK_EVENT_RESET";
        case VK_INCOMPLETE:
            return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED:
            return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS:
            return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:
            return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN:
            return "VK_ERROR_UNKNOWN";
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE:
            return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_FRAGMENTATION:
            return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
            return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
            return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT:
            return "VK_ERROR_VALIDATION_FAILED_EXT";
        case VK_ERROR_INVALID_SHADER_NV:
            return "VK_ERROR_INVALID_SHADER_NV";
        case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
            return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
        case VK_ERROR_NOT_PERMITTED_EXT:
            return "VK_ERROR_NOT_PERMITTED_EXT";
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
            return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
        case VK_THREAD_IDLE_KHR:
            return "VK_THREAD_IDLE_KHR";
        case VK_THREAD_DONE_KHR:
            return "VK_THREAD_DONE_KHR";
        case VK_OPERATION_DEFERRED_KHR:
            return "VK_OPERATION_DEFERRED_KHR";
        case VK_OPERATION_NOT_DEFERRED_KHR:
            return "VK_OPERATION_NOT_DEFERRED_KHR";
        case VK_PIPELINE_COMPILE_REQUIRED_EXT:
            return "VK_PIPELINE_COMPILE_REQUIRED_EXT";
        }
        ORG_CORE_ASSERT(false);
        return nullptr;
    }

    /**
     * @brief 将VkObjectType枚举值转换为字符串
     * @param type Vulkan对象类型
     * @return 对应的字符串描述
     * @details 用于调试和日志记录，提供可读的对象类型信息
     */
    inline const char *VkObjectTypeToString(const VkObjectType type)
    {
        switch (type)
        {
        case VK_OBJECT_TYPE_COMMAND_BUFFER:
            return "VK_OBJECT_TYPE_COMMAND_BUFFER";
        case VK_OBJECT_TYPE_PIPELINE:
            return "VK_OBJECT_TYPE_PIPELINE";
        case VK_OBJECT_TYPE_FRAMEBUFFER:
            return "VK_OBJECT_TYPE_FRAMEBUFFER";
        case VK_OBJECT_TYPE_IMAGE:
            return "VK_OBJECT_TYPE_IMAGE";
        case VK_OBJECT_TYPE_QUERY_POOL:
            return "VK_OBJECT_TYPE_QUERY_POOL";
        case VK_OBJECT_TYPE_RENDER_PASS:
            return "VK_OBJECT_TYPE_RENDER_PASS";
        case VK_OBJECT_TYPE_COMMAND_POOL:
            return "VK_OBJECT_TYPE_COMMAND_POOL";
        case VK_OBJECT_TYPE_PIPELINE_CACHE:
            return "VK_OBJECT_TYPE_PIPELINE_CACHE";
        case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR:
            return "VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR";
        case VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV:
            return "VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV";
        case VK_OBJECT_TYPE_BUFFER:
            return "VK_OBJECT_TYPE_BUFFER";
        case VK_OBJECT_TYPE_BUFFER_VIEW:
            return "VK_OBJECT_TYPE_BUFFER_VIEW";
        case VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT:
            return "VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT";
        case VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT:
            return "VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT";
        case VK_OBJECT_TYPE_DEFERRED_OPERATION_KHR:
            return "VK_OBJECT_TYPE_DEFERRED_OPERATION_KHR";
        case VK_OBJECT_TYPE_DESCRIPTOR_POOL:
            return "VK_OBJECT_TYPE_DESCRIPTOR_POOL";
        case VK_OBJECT_TYPE_DESCRIPTOR_SET:
            return "VK_OBJECT_TYPE_DESCRIPTOR_SET";
        case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT:
            return "VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT";
        case VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE:
            return "VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE";
        case VK_OBJECT_TYPE_PRIVATE_DATA_SLOT_EXT:
            return "VK_OBJECT_TYPE_PRIVATE_DATA_SLOT_EXT";
        case VK_OBJECT_TYPE_DEVICE:
            return "VK_OBJECT_TYPE_DEVICE";
        case VK_OBJECT_TYPE_DEVICE_MEMORY:
            return "VK_OBJECT_TYPE_DEVICE_MEMORY";
        case VK_OBJECT_TYPE_PIPELINE_LAYOUT:
            return "VK_OBJECT_TYPE_PIPELINE_LAYOUT";
        case VK_OBJECT_TYPE_DISPLAY_KHR:
            return "VK_OBJECT_TYPE_DISPLAY_KHR";
        case VK_OBJECT_TYPE_DISPLAY_MODE_KHR:
            return "VK_OBJECT_TYPE_DISPLAY_MODE_KHR";
        case VK_OBJECT_TYPE_PHYSICAL_DEVICE:
            return "VK_OBJECT_TYPE_PHYSICAL_DEVICE";
        case VK_OBJECT_TYPE_EVENT:
            return "VK_OBJECT_TYPE_EVENT";
        case VK_OBJECT_TYPE_FENCE:
            return "VK_OBJECT_TYPE_FENCE";
        case VK_OBJECT_TYPE_IMAGE_VIEW:
            return "VK_OBJECT_TYPE_IMAGE_VIEW";
        case VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV:
            return "VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV";
        case VK_OBJECT_TYPE_INSTANCE:
            return "VK_OBJECT_TYPE_INSTANCE";
        case VK_OBJECT_TYPE_PERFORMANCE_CONFIGURATION_INTEL:
            return "VK_OBJECT_TYPE_PERFORMANCE_CONFIGURATION_INTEL";
        case VK_OBJECT_TYPE_QUEUE:
            return "VK_OBJECT_TYPE_QUEUE";
        case VK_OBJECT_TYPE_SAMPLER:
            return "VK_OBJECT_TYPE_SAMPLER";
        case VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION:
            return "VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION";
        case VK_OBJECT_TYPE_SEMAPHORE:
            return "VK_OBJECT_TYPE_SEMAPHORE";
        case VK_OBJECT_TYPE_SHADER_MODULE:
            return "VK_OBJECT_TYPE_SHADER_MODULE";
        case VK_OBJECT_TYPE_SURFACE_KHR:
            return "VK_OBJECT_TYPE_SURFACE_KHR";
        case VK_OBJECT_TYPE_SWAPCHAIN_KHR:
            return "VK_OBJECT_TYPE_SWAPCHAIN_KHR";
        case VK_OBJECT_TYPE_VALIDATION_CACHE_EXT:
            return "VK_OBJECT_TYPE_VALIDATION_CACHE_EXT";
        case VK_OBJECT_TYPE_UNKNOWN:
            return "VK_OBJECT_TYPE_UNKNOWN";
        case VK_OBJECT_TYPE_MAX_ENUM:
            return "VK_OBJECT_TYPE_MAX_ENUM";
        }
        ORG_CORE_ASSERT(false);
        return "";
    }

    /**
     * @brief 获取诊断检查点信息
     * @details 用于GPU设备丢失时的诊断，获取GPU状态信息
     */
    void RetrieveDiagnosticCheckpoints();

    /**
     * @brief 检查Vulkan操作结果
     * @param result Vulkan操作返回的结果
     * @details 如果操作失败，输出错误信息并断言。对于设备丢失错误，会进行特殊处理
     */
    inline void VulkanCheckResult(VkResult result)
    {
        if (result != VK_SUCCESS)
        {
            ORG_CORE_ERROR("VkResult is '{0}'", Utils::VKResultToString(result));
            if (result == VK_ERROR_DEVICE_LOST)
            {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(3s);
                // Utils::RetrieveDiagnosticCheckpoints();
#ifdef TODO
                Utils::DumpGPUInfo();
#endif
            }
            ORG_CORE_ASSERT(result == VK_SUCCESS);
        }
    }

    /**
     * @brief 检查Vulkan操作结果（带文件和行号信息）
     * @param result Vulkan操作返回的结果
     * @param file 调用文件名
     * @param line 调用行号
     * @details 提供更详细的错误信息，包括错误发生的位置
     */
    inline void VulkanCheckResult(VkResult result, const char *file, int line)
    {
        if (result != VK_SUCCESS)
        {
            ORG_CORE_ERROR("VkResult is '{0}' in {1}:{2}", Utils::VKResultToString(result), file, line);
            if (result == VK_ERROR_DEVICE_LOST)
            {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(3s);
                // Utils::RetrieveDiagnosticCheckpoints();
#ifdef TODO
                Utils::DumpGPUInfo();
#endif
            }
            ORG_CORE_ASSERT(result == VK_SUCCESS);
        }
    }
}

/**
 * @def VK_CHECK_RESULT
 * @brief Vulkan结果检查宏
 * @param f 要检查的Vulkan函数调用
 * @details 自动检查Vulkan函数的返回值，如果失败则输出错误信息和位置
 */
#define VK_CHECK_RESULT(f)                                           \
    {                                                                \
        VkResult res = (f);                                          \
        ::Orange::Utils::VulkanCheckResult(res, __FILE__, __LINE__); \
    }

namespace Orange::VKUtils
{
    /**
     * @brief 设置Vulkan对象的调试名称
     * @param device Vulkan逻辑设备
     * @param objectType 对象类型
     * @param name 对象名称
     * @param handle 对象句柄
     * @details 用于调试时标识Vulkan对象，便于在调试工具中识别
     */
    inline static void SetDebugUtilsObjectName(const VkDevice device, const VkObjectType objectType, const std::string &name, const void *handle)
    {
        VkDebugUtilsObjectNameInfoEXT nameInfo;
        nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        nameInfo.objectType = objectType;
        nameInfo.pObjectName = name.c_str();
        nameInfo.objectHandle = (uint64_t)handle;
        nameInfo.pNext = VK_NULL_HANDLE;

        VK_CHECK_RESULT(fpSetDebugUtilsObjectNameEXT(device, &nameInfo));
    }
}

#endif