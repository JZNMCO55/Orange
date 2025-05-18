#ifndef ORANGE_MEMORY_POOL_H
#define ORANGE_MEMORY_POOL_H

#include "MemoryAllocator.h"
#include <vector>
#include <mutex>

namespace Orange
{
    class MemoryPool : public MemoryAllocator
    {
    public:
        MemoryPool(size_t blockSize, size_t initialBlocks = 16);
        ~MemoryPool() override;

        void *Allocate(size_t size, size_t alignment = 8) override;
        void Deallocate(void *ptr) override;
        void *Reallocate(void *ptr, size_t newSize, size_t alignment = 8) override;
        const char *GetName() const override { return "MemoryPool"; }
        size_t GetTotalAllocated() const override;
        size_t GetMaxAllocated() const override;

    private:
        struct Block
        {
            void *memory;
            bool inUse;
        };

        void ExpandPool(size_t additionalBlocks);
        void *AllocateBlock();
        void FreeBlock(void *ptr);

        size_t m_blockSize;
        std::vector<Block> m_blocks;
        std::mutex m_mutex;
        size_t m_totalAllocated;
        size_t m_maxAllocated;
    };
}
#endif // ORANGE_MEMORY_POOL_H