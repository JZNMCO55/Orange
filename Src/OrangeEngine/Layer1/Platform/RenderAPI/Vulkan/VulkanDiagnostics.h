#ifndef VULKAN_DIAGNOSTICS_H
#define VULKAN_DIAGNOSTICS_H

#include "Vulkan.h"

namespace Orange::Utils
{

    /**
     * @struct VulkanCheckpointData
     * @brief Vulkan检查点数据结构
     * @details 用于存储GPU诊断检查点的数据信息
     */
    struct VulkanCheckpointData
    {
        char Data[64 + 1]{}; ///< 检查点数据，最大64字符加终止符
    };

    /**
     * @brief 设置Vulkan检查点
     * @param commandBuffer 命令缓冲区句柄
     * @param data 检查点数据字符串
     * @details 在命令缓冲区中插入检查点，用于GPU崩溃时的诊断
     *
     * 当GPU设备丢失时，可以通过检查点数据确定GPU执行到哪个位置，
     * 有助于定位导致崩溃的具体渲染命令。
     */
    void SetVulkanCheckpoint(VkCommandBuffer commandBuffer, const std::string &data);

} // namespace Orange::Utils

#endif // VULKAN_DIAGNOSTICS_H
