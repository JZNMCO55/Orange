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

int main(int argc, char *argv[])
{
    // 初始化Logger
    Orange::Logger::Init();

    ORG_LOG_INFO("Orange Engine - Editor Application");

    // 显示当前工作目录，帮助调试路径问题
    char currentPath[FILENAME_MAX];
    if (getcwd(currentPath, sizeof(currentPath)))
    {
        ORG_LOG_INFO("Current working directory: {}", currentPath);
    }

    try
    {
        // 创建应用程序
        auto app = std::make_unique<Orange::Core::Application>();

        // 创建并添加编辑器层
        auto editorLayer = std::make_shared<Orange::EditorLayer>();

        // 初始化编辑器层
        if (!editorLayer->Initialize())
        {
            ORG_LOG_ERROR("Failed to initialize EditorLayer");
            return -1;
        }

        // 添加层到应用程序
        app->PushLayer(editorLayer);

        // 运行应用程序
        ORG_LOG_INFO("Starting Orange Editor...");
        app->Run();

        ORG_LOG_INFO("Orange Editor shutdown complete");
    }
    catch (const std::exception &e)
    {
        ORG_LOG_ERROR("Application failed: {}", e.what());
        std::cout << "Press Enter to exit..." << std::endl;
        std::cin.get();
        return -1;
    }

    return 0;
}