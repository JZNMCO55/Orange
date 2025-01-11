#include "Sanbox2D.h"
#include "Platform/OpenGL/OpenGLShader.h"
#include "imgui/imgui.h"

Sandbox2D::Sandbox2D()
    : Layer("Sandbox2D")
{
    mpCameraController = std::make_shared<Orange::OrthographicCameraControler>(1280.0f / 720.0f);
}

Sandbox2D::~Sandbox2D()
{
}

void Sandbox2D::OnAttach()
{
}

void Sandbox2D::OnDetach()
{
    
}

void Sandbox2D::OnUpdate(Orange::Timestep ts)
{
    mpCameraController->OnUpdate(ts);
    
    Orange::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
    Orange::RenderCommand::Clear();
    
    Orange::Renderer2D::BeginScene(mpCameraController->GetCamera());
    Orange::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f }, mSquareColor);
    Orange::Renderer2D::EndScene();
}

void Sandbox2D::OnImGuiRender()
{
    ImGui::Begin("Settings");
    ImGui::ColorEdit4("Square Color", glm::value_ptr(mSquareColor));
    ImGui::End();
}

void Sandbox2D::OnEvent(Orange::Event& e)
{
    mpCameraController->OnEvent(e);
}
