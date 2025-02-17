#include "Sanbox2D.h"
#include "Platform/OpenGL/OpenGLShader.h"


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
    //{
//    ORG_PROFILE_SCOPE("Renderer2D Draw");
//    Orange::Renderer2D::BeginScene(mCameraControler.GetCamera());

//    Orange::Renderer2D::DrawQuad({ -1.0f, 0.0f }, { 0.8f, 0.8f }, { 0.8f, 0.2f, 0.3f, 1.0f });
//    Orange::Renderer2D::DrawQuad({ 0.5f, -0.5f }, { 0.5f, 0.75f }, mSquareColor);
//    Orange::Renderer2D::DrawQuad({ -5.f, -5.f, -0.1f }, { 20.0f, 20.0f }, mpCheckerboardTexture, 10.0f);
//    Orange::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f, 0.1f }, { 1.0f, 1.0f }, 45.0f, mSquareColor);

//    Orange::Renderer2D::EndScene();

//    Orange::Renderer2D::BeginScene(mCameraControler.GetCamera());
//    for (float y = -5.0f; y < 5.0f; y += 0.05f)
//    {
//        for (float x = -5.0f; x < 5.0f; x += 0.05f)
//        {
//            glm::vec4 color = { (x + 5.0f) / 10.0f, 0.4f, (y + 5.0f) / 10.0f, 0.7f };
//            Orange::Renderer2D::DrawQuad({ x, y }, { 0.45f, 0.45f }, color);
//        }
//    }
//    Orange::Renderer2D::EndScene();
//}
}

void Sandbox2D::OnImGuiRender()
{

}

void Sandbox2D::OnEvent(Orange::Event& e)
{
    
}
