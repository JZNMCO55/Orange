#include "pch.h"
#include "ImGuiLayer.h"
#include "OpenGL/ImGuiOpenGLRender.h"
#include "Application.h"

namespace Orange
{
    ImGuiLayer::ImGuiLayer()
        : Layer("ImGuiLayer")
    {
    }

    ImGuiLayer::~ImGuiLayer()
    {
    }

    void ImGuiLayer::OnAttach()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

        ImGui_ImplOpenGL3_Init("#version 410");
    }

    void ImGuiLayer::OnDetach()
    {
    }

    void ImGuiLayer::OnUpdate()
    {
        ImGuiIO& io = ImGui::GetIO();
        auto tpApp = Application::GetInstance();
        io.DisplaySize = ImVec2((float)tpApp->GetWindow().GetWidth(), (float)tpApp->GetWindow().GetHeight());
        float time = (float)glfwGetTime();
        io.DeltaTime = mTime > 0.0f ? (time - mTime) : (1.0f / 60.0f);
        mTime = time;


        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();


        static bool show = true;
        ImGui::ShowDemoWindow(&show);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void ImGuiLayer::OnEvent(Event& event)
    {
    }
}