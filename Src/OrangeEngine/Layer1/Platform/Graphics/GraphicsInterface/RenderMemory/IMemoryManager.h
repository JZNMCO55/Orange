/**
 * @file IMemoryManager.h
 * @brief 内存管理器接口定义
 */

#ifndef ORANGE_IMEMORY_MANAGER_H
#define ORANGE_IMEMORY_MANAGER_H

#include "MemoryCommon.h"

namespace Orange
{
    namespace Graphics
    {
        // 前向声明
        class IRenderDevice;
        class IMemoryAllocator;
        class IMemoryPool;
        class IMemoryAllocation;
        class IBuffer;
        class ITexture;

        /**
         * @brief 资源内存分配器接口
         *
         * 用于将内存分配关联到特定资源类型
         */
        class IResourceMemoryAllocator
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IResourceMemoryAllocator() = default;

            /**
             * @brief 为缓冲区分配内存
             * @param buffer 缓冲区对象
             * @param memoryTypeFlags 内存类型标志，0表示使用默认类型
             * @return 是否成功分配
             */
            virtual bool AllocateMemoryForBuffer(IBuffer *buffer, MemoryTypeFlags memoryTypeFlags = static_cast<MemoryTypeFlags>(MemoryTypeFlagBits::None)) {}

            /**
             * @brief 为纹理分配内存
             * @param texture 纹理对象
             * @param memoryTypeFlags 内存类型标志，0表示使用默认类型
             * @return 是否成功分配
             */
            virtual bool AllocateMemoryForTexture(ITexture *texture, MemoryTypeFlags memoryTypeFlags = static_cast<MemoryTypeFlags>(MemoryTypeFlagBits::None)) {}

            /**
             * @brief 释放资源内存
             * @param resource 资源对象（IBuffer或ITexture）
             */
            virtual void FreeResourceMemory(void *resource) {}

            /**
             * @brief 获取资源内存分配
             * @param resource 资源对象（IBuffer或ITexture）
             * @return 内存分配对象，未分配返回nullptr
             */
            virtual IMemoryAllocation *GetResourceAllocation(void *resource) const {}

            /**
             * @brief 获取缓冲区内存要求
             * @param buffer 缓冲区对象
             * @param memReqs 输出内存要求
             */
            virtual void GetBufferMemoryRequirements(IBuffer *buffer, MemoryRequirements &memReqs) const {}

            /**
             * @brief 获取纹理内存要求
             * @param texture 纹理对象
             * @param memReqs 输出内存要求
             */
            virtual void GetTextureMemoryRequirements(ITexture *texture, MemoryRequirements &memReqs) const {}
        };

        /**
         * @brief 内存管理器接口
         *
         * 顶层内存管理接口，管理内存分配器和内存池
         */
        class IMemoryManager
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IMemoryManager() = default;

            /**
             * @brief 获取主内存分配器
             * @return 内存分配器
             */
            virtual IMemoryAllocator *GetAllocator() const {}

            /**
             * @brief 获取资源内存分配器
             * @return 资源内存分配器
             */
            virtual IResourceMemoryAllocator *GetResourceAllocator() const {}

            /**
             * @brief 获取缓冲区内存池
             * @return 缓冲区内存池
             */
            virtual IMemoryPool *GetBufferPool() const {}

            /**
             * @brief 获取图像内存池
             * @return 图像内存池
             */
            virtual IMemoryPool *GetImagePool() const {}

            /**
             * @brief 获取暂存内存池
             * @return 暂存内存池
             */
            virtual IMemoryPool *GetStagingPool() const {}

            /**
             * @brief 创建内存池
             * @param createInfo 创建信息
             * @return 新创建的内存池，失败返回nullptr
             */
            virtual IMemoryPool *CreatePool(const MemoryPoolCreateInfo &createInfo) {}

            /**
             * @brief 销毁内存池
             * @param pool 要销毁的内存池
             */
            virtual void DestroyPool(IMemoryPool *pool) {}

            /**
             * @brief 分配内存
             * @param allocationInfo 分配信息
             * @return 内存分配对象，失败返回nullptr
             */
            virtual IMemoryAllocation *Allocate(const MemoryAllocationInfo &allocationInfo) {}

            /**
             * @brief 释放内存
             * @param allocation 要释放的内存分配
             */
            virtual void Free(IMemoryAllocation *allocation) {}

            /**
             * @brief 获取内存统计信息
             * @param stats 输出统计信息
             */
            virtual void GetStats(MemoryStats &stats) const {}

            /**
             * @brief 获取内存预算信息
             * @param budget 输出预算信息
             */
            virtual void GetBudget(MemoryBudget &budget) const {}

            /**
             * @brief 执行全局碎片整理
             * @param info 碎片整理信息
             * @param stats 输出碎片整理统计信息
             * @return 是否成功执行
             */
            virtual bool Defragment(const DefragmentationInfo &info, DefragmentationStats &stats) {}

            /**
             * @brief 触发垃圾回收
             */
            virtual void TriggerGarbageCollection() {}

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
         * @brief 内存管理器创建信息
         */
        struct MemoryManagerCreateInfo
        {
            MemoryAllocatorCreateInfo allocatorInfo;    ///< 分配器创建信息
            bool createDefaultPools = true;             ///< 是否创建默认池
            uint64_t bufferPoolSize = 16 * 1024 * 1024; ///< 缓冲区池大小
            uint32_t bufferPoolMaxBlocks = 16;          ///< 缓冲区池最大块数
            uint64_t imagePoolSize = 64 * 1024 * 1024;  ///< 图像池大小
            uint32_t imagePoolMaxBlocks = 8;            ///< 图像池最大块数
            uint64_t stagingPoolSize = 8 * 1024 * 1024; ///< 暂存池大小
            uint32_t stagingPoolMaxBlocks = 4;          ///< 暂存池最大块数
            const char *debugName = nullptr;            ///< 调试名称
        };

        /**
         * @brief 内存管理器工厂接口
         */
        class IMemoryManagerFactory
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IMemoryManagerFactory() = default;

            /**
             * @brief 创建内存管理器
             * @param device 渲染设备
             * @param createInfo 创建信息
             * @return 内存管理器，失败返回nullptr
             */
            virtual IMemoryManager *CreateMemoryManager(IRenderDevice *device, const MemoryManagerCreateInfo &createInfo) {}

            /**
             * @brief 销毁内存管理器
             * @param manager 要销毁的内存管理器
             */
            virtual void DestroyMemoryManager(IMemoryManager *manager) {}
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IMEMORY_MANAGER_H