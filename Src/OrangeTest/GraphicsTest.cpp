#include <Orange.h>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>

using namespace Orange::Graphics;

class GraphicsSystemTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 测试前的设置
    }

    void TearDown() override
    {
        // 测试后的清理
    }
};

TEST_F(GraphicsSystemTest, FactoryTest)
{
    // 检查支持的图形API
    auto supportedAPIs = GraphicsFactory::GetSupportedAPIs();
    std::cout << "Supported Graphics APIs:" << std::endl;
    for (auto api : supportedAPIs)
    {
        std::cout << "  - " << GraphicsFactory::GetAPIName(api) << std::endl;
    }

    // 获取默认API
    auto defaultAPI = GraphicsFactory::GetDefaultAPI();
    std::cout << "Default API: " << GraphicsFactory::GetAPIName(defaultAPI) << std::endl;

    EXPECT_NE(defaultAPI, GraphicsAPI::None) << "No graphics API available!";
}

TEST_F(GraphicsSystemTest, SystemCreationTest)
{
    auto defaultAPI = GraphicsFactory::GetDefaultAPI();
    ASSERT_NE(defaultAPI, GraphicsAPI::None);

    // 创建图形系统
    auto graphicsSystem = GraphicsFactory::CreateGraphicsSystem(defaultAPI);
    ASSERT_NE(graphicsSystem, nullptr) << "Failed to create graphics system!";

    std::cout << "Graphics system created successfully" << std::endl;
}

TEST_F(GraphicsSystemTest, SystemInitializationTest)
{
    auto defaultAPI = GraphicsFactory::GetDefaultAPI();
    ASSERT_NE(defaultAPI, GraphicsAPI::None);

    auto graphicsSystem = GraphicsFactory::CreateGraphicsSystem(defaultAPI);
    ASSERT_NE(graphicsSystem, nullptr);

    // 初始化图形系统
    EXPECT_TRUE(graphicsSystem->Initialize()) << "Failed to initialize graphics system!";

    std::cout << "Graphics system initialized successfully" << std::endl;
    std::cout << "Backbuffer size: " << graphicsSystem->GetBackbufferWidth()
              << "x" << graphicsSystem->GetBackbufferHeight() << std::endl;

    // 获取渲染设备信息
    auto renderDevice = graphicsSystem->GetRenderDevice();
    ASSERT_NE(renderDevice, nullptr);
    std::cout << "Render device: " << renderDevice->GetDeviceName() << std::endl;
    std::cout << "Available memory: " << (renderDevice->GetAvailableMemory() / 1024 / 1024) << " MB" << std::endl;

    // 获取着色器编译器信息
    auto shaderCompiler = graphicsSystem->GetShaderCompiler();
    ASSERT_NE(shaderCompiler, nullptr);
    std::cout << "Shader compiler: " << shaderCompiler->GetVersion() << std::endl;
    auto languages = shaderCompiler->GetSupportedLanguages();
    std::cout << "Supported shader languages: ";
    for (size_t i = 0; i < languages.size(); ++i)
    {
        std::cout << languages[i];
        if (i < languages.size() - 1)
            std::cout << ", ";
    }
    std::cout << std::endl;

    // 关闭图形系统
    graphicsSystem->Shutdown();
    std::cout << "Graphics system shutdown complete" << std::endl;
}

TEST_F(GraphicsSystemTest, RenderLoopTest)
{
    auto defaultAPI = GraphicsFactory::GetDefaultAPI();
    ASSERT_NE(defaultAPI, GraphicsAPI::None);

    auto graphicsSystem = GraphicsFactory::CreateGraphicsSystem(defaultAPI);
    ASSERT_NE(graphicsSystem, nullptr);
    ASSERT_TRUE(graphicsSystem->Initialize());

    // 简单的渲染循环测试
    std::cout << "Starting render loop test..." << std::endl;
    graphicsSystem->SetClearColor(0.2f, 0.3f, 0.8f, 1.0f);

    for (int frame = 0; frame < 3; ++frame)
    {
        EXPECT_NO_THROW({
            graphicsSystem->BeginFrame();
            graphicsSystem->Clear();
            // 这里可以添加实际的渲染命令
            graphicsSystem->EndFrame();
            graphicsSystem->Present();
        });

        std::cout << "Frame " << frame + 1 << " rendered" << std::endl;
    }

    std::cout << "Render loop test completed" << std::endl;

    // 关闭图形系统
    graphicsSystem->Shutdown();
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}