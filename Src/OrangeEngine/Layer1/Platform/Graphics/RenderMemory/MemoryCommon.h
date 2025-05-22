/**
 * @file MemoryCommon.h
 * @brief 渲染内存通用定义
 */

#ifndef ORANGE_MEMORY_COMMON_H
#define ORANGE_MEMORY_COMMON_H

#include "../RenderCommon/RenderCommon.h"
#include <string>

namespace Orange
{
    namespace Graphics
    {
        /**
         * @brief 内存类型标志位
         */
        enum class MemoryTypeFlagBits : uint32_t
        {
            None = 0,
            Device = 0x01,          ///< 设备内存，GPU可访问
            Host = 0x02,            ///< 主机内存，CPU可访问
            HostCached = 0x04,      ///< 主机缓存内存，带缓存
            HostUncached = 0x08,    ///< 主机非缓存内存，无缓存
            Shared = 0x10,          ///< 共享内存，可被多个设备访问
            Dedicated = 0x20,       ///< 专用内存，为特定资源分配
            Protected = 0x40,       ///< 受保护内存，内容受保护
            Mappable = 0x80,        ///< 可映射内存，可被CPU映射
            Coherent = 0x100,       ///< 一致性内存，CPU/GPU数据自动同步
            NonCoherent = 0x200,    ///< 非一致性内存，需手动同步
            LazilyAllocated = 0x400 ///< 延迟分配内存，仅在需要时分配
        };
        // 定义类型别名
        using MemoryTypeFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(MemoryTypeFlagBits, MemoryTypeFlags);

        /**
         * @brief 内存分配标志位
         */
        enum class AllocationFlagBits : uint32_t
        {
            None = 0,
            Mapped = 0x01,                   ///< 分配时映射
            UpperAddress = 0x02,             ///< 优先使用较高地址
            DontBind = 0x04,                 ///< 不立即绑定
            External = 0x08,                 ///< 允许外部内存
            Strategy_MinMemory = 0x10,       ///< 优先节省内存
            Strategy_MinTime = 0x20,         ///< 优先节省时间
            Strategy_MinFragmentation = 0x40 ///< 优先减少碎片
        };
        // 定义类型别名
        using AllocationFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(AllocationFlagBits, AllocationFlags);

        /**
         * @brief 内存分配信息
         */
        struct MemoryAllocationInfo
        {
            uint64_t size = 0;                                                                          ///< 分配大小（字节）
            uint64_t alignment = 16;                                                                    ///< 对齐要求（字节）
            MemoryTypeFlags memoryTypeFlags = static_cast<MemoryTypeFlags>(MemoryTypeFlagBits::Device); ///< 内存类型标志
            AllocationFlags allocationFlags = static_cast<AllocationFlags>(AllocationFlagBits::None);   ///< 分配标志
            uint32_t memoryTypeIndex = UINT32_MAX;                                                      ///< 内存类型索引（如果已知）
            const char *debugName = nullptr;                                                            ///< 调试名称
        };

        /**
         * @brief 内存分配统计信息
         */
        struct MemoryStats
        {
            uint64_t totalBytesAllocated = 0;  ///< 总分配字节数
            uint64_t totalBytesFree = 0;       ///< 总空闲字节数
            uint64_t largestFreeBlockSize = 0; ///< 最大空闲块大小
            uint32_t allocationCount = 0;      ///< 分配计数
            uint32_t freeCount = 0;            ///< 释放计数
            float fragmentationRatio = 0.0f;   ///< 碎片率
        };

        /**
         * @brief 内存映射标志位
         */
        enum class MemoryMapFlagBits : uint32_t
        {
            None = 0,
            Read = 0x01,       ///< 读访问
            Write = 0x02,      ///< 写访问
            Persistent = 0x04, ///< 持久映射
            Coherent = 0x08    ///< 一致性映射
        };
        // 定义类型别名
        using MemoryMapFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(MemoryMapFlagBits, MemoryMapFlags);

        /**
         * @brief 内存池类型
         */
        enum class MemoryPoolType
        {
            Unknown,  ///< 未知
            General,  ///< 通用
            Buffer,   ///< 缓冲区
            Image,    ///< 图像
            Staging,  ///< 暂存
            Readback, ///< 回读
            Upload    ///< 上传
        };

        /**
         * @brief 内存池创建信息
         */
        struct MemoryPoolCreateInfo
        {
            MemoryPoolType type = MemoryPoolType::General;                                              ///< 内存池类型
            uint64_t blockSize = 16 * 1024 * 1024;                                                      ///< 块大小，通常为16MB
            uint32_t maxBlockCount = 64;                                                                ///< 最大块数量
            MemoryTypeFlags memoryTypeFlags = static_cast<MemoryTypeFlags>(MemoryTypeFlagBits::Device); ///< 内存类型标志
            bool allowDefragmentation = false;                                                          ///< 是否允许碎片整理
            const char *debugName = nullptr;                                                            ///< 调试名称
        };

        /**
         * @brief 内存要求
         */
        struct MemoryRequirements
        {
            uint64_t size = 0;                    ///< 大小（字节）
            uint64_t alignment = 16;              ///< 对齐要求（字节）
            uint32_t memoryTypeMask = 0xFFFFFFFF; ///< 内存类型掩码
            bool dedicatedAllocation = false;     ///< 是否需要专用分配
        };

        /**
         * @brief 内存优先级
         */
        enum class MemoryPriority
        {
            Low,     ///< 低优先级
            Normal,  ///< 普通优先级
            High,    ///< 高优先级
            Realtime ///< 实时优先级
        };

        /**
         * @brief 碎片整理标志位
         */
        enum class DefragmentationFlagBits : uint32_t
        {
            None = 0,
            MoveAllAllocations = 0x01, ///< 移动所有分配
            PreventBinds = 0x02,       ///< 防止绑定
            FastAlgorithm = 0x04       ///< 快速算法
        };
        // 定义类型别名
        using DefragmentationFlags = uint32_t;
        DEFINE_ENUM_FLAG_OPERATORS(DefragmentationFlagBits, DefragmentationFlags);

        /**
         * @brief 碎片整理信息
         */
        struct DefragmentationInfo
        {
            DefragmentationFlags flags = static_cast<DefragmentationFlags>(DefragmentationFlagBits::None); ///< 碎片整理标志
            uint64_t maxBytesPerPass = 0;                                                                  ///< 每次通过最大字节数（0表示无限制）
            uint32_t maxAllocationsPerPass = 0;                                                            ///< 每次通过最大分配数（0表示无限制）
        };

        /**
         * @brief 碎片整理统计信息
         */
        struct DefragmentationStats
        {
            uint32_t movesCount = 0;          ///< 移动次数
            uint64_t bytesMoved = 0;          ///< 移动字节数
            uint32_t allocationsMoved = 0;    ///< 移动分配数
            float fragmentationBefore = 0.0f; ///< 整理前碎片率
            float fragmentationAfter = 0.0f;  ///< 整理后碎片率
        };

        /**
         * @brief 内存预算信息
         */
        struct MemoryBudget
        {
            uint64_t totalMemory = 0;     ///< 总内存大小（字节）
            uint64_t availableMemory = 0; ///< 可用内存大小（字节）
            uint64_t usedMemory = 0;      ///< 已用内存大小（字节）
            float usagePercentage = 0.0f; ///< 使用百分比
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_MEMORY_COMMON_H