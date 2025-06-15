#include "orgpch.h"
#include "Vulkan.h"

#include "VulkanContext.h"
#include "VulkanDiagnostics.h"

namespace Orange::Utils
{
    void VulkanLoadDebugUtilsExtensions(VkInstance instance)
    {
        // 加载设置对象名称的扩展函数
        fpSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)(vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));
        if (fpSetDebugUtilsObjectNameEXT == nullptr)
            // 如果扩展不可用，提供空实现避免崩溃
            fpSetDebugUtilsObjectNameEXT = [](VkDevice device, const VkDebugUtilsObjectNameInfoEXT *pNameInfo)
            { return VK_SUCCESS; };

        // 加载开始调试标签的扩展函数
        fpCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)(vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT"));
        if (fpCmdBeginDebugUtilsLabelEXT == nullptr)
            // 如果扩展不可用，提供空实现
            fpCmdBeginDebugUtilsLabelEXT = [](VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT *pLabelInfo) {};

        // 加载结束调试标签的扩展函数
        fpCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)(vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT"));
        if (fpCmdEndDebugUtilsLabelEXT == nullptr)
            // 如果扩展不可用，提供空实现
            fpCmdEndDebugUtilsLabelEXT = [](VkCommandBuffer commandBuffer) {};

        // 加载插入调试标签的扩展函数
        fpCmdInsertDebugUtilsLabelEXT = (PFN_vkCmdInsertDebugUtilsLabelEXT)(vkGetInstanceProcAddr(instance, "vkCmdInsertDebugUtilsLabelEXT"));
        if (fpCmdInsertDebugUtilsLabelEXT == nullptr)
            // 如果扩展不可用，提供空实现
            fpCmdInsertDebugUtilsLabelEXT = [](VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT *pLabelInfo) {};
    }

    /**
     * @brief 将管线阶段标志转换为字符串
     * @param stage 管线阶段标志
     * @return 对应的字符串描述
     * @details 用于调试输出时显示可读的管线阶段信息
     */
    static const char *StageToString(VkPipelineStageFlagBits stage)
    {
        switch (stage)
        {
        case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:
            return "VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT";
        case VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:
            return "VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT";
        }
        ORG_CORE_ASSERT(false);
        return nullptr;
    }

    void RetrieveDiagnosticCheckpoints()
    {
        // 检查是否支持NVIDIA设备诊断检查点扩展
        bool supported = VulkanContext::GetCurrentDevice()->GetPhysicalDevice()->IsExtensionSupported(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
        if (!supported)
            return;

        // 获取图形队列的检查点数据
        {
            const uint32_t checkpointCount = 4;
            VkCheckpointDataNV data[checkpointCount];
            for (uint32_t i = 0; i < checkpointCount; i++)
                data[i].sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;

            uint32_t retrievedCount = checkpointCount;
            vkGetQueueCheckpointDataNV(::Orange::VulkanContext::GetCurrentDevice()->GetGraphicsQueue(), &retrievedCount, data);
            ORG_CORE_ERROR("RetrieveDiagnosticCheckpoints (Graphics Queue):");
            for (uint32_t i = 0; i < retrievedCount; i++)
            {
                VulkanCheckpointData *checkpoint = (VulkanCheckpointData *)data[i].pCheckpointMarker;
                ORG_CORE_ERROR("Checkpoint: {0} (stage: {1})", checkpoint->Data, StageToString(data[i].stage));
            }
        }

        // 获取计算队列的检查点数据
        {
            const uint32_t checkpointCount = 4;
            VkCheckpointDataNV data[checkpointCount];
            for (uint32_t i = 0; i < checkpointCount; i++)
                data[i].sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;

            uint32_t retrievedCount = checkpointCount;
            vkGetQueueCheckpointDataNV(::Orange::VulkanContext::GetCurrentDevice()->GetComputeQueue(), &retrievedCount, data);
            ORG_CORE_ERROR("RetrieveDiagnosticCheckpoints (Compute Queue):");
            for (uint32_t i = 0; i < retrievedCount; i++)
            {
                VulkanCheckpointData *checkpoint = (VulkanCheckpointData *)data[i].pCheckpointMarker;
                ORG_CORE_ERROR("Checkpoint: {0} (stage: {1})", checkpoint->Data, StageToString(data[i].stage));
            }
        }
    }
}