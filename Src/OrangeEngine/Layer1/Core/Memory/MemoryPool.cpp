#include "MemoryPool.h"
#include <cstdlib>
#include <algorithm>

namespace Orange
{

    MemoryPool::MemoryPool(size_t blockSize, size_t initialBlocks)
        : m_blockSize(blockSize), m_totalAllocated(0), m_maxAllocated(0)
    {
        ExpandPool(initialBlocks);
    }

    MemoryPool::~MemoryPool()
    {
        for (const auto &block : m_blocks)
        {
            if (block.memory)
            {
                std::free(block.memory);
            }
        }
    }

    void *MemoryPool::Allocate(size_t size, size_t alignment)
    {
        if (size > m_blockSize)
        {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        void *ptr = AllocateBlock();
        if (ptr)
        {
            m_totalAllocated += m_blockSize;
            m_maxAllocated = std::max(m_maxAllocated, m_totalAllocated);
        }
        return ptr;
    }

    void MemoryPool::Deallocate(void *ptr)
    {
        if (!ptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        FreeBlock(ptr);
        m_totalAllocated -= m_blockSize;
    }

    void *MemoryPool::Reallocate(void *ptr, size_t newSize, size_t alignment)
    {
        if (newSize <= m_blockSize)
        {
            return ptr;
        }

        void *newPtr = Allocate(newSize, alignment);
        if (newPtr && ptr)
        {
            std::memcpy(newPtr, ptr, m_blockSize);
            Deallocate(ptr);
        }
        return newPtr;
    }

    size_t MemoryPool::GetTotalAllocated() const
    {
        return m_totalAllocated;
    }

    size_t MemoryPool::GetMaxAllocated() const
    {
        return m_maxAllocated;
    }

    void MemoryPool::ExpandPool(size_t additionalBlocks)
    {
        for (size_t i = 0; i < additionalBlocks; ++i)
        {
            Block block;
            block.memory = std::malloc(m_blockSize);
            block.inUse = false;
            m_blocks.push_back(block);
        }
    }

    void *MemoryPool::AllocateBlock()
    {
        for (auto &block : m_blocks)
        {
            if (!block.inUse)
            {
                block.inUse = true;
                return block.memory;
            }
        }

        // 如果没有可用块，扩展池
        ExpandPool(m_blocks.size());
        return AllocateBlock();
    }

    void MemoryPool::FreeBlock(void *ptr)
    {
        for (auto &block : m_blocks)
        {
            if (block.memory == ptr)
            {
                block.inUse = false;
                return;
            }
        }
    }
}