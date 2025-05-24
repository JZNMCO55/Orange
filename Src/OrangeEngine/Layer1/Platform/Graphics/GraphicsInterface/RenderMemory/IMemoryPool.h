/**
 * @file IMemoryPool.h
 * @brief 内存池接口定义
 */

#ifndef ORANGE_IMEMORY_POOL_H
#define ORANGE_IMEMORY_POOL_H

#include "MemoryCommon.h"

namespace Orange
{
    namespace Graphics
    {
        // 前向声明
        class IRenderDevice;
        class IMemoryAllocator;
        class IMemoryAllocation;

        /**
         * @brief 内存池接口
         *
         * 内存池是预分配的内存块的集合，用于高效地分配和回收特定类型的资源
         */
        class IMemoryPool
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IMemoryPool() = default;

            /**
             * @brief 获取内存池类型
             * @return 内存池类型
             */
            virtual MemoryPoolType GetType() const {}

            /**
             * @brief 获取块大小
             * @return 块大小（字节）
             */
            virtual uint64_t GetBlockSize() const {}

            /**
             * @brief 获取最大块数量
             * @return 最大块数量
             */
            virtual uint32_t GetMaxBlockCount() const {}

            /**
             * @brief 获取当前块数量
             * @return 当前块数量
             */
            virtual uint32_t GetBlockCount() const {}

            /**
             * @brief 分配内存
             * @param size 大小（字节）
             * @param alignment 对齐要求（字节）
             * @param debugName 调试名称
             * @return 内存分配对象，失败返回nullptr
             */
            virtual IMemoryAllocation *Allocate(uint64_t size, uint64_t alignment = 16, const char *debugName = nullptr) {}

            /**
             * @brief 释放内存
             * @param allocation 要释放的内存分配
             */
            virtual void Free(IMemoryAllocation *allocation) {}

            /**
             * @brief 重置内存池
             *
             * 释放所有分配并重置池状态
             */
            virtual void Reset() {}

            /**
             * @brief 获取内存类型标志
             * @return 内存类型标志
             */
            virtual MemoryTypeFlags GetMemoryTypeFlags() const {}

            /**
             * @brief 获取内存类型索引
             * @return 内存类型索引
             */
            virtual uint32_t GetMemoryTypeIndex() const {}

            /**
             * @brief 是否支持碎片整理
             * @return 是否支持碎片整理
             */
            virtual bool SupportsDefragmentation() const {}

            /**
             * @brief 执行碎片整理
             * @param info 碎片整理信息
             * @param stats 输出碎片整理统计信息
             * @return 是否成功执行
             */
            virtual bool Defragment(const DefragmentationInfo &info, DefragmentationStats &stats) {}

            /**
             * @brief 获取统计信息
             * @param stats 输出统计信息
             */
            virtual void GetStats(MemoryStats &stats) const {}

            /**
             * @brief 获取所属内存分配器
             * @return 内存分配器
             */
            virtual IMemoryAllocator *GetAllocator() const {}

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const {}

            /**
             * @brief 设置名称
             * @param name 名称
             */
            virtual void SetName(const char *name) {}

            /**
             * @brief 获取名称
             * @return 名称
             */
            virtual const char *GetName() const {}
        };

        /**
         * @brief 内存池工厂接口
         */
        class IMemoryPoolFactory
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IMemoryPoolFactory() = default;

            /**
             * @brief 创建内存池
             * @param allocator 内存分配器
             * @param createInfo 创建信息
             * @return 内存池，失败返回nullptr
             */
            virtual IMemoryPool *CreatePool(IMemoryAllocator *allocator, const MemoryPoolCreateInfo &createInfo) {}

            /**
             * @brief 销毁内存池
             * @param pool 要销毁的内存池
             */
            virtual void DestroyPool(IMemoryPool *pool) {}

            /**
             * @brief 创建缓冲区池
             * @param allocator 内存分配器
             * @param blockSize 块大小
             * @param maxBlockCount 最大块数量
             * @param debugName 调试名称
             * @return 内存池，失败返回nullptr
             */
            virtual IMemoryPool *CreateBufferPool(
                IMemoryAllocator *allocator,
                uint64_t blockSize = 16 * 1024 * 1024,
                uint32_t maxBlockCount = 16,
                const char *debugName = "BufferPool") {}

            /**
             * @brief 创建图像池
             * @param allocator 内存分配器
             * @param blockSize 块大小
             * @param maxBlockCount 最大块数量
             * @param debugName 调试名称
             * @return 内存池，失败返回nullptr
             */
            virtual IMemoryPool *CreateImagePool(
                IMemoryAllocator *allocator,
                uint64_t blockSize = 64 * 1024 * 1024,
                uint32_t maxBlockCount = 8,
                const char *debugName = "ImagePool") {}

            /**
             * @brief 创建暂存池
             * @param allocator 内存分配器
             * @param blockSize 块大小
             * @param maxBlockCount 最大块数量
             * @param debugName 调试名称
             * @return 内存池，失败返回nullptr
             */
            virtual IMemoryPool *CreateStagingPool(
                IMemoryAllocator *allocator,
                uint64_t blockSize = 8 * 1024 * 1024,
                uint32_t maxBlockCount = 4,
                const char *debugName = "StagingPool") {}
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IMEMORY_POOL_H