/**
 * @file RenderMemory.h
 * @brief 渲染内存管理模块主头文件
 */

#ifndef ORANGE_RENDER_MEMORY_H
#define ORANGE_RENDER_MEMORY_H

#include "MemoryCommon.h"
#include "IMemoryAllocator.h"
#include "IMemoryPool.h"
#include "IMemoryManager.h"

namespace Orange
{
    namespace Graphics
    {
        // 前向声明
        class IRenderDevice;

        /**
         * @brief 初始化内存管理子系统
         * @param device 渲染设备
         * @return 是否成功初始化
         */
        bool InitializeMemorySystem(IRenderDevice *device);

        /**
         * @brief 关闭内存管理子系统
         * @param device 渲染设备
         */
        void ShutdownMemorySystem(IRenderDevice *device);

        /**
         * @brief 创建内存分配器
         * @param device 渲染设备
         * @return 内存分配器
         */
        IMemoryAllocator *CreateMemoryAllocator(IRenderDevice *device);

        /**
         * @brief 创建内存池
         * @param device 渲染设备
         * @param createInfo 创建信息
         * @return 内存池
         */
        IMemoryPool *CreateMemoryPool(IRenderDevice *device, const MemoryPoolCreateInfo &createInfo);

        /**
         * @brief 创建内存管理器
         * @param device 渲染设备
         * @return 内存管理器
         */
        IMemoryManager *CreateMemoryManager(IRenderDevice *device);

        /**
         * @brief 销毁内存管理器
         * @param manager 内存管理器
         */
        void DestroyMemoryManager(IMemoryManager *manager);

        /**
         * @brief 获取内存系统工厂单例
         * @return 内存管理器工厂
         */
        IMemoryManagerFactory *GetMemoryManagerFactory();

        /**
         * @brief 创建内存分配器工厂
         * @return 内存分配器工厂
         */
        IMemoryAllocatorFactory *CreateMemoryAllocatorFactory();

        /**
         * @brief 创建内存池工厂
         * @return 内存池工厂
         */
        IMemoryPoolFactory *CreateMemoryPoolFactory();

        /**
         * @brief 获取内存类型名称
         * @param memoryTypeFlags 内存类型标志
         * @return 内存类型名称
         */
        const char *GetMemoryTypeName(MemoryTypeFlags memoryTypeFlags);

        /**
         * @brief 获取内存池类型名称
         * @param poolType 内存池类型
         * @return 内存池类型名称
         */
        const char *GetMemoryPoolTypeName(MemoryPoolType poolType);

        /**
         * @brief 获取内存优先级名称
         * @param priority 内存优先级
         * @return 内存优先级名称
         */
        const char *GetMemoryPriorityName(MemoryPriority priority);

        /**
         * @brief 获取内存系统版本信息
         * @param major 主版本号
         * @param minor 次版本号
         * @param patch 补丁版本号
         */
        void GetMemorySystemVersion(uint32_t &major, uint32_t &minor, uint32_t &patch);

        /**
         * @brief 创建内存分配信息辅助函数
         * @param size 分配大小
         * @param alignment 对齐要求
         * @param memoryTypeFlags 内存类型标志
         * @param debugName 调试名称
         * @return 内存分配信息
         */
        inline MemoryAllocationInfo CreateMemoryAllocationInfo(
            uint64_t size,
            uint64_t alignment = 16,
            MemoryTypeFlags memoryTypeFlags = static_cast<MemoryTypeFlags>(MemoryTypeFlagBits::Device),
            const char *debugName = nullptr)
        {
            MemoryAllocationInfo info;
            info.size = size;
            info.alignment = alignment;
            info.memoryTypeFlags = memoryTypeFlags;
            info.debugName = debugName;
            return info;
        }

        /**
         * @brief 创建内存要求辅助函数
         * @param size 大小
         * @param alignment 对齐要求
         * @param memoryTypeMask 内存类型掩码
         * @param dedicatedAllocation 是否需要专用分配
         * @return 内存要求
         */
        inline MemoryRequirements CreateMemoryRequirements(
            uint64_t size,
            uint64_t alignment = 16,
            uint32_t memoryTypeMask = 0xFFFFFFFF,
            bool dedicatedAllocation = false)
        {
            MemoryRequirements reqs;
            reqs.size = size;
            reqs.alignment = alignment;
            reqs.memoryTypeMask = memoryTypeMask;
            reqs.dedicatedAllocation = dedicatedAllocation;
            return reqs;
        }

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_RENDER_MEMORY_H