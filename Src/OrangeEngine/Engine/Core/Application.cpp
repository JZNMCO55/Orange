#include "pch.h"
#include "Windows/WinWindow.h"
#include "ApplicationEvent.h"
#include "Input.h"
#include "imgui/ImGuiLayer.h"
#include "Renderer/Shader.h"
#include "Renderer/Buffer.h"
#include "Renderer/VertexArray.h"
#include "Renderer/Renderer.h"
#include "Application.h"
#include "Renderer/OrthographicCamera.h"

namespace Orange
{
#define BIND_EVENT_FN(x) std::bind(&Application::x, this, std::placeholders::_1)
    
    Application* Application::spInstance = nullptr;
    
    Application* Application::GetInstance()
    {
        return spInstance;
    }

    Application::Application()
    {
        ORANGE_CORE_ASSERT(!spInstance, "Application already exists!");
        spInstance = this;
        mpWindow = WinWindow::Create();
        mpWindow->SetEventCallback(BIND_EVENT_FN(OnEvent));

        mpImGuiLayer = std::make_unique<ImGuiLayer>();
        PushOverlay(mpImGuiLayer.get());

        mpCamera = std::make_shared<OrthographicCamera>(-1.6f, 1.6f, -0.9f, 0.9f);
        //Triangle
        float vertices[3 * 7] = {
            -0.5f, -0.5f, 0.0f, 0.8f, 0.2f, 0.8f, 1.0f,
             0.5f, -0.5f, 0.0f, 0.2f, 0.3f, 0.8f, 1.0f,
             0.0f,  0.5f, 0.0f, 0.8f, 0.8f, 0.2f, 1.0f
        };

        mpVertexArray.reset(VertexArray::Create());
        std::shared_ptr<VertexBuffer> tpVertexBuffer(VertexBuffer::Create(vertices, sizeof(vertices)));
        
        BufferLayout layout = {
            {EShaderDataType::Float3, "a_Position"},
            {EShaderDataType::Float4, "a_Color"}
        };

        tpVertexBuffer->SetLayout(layout);
        mpVertexArray->AddVertexBuffer(tpVertexBuffer);

        uint32_t indices[3] = { 0, 1, 2 };
        std::shared_ptr<IndexBuffer> tpIndexBuffer;
        tpIndexBuffer.reset(IndexBuffer::Create(indices, sizeof(indices) / sizeof(uint32_t)));
        mpVertexArray->SetIndexBuffer(tpIndexBuffer);

        // Squares
        mpBlueVertexArray.reset(VertexArray::Create());
        float squareVertices[3 * 4] = {
            -0.75f, -0.75f, 0.0f,
             0.75f, -0.75f, 0.0f,
             0.75f,  0.75f, 0.0f,
            -0.75f,  0.75f, 0.0f
        };

        std::shared_ptr<VertexBuffer> squareVB;
        squareVB.reset(VertexBuffer::Create(squareVertices, sizeof(squareVertices)));
        squareVB->SetLayout({
            { EShaderDataType::Float3, "a_Position" }
            });
        mpBlueVertexArray->AddVertexBuffer(squareVB);

        uint32_t squareIndices[6] = { 0, 1, 2, 2, 3, 0 };
        std::shared_ptr<IndexBuffer> squareIB;
        squareIB.reset(IndexBuffer::Create(squareIndices, sizeof(squareIndices) / sizeof(uint32_t)));
        mpBlueVertexArray->SetIndexBuffer(squareIB);

        std::string vertexSrc = R"(
            #version 330 core
            
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec4 a_Color;
            uniform mat4 u_ViewProjection;

            out vec3 v_Position;
            out vec4 v_Color;

            void main()
            {
                v_Position = a_Position;
                v_Color = a_Color;
                gl_Position = u_ViewProjection * vec4(a_Position, 1.0);    
            }
        )";

        std::string fragmentSrc = R"(
            #version 330 core
            
            layout(location = 0) out vec4 color;

            in vec3 v_Position;
            in vec4 v_Color;

            void main()
            {
                color = vec4(v_Position * 0.5 + 0.5, 1.0);
                color = v_Color;
            }
        )";

        mpShader = std::make_unique<Shader>(vertexSrc, fragmentSrc);

        std::string blueShaderVertexSrc = R"(
            #version 330 core
            
            layout(location = 0) in vec3 a_Position;
            uniform mat4 u_ViewProjection;
            out vec3 v_Position;

            void main()
            {
                v_Position = a_Position;
                gl_Position = u_ViewProjection * vec4(a_Position, 1.0);    
            }
        )";

        std::string blueShaderFragmentSrc = R"(
            #version 330 core
            
            layout(location = 0) out vec4 color;

            in vec3 v_Position;

            void main()
            {
                color = vec4(0.2, 0.3, 0.8, 1.0);
            }
        )";

        mpBlueShader = std::make_unique<Shader>(blueShaderVertexSrc, blueShaderFragmentSrc);

    }

    Application::~Application()
    {
    }

    void Application::Run()
    {
        while (mbRunning)
        {
            RenderCommand::SetClearColor({ 0.2f, 0.3f, 0.3f, 1.0f });
            RenderCommand::Clear();

            mpCamera->SetPosition({ 0.5f, 0.5f, 0.0f });
            mpCamera->SetRotation(45.f);
            
            Renderer::BeginScene(mpCamera);
            mpBlueShader->Bind();
            Renderer::Submit(mpBlueShader, mpBlueVertexArray);

            mpShader->Bind();
            Renderer::Submit(mpShader, mpVertexArray);
            Renderer::EndScene();

            for (const auto& layer : mLayerStack)
            {
                layer->OnUpdate();
            }

            mpImGuiLayer->Begin();
            for (const auto& layer : mLayerStack)
            {
                layer->OnImGuiRender();
            }
            mpImGuiLayer->End();

            mpWindow->OnUpdate();
        }
    }

    void Application::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(OnWindowClose));
        ORANGE_LOG_INFO("Event: {0}", e.ToString());

        for (auto it = mLayerStack.end(); it != mLayerStack.begin(); )
        {
            (*--it)->OnEvent(e);
            if (e.IsHandled())
            {
                break;
            }
        }
    }

    void Application::PushLayer(Layer* layer)
    {
        mLayerStack.PushLayer(layer);
        layer->OnAttach();
    }

    void Application::PushOverlay(Layer* layer)
    {
        mLayerStack.PushOverlay(layer);
        layer->OnAttach();
    }

    bool Application::OnWindowClose(WindowCloseEvent& e)
    {
        mbRunning = false;

        return true;
    }
}