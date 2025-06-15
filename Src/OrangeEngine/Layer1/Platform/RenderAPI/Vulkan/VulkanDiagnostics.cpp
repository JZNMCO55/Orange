/**
 * @file VulkanDiagnostics.cpp
 * @brief Vulkan诊断工具实现文件
 * @details 实现GPU崩溃诊断和检查点功能，用于调试和性能分析
 * @author Hazel Engine
 */

#include "orgpch.h"
#include "VulkanDiagnostics.h"

#include "Platform/RenderAPI/Vulkan/VulkanContext.h"

namespace Orange::Utils
{

    // 静态存储：检查点数据缓冲区，用于存储GPU执行过程中的调试信息
    // 使用环形缓冲区设计，避免内存无限增长
    static std::vector<VulkanCheckpointData> s_CheckpointStorage(1024);

    // 当前检查点存储索引，用于环形缓冲区的索引管理
    static uint32_t s_CheckpointStorageIndex = 0;

    /**
     * @brief 设置Vulkan检查点
     * @param commandBuffer 要设置检查点的命令缓冲区
     * @param data 检查点数据字符串，用于标识当前GPU执行位置
     *
     * @details 该函数在GPU命令流中插入检查点标记，当GPU崩溃时可以通过
     * 这些检查点确定崩溃发生的大致位置。这对于调试复杂的渲染管线非常有用。
     *
     * 工作原理：
     * 1. 首先检查设备是否支持NVIDIA诊断检查点扩展
     * 2. 如果支持，则在环形缓冲区中分配一个新的检查点槽位
     * 3. 将调试字符串复制到检查点数据结构中
     * 4. 调用vkCmdSetCheckpointNV在命令缓冲区中插入检查点
     *
     * 注意事项：
     * - 该功能仅在支持VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME扩展的设备上可用
     * - 检查点数据存储在静态缓冲区中，使用环形缓冲区避免内存泄漏
     * - 检查点字符串长度受VulkanCheckpointData::Data数组大小限制
     */
    void SetVulkanCheckpoint(VkCommandBuffer commandBuffer, const std::string &data)
    {
        // 检查当前设备是否支持NVIDIA诊断检查点扩展
        // 这是一个NVIDIA特有的扩展，用于GPU崩溃诊断
        const bool supported = VulkanContext::GetCurrentDevice()->GetPhysicalDevice()->IsExtensionSupported(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
        if (!supported)
            return;

        // 更新环形缓冲区索引，确保不会超出缓冲区边界
        // 使用模运算实现环形缓冲区：当索引达到1024时重新从0开始
        s_CheckpointStorageIndex = (s_CheckpointStorageIndex + 1) % 1024;

        // 获取当前检查点数据槽位的引用
        VulkanCheckpointData &checkpoint = s_CheckpointStorage[s_CheckpointStorageIndex];

        // 清零检查点数据缓冲区，确保没有残留的旧数据
        memset(checkpoint.Data, 0, sizeof(checkpoint.Data));

        // 将调试字符串复制到检查点数据中
        // 注意：这里使用strcpy可能存在缓冲区溢出风险，在生产环境中应该使用更安全的字符串复制函数
        strcpy(checkpoint.Data, data.data());

        // 在Vulkan命令缓冲区中插入检查点
        // 当GPU执行到这个命令时，会记录检查点信息
        // 如果GPU崩溃，可以通过vkGetQueueCheckpointDataNV获取最后执行的检查点
        vkCmdSetCheckpointNV(commandBuffer, &checkpoint);
    }

} // namespace Orange::Utils
