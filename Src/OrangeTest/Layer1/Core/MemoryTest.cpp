#include <gtest/gtest.h>
#include "Layer1/Core/Memory/MemoryPool.h"
#include "Layer1/Core/Memory/MemoryLeakDetector.h"

namespace Orange
{
    namespace Test
    {
        class MemoryTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                // 使用Reset方法重置内存泄漏检测器
                MemoryLeakDetector::GetInstance().Reset();
            }
        };

        TEST_F(MemoryTest, MemoryPoolAllocation)
        {
            const size_t blockSize = 1024;
            const size_t initialBlocks = 4;
            MemoryPool pool(blockSize, initialBlocks);

            // 测试基本分配
            void *ptr1 = pool.Allocate(512);
            ASSERT_NE(ptr1, nullptr);
            EXPECT_EQ(pool.GetTotalAllocated(), blockSize);

            // 测试分配超过块大小
            void *ptr2 = pool.Allocate(2048);
            EXPECT_EQ(ptr2, nullptr);

            // 测试释放
            pool.Deallocate(ptr1);
            EXPECT_EQ(pool.GetTotalAllocated(), 0);
        }

        TEST_F(MemoryTest, MemoryPoolReallocation)
        {
            const size_t blockSize = 1024;
            MemoryPool pool(blockSize);

            void *ptr = pool.Allocate(512);
            ASSERT_NE(ptr, nullptr);

            // 测试重新分配
            void *newPtr = pool.Reallocate(ptr, 2048);
            EXPECT_EQ(newPtr, nullptr); // 应该失败，因为新大小超过块大小

            // 测试重新分配较小的大小
            newPtr = pool.Reallocate(ptr, 256);
            EXPECT_NE(newPtr, nullptr);
            EXPECT_EQ(pool.GetTotalAllocated(), blockSize);

            pool.Deallocate(newPtr);
        }

        TEST_F(MemoryTest, MemoryLeakDetection)
        {
            auto &detector = MemoryLeakDetector::GetInstance();

            // 测试内存分配跟踪
            void *ptr = std::malloc(100);
            detector.TrackAllocation(ptr, 100, __FILE__, __LINE__);

            // 测试内存释放跟踪
            detector.TrackDeallocation(ptr);
            std::free(ptr);

            // 测试内存泄漏检测
            void *leakedPtr = std::malloc(200);
            detector.TrackAllocation(leakedPtr, 200, __FILE__, __LINE__);

            // 不释放内存，让检测器报告泄漏
            std::cout << "Expecting memory leak report:" << std::endl;
            detector.ReportLeaks();

            // 清理
            std::free(leakedPtr);
        }

        TEST_F(MemoryTest, MemoryPoolExpansion)
        {
            const size_t blockSize = 1024;
            const size_t initialBlocks = 2;
            MemoryPool pool(blockSize, initialBlocks);

            // 分配初始块
            void *ptr1 = pool.Allocate(512);
            void *ptr2 = pool.Allocate(512);
            ASSERT_NE(ptr1, nullptr);
            ASSERT_NE(ptr2, nullptr);

            // 测试池扩展
            void *ptr3 = pool.Allocate(512);
            ASSERT_NE(ptr3, nullptr);
            EXPECT_EQ(pool.GetTotalAllocated(), blockSize * 3);

            // 清理
            pool.Deallocate(ptr1);
            pool.Deallocate(ptr2);
            pool.Deallocate(ptr3);
        }
    }
}