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
    ORG_PROFILE_FUNCTION();

    mpCheckerboardTexture = Orange::Texture2D::Create(R"(..\..\Resource\Textures\Checkerboard.png)");
}

void Sandbox2D::OnDetach()
{
    ORG_PROFILE_FUNCTION();

}

void Sandbox2D::OnUpdate(Orange::Timestep ts)
{
    ORG_PROFILE_FUNCTION();

    //ORG_PROFILE_SCOPE("CameraController::OnUpdate");
    mpCameraController->OnUpdate(ts);


    // Render
    {
        ORG_PROFILE_SCOPE("Render2D Preparation");
        Orange::RenderCommand::SetClearColor({ 0.2f, 0.3f, 0.3f, 1.0f });
        Orange::RenderCommand::Clear();
    }

    {
        ORG_PROFILE_SCOPE("Renderer2D Draw");
        Orange::Renderer2D::BeginScene(mpCameraController->GetCamera());

        Orange::Renderer2D::DrawQuad({ -1.0f, 0.0f }, { 0.8f, 0.8f }, { 0.8f, 0.2f, 0.3f, 1.0f });
        Orange::Renderer2D::DrawQuad({ 0.5f, -0.5f }, { 0.5f, 0.75f }, mSquareColor);
        Orange::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.1f }, { 10.0f, 10.0f }, mpCheckerboardTexture, 10.0f);
    
        Orange::Renderer2D::EndScene();
    }
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
