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
        ORG_PROFILE_FUNCTION();

        ORANGE_CORE_ASSERT(!spInstance, "Application already exists!");
        spInstance = this;
        mpWindow = WinWindow::Create();
        mpWindow->SetEventCallback(BIND_EVENT_FN(OnEvent));

        Renderer::Init();

        mpImGuiLayer = std::make_unique<ImGuiLayer>();
        PushOverlay(mpImGuiLayer.get());
    }

    Application::~Application()
    {
        ORG_PROFILE_FUNCTION();

        //Renderer::Shutdown();
    }

    void Application::Run()
    {
        ORG_PROFILE_FUNCTION();

        while (mbRunning)
        {
            ORG_PROFILE_SCOPE("Run Loop");
            float time = (float)glfwGetTime();
            Timestep timestep = time - mLastFrameTime;
            mLastFrameTime = time;

            if (!mbMinimized)
            {
                {
                    ORG_PROFILE_SCOPE("LayerStack OnUpdate");
                    for (const auto& layer : mLayerStack)
                    {
                        layer->OnUpdate(timestep);
                    }
                }

                {
                    ORG_PROFILE_SCOPE("LayerStack OnImGuiRender");
                    mpImGuiLayer->Begin();
                    for (const auto& layer : mLayerStack)
                    {
                        layer->OnImGuiRender();
                    }
                    mpImGuiLayer->End();
                }

            }


            mpWindow->OnUpdate();
        }
    }

    void Application::OnEvent(Event& e)
    {
        ORG_PROFILE_FUNCTION();

        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(OnWindowClose));
        dispatcher.Dispatch<WindowResizeEvent>(BIND_EVENT_FN(OnWindowResize));
        //ORANGE_LOG_INFO("Event: {0}", e.ToString());

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
        ORG_PROFILE_FUNCTION();

        mLayerStack.PushLayer(layer);
        layer->OnAttach();
    }

    void Application::PushOverlay(Layer* layer)
    {
        ORG_PROFILE_FUNCTION();

        mLayerStack.PushOverlay(layer);
        layer->OnAttach();
    }

    bool Application::OnWindowClose(WindowCloseEvent& e)
    {
        mbRunning = false;

        return true;
    }

    bool Application::OnWindowResize(WindowResizeEvent& e)
    {
        ORG_PROFILE_FUNCTION();
        if (e.GetWidth() == 0 || e.GetHeight() == 0)
        {
            mbMinimized = true;
            return false;
        }

        mbMinimized = false;
        Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

        return false;
    }
}