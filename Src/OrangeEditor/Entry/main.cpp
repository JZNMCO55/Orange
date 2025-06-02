#include <iostream>
#include <thread>
#include <chrono>
#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif
#include "Layer1/Core/Application/Application.h"
#include "../EditorLayer.h"
#include "Orange.h"

// 图形系统测试函数
bool TestGraphicsSystem()
{
    ORG_LOG_INFO("=== Graphics System Test ===");

    // 首先测试文件系统和着色器文件
    ORG_LOG_INFO("Checking shader files...");
    if (!Orange::Core::FileSystem::FileExists("Resources/Shaders/triangle.vert"))
    {
        ORG_LOG_ERROR("Error: triangle.vert not found!");
        return false;
    }
    if (!Orange::Core::FileSystem::FileExists("Resources/Shaders/triangle.frag"))
    {
        ORG_LOG_ERROR("Error: triangle.frag not found!");
        return false;
    }
    ORG_LOG_INFO("Shader files found successfully!");

    try
    {
        auto graphicsSystem = Orange::Graphics::GraphicsFactory::CreateGraphicsSystem(Orange::Graphics::GraphicsAPI::Vulkan);
        if (!graphicsSystem)
        {
            ORG_LOG_ERROR("Failed to create graphics system");
            return false;
        }

        ORG_LOG_INFO("Graphics system created successfully");

        if (!graphicsSystem->Initialize())
        {
            ORG_LOG_ERROR("Failed to initialize graphics system");
            return false;
        }

        ORG_LOG_INFO("Graphics system initialized successfully!");
        ORG_LOG_INFO("Backbuffer size: {}x{}", graphicsSystem->GetBackbufferWidth(), graphicsSystem->GetBackbufferHeight());

        // 测试基本渲染循环 - 绘制渐变三角形
        ORG_LOG_INFO("Testing triangle rendering...");
        for (int i = 0; i < 180; ++i) // 渲染180帧，约3秒
        {
            if (i % 60 == 0)
            {
                ORG_LOG_INFO("  Frame {}/180", i + 1);
            }
            graphicsSystem->BeginFrame();
            graphicsSystem->SetClearColor(0.2f, 0.3f, 0.3f, 1.0f); // 深灰绿色背景
            graphicsSystem->Clear();
            graphicsSystem->EndFrame();
            graphicsSystem->Present();

            // 简单的延迟，让我们能看到窗口
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // 约60FPS
        }

        ORG_LOG_INFO("Keeping window open for 3 seconds...");
        std::this_thread::sleep_for(std::chrono::seconds(3));

        graphicsSystem->Shutdown();
        ORG_LOG_INFO("Graphics system test completed successfully!");
        ORG_LOG_INFO("=== Test Complete ===");
        return true;
    }
    catch (const std::exception &e)
    {
        ORG_LOG_ERROR("Graphics system test failed: {}", e.what());
        return false;
    }
}

int main(int argc, char *argv[])
{
    // 初始化Logger
    Orange::Logger::Init();

    ORG_LOG_INFO("Orange Engine - Graphics System Test");

    // 显示当前工作目录，帮助调试路径问题
    char currentPath[FILENAME_MAX];
    if (getcwd(currentPath, sizeof(currentPath)))
    {
        ORG_LOG_INFO("Current working directory: {}", currentPath);
    }

    // 首先测试文件系统和着色器文件
    ORG_LOG_INFO("Checking shader files...");
    if (!Orange::Core::FileSystem::FileExists("Resources/Shaders/triangle.vert"))
    {
        ORG_LOG_ERROR("Error: triangle.vert not found!");
        ORG_LOG_ERROR("Looking for: {}/Resources/Shaders/triangle.vert", currentPath);
        return false;
    }
    if (!Orange::Core::FileSystem::FileExists("Resources/Shaders/triangle.frag"))
    {
        ORG_LOG_ERROR("Error: triangle.frag not found!");
        ORG_LOG_ERROR("Looking for: {}/Resources/Shaders/triangle.frag", currentPath);
        return false;
    }
    ORG_LOG_INFO("Shader files found successfully!");

    // 运行图形系统测试
    if (!TestGraphicsSystem())
    {
        ORG_LOG_ERROR("Graphics system test failed!");
        std::cout << "Press Enter to exit..." << std::endl;
        std::cin.get();
        return -1;
    }

    ORG_LOG_INFO("Graphics system test passed!");
    std::cout << "Press Enter to exit..." << std::endl;
    std::cin.get();

    return 0;
}