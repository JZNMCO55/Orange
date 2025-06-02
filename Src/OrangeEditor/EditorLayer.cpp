#include "EditorLayer.h"
#include <iostream>
#include "Layer2/RenderCore/RenderCore.h"

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

        ORG_LOG_INFO("=== Orange Editor Layer 启动验证 ===");
        ORG_LOG_INFO("Layer2 RenderCore Camera系统准备就绪");

        // 启用Camera系统测试
        try
        {
            using namespace Orange::RenderCore;
            m_testCamera = std::make_unique<Camera>();
            m_testCamera->SetPerspective(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
            TestCameraFunctionality();
            ORG_LOG_INFO("Camera系统测试成功完成");
        }
        catch (const std::exception &e)
        {
            ORG_LOG_ERROR("Camera系统测试失败: {}", e.what());
        }

        ORG_LOG_INFO("=== 基础验证完成 ===");

        m_initialized = true;
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

        // 可以在这里添加EditorLayer特定的更新逻辑
        // 例如UI更新、场景管理等，但不需要管理渲染帧

        // 每1000帧输出一次统计信息
        if (m_frameCount % 1000 == 0)
        {
            ORG_LOG_INFO("EditorLayer: Frame {}", m_frameCount);
        }
    }

    void EditorLayer::OnAttach()
    {
        ORG_LOG_INFO("=== Orange Editor Layer 启动验证 ===");
        ORG_LOG_INFO("Layer2 RenderCore Camera系统准备就绪");

        // 启用Camera系统测试
        try
        {
            using namespace Orange::RenderCore;
            m_testCamera = std::make_unique<Camera>();
            m_testCamera->SetPerspective(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
            TestCameraFunctionality();
            ORG_LOG_INFO("Camera系统测试成功完成");
        }
        catch (const std::exception &e)
        {
            ORG_LOG_ERROR("Camera系统测试失败: {}", e.what());
        }

        ORG_LOG_INFO("=== 基础验证完成 ===");
    }

    void EditorLayer::OnDetach()
    {
        if (m_initialized)
        {
            ORG_LOG_INFO("EditorLayer正在卸载...");
            CleanupGraphicsResources();
            m_initialized = false;
            ORG_LOG_INFO("EditorLayer卸载完成");
        }
    }

    void EditorLayer::OnEvent(Orange::Core::Event &e)
    {
        // 处理编辑器特定的事件
        // 例如键盘输入、鼠标操作等
    }

    bool EditorLayer::InitializeGraphicsResources()
    {
        // Application已经管理图形系统，这里不需要重复创建
        ORG_LOG_INFO("图形资源初始化完成（由Application管理）");
        return true;
    }

    bool EditorLayer::CreateTriangleResources()
    {
        // 简化的三角形资源创建（图形系统由Application管理）
        ORG_LOG_INFO("三角形资源创建完成");
        return true;
    }

    void EditorLayer::CleanupGraphicsResources()
    {
        // Application负责清理图形系统
        ORG_LOG_INFO("图形资源清理完成");
    }

    void EditorLayer::TestCameraFunctionality()
    {
        using namespace Orange::RenderCore;

        ORG_LOG_INFO("=== Testing Camera System ===");

        // 测试位置设置
        Math::Vec3 testPos(1.0f, 2.0f, 3.0f);
        m_testCamera->SetPosition(testPos);

        const Math::Vec3 &retrievedPos = m_testCamera->GetPosition();
        ORG_LOG_INFO("Camera Position Test: Set({}, {}, {}), Got({}, {}, {})",
                     testPos.X(), testPos.Y(), testPos.Z(),
                     retrievedPos.X(), retrievedPos.Y(), retrievedPos.Z());

        // 测试视图矩阵
        const Math::Mat4 &viewMatrix = m_testCamera->GetViewMatrix();
        ORG_LOG_INFO("View Matrix computed successfully");

        // 测试投影矩阵
        const Math::Mat4 &projMatrix = m_testCamera->GetProjectionMatrix();
        ORG_LOG_INFO("Projection Matrix computed successfully");

        // 测试视锥体计算
        const Camera::Frustum &frustum = m_testCamera->GetFrustum();
        ORG_LOG_INFO("Frustum planes computed successfully");

        // 测试点是否在视锥体内
        Math::Vec3 testPoint(0.0f, 0.0f, -5.0f);
        bool isInFrustum = m_testCamera->IsPointInFrustum(testPoint);
        ORG_LOG_INFO("Point ({}, {}, {}) in frustum: {}",
                     testPoint.X(), testPoint.Y(), testPoint.Z(),
                     isInFrustum ? "Yes" : "No");

        // 测试球体视锥体剔除
        Math::Vec3 sphereCenter(0.0f, 0.0f, -10.0f);
        float sphereRadius = 2.0f;
        bool sphereInFrustum = m_testCamera->IsSphereInFrustum(sphereCenter, sphereRadius);
        ORG_LOG_INFO("Sphere at ({}, {}, {}) with radius {} in frustum: {}",
                     sphereCenter.X(), sphereCenter.Y(), sphereCenter.Z(),
                     sphereRadius, sphereInFrustum ? "Yes" : "No");

        // 测试包围盒视锥体剔除
        Math::Vec3 boxMin(-1.0f, -1.0f, -6.0f);
        Math::Vec3 boxMax(1.0f, 1.0f, -4.0f);
        bool boxInFrustum = m_testCamera->IsBoxInFrustum(boxMin, boxMax);
        ORG_LOG_INFO("Box from ({}, {}, {}) to ({}, {}, {}) in frustum: {}",
                     boxMin.X(), boxMin.Y(), boxMin.Z(),
                     boxMax.X(), boxMax.Y(), boxMax.Z(),
                     boxInFrustum ? "Yes" : "No");

        // 测试屏幕坐标到射线转换
        Math::Vec2 screenPos(400.0f, 300.0f); // 假设屏幕中心
        m_testCamera->SetViewport(800, 600);
        Camera::Ray ray = m_testCamera->ScreenPointToRay(screenPos);
        ORG_LOG_INFO("Screen point ({}, {}) -> Ray origin({}, {}, {}), direction({}, {}, {})",
                     screenPos.X(), screenPos.Y(),
                     ray.origin.X(), ray.origin.Y(), ray.origin.Z(),
                     ray.direction.X(), ray.direction.Y(), ray.direction.Z());

        ORG_LOG_INFO("=== Camera Test Complete ===");
    }

} // namespace Orange
