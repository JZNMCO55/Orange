#ifndef ORANGE_MEMORY_ALLOCATOR_H
#define ORANGE_MEMORY_ALLOCATOR_H
#include <cstddef>

namespace Orange
{
    class MemoryAllocator
    {
    public:
        virtual ~MemoryAllocator() = default;

        // 分配内存
        virtual void *Allocate(size_t size, size_t alignment = 8) = 0;

        // 释放内存
        virtual void Deallocate(void *ptr) = 0;

        // 重新分配内存
        virtual void *Reallocate(void *ptr, size_t newSize, size_t alignment = 8) = 0;

        // 获取分配器名称
        virtual const char *GetName() const = 0;

        // 获取已分配的总内存大小
        virtual size_t GetTotalAllocated() const = 0;

        // 获取最大分配的内存大小
        virtual size_t GetMaxAllocated() const = 0;
    };
}
#endif // ORANGE_MEMORY_ALLOCATOR_H