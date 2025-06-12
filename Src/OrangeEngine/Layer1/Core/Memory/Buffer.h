#ifndef ORANGE_BUFFER_H
#define ORANGE_BUFFER_H

#include "orgpch.h"

#include "../Base/Assert.h"
#include "../Memory/Memory.h"
/**
 * @file Buffer.h
 * @brief Orange 引擎通用缓冲区管理系统
 * @details 提供内存缓冲区的分配、管理和操作功能，支持多种数据类型
 * @author Orange Engine Team
 * 
 * 依赖项:
 * - Base/Assert.h: 断言宏定义
 * - <array>: STL 数组容器支持
 */

namespace Orange 
{

    /**
     * @brief 通用内存缓冲区类
     * @details 提供内存缓冲区的分配、管理和访问功能，支持多种数据源
     * @note 这是一个基础缓冲区类，不自动管理内存生命周期
     */
    struct Buffer
    {
        void* Data = nullptr;   ///< 缓冲区数据指针
        uint64_t Size = 0;      ///< 缓冲区大小（字节）

        /**
         * @brief 默认构造函数
         * @details 创建空缓冲区，Data 为 nullptr，Size 为 0
         */
        Buffer() = default;

        /**
         * @brief 从指针和大小构造缓冲区
         * @param data 数据指针
         * @param size 数据大小（字节），默认为 0
         * @note 不复制数据，只保存指针引用
         */
        Buffer(const void* data, uint64_t size = 0)
            : Data((void*)data), Size(size) { }

        /**
         * @brief 从 std::array 构造缓冲区
         * @tparam T 数组元素类型
         * @tparam S 数组大小
         * @param array 源数组引用
         * @details 自动计算缓冲区大小为数组元素数量乘以元素大小
         */
        template<typename T, size_t S>
        Buffer(const std::array<T, S>& array)
            : Data(array.data()), Size(array.size() * sizeof(T)) { }

        /**
         * @brief 从 std::vector 构造缓冲区
         * @tparam T 向量元素类型
         * @param vector 源向量引用
         * @details 自动计算缓冲区大小为向量元素数量乘以元素大小
         */
        template<typename T>
        Buffer(const std::vector<T>& vector)
            : Data(vector.data()), Size(vector.size() * sizeof(T))
        {
        }

        /**
         * @brief 复制缓冲区
         * @param other 源缓冲区
         * @return 新分配的缓冲区副本
         * @details 分配新内存并复制源缓冲区的所有数据
         */
        static Buffer Copy(const Buffer& other)
        {
            Buffer buffer;
            buffer.Allocate(other.Size);
            memcpy(buffer.Data, other.Data, other.Size);
            return buffer;
        }

        /**
         * @brief 从指针和大小复制数据创建缓冲区
         * @param data 源数据指针
         * @param size 数据大小（字节）
         * @return 新分配的缓冲区副本
         * @details 分配新内存并复制指定大小的数据
         */
        static Buffer Copy(const void* data, uint64_t size)
        {
            Buffer buffer;
            buffer.Allocate(size);
            if(size) memcpy(buffer.Data, data, size);
            return buffer;
        }

        /**
         * @brief 分配缓冲区内存
         * @param size 要分配的字节数
         * @details 释放现有内存（如果有），然后分配新内存
         * @note 如果 size 为 0，只释放内存不分配新内存
         */
        void Allocate(uint64_t size)
        {
            delete[] (byte*)Data;
            Data = nullptr;
            Size = size;

            if (size == 0)
                return;

            Data = onew byte[size];
        }

        /**
         * @brief 释放缓冲区内存
         * @details 删除分配的内存并重置缓冲区状态
         */
        void Release()
        {
            delete[] (byte*)Data;
            Data = nullptr;
            Size = 0;
        }

        /**
         * @brief 将缓冲区内存清零
         * @details 如果缓冲区有效，将所有字节设置为 0
         */
        void ZeroInitialize()
        {
            if (Data)
                memset(Data, 0, Size);
        }

        /**
         * @brief 从缓冲区读取类型化数据
         * @tparam T 要读取的数据类型
         * @param offset 字节偏移量，默认为 0
         * @return 指定类型数据的引用
         * @warning 不检查边界，调用者需确保偏移量有效
         */
        template<typename T>
        T& Read(uint64_t offset = 0)
        {
            return *(T*)((byte*)Data + offset);
        }

        /**
         * @brief 从缓冲区读取类型化数据（常量版本）
         * @tparam T 要读取的数据类型
         * @param offset 字节偏移量，默认为 0
         * @return 指定类型数据的常量引用
         * @warning 不检查边界，调用者需确保偏移量有效
         */
        template<typename T>
        const T& Read(uint64_t offset = 0) const
        {
            return *(T*)((byte*)Data + offset);
        }

        /**
         * @brief 从缓冲区读取字节数组
         * @param size 要读取的字节数
         * @param offset 起始偏移量
         * @return 新分配的字节数组指针
         * @details 分配新内存并复制指定范围的数据
         * @note 调用者负责释放返回的内存
         * @throw 如果超出缓冲区边界，触发断言
         */
        byte* ReadBytes(uint64_t size, uint64_t offset) const
        {
            ORG_CORE_ASSERT(offset + size <= Size, "Buffer overflow!");
            byte* buffer = onew byte[size];
            memcpy(buffer, (byte*)Data + offset, size);
            return buffer;
        }
                
        /**
         * @brief 向缓冲区写入数据
         * @param data 源数据指针
         * @param size 要写入的字节数
         * @param offset 写入偏移量，默认为 0
         * @details 将源数据复制到缓冲区的指定位置
         * @throw 如果超出缓冲区边界，触发断言
         */
        void Write(const void* data, uint64_t size, uint64_t offset = 0)
        {
            ORG_CORE_ASSERT(offset + size <= Size, "Buffer overflow!");
            memcpy((byte*)Data + offset, data, size);
        }

        /**
         * @brief 布尔转换操作符
         * @return 如果缓冲区有效（Data 不为空）返回 true
         * @details 用于检查缓冲区是否包含有效数据
         */
        operator bool() const
        {
            return (bool)Data;
        }

        /**
         * @brief 数组下标操作符（可修改）
         * @param index 字节索引
         * @return 指定位置字节的引用
         * @warning 不检查边界，调用者需确保索引有效
         */
        byte& operator[](int index)
        {
            return ((byte*)Data)[index];
        }

        /**
         * @brief 数组下标操作符（只读）
         * @param index 字节索引
         * @return 指定位置字节的值
         * @warning 不检查边界，调用者需确保索引有效
         */
        byte operator[](int index) const
        {
            return ((byte*)Data)[index];
        }

        /**
         * @brief 类型转换操作符
         * @tparam T 目标指针类型
         * @return 转换为指定类型的指针
         * @details 将内部数据指针转换为指定类型
         */
        template<typename T>
        T* As() const
        {
            return (T*)Data;
        }

        /**
         * @brief 获取缓冲区大小
         * @return 缓冲区大小（字节）
         */
        inline uint64_t GetSize() const { return Size; }
    };

    /**
     * @brief 安全缓冲区类
     * @details 继承自 Buffer，在析构时自动释放内存，提供 RAII 语义
     * @note 适用于需要自动内存管理的场景
     */
    struct BufferSafe : public Buffer
    {
        /**
         * @brief 析构函数
         * @details 自动释放缓冲区内存，提供 RAII 保证
         */
        ~BufferSafe()
        {
            Release();
        }

        /**
         * @brief 复制数据创建安全缓冲区
         * @param data 源数据指针
         * @param size 数据大小（字节）
         * @return 新的安全缓冲区实例
         * @details 分配新内存并复制源数据，返回自管理缓冲区
         */
        static BufferSafe Copy(const void* data, uint64_t size)
        {
            BufferSafe buffer;
            buffer.Allocate(size);
            memcpy(buffer.Data, data, size);
            return buffer;
        }
    };
}

#endif // ORANGE_BUFFER_H