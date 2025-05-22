/**
 * @file IMemoryAllocator.h
 * @brief 内存分配器接口定义
 */

#ifndef ORANGE_IMEMORY_ALLOCATOR_H
#define ORANGE_IMEMORY_ALLOCATOR_H

#include "MemoryCommon.h"

namespace Orange
{
    namespace Graphics
    {
        // 前向声明
        class IRenderDevice;

        /**
         * @brief 内存分配接口
         *
         * 表示一块已分配的GPU内存
         */
        class IMemoryAllocation
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IMemoryAllocation() = default;

            /**
             * @brief 获取分配大小
             * @return 分配大小（字节）
             */
            virtual uint64_t GetSize() const = 0;

            /**
             * @brief 获取对齐大小
             * @return 对齐大小（字节）
             */
            virtual uint64_t GetAlignment() const = 0;

            /**
             * @brief 获取内存类型
             * @return 内存类型标志
             */
            virtual MemoryTypeFlags GetMemoryType() const = 0;

            /**
             * @brief 获取内存类型索引
             * @return 内存类型索引
             */
            virtual uint32_t GetMemoryTypeIndex() const = 0;

            /**
             * @brief 检查是否已映射
             * @return 是否已映射
             */
            virtual bool IsMapped() const = 0;

            /**
             * @brief 映射内存
             * @param mapFlags 映射标志
             * @return 映射的内存指针，失败返回nullptr
             */
            virtual void *Map(MemoryMapFlags mapFlags = MemoryMapFlagBits::Read | MemoryMapFlagBits::Write) = 0;

            /**
             * @brief 解除内存映射
             */
            virtual void Unmap() = 0;

            /**
             * @brief 刷新映射内存范围
             * @param offset 偏移量
             * @param size 大小，0表示整个分配
             */
            virtual void FlushMappedRange(uint64_t offset = 0, uint64_t size = 0) = 0;

            /**
             * @brief 使映射内存范围失效
             * @param offset 偏移量
             * @param size 大小，0表示整个分配
             */
            virtual void InvalidateMappedRange(uint64_t offset = 0, uint64_t size = 0) = 0;

            /**
             * @brief 获取设备地址（如果支持）
             * @return 设备地址，不支持则返回0
             */
            virtual uint64_t GetDeviceAddress() const = 0;

            /**
             * @brief 获取原生内存句柄
             * @return 原生内存句柄
             */
            virtual void *GetNativeMemory() const = 0;

            /**
             * @brief 获取所属分配器
             * @return 内存分配器
             */
            virtual class IMemoryAllocator *GetAllocator() const = 0;

            /**
             * @brief 设置名称
             * @param name 名称
             */
            virtual void SetName(const char *name) = 0;

            /**
             * @brief 获取名称
             * @return 名称
             */
            virtual const char *GetName() const = 0;

            /**
             * @brief 设置优先级
             * @param priority 内存优先级
             */
            virtual void SetPriority(MemoryPriority priority) = 0;
        };

        /**
         * @brief 内存分配器接口
         *
         * 负责GPU内存的分配和管理
         */
        class IMemoryAllocator
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IMemoryAllocator() = default;

            /**
             * @brief 分配内存
             * @param allocationInfo 分配信息
             * @return 内存分配对象，失败返回nullptr
             */
            virtual IMemoryAllocation *Allocate(const MemoryAllocationInfo &allocationInfo) = 0;

            /**
             * @brief 释放内存
             * @param allocation 要释放的内存分配
             */
            virtual void Free(IMemoryAllocation *allocation) = 0;

            /**
             * @brief 获取支持的内存类型掩码
             * @param memoryTypeFlags 内存类型标志
             * @return 内存类型掩码
             */
            virtual uint32_t GetMemoryTypeMask(MemoryTypeFlags memoryTypeFlags) const = 0;

            /**
             * @brief 获取内存类型属性
             * @param memoryTypeIndex 内存类型索引
             * @return 内存类型标志
             */
            virtual MemoryTypeFlags GetMemoryTypeProperties(uint32_t memoryTypeIndex) const = 0;

            /**
             * @brief 查找最佳内存类型
             * @param memoryTypeMask 内存类型掩码
             * @param memoryTypeFlags 内存类型标志
             * @param requireExactFlags 是否需要精确匹配标志
             * @return 内存类型索引，失败返回UINT32_MAX
             */
            virtual uint32_t FindMemoryType(uint32_t memoryTypeMask, MemoryTypeFlags memoryTypeFlags, bool requireExactFlags = false) const = 0;

            /**
             * @brief 获取内存预算信息
             * @param budget 输出预算信息
             */
            virtual void GetMemoryBudget(MemoryBudget &budget) const = 0;

            /**
             * @brief 获取统计信息
             * @param stats 输出统计信息
             */
            virtual void GetStats(MemoryStats &stats) const = 0;

            /**
             * @brief 触发全局垃圾回收
             */
            virtual void TriggerGarbageCollection() = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 设置内存溢出处理回调
             * @param callback 回调函数
             * @param userData 用户数据
             */
            virtual void SetOutOfMemoryCallback(void (*callback)(void *userData), void *userData) = 0;

            /**
             * @brief 计算资源的内存要求
             * @param renderResourceDesc 渲染资源描述（IBuffer或ITexture的创建描述）
             * @param memReqs 输出内存要求
             */
            virtual void CalculateResourceMemoryRequirements(const void *renderResourceDesc, MemoryRequirements &memReqs) const = 0;
        };

        /**
         * @brief 内存分配器创建信息
         */
        struct MemoryAllocatorCreateInfo
        {
            bool preferIntegratedGpu = false;    ///< 是否优先使用集成GPU
            bool useCustomHeaps = false;         ///< 是否使用自定义堆
            bool allowMappingOverlap = false;    ///< 是否允许映射重叠
            bool useDedicatedAllocations = true; ///< 是否使用专用分配
            uint64_t defaultBudget = 0;          ///< 默认预算（0表示使用系统默认值）
            const char *debugName = nullptr;     ///< 调试名称
        };

        /**
         * @brief 内存分配器工厂接口
         */
        class IMemoryAllocatorFactory
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IMemoryAllocatorFactory() = default;

            /**
             * @brief 创建内存分配器
             * @param device 渲染设备
             * @param createInfo 创建信息
             * @return 内存分配器，失败返回nullptr
             */
            virtual IMemoryAllocator *CreateAllocator(IRenderDevice *device, const MemoryAllocatorCreateInfo &createInfo) = 0;

            /**
             * @brief 销毁内存分配器
             * @param allocator 要销毁的分配器
             */
            virtual void DestroyAllocator(IMemoryAllocator *allocator) = 0;
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IMEMORY_ALLOCATOR_H