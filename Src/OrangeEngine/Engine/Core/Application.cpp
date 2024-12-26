#include "pch.h"
#include "Windows/WinWindow.h"
#include "ApplicationEvent.h"
#include "Application.h"
#include "Input.h"
#include "imgui/ImGuiLayer.h"
#include "Renderer/Shader.h"

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
        mpWindow = WinWindow::Create();
        mpWindow->SetEventCallback(BIND_EVENT_FN(OnEvent));
        spInstance = this;

        mpImGuiLayer = std::make_unique<ImGuiLayer>();
        PushOverlay(mpImGuiLayer.get());

        std::string vertexSrc = R"(
			#version 330 core
			
			layout(location = 0) in vec3 a_Position;
			out vec3 v_Position;
			void main()
			{
				v_Position = a_Position;
				gl_Position = vec4(a_Position, 1.0);	
			}
		)";
        std::string fragmentSrc = R"(
			#version 330 core
			
			layout(location = 0) out vec4 color;
			in vec3 v_Position;
			void main()
			{
				color = vec4(v_Position * 0.5 + 0.5, 1.0);
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