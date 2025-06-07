#ifndef ORANGE_GRAPHICS_BUFFER_H
#define ORANGE_GRAPHICS_BUFFER_H

#include <cstdint>
#include <cstddef>
#include <string>

namespace Orange
{
    namespace Graphics
    {
        /**
         * @brief 缓冲区接口
         * 提供GPU缓冲区的基本操作功能
         */
        class Buffer
        {
        public:
            virtual ~Buffer() = default;

            /**
             * @brief 映射缓冲区内存到CPU地址空间
             * @return 映射的内存指针，失败返回nullptr
             * @note 只有HostVisible内存属性的缓冲区才能映射
             */
            virtual void *Map() = 0;

            /**
             * @brief 取消内存映射
             */
            virtual void Unmap() = 0;

            /**
             * @brief 复制数据到缓冲区
             * @param data 源数据指针
             * @param size 数据大小（字节）
             * @param offset 目标缓冲区偏移量（字节）
             * @note 如果缓冲区不支持直接写入，将使用暂存缓冲区
             */
            virtual void CopyFrom(const void *data, size_t size, size_t offset = 0) = 0;

            /**
             * @brief 从缓冲区复制数据
             * @param data 目标数据指针
             * @param size 数据大小（字节）
             * @param offset 源缓冲区偏移量（字节）
             */
            virtual void CopyTo(void *data, size_t size, size_t offset = 0) = 0;

            /**
             * @brief 获取缓冲区大小
             * @return 缓冲区大小（字节）
             */
            virtual size_t GetSize() const = 0;

            /**
             * @brief 检查缓冲区是否当前已映射
             * @return 已映射返回true
             */
            virtual bool IsMapped() const = 0;

            /**
             * @brief 获取缓冲区调试名称
             * @return 调试名称字符串
             */
            virtual const std::string &GetDebugName() const = 0;

            /**
             * @brief 刷新映射的内存区域
             * 确保CPU写入的数据对GPU可见
             * @param offset 刷新起始偏移量
             * @param size 刷新大小，0表示刷新整个缓冲区
             */
            virtual void Flush(size_t offset = 0, size_t size = 0) = 0;

            /**
             * @brief 使映射的内存区域无效
             * 确保GPU写入的数据对CPU可见
             * @param offset 无效化起始偏移量
             * @param size 无效化大小，0表示整个缓冲区
             */
            virtual void Invalidate(size_t offset = 0, size_t size = 0) = 0;
        };
    }
}

#endif // ORANGE_GRAPHICS_BUFFER_H