#ifndef ORANGE_MEMORY_LEAK_DETECTOR_H
#define ORANGE_MEMORY_LEAK_DETECTOR_H

#include <unordered_map>
#include <mutex>
#include <string>
#include <cstddef>

namespace Orange
{

    class MemoryLeakDetector
    {
    public:
        static MemoryLeakDetector &GetInstance();
        void TrackAllocation(void *ptr, size_t size, const char *file, int line);
        void TrackDeallocation(void *ptr);
        void ReportLeaks();

        // 添加重置方法
        void Reset()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_allocations.clear();
            m_totalAllocated = 0;
        }

    private:
        MemoryLeakDetector() : m_totalAllocated(0) {}
        ~MemoryLeakDetector();

        struct AllocationInfo
        {
            size_t size;
            std::string file;
            int line;
        };

        std::unordered_map<void *, AllocationInfo> m_allocations;
        std::mutex m_mutex;
        size_t m_totalAllocated;
    };

#ifdef _DEBUG
#define TRACK_ALLOCATION(ptr, size) \
    Orange::Memory::MemoryLeakDetector::GetInstance().TrackAllocation(ptr, size, __FILE__, __LINE__)
#define TRACK_DEALLOCATION(ptr) \
    Orange::Memory::MemoryLeakDetector::GetInstance().TrackDeallocation(ptr)
#else
#define TRACK_ALLOCATION(ptr, size)
#define TRACK_DEALLOCATION(ptr)
#endif
}
#endif // ORANGE_MEMORY_LEAK_DETECTOR_H