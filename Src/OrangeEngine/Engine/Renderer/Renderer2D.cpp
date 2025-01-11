#include "pch.h"
#include "OrthographicCamera.h"
#include "VertexArray.h"
#include "Shader.h"
#include "RenderCommand.h"
#include "Renderer2D.h"

#include "OpenGL/OpenGLShader.h"

namespace Orange
{
    struct Renderer2DStorage
    {
        Ref<VertexArray> mpQuadVertexArray;
        Ref<Shader> mpShader;
    };

    static Renderer2DStorage* spData;

    void Renderer2D::Init()
    {
        spData = new Renderer2DStorage();
        spData->mpQuadVertexArray = VertexArray::Create();

        float squareVertices[5 * 4] = {
            -0.5f, -0.5f, 0.0f,
            0.5f, -0.5f, 0.0f,
            0.5f, 0.5f, 0.0f,
            -0.5f, 0.5f, 0.0f,
        };

        Ref<VertexBuffer> squareVB;
        squareVB.reset(VertexBuffer::Create(squareVertices, sizeof(squareVertices)));
        squareVB->SetLayout({
            { EShaderDataType::Float3, "a_Position" }
            });
        spData->mpQuadVertexArray->AddVertexBuffer(squareVB);
        uint32_t squareIndices[6] = { 0, 1, 2, 2, 3, 0 };

        Ref<IndexBuffer> squareIB;
        squareIB.reset(IndexBuffer::Create(squareIndices, sizeof(squareIndices) / sizeof(uint32_t)));
        spData->mpQuadVertexArray->SetIndexBuffer(squareIB);
        spData->mpShader = Shader::Create(R"(../../Resource/Shaders/FlatShader.glsl)");
    }

    void Renderer2D::Shutdown()
    {
        delete spData;
        spData = nullptr;
    }

    void Renderer2D::BeginScene(const Ref<OrthographicCamera>& camera)
    {
        std::dynamic_pointer_cast<OpenGLShader>(spData->mpShader)->Bind();
        std::dynamic_pointer_cast<OpenGLShader>(spData->mpShader)->UploadUniformMat4("u_ViewProjection", camera->GetViewProjectionMatrix());
        std::dynamic_pointer_cast<OpenGLShader>(spData->mpShader)->UploadUniformMat4("u_Transform", glm::mat4(1.0f));
    }

    void Renderer2D::EndScene()
    {

    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
    {
        DrawQuad({ position.x, position.y, 0.0f }, size, color);
    }

    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
    {
        std::dynamic_pointer_cast<OpenGLShader>(spData->mpShader)->Bind();
        std::dynamic_pointer_cast<OpenGLShader>(spData->mpShader)->UploadUniformFloat4("u_Color", color);

        spData->mpQuadVertexArray->Bind();
        RenderCommand::DrawIndexed(spData->mpQuadVertexArray);
    }
}