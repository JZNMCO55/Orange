#include "MemoryLeakDetector.h"
#include <iostream>
#include <iomanip>

namespace Orange
{

    MemoryLeakDetector &MemoryLeakDetector::GetInstance()
    {
        static MemoryLeakDetector instance;
        return instance;
    }

    MemoryLeakDetector::~MemoryLeakDetector()
    {
        ReportLeaks();
    }

    void MemoryLeakDetector::TrackAllocation(void *ptr, size_t size, const char *file, int line)
    {
        if (!ptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        AllocationInfo info;
        info.size = size;
        info.file = file;
        info.line = line;
        m_allocations[ptr] = info;
        m_totalAllocated += size;
    }

    void MemoryLeakDetector::TrackDeallocation(void *ptr)
    {
        if (!ptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_allocations.find(ptr);
        if (it != m_allocations.end())
        {
            m_totalAllocated -= it->second.size;
            m_allocations.erase(it);
        }
    }

    void MemoryLeakDetector::ReportLeaks()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_allocations.empty())
        {
            std::cout << "No memory leaks detected." << std::endl;
            return;
        }

        std::cout << "\nMemory Leak Report:" << std::endl;
        std::cout << "==================" << std::endl;
        std::cout << "Total leaks: " << m_allocations.size() << std::endl;
        std::cout << "Total leaked memory: " << m_totalAllocated << " bytes" << std::endl;
        std::cout << "\nDetailed Report:" << std::endl;
        std::cout << "---------------" << std::endl;

        for (const auto &allocation : m_allocations)
        {
            std::cout << "Leak at address: " << allocation.first << std::endl;
            std::cout << "Size: " << allocation.second.size << " bytes" << std::endl;
            std::cout << "File: " << allocation.second.file << std::endl;
            std::cout << "Line: " << allocation.second.line << std::endl;
            std::cout << "---------------" << std::endl;
        }
    }
}