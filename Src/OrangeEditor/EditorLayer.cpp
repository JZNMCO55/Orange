/**
 * @file EditorLayer.cpp
 * @brief 编辑器层实现
 */

#include "EditorLayer.h"
#include <iostream>
#include <vector>

namespace Orange
{
    EditorLayer::EditorLayer()
    {
        std::cout << "EditorLayer created" << std::endl;
    }

    EditorLayer::~EditorLayer()
    {
        if (m_initialized)
        {
            Shutdown();
        }
        std::cout << "EditorLayer destroyed" << std::endl;
    }

    bool EditorLayer::Initialize()
    {
        std::cout << "Initializing EditorLayer..." << std::endl;

        // 1. 配置图形系统
        Graphics::GraphicsSystemConfig config;
        config.preferredBackend = Graphics::RenderAPI::Vulkan;
        config.enableValidation = true;
        config.enableDebugMarkers = true;
        config.maxFramesInFlight = 2;
        config.applicationName = "OrangeEditor";
        config.applicationVersion = 1;

        // 2. 初始化图形系统
        if (!Graphics::GraphicsSystem::Initialize(config))
        {
            std::cerr << "Failed to initialize graphics system" << std::endl;
            return false;
        }

        // 3. 初始化图形资源
        if (!InitializeGraphicsResources())
        {
            std::cerr << "Failed to initialize graphics resources" << std::endl;
            Graphics::GraphicsSystem::Shutdown();
            return false;
        }

        // 4. 创建三角形渲染资源
        if (!CreateTriangleResources())
        {
            std::cerr << "Failed to create triangle resources" << std::endl;
            CleanupGraphicsResources();
            Graphics::GraphicsSystem::Shutdown();
            return false;
        }

        m_initialized = true;
        std::cout << "EditorLayer initialized successfully!" << std::endl;
        return true;
    }

    void EditorLayer::Shutdown()
    {
        if (!m_initialized)
        {
            return;
        }

        std::cout << "Shutting down EditorLayer..." << std::endl;

        // 等待GPU完成所有操作
        if (m_renderDevice)
        {
            m_renderDevice->WaitIdle();
        }

        // 清理图形资源
        CleanupGraphicsResources();

        // 关闭图形系统
        Graphics::GraphicsSystem::Shutdown();

        m_initialized = false;
        std::cout << "EditorLayer shutdown complete!" << std::endl;
    }

    void EditorLayer::OnUpdate()
    {
        if (!m_initialized)
        {
            return;
        }

        // 更新帧计数
        m_frameCount++;

        // 开始新帧
        Graphics::GraphicsSystem::BeginFrame(m_frameCount);

        // 这里可以添加编辑器逻辑更新
        // 例如：场景更新、UI更新等

        // 结束帧
        Graphics::GraphicsSystem::EndFrame();
    }

    void EditorLayer::OnRender()
    {
        if (!m_initialized)
        {
            return;
        }

        // 渲染三角形
        RenderTriangle();

        // 这里可以添加其他渲染逻辑
        // 例如：UI渲染、场景渲染等
    }

    void EditorLayer::OnEvent(Core::Event& event)
    {
        // 处理编辑器事件
        // 例如：鼠标、键盘、窗口事件等
    }

    bool EditorLayer::InitializeGraphicsResources()
    {
        // 获取图形系统组件
        m_renderDevice = Graphics::GraphicsSystem::GetDevice();
        m_renderContext = Graphics::GraphicsSystem::GetContext();
        m_fenceFactory = Graphics::GraphicsSystem::GetFenceFactory();
        m_semaphoreFactory = Graphics::GraphicsSystem::GetSemaphoreFactory();
        m_memoryManager = Graphics::GraphicsSystem::GetMemoryManager();

        if (!m_renderDevice || !m_renderContext || !m_fenceFactory || !m_semaphoreFactory)
        {
            std::cerr << "Failed to get graphics system components" << std::endl;
            return false;
        }

        // 注意：内存管理器在简化实现中可能为nullptr，这是正常的
        std::cout << "Graphics resources initialized successfully!" << std::endl;
        return true;
    }

    bool EditorLayer::CreateTriangleResources()
    {
        // 1. 创建同步对象
        Graphics::FenceCreateInfo fenceInfo;
        fenceInfo.flags = static_cast<Graphics::FenceCreateFlags>(Graphics::FenceCreateFlagBits::None);
        fenceInfo.debugName = "EditorTriangleFence";

        m_renderFence = m_fenceFactory->CreateFence(fenceInfo);
        if (!m_renderFence)
        {
            std::cerr << "Failed to create render fence" << std::endl;
            return false;
        }

        Graphics::SemaphoreCreateInfo semaphoreInfo;
        semaphoreInfo.type = Graphics::SemaphoreType::Binary;
        semaphoreInfo.debugName = "EditorTriangleSemaphore";

        m_renderSemaphore = m_semaphoreFactory->CreateSemaphore(semaphoreInfo);
        if (!m_renderSemaphore)
        {
            std::cerr << "Failed to create render semaphore" << std::endl;
            return false;
        }

        // 2. 创建三角形顶点缓冲区
        struct Vertex
        {
            float x, y, z;
            float r, g, b; // 添加颜色
        };

        std::vector<Vertex> vertices = {
            {0.0f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f}, // 底部 - 红色
            {0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f},  // 右上 - 绿色
            {-0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f}  // 左上 - 蓝色
        };

        Graphics::BufferCreateInfo bufferInfo;
        bufferInfo.size = vertices.size() * sizeof(Vertex);
        bufferInfo.type = Graphics::BufferType::Vertex;
        bufferInfo.hostVisible = true;
        bufferInfo.hostCoherent = true;
        bufferInfo.deviceLocal = false;
        bufferInfo.name = "EditorTriangleVertexBuffer";

        m_triangleVertexBuffer = m_renderDevice->CreateBuffer(bufferInfo);
        if (!m_triangleVertexBuffer)
        {
            std::cerr << "Failed to create triangle vertex buffer" << std::endl;
            return false;
        }

        // 3. 上传顶点数据
        void *mappedData = m_triangleVertexBuffer->Map();
        if (mappedData)
        {
            memcpy(mappedData, vertices.data(), bufferInfo.size);
            m_triangleVertexBuffer->Unmap();
            std::cout << "Triangle vertex data uploaded successfully!" << std::endl;
        }
        else
        {
            std::cerr << "Failed to map triangle vertex buffer" << std::endl;
            return false;
        }

        std::cout << "Triangle resources created successfully!" << std::endl;
        return true;
    }

    void EditorLayer::RenderTriangle()
    {
        if (!m_renderContext || !m_triangleVertexBuffer)
        {
            return;
        }

        // 开始命令记录
        if (!m_renderContext->Begin())
        {
            std::cerr << "Failed to begin command buffer" << std::endl;
            return;
        }

        // 绑定顶点缓冲区
        m_renderContext->BindVertexBuffer(m_triangleVertexBuffer, 0, 0);

        // 绘制三角形
        m_renderContext->Draw(3, 1, 0, 0);

        // 结束命令记录
        if (!m_renderContext->End())
        {
            std::cerr << "Failed to end command buffer" << std::endl;
            return;
        }

        // 提交命令
        std::vector<Graphics::IRenderSemaphore *> waitSemaphores;
        std::vector<Graphics::IRenderSemaphore *> signalSemaphores;
        if (m_renderSemaphore)
        {
            signalSemaphores.push_back(m_renderSemaphore);
        }

        if (!m_renderContext->Submit(waitSemaphores, signalSemaphores, m_renderFence))
        {
            std::cerr << "Failed to submit command buffer" << std::endl;
            return;
        }

        // 每100帧输出一次信息，避免日志过多
        if (m_frameCount % 100 == 0)
        {
            std::cout << "Triangle rendered successfully! Frame: " << m_frameCount << std::endl;
        }
    }

    void EditorLayer::CleanupGraphicsResources()
    {
        // 清理三角形资源
        if (m_triangleVertexBuffer)
        {
            delete m_triangleVertexBuffer;
            m_triangleVertexBuffer = nullptr;
        }

        if (m_renderFence)
        {
            delete m_renderFence;
            m_renderFence = nullptr;
        }

        if (m_renderSemaphore)
        {
            delete m_renderSemaphore;
            m_renderSemaphore = nullptr;
        }

        // 重置指针
        m_renderDevice = nullptr;
        m_renderContext = nullptr;
        m_fenceFactory = nullptr;
        m_semaphoreFactory = nullptr;
        m_memoryManager = nullptr;

        std::cout << "Graphics resources cleaned up!" << std::endl;
    }

} // namespace Orange
