#include "pch.h"
#include "OrthographicCamera.h"
#include "VertexArray.h"
#include "Shader.h"
#include "RenderCommand.h"
#include "Renderer2D.h"
#include "Texture.h"

#include "OpenGL/OpenGLShader.h"

namespace Orange
{
    struct Renderer2DStorage
    {
        Ref<VertexArray> mpQuadVertexArray;
        Ref<Shader> mpFlatShader;
        Ref<Shader> mpTextureShader;
    };

    static Renderer2DStorage* spData;

    void Renderer2D::Init()
    {
        spData = new Renderer2DStorage();
        spData->mpQuadVertexArray = VertexArray::Create();

        float squareVertices[5 * 4] = {
            -0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
             0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
             0.5f,  0.5f, 0.0f, 1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f, 0.0f, 1.0f
        };

        Ref<VertexBuffer> squareVB;
        squareVB.reset(VertexBuffer::Create(squareVertices, sizeof(squareVertices)));
        squareVB->SetLayout({
            { EShaderDataType::Float3, "a_Position" },
            { EShaderDataType::Float2, "a_TexCoord" }
            });
        spData->mpQuadVertexArray->AddVertexBuffer(squareVB);
        uint32_t squareIndices[6] = { 0, 1, 2, 2, 3, 0 };

        Ref<IndexBuffer> squareIB;
        squareIB.reset(IndexBuffer::Create(squareIndices, sizeof(squareIndices) / sizeof(uint32_t)));
        spData->mpQuadVertexArray->SetIndexBuffer(squareIB);
        spData->mpFlatShader = Shader::Create(R"(../../Resource/Shaders/FlatShader.glsl)");
        spData->mpTextureShader = Shader::Create(R"(../../Resource/Shaders/Texture.glsl)");
        spData->mpTextureShader->Bind();
        spData->mpTextureShader->SetInt("u_Texture", 0);
    }

    void Renderer2D::Shutdown()
    {
        delete spData;
        spData = nullptr;
    }

    void Renderer2D::BeginScene(const Ref<OrthographicCamera>& camera)
    {
        spData->mpFlatShader->Bind();
        spData->mpFlatShader->SetMat4("u_ViewProjection", camera->GetViewProjectionMatrix());

        spData->mpTextureShader->Bind();
        spData->mpTextureShader->SetMat4("u_ViewProjection", camera->GetViewProjectionMatrix());

        spData->mpQuadVertexArray->Bind();
        RenderCommand::DrawIndexed(spData->mpQuadVertexArray);
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
        spData->mpFlatShader->Bind();
        spData->mpFlatShader->SetFloat4("u_Color", color);
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        spData->mpFlatShader->SetMat4("u_Transform", transform);
        spData->mpQuadVertexArray->Bind();
        RenderCommand::DrawIndexed(spData->mpQuadVertexArray);
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& texture)
    {
        DrawQuad({ position.x, position.y, 0.0f }, size, texture);
    }

    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture)
    {
        spData->mpTextureShader->Bind();
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        spData->mpTextureShader->SetMat4("u_Transform", transform);
        texture->Bind();
    }
}