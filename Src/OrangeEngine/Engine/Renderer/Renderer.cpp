#include "pch.h"
#include "Renderer.h"
#include "OrthographicCamera.h"
#include "OpenGL/OpenGLShader.h"

namespace Orange
{
    Renderer::SceneData* Renderer::mpSceneData = new Renderer::SceneData;

    void Renderer::BeginScene(std::shared_ptr<OrthographicCamera>& camera)
    {
        mpSceneData->ViewProjectionMatrix = camera->GetViewProjectionMatrix();
    }

    void Renderer::EndScene()
    {
    }

    void Renderer::Submit(const std::shared_ptr<Shader>& shader,
        const std::shared_ptr<VertexArray>& vertexArray, 
        const glm::mat4& transform)
    {
        shader->Bind();
        std::dynamic_pointer_cast<OpenGLShader>(shader)->UploadUniformMat4("u_ViewProjection", mpSceneData->ViewProjectionMatrix);
        std::dynamic_pointer_cast<OpenGLShader>(shader)->UploadUniformMat4("u_Transform", transform);
        vertexArray->Bind();
        RenderCommand::DrawIndexed(vertexArray);
    }
}