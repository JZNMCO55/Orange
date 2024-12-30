#include "pch.h"
#include "Windows/WinWindow.h"
#include "ApplicationEvent.h"
#include "Input.h"
#include "imgui/ImGuiLayer.h"
#include "Renderer/Shader.h"
#include "Renderer/Buffer.h"
#include "Application.h"

namespace Orange
{
#define BIND_EVENT_FN(x) std::bind(&Application::x, this, std::placeholders::_1)
    
    Application* Application::spInstance = nullptr;
    
    Application* Application::GetInstance()
    {
        return spInstance;
    }

    static GLenum ShaderDataTypeToOpenGLBaseType(EShaderDataType type)
    {
        switch (type)
        {
        case EShaderDataType::Float:    return GL_FLOAT;
        case EShaderDataType::Float2:   return GL_FLOAT;
        case EShaderDataType::Float3:   return GL_FLOAT;
        case EShaderDataType::Float4:   return GL_FLOAT;
        case EShaderDataType::Mat3:     return GL_FLOAT;
        case EShaderDataType::Mat4:     return GL_FLOAT;
        case EShaderDataType::Int:      return GL_INT;
        case EShaderDataType::Int2:     return GL_INT;
        case EShaderDataType::Int3:     return GL_INT;
        case EShaderDataType::Int4:     return GL_INT;
        case EShaderDataType::Bool:     return GL_BOOL;
        }

        ORANGE_CORE_ASSERT(false, "Unknown ShaderDataType!");
        return 0;
    }

    Application::Application()
    {
        ORANGE_CORE_ASSERT(!spInstance, "Application already exists!");
        spInstance = this;
        mpWindow = WinWindow::Create();
        mpWindow->SetEventCallback(BIND_EVENT_FN(OnEvent));

        mpImGuiLayer = std::make_unique<ImGuiLayer>();
        PushOverlay(mpImGuiLayer.get());

        glGenVertexArrays(1, &mVertexArray);
        glBindVertexArray(mVertexArray);

        float vertices[3 * 7] = {
            -0.5f, -0.5f, 0.0f, 0.8f, 0.2f, 0.8f, 1.0f,
             0.5f, -0.5f, 0.0f, 0.2f, 0.3f, 0.8f, 1.0f,
             0.0f,  0.5f, 0.0f, 0.8f, 0.8f, 0.2f, 1.0f
        };

        mpVertexBuffer.reset(VertexBuffer::Create(vertices, sizeof(vertices)));

        {
            BufferLayout layout = {
                {EShaderDataType::Float3, "a_Position"},
                {EShaderDataType::Float4, "a_Color"}
            };

            mpVertexBuffer->SetLayout(layout);
        }

        uint32_t index = 0;
        const auto& layout = mpVertexBuffer->GetLayout();

        for (const auto& element : layout)
        {
            glEnableVertexAttribArray(index);
            glVertexAttribPointer(index, 
                element.GetComponentCount(), 
                ShaderDataTypeToOpenGLBaseType(element.Type),
                GL_FALSE,
                layout.GetStride(),
                (const void*)element.Offset);
            index++;
        }

        uint32_t indices[3] = { 0, 1, 2 };
        mpIndexBuffer.reset(IndexBuffer::Create(indices, sizeof(indices) / sizeof(uint32_t)));

        std::string vertexSrc = R"(
			#version 330 core
			
			layout(location = 0) in vec3 a_Position;
			layout(location = 1) in vec4 a_Color;

			out vec3 v_Position;
			out vec4 v_Color;

			void main()
			{
				v_Position = a_Position;
				v_Color = a_Color;
				gl_Position = vec4(a_Position, 1.0);	
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
    }

    Application::~Application()
    {
    }

    void Application::Run()
    {
        while (mbRunning)
        {
            glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            mpShader->Bind();
            glBindVertexArray(mVertexArray);
            glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, nullptr);

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