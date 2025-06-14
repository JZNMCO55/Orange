#ifndef GPU_STATS_H
#define GPU_STATS_H

#include <cstdint>

namespace Orange
{
    /**
     * @struct GPUMemoryStats
     * @brief GPU内存统计信息结构体
     *
     * 包含GPU内存使用的详细统计信息，用于监控和分析GPU内存的
     * 分配和使用情况。这些信息对于性能优化和内存管理至关重要。
     *
     * 统计信息分类：
     * - 总体内存使用：已使用内存和总可用内存
     * - 分配统计：分配次数和总分配大小
     * - 资源类型统计：缓冲区和图像的独立统计
     *
     * 使用场景：
     * - 性能分析：识别内存使用热点
     * - 内存优化：监控内存碎片和使用效率
     * - 调试工具：检测内存泄漏和异常分配
     * - 运行时监控：实时显示内存使用情况
     */
    struct GPUMemoryStats
    {
        uint64_t Used = 0;            ///< 已使用的GPU内存大小（字节）
        uint64_t TotalAvailable = 0;  ///< 总可用GPU内存大小（字节）
        uint64_t AllocationCount = 0; ///< 总分配次数

        uint64_t BufferAllocationSize = 0;  ///< 缓冲区分配的总大小（字节）
        uint64_t BufferAllocationCount = 0; ///< 缓冲区分配次数

        uint64_t ImageAllocationSize = 0;  ///< 图像分配的总大小（字节）
        uint64_t ImageAllocationCount = 0; ///< 图像分配次数
    };
}

#endif