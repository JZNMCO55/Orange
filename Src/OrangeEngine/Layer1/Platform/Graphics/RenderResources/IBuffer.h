/**
 * @file IBuffer.h
 * @brief 缓冲区接口定义
 */

#ifndef ORANGE_IBUFFER_H
#define ORANGE_IBUFFER_H

#include "../RenderCommon/RenderCommon.h"

namespace Orange
{
    namespace Graphics
    {

        // 前向声明
        class IRenderDevice;

        /**
         * @brief 缓冲区接口
         *
         * 缓冲区是GPU内存的线性块，用于存储顶点、索引、统一数据等。
         */
        class IBuffer
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IBuffer() = default;

            /**
             * @brief 获取缓冲区大小
             * @return 缓冲区大小（字节）
             */
            virtual uint64_t GetSize() const = 0;

            /**
             * @brief 获取缓冲区类型
             * @return 缓冲区类型
             */
            virtual BufferType GetType() const = 0;

            /**
             * @brief 获取缓冲区用途
             * @return 缓冲区用途标志
             */
            virtual BufferUsageFlags GetUsage() const = 0;

            /**
             * @brief 获取是否为主机可见
             * @return 是否为主机可见
             */
            virtual bool IsHostVisible() const = 0;

            /**
             * @brief 映射缓冲区内存
             * @param offset 偏移量
             * @param size 大小，0表示映射整个缓冲区
             * @return 映射的内存指针，失败返回nullptr
             */
            virtual void *Map(uint64_t offset = 0, uint64_t size = 0) = 0;

            /**
             * @brief 解除缓冲区内存映射
             */
            virtual void Unmap() = 0;

            /**
             * @brief 更新缓冲区数据
             * @param data 源数据指针
             * @param size 数据大小
             * @param offset 目标偏移量
             * @return 是否成功更新
             */
            virtual bool Update(const void *data, uint64_t size, uint64_t offset = 0) = 0;

            /**
             * @brief 刷新映射内存（对于某些平台需要）
             * @param offset 偏移量
             * @param size 大小，0表示整个缓冲区
             */
            virtual void FlushMappedMemory(uint64_t offset = 0, uint64_t size = 0) = 0;

            /**
             * @brief 使映射内存失效（对于某些平台需要）
             * @param offset 偏移量
             * @param size 大小，0表示整个缓冲区
             */
            virtual void InvalidateMappedMemory(uint64_t offset = 0, uint64_t size = 0) = 0;

            /**
             * @brief 获取当前缓冲区的内存状态
             * @return 资源状态
             */
            virtual ResourceState GetState() const = 0;

            /**
             * @brief 转换缓冲区状态
             * @param newState 新状态
             * @param immediate 是否立即执行转换
             * @return 是否成功转换
             */
            virtual bool TransitionState(ResourceState newState, bool immediate = true) = 0;

            /**
             * @brief 获取设备地址（仅在支持的平台上）
             * @return 设备地址，不支持则返回0
             */
            virtual uint64_t GetDeviceAddress() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生缓冲区句柄
             * @return 原生缓冲区句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeBuffer() const = 0;

            /**
             * @brief 获取绑定信息
             * @return 资源绑定描述
             */
            virtual ResourceBindingDesc GetBindingDesc() const = 0;

            /**
             * @brief 获取是否支持间接绘制
             * @return 是否支持间接绘制
             */
            virtual bool SupportsIndirectDrawing() const = 0;

            /**
             * @brief 创建缓冲区视图
             * @param offset 视图偏移量
             * @param range 视图范围
             * @return 缓冲区视图描述
             */
            virtual BufferViewDesc CreateView(uint64_t offset, uint64_t range) const = 0;
        };

        /**
         * @brief 创建缓冲区描述
         */
        struct BufferCreateDesc
        {
            uint64_t size = 0;                                                     ///< 缓冲区大小（字节）
            BufferType type = BufferType::Vertex;                                  ///< 缓冲区类型
            BufferUsageFlags usage = BufferUsageFlagBits::TransferDst;             ///< 缓冲区用途
            MemoryPropertyFlags memoryFlags = MemoryPropertyFlagBits::DeviceLocal; ///< 内存属性
            SharingMode sharingMode = SharingMode::Exclusive;                      ///< 共享模式
            const char *debugName = nullptr;                                       ///< 调试名称

            /**
             * @brief 创建顶点缓冲区描述
             * @param bufferSize 缓冲区大小
             * @param hostVisible 是否主机可见
             * @return 缓冲区创建描述
             */
            static BufferCreateDesc Vertex(uint64_t bufferSize, bool hostVisible = false)
            {
                BufferCreateDesc desc;
                desc.size = bufferSize;
                desc.type = BufferType::Vertex;
                desc.usage = BufferUsageFlagBits::VertexBuffer | BufferUsageFlagBits::TransferDst;
                desc.memoryFlags = hostVisible ? (MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent) : MemoryPropertyFlagBits::DeviceLocal;
                return desc;
            }

            /**
             * @brief 创建索引缓冲区描述
             * @param bufferSize 缓冲区大小
             * @param hostVisible 是否主机可见
             * @return 缓冲区创建描述
             */
            static BufferCreateDesc Index(uint64_t bufferSize, bool hostVisible = false)
            {
                BufferCreateDesc desc;
                desc.size = bufferSize;
                desc.type = BufferType::Index;
                desc.usage = BufferUsageFlagBits::IndexBuffer | BufferUsageFlagBits::TransferDst;
                desc.memoryFlags = hostVisible ? (MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent) : MemoryPropertyFlagBits::DeviceLocal;
                return desc;
            }

            /**
             * @brief 创建统一缓冲区描述
             * @param bufferSize 缓冲区大小
             * @param dynamic 是否为动态缓冲区
             * @return 缓冲区创建描述
             */
            static BufferCreateDesc Uniform(uint64_t bufferSize, bool dynamic = true)
            {
                BufferCreateDesc desc;
                desc.size = bufferSize;
                desc.type = BufferType::Uniform;
                desc.usage = BufferUsageFlagBits::UniformBuffer;
                desc.memoryFlags = dynamic ? (MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent) : (MemoryPropertyFlagBits::DeviceLocal | BufferUsageFlagBits::TransferDst);
                return desc;
            }

            /**
             * @brief 创建存储缓冲区描述
             * @param bufferSize 缓冲区大小
             * @param hostVisible 是否主机可见
             * @return 缓冲区创建描述
             */
            static BufferCreateDesc Storage(uint64_t bufferSize, bool hostVisible = false)
            {
                BufferCreateDesc desc;
                desc.size = bufferSize;
                desc.type = BufferType::Storage;
                desc.usage = BufferUsageFlagBits::StorageBuffer | BufferUsageFlagBits::TransferDst;
                desc.memoryFlags = hostVisible ? (MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent) : MemoryPropertyFlagBits::DeviceLocal;
                return desc;
            }

            /**
             * @brief 创建间接绘制缓冲区描述
             * @param bufferSize 缓冲区大小
             * @param hostVisible 是否主机可见
             * @return 缓冲区创建描述
             */
            static BufferCreateDesc Indirect(uint64_t bufferSize, bool hostVisible = false)
            {
                BufferCreateDesc desc;
                desc.size = bufferSize;
                desc.type = BufferType::Indirect;
                desc.usage = BufferUsageFlagBits::IndirectBuffer | BufferUsageFlagBits::TransferDst;
                desc.memoryFlags = hostVisible ? (MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent) : MemoryPropertyFlagBits::DeviceLocal;
                return desc;
            }

            /**
             * @brief 创建暂存缓冲区描述
             * @param bufferSize 缓冲区大小
             * @return 缓冲区创建描述
             */
            static BufferCreateDesc Staging(uint64_t bufferSize)
            {
                BufferCreateDesc desc;
                desc.size = bufferSize;
                desc.type = BufferType::Staging;
                desc.usage = BufferUsageFlagBits::TransferSrc;
                desc.memoryFlags = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
                return desc;
            }
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IBUFFER_H