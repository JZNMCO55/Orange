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
        Ref<Shader> mpTextureShader;
        Ref<Texture2D> mpWhiteTexture;
    };

    static Renderer2DStorage* spData;

    void Renderer2D::Init()
    {
        ORG_PROFILE_FUNCTION();

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

        spData->mpWhiteTexture = Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff;
        spData->mpWhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

        Ref<IndexBuffer> squareIB;
        squareIB.reset(IndexBuffer::Create(squareIndices, sizeof(squareIndices) / sizeof(uint32_t)));
        spData->mpQuadVertexArray->SetIndexBuffer(squareIB);
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
        ORG_PROFILE_FUNCTION();

        spData->mpTextureShader->Bind();
        spData->mpTextureShader->SetMat4("u_ViewProjection", camera->GetViewProjectionMatrix());
    }

    void Renderer2D::EndScene()
    {
        ORG_PROFILE_FUNCTION();

    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
    {
        DrawQuad({ position.x, position.y, 0.0f }, size, color);
    }

    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
    {
        ORG_PROFILE_FUNCTION();

        spData->mpTextureShader->SetFloat4("u_Color", color);
        spData->mpWhiteTexture->Bind();
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        spData->mpTextureShader->SetMat4("u_Transform", transform);
        spData->mpQuadVertexArray->Bind();
        RenderCommand::DrawIndexed(spData->mpQuadVertexArray);
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& texture)
    {
        DrawQuad({ position.x, position.y, 0.0f }, size, texture);
    }

    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture)
    {
        ORG_PROFILE_FUNCTION();

        spData->mpTextureShader->SetFloat4("u_Color", glm::vec4(1.0f));
        texture->Bind();
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        spData->mpTextureShader->SetMat4("u_Transform", transform);
        spData->mpQuadVertexArray->Bind();
        RenderCommand::DrawIndexed(spData->mpQuadVertexArray);
    }
}