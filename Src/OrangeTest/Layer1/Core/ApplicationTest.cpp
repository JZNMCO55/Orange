#include <gtest/gtest.h>
#include "Layer1/Core/Application/Application.h"
#include <thread>
#include <chrono>

namespace Orange
{
    namespace Core
    {
        // 测试Application的基本创建和销毁
        TEST(ApplicationTest, BasicCreationAndDestruction)
        {
            EXPECT_NO_THROW({
                Application app;
            });
        }

        // 测试Application的Close功能
        TEST(ApplicationTest, CloseFunction)
        {
            Application app;

            // 在另一个线程中延迟关闭应用程序
            std::thread closeThread([&app]()
                                    {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                app.Close(); });

            // 运行应用程序（应该很快退出）
            auto start = std::chrono::high_resolution_clock::now();
            app.Run();
            auto end = std::chrono::high_resolution_clock::now();

            closeThread.join();

            // 验证应用程序在合理时间内退出（不超过1秒）
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            EXPECT_LT(duration.count(), 1000);
        }
    }
}