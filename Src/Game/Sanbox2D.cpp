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

}

void Sandbox2D::OnImGuiRender()
{

}

void Sandbox2D::OnEvent(Orange::Event& e)
{
    
}
