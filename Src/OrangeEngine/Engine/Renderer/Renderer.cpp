#include "pch.h"
#include "Renderer.h"
#include "OrthographicCamera.h"
#include "OpenGL/OpenGLShader.h"
#include "RenderCommand.h"
#include "Renderer2D.h"

namespace Orange
{
    Renderer::SceneData* Renderer::mpSceneData = new Renderer::SceneData;

    void Renderer::Init()
    {
        RenderCommand::Init();
        Renderer2D::Init();
    }

    void Renderer::BeginScene(std::shared_ptr<OrthographicCamera>& camera)
    {
        mpSceneData->ViewProjectionMatrix = camera->GetViewProjectionMatrix();
    }

    void Renderer::EndScene()
    {
    }

    void Renderer::OnWindowResize(uint32_t width, uint32_t height)
    {
        RenderCommand::SetViewport(0, 0, width, height);
    }

    void Renderer::Submit(const std::shared_ptr<Shader>& shader,
        const std::shared_ptr<VertexArray>& vertexArray, 
        const glm::mat4& transform)
    {
        shader->Bind();
        std::dynamic_pointer_cast<OpenGLShader>(shader)->SetMat4("u_ViewProjection", mpSceneData->ViewProjectionMatrix);
        std::dynamic_pointer_cast<OpenGLShader>(shader)->SetMat4("u_Transform", transform);
        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray);
    }
}