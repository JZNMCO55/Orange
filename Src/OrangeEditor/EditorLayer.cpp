#include "EditorLayer.h"
#include <iostream>

namespace Orange
{

    EditorLayer::EditorLayer()
    {
        std::cout << "EditorLayer constructor" << std::endl;
    }

    EditorLayer::~EditorLayer()
    {
        if (m_initialized)
        {
            Shutdown();
        }
        std::cout << "EditorLayer destructor" << std::endl;
    }

    bool EditorLayer::Initialize()
    {
        if (m_initialized)
        {
            return true;
        }

        std::cout << "Initializing EditorLayer..." << std::endl;

        // 初始化图形资源
        if (!InitializeGraphicsResources())
        {
            std::cerr << "Failed to initialize graphics resources" << std::endl;
            return false;
        }

        m_initialized = true;
        std::cout << "EditorLayer initialized successfully" << std::endl;
        return true;
    }

    void EditorLayer::Shutdown()
    {
        if (!m_initialized)
        {
            return;
        }

        std::cout << "Shutting down EditorLayer..." << std::endl;
        CleanupGraphicsResources();
        m_initialized = false;
    }

    void EditorLayer::OnUpdate()
    {
        if (!m_initialized)
        {
            return;
        }

        m_frameCount++;
        // 基础渲染循环
        RenderTriangle();
    }

    void EditorLayer::OnAttach()
    {
        std::cout << "EditorLayer attached" << std::endl;
    }

    void EditorLayer::OnDetach()
    {
        std::cout << "EditorLayer detached" << std::endl;
    }

    void EditorLayer::OnEvent(Core::Event &event)
    {
        // 处理事件
    }

    bool EditorLayer::InitializeGraphicsResources()
    {
        std::cout << "Initializing graphics resources..." << std::endl;

        // 图形系统已经在main中测试过了，这里只做简单的资源创建
        return CreateTriangleResources();
    }

    bool EditorLayer::CreateTriangleResources()
    {
        // TODO: 创建三角形渲染资源
        std::cout << "Creating triangle resources..." << std::endl;
        return true;
    }

    void EditorLayer::RenderTriangle()
    {
        // TODO: 渲染三角形
        if (m_frameCount % 3600 == 0) // 每分钟输出一次 (假设60FPS)
        {
            std::cout << "Rendering frame " << m_frameCount << " (1 minute mark)" << std::endl;
        }
    }

    void EditorLayer::CleanupGraphicsResources()
    {
        std::cout << "Cleaning up graphics resources..." << std::endl;
        // TODO: 清理图形资源
    }

} // namespace Orange
