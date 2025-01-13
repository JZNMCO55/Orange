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
    struct QuadVertex
    {
        glm::vec3 mPosition;
        glm::vec4 mColor;
        glm::vec2 mTexCoord;

        float mTexIndex;
        float mTilingFactor;
    };

    struct Renderer2DData
    {
        const uint32_t MaxQuads = 10000;
        const uint32_t MaxVertices = MaxQuads * 4;
        const uint32_t MaxIndices = MaxQuads * 6;
        static const uint32_t MaxTextureSlots = 32;

        Ref<VertexArray> mpQuadVertexArray;
        Ref<VertexBuffer> mpQuadVertexBuffer;
        Ref<Shader> mpTextureShader;
        Ref<Texture2D> mpWhiteTexture;

        uint32_t QuadIndexCount = 0;
        QuadVertex* QuadVertexBufferBase = nullptr;
        QuadVertex* QuadVertexBufferPtr = nullptr;

        std::array<Ref<Texture2D>, MaxTextureSlots> TextureSlots;
        
        // 0 - white texture
        uint32_t TextureSlotIndex = 1;
    };

    static Renderer2DData sData;

    void Renderer2D::Init()
    {
        ORG_PROFILE_FUNCTION();

        sData.mpQuadVertexArray = VertexArray::Create();
        sData.mpQuadVertexBuffer = VertexBuffer::Create(sData.MaxVertices * sizeof(QuadVertex));

        sData.mpQuadVertexBuffer->SetLayout({
            {EShaderDataType::Float3, "a_Postion"},
            {EShaderDataType::Float4, "a_Color"},
            {EShaderDataType::Float2, "a_TexCoord"},
            {EShaderDataType::Float, "a_TexIndex"},
            {EShaderDataType::Float, "a_TilingFactor"}
            });
        
        sData.mpQuadVertexArray->AddVertexBuffer(sData.mpQuadVertexBuffer);

        sData.QuadVertexBufferBase = new QuadVertex[sData.MaxVertices];
        
        std::vector<uint32_t> quadIndices(sData.MaxIndices);
        uint32_t offset = 0;
        for (uint32_t i = 0; i < sData.MaxIndices; i += 6)
        {
            quadIndices[i + 0] = offset + 0;
            quadIndices[i + 1] = offset + 1;
            quadIndices[i + 2] = offset + 2;
            quadIndices[i + 3] = offset + 2;
            quadIndices[i + 4] = offset + 3;
            quadIndices[i + 5] = offset + 0;
            offset += 4;
        }

        Ref<IndexBuffer> quadIB = IndexBuffer::Create(quadIndices.data(), sData.MaxIndices);
        sData.mpQuadVertexArray->SetIndexBuffer(quadIB);

        sData.mpWhiteTexture = Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff;
        sData.mpWhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

        int32_t samplers[sData.MaxTextureSlots];
        for (uint32_t i = 0; i < sData.MaxTextureSlots; i++)
        {
            samplers[i] = i;
        }

        sData.mpTextureShader = Shader::Create(R"(../../Resource/Shaders/Texture.glsl)");
        sData.mpTextureShader->Bind();
        sData.mpTextureShader->SetIntArray("u_Textures", samplers, sData.MaxTextureSlots);
        sData.TextureSlots[0] = sData.mpWhiteTexture;
    }

    void Renderer2D::Shutdown()
    {

    }

    void Renderer2D::BeginScene(const Ref<OrthographicCamera>& camera)
    {
        ORG_PROFILE_FUNCTION();

        sData.mpTextureShader->Bind();
        sData.mpTextureShader->SetMat4("u_ViewProjection", camera->GetViewProjectionMatrix());

        sData.QuadIndexCount = 0;
        sData.QuadVertexBufferPtr = sData.QuadVertexBufferBase;
        sData.TextureSlotIndex = 1;
    }

    void Renderer2D::EndScene()
    {
        ORG_PROFILE_FUNCTION();

        uint32_t dataSize = (uint8_t*)sData.QuadVertexBufferPtr - (uint8_t*)sData.QuadVertexBufferBase;
        sData.mpQuadVertexBuffer->SetData(sData.QuadVertexBufferBase, dataSize);

        Flush();
    }

    void Renderer2D::Flush()
    {
        ORG_PROFILE_FUNCTION();

        for (uint32_t i = 0; i < sData.TextureSlotIndex; i++)
        {
            sData.TextureSlots[i]->Bind(i);
        }
        RenderCommand::DrawIndexed(sData.mpQuadVertexArray, sData.QuadIndexCount);
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
    {
        DrawQuad({ position.x, position.y, 0.0f }, size, color);
    }

    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color)
    {
        ORG_PROFILE_FUNCTION();

        const float textureIndex = 0.0f;
        const float tilingFactor = 1.0f;

        CreateQuad(position, size, color, textureIndex, tilingFactor);
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
    {
        DrawQuad({ position.x, position.y, 0.0f }, size, texture, tilingFactor, tintColor);
    }

    void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
    {
        ORG_PROFILE_FUNCTION();

        constexpr glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};

        float textureIndex = 0.0f;
        for (uint32_t i = 1; i < sData.TextureSlotIndex; i++)
        {
            if (*sData.TextureSlots[i] == *texture)
            {
                textureIndex = (float)i;
                break;
            }
        }

        if (textureIndex == 0.0f)
        {
            textureIndex = (float)sData.TextureSlotIndex;
            sData.TextureSlots[sData.TextureSlotIndex] = texture;
            sData.TextureSlotIndex++;
        }

        CreateQuad(position, size, color, textureIndex, tilingFactor);

#ifdef Old_Renderer
        sData.mpTextureShader->SetFloat4("u_Color", tintColor);
        sData.mpTextureShader->SetFloat("u_TilingFactor", tilingFactor);
        texture->Bind();
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        sData.mpTextureShader->SetMat4("u_Transform", transform);

        sData.mpQuadVertexArray->Bind();
        RenderCommand::DrawIndexed(sData.mpQuadVertexArray);
#endif // Old_Renderer
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color)
    {
        DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, color);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color)
    {
        ORG_PROFILE_FUNCTION();
        sData.mpTextureShader->SetFloat4("u_Color", color);
        sData.mpTextureShader->SetFloat("u_TilingFactor", 1.0f);

        sData.mpWhiteTexture->Bind();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * 
            glm::rotate(glm::mat4(1.0f), rotation, { 0.0f, 0.0f, 1.0f }) * 
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        sData.mpTextureShader->SetMat4("u_Transform", transform);

        sData.mpQuadVertexArray->Bind();
        RenderCommand::DrawIndexed(sData.mpQuadVertexArray);

    }

    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
    {
        DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, texture, tilingFactor, tintColor);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
    {
        ORG_PROFILE_FUNCTION();

        sData.mpTextureShader->SetFloat4("u_Color", tintColor);
        sData.mpTextureShader->SetFloat("u_TilingFactor", tilingFactor);
        texture->Bind();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
            glm::rotate(glm::mat4(1.0f), rotation, { 0.0f, 0.0f, 1.0f }) *
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        sData.mpTextureShader->SetMat4("u_Transform", transform);

        sData.mpQuadVertexArray->Bind();
        RenderCommand::DrawIndexed(sData.mpQuadVertexArray);
    }

    void Renderer2D::CreateQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, float textureIndex, float tilingFactor)
    {
        sData.QuadVertexBufferPtr->mPosition = position;
        sData.QuadVertexBufferPtr->mColor = color;
        sData.QuadVertexBufferPtr->mTexCoord = { 0.0f, 0.0f };
        sData.QuadVertexBufferPtr->mTexIndex = textureIndex;
        sData.QuadVertexBufferPtr->mTilingFactor = tilingFactor;
        sData.QuadVertexBufferPtr++;

        sData.QuadVertexBufferPtr->mPosition = { position.x + size.x, position.y, 0.0f };
        sData.QuadVertexBufferPtr->mColor = color;
        sData.QuadVertexBufferPtr->mTexCoord = { 1.0f, 0.0f };
        sData.QuadVertexBufferPtr->mTexIndex = textureIndex;
        sData.QuadVertexBufferPtr->mTilingFactor = tilingFactor;
        sData.QuadVertexBufferPtr++;

        sData.QuadVertexBufferPtr->mPosition = { position.x + size.x, position.y + size.y, 0.0f };
        sData.QuadVertexBufferPtr->mColor = color;
        sData.QuadVertexBufferPtr->mTexCoord = { 1.0f, 1.0f };
        sData.QuadVertexBufferPtr->mTexIndex = textureIndex;
        sData.QuadVertexBufferPtr->mTilingFactor = tilingFactor;
        sData.QuadVertexBufferPtr++;

        sData.QuadVertexBufferPtr->mPosition = { position.x, position.y + size.y, 0.0f };
        sData.QuadVertexBufferPtr->mColor = color;
        sData.QuadVertexBufferPtr->mTexCoord = { 0.0f, 1.0f };
        sData.QuadVertexBufferPtr->mTexIndex = textureIndex;
        sData.QuadVertexBufferPtr->mTilingFactor = tilingFactor;
        sData.QuadVertexBufferPtr++;
        sData.QuadIndexCount += 6;
    }

}