#include "pch.h"
#include "OrthographicCamera.h"
#include "VertexArray.h"
#include "Shader.h"
#include "RenderCommand.h"
#include "Texture.h"
#include "Camera.h"
#include "EditorCamera.h"
#include "OpenGL/OpenGLShader.h"
#include "Scene/Components.h"
#include "Renderer2D.h"
#include "UniformBuffer.h"

namespace Orange
{
    struct QuadVertex
    {
        glm::vec3 mPosition;
        glm::vec4 mColor;
        glm::vec2 mTexCoord;

        float mTexIndex;
        float mTilingFactor;

        int mEntityID;
    };

    struct CirclVertex
    {
        glm::vec3 WorldPosition;
        glm::vec3 LocalPosition;
        glm::vec4 Color;
        float Thickness;
        float Fade;

        // Editor-only
        int EntityID;
    };

    struct LineVertex
    {
        glm::vec3 Position;
        glm::vec4 Color;

        // Editor-only
        int EntityID;
    };

    struct Renderer2DData
    {
        static const uint32_t MaxQuads = 10000;
        static const uint32_t MaxVertices = MaxQuads * 4;
        static const uint32_t MaxIndices = MaxQuads * 6;
        static const uint32_t MaxTextureSlots = 32;

        // Quad data
        Ref<VertexArray> mpQuadVertexArray;
        Ref<VertexBuffer> mpQuadVertexBuffer;
        Ref<Shader> mpQuadShader;
        Ref<Texture2D> mpWhiteTexture;

        uint32_t QuadIndexCount = 0;
        QuadVertex* QuadVertexBufferBase = nullptr;
        QuadVertex* QuadVertexBufferPtr = nullptr;

        // Circle data
        Ref<VertexArray> mpCircleVertexArray;
        Ref<VertexBuffer> mpCircleVertexBuffer;
        Ref<Shader> mpCircleShader;

        uint32_t CircleIndexCount = 0;
        CirclVertex* CircleVertexBufferBase = nullptr;
        CirclVertex* CircleVertexBufferPtr = nullptr;

        // Line data
        Ref<VertexArray> mpLineVertexArray;
        Ref<VertexBuffer> mpLineVertexBuffer;
        Ref<Shader> mpLineShader;

        uint32_t LineIndexCount = 0;
        float LineWidth = 2.0f;
        LineVertex* LineVertexBufferBase = nullptr;
        LineVertex* LineVertexBufferPtr = nullptr;

        std::array<Ref<Texture2D>, MaxTextureSlots> TextureSlots;
        
        // 0 - white texture
        uint32_t TextureSlotIndex = 1;

        glm::vec4 QuadVertexPositions[4];

        Renderer2D::Statistics Stats;

        struct CameraData
        {
            glm::mat4 ViewProjectionMatrix;
        };

        CameraData CameraBuffer;
        Ref<UniformBuffer> CameraUniformBuffer;
    };

    static Renderer2DData sData;

    void Renderer2D::Init()
    {
        ORG_PROFILE_FUNCTION();

        // Quad data
        sData.mpQuadVertexArray = VertexArray::Create();
        sData.mpQuadVertexBuffer = VertexBuffer::Create(sData.MaxVertices * sizeof(QuadVertex));

        sData.mpQuadVertexBuffer->SetLayout({
            {EShaderDataType::Float3, "a_Postion"},
            {EShaderDataType::Float4, "a_Color"},
            {EShaderDataType::Float2, "a_TexCoord"},
            {EShaderDataType::Float, "a_TexIndex"},
            {EShaderDataType::Float, "a_TilingFactor"},
            {EShaderDataType::Int, "a_EntityID"}
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

        // Circle data
        sData.mpCircleVertexArray = VertexArray::Create();
        sData.mpCircleVertexBuffer = VertexBuffer::Create(sData.MaxVertices * sizeof(CirclVertex));

        sData.mpCircleVertexBuffer->SetLayout({
            {EShaderDataType::Float3, "a_WorldPosition"},
            {EShaderDataType::Float3, "a_LocalPosition"},
            {EShaderDataType::Float4, "a_Color"},
            {EShaderDataType::Float, "a_Thickness"},
            {EShaderDataType::Float, "a_Fade"},
            {EShaderDataType::Int, "a_EntityID"}
            });

        sData.mpCircleVertexArray->AddVertexBuffer(sData.mpCircleVertexBuffer);
        sData.mpCircleVertexArray->SetIndexBuffer(quadIB); // Use the same index buffer as the quad
        sData.CircleVertexBufferBase = new CirclVertex[sData.MaxVertices];

        // Line data
        sData.mpLineVertexArray = VertexArray::Create();
        sData.mpLineVertexBuffer = VertexBuffer::Create(sData.MaxVertices * sizeof(LineVertex));
        sData.mpLineVertexBuffer->SetLayout({
            {EShaderDataType::Float3, "a_Position"},
            {EShaderDataType::Float4, "a_Color"},
            {EShaderDataType::Int, "a_EntityID"}
            });
        sData.mpLineVertexArray->AddVertexBuffer(sData.mpLineVertexBuffer);
        sData.LineVertexBufferBase = new LineVertex[sData.MaxVertices];

        sData.mpWhiteTexture = Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff;
        sData.mpWhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

        int32_t samplers[sData.MaxTextureSlots];
        for (uint32_t i = 0; i < sData.MaxTextureSlots; i++)
        {
            samplers[i] = i;
        }

        sData.mpQuadShader = Shader::Create(R"(../../Resource/Shaders/Renderer2D_Quad.glsl)");
        sData.mpCircleShader = Shader::Create(R"(../../Resource/Shaders/Renderer2D_Circle.glsl)");
        sData.mpLineShader = Shader::Create(R"(../../Resource/Shaders/Renderer2D_Line.glsl)");

        sData.TextureSlots[0] = sData.mpWhiteTexture;

        sData.QuadVertexPositions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
        sData.QuadVertexPositions[1] = {  0.5f, -0.5f, 0.0f, 1.0f };
        sData.QuadVertexPositions[2] = {  0.5f,  0.5f, 0.0f, 1.0f };
        sData.QuadVertexPositions[3] = { -0.5f,  0.5f, 0.0f, 1.0f };

        sData.CameraUniformBuffer = UniformBuffer::Create(sizeof(Renderer2DData::CameraData), 0);
    }

    void Renderer2D::Shutdown()
    {

    }

    void Renderer2D::BeginScene(const Ref<Camera>& camera, const glm::mat4& transform)
    {
        ORG_PROFILE_FUNCTION();

        sData.CameraBuffer.ViewProjectionMatrix = camera->GetProjectionMatrix() * glm::inverse(transform);
        sData.CameraUniformBuffer->SetData(&sData.CameraBuffer, sizeof(Renderer2DData::CameraData));

        StartBatch();
    }

    void Renderer2D::BeginScene(const Ref<OrthographicCamera>& camera)
    {
        ORG_PROFILE_FUNCTION();

        sData.mpQuadShader->Bind();
        sData.mpQuadShader->SetMat4("u_ViewProjection", camera->GetViewProjectionMatrix());

        StartBatch();
    }

    void Renderer2D::BeginScene(const Ref<EditorCamera>& camera)
    {
        ORG_PROFILE_FUNCTION();

        sData.CameraBuffer.ViewProjectionMatrix = camera->GetViewProjection();
        sData.CameraUniformBuffer->SetData(&sData.CameraBuffer, sizeof(Renderer2DData::CameraData));

        StartBatch();
    }

    void Renderer2D::EndScene()
    {
        ORG_PROFILE_FUNCTION();

        Flush();
    }

    void Renderer2D::Flush()
    {
        ORG_PROFILE_FUNCTION();

        if (sData.QuadIndexCount)
        {
            uint32_t dataSize = (uint8_t*)sData.QuadVertexBufferPtr - (uint8_t*)sData.QuadVertexBufferBase;
            sData.mpQuadVertexBuffer->SetData(sData.QuadVertexBufferBase, dataSize);

            // Bind textures

            for (uint32_t i = 0; i < sData.TextureSlotIndex; i++)
            {
                sData.TextureSlots[i]->Bind(i);
            }
            sData.mpQuadShader->Bind();
            RenderCommand::DrawIndexed(sData.mpQuadVertexArray, sData.QuadIndexCount);
            sData.Stats.DrawCalls++;
        }

        if (sData.CircleIndexCount)
        {
            uint32_t dataSize = (uint32_t)((uint8_t*)sData.CircleVertexBufferPtr - (uint8_t*)sData.CircleVertexBufferBase);
            sData.mpCircleVertexBuffer->SetData(sData.CircleVertexBufferBase, dataSize);

            sData.mpCircleShader->Bind();
            RenderCommand::DrawIndexed(sData.mpCircleVertexArray, sData.CircleIndexCount);
            sData.Stats.DrawCalls++;
        }

        if (sData.LineIndexCount)
        {
            uint32_t dataSize = (uint32_t)((uint8_t*)sData.LineVertexBufferPtr - (uint8_t*)sData.LineVertexBufferBase);
            sData.mpLineVertexBuffer->SetData(sData.LineVertexBufferBase, dataSize);

            sData.mpLineShader->Bind();
            RenderCommand::SetLineWidth(sData.LineWidth);
            RenderCommand::DrawLines(sData.mpLineVertexArray, sData.LineIndexCount);
            sData.Stats.DrawCalls++;
        }

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
        const glm::vec2 textureCoords[] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f} };

        //CreateQuad(position, size, color, textureIndex, tilingFactor);

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        CreateQuad(transform, color, textureIndex, tilingFactor, textureCoords);
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
    {
        auto transform = glm::translate(glm::mat4(1.0f), { position.x, position.y, 0.0f }) * 
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });
        DrawQuad(transform, texture, tilingFactor, tintColor);
    }

    void Renderer2D::DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
    {
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

        const glm::vec2 textureCoords[] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f} };

        CreateQuad(transform, tintColor, textureIndex, tilingFactor, textureCoords);
        // Old Renderer
        //CreateQuad(position, size, tintColor, textureIndex, tilingFactor);

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

    void Renderer2D::DrawQuad(const glm::mat4& transform, const glm::vec4& color, int entityId)
    {
        ORG_PROFILE_FUNCTION();

        const float textureIndex = 0.0f;
        const float tilingFactor = 1.0f;
        const glm::vec2 textureCoords[] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f} };


        CreateQuad(transform, color, textureIndex, tilingFactor, textureCoords, entityId);
    }
    

    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color)
    {
        DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, color);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color)
    {
        //CreateRotatedQuad(position, size, rotation, color, 0.0f, 1.0f);
        const float textureIndex = 0.0f;
        const float tilingFactor = 1.0f;
        const glm::vec2 textureCoords[] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f} };

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
            glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 0.0f, 0.0f, 1.0f }) *
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });  

        CreateQuad(transform, color, textureIndex, tilingFactor, textureCoords);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
    {
        DrawRotatedQuad({ position.x, position.y, 0.0f }, size, rotation, texture, tilingFactor, tintColor);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor)
    {
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

        //CreateRotatedQuad(position, size, rotation, tintColor, textureIndex, tilingFactor);
        const glm::vec2 textureCoords[] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f} };

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
            glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 0.0f, 0.0f, 1.0f }) *
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        CreateQuad(transform, tintColor, textureIndex, tilingFactor, textureCoords);
    }
    void Renderer2D::DrawSprite(const glm::mat4& transform, SpriteRendererComponent& spriteRendererComponent, int entityId)
    {
        ORG_PROFILE_FUNCTION();
        if (spriteRendererComponent.Texture)
        {
            DrawQuad(transform, spriteRendererComponent.Texture,
                spriteRendererComponent.TilingFactor, spriteRendererComponent.Color);
        }
        DrawQuad(transform, spriteRendererComponent.Color, entityId);
    }

    void Renderer2D::DrawCircle(const glm::mat4& transform, const glm::vec4& color, float thickness, float fade, int entityID)
    {
        ORG_PROFILE_FUNCTION();

        for (size_t i = 0; i < 4; i++)
        {
            sData.CircleVertexBufferPtr->WorldPosition = transform * sData.QuadVertexPositions[i];
            sData.CircleVertexBufferPtr->LocalPosition = sData.QuadVertexPositions[i] * 2.0f;
            sData.CircleVertexBufferPtr->Color = color;
            sData.CircleVertexBufferPtr->Thickness = thickness;
            sData.CircleVertexBufferPtr->Fade = fade;
            sData.CircleVertexBufferPtr->EntityID = entityID;
            sData.CircleVertexBufferPtr++;
        }

        sData.CircleIndexCount += 6;
        sData.Stats.QuadCount++;
    }

    void Renderer2D::DrawLine(const glm::vec3& p0, const glm::vec3& p1, const glm::vec4& color, int entityID)
    {
        ORG_PROFILE_FUNCTION();

        sData.LineVertexBufferPtr->Position = p0;
        sData.LineVertexBufferPtr->Color = color;
        sData.LineVertexBufferPtr->EntityID = entityID;

        sData.LineVertexBufferPtr++;

        sData.LineVertexBufferPtr->Position = p1;
        sData.LineVertexBufferPtr->Color = color;
        sData.LineVertexBufferPtr->EntityID = entityID;

        sData.LineVertexBufferPtr++;

        sData.LineIndexCount += 2;
    }

    void Renderer2D::DrawRect(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color, int entityID)
    {
        ORG_PROFILE_FUNCTION();

        glm::vec3 p0 = glm::vec3(position.x - size.x * 0.5f, position.y - size.y * 0.5f, position.z);
        glm::vec3 p1 = glm::vec3(position.x + size.x * 0.5f, position.y - size.y * 0.5f, position.z);
        glm::vec3 p2 = glm::vec3(position.x + size.x * 0.5f, position.y + size.y * 0.5f, position.z);
        glm::vec3 p3 = glm::vec3(position.x - size.x * 0.5f, position.y + size.y * 0.5f, position.z);

        DrawLine(p0, p1, color);
        DrawLine(p1, p2, color);
        DrawLine(p2, p3, color);
        DrawLine(p3, p0, color);
    }

    void Renderer2D::DrawRect(const glm::mat4& transform, const glm::vec4& color, int entityID)
    {
        ORG_PROFILE_FUNCTION();

        glm::vec3 lineVertices[4];
        for (size_t i = 0; i < 4; i++)
        {
            lineVertices[i] = transform * sData.QuadVertexPositions[i];
        }

        DrawLine(lineVertices[0], lineVertices[1], color);
        DrawLine(lineVertices[1], lineVertices[2], color);
        DrawLine(lineVertices[2], lineVertices[3], color);
        DrawLine(lineVertices[3], lineVertices[0], color);
    }

    void Renderer2D::CreateQuad(const glm::vec3& position, const glm::vec2& size, 
        const glm::vec4& color, float textureIndex, float tilingFactor, int entityId)
    {
        ORG_PROFILE_FUNCTION();

        constexpr size_t quadVertexCount = 4;
        constexpr glm::vec2 textureCoords[] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f} };
        if (sData.QuadIndexCount >= sData.MaxIndices)
        {
            NextBatch();
        }
        for(int i = 0; i < quadVertexCount; i++)
        {
          sData.QuadVertexBufferPtr->mPosition = position;
          sData.QuadVertexBufferPtr->mColor = color;
          sData.QuadVertexBufferPtr->mTexCoord = textureCoords[i];
          sData.QuadVertexBufferPtr->mTexIndex = textureIndex;
          sData.QuadVertexBufferPtr->mTilingFactor = tilingFactor;
          sData.QuadVertexBufferPtr->mEntityID = entityId;
          sData.QuadVertexBufferPtr++;
        }
        sData.QuadIndexCount += 6;

        sData.Stats.QuadCount++;
    }

    void Renderer2D::ResetStats()
    {
        memset(&sData.Stats, 0, sizeof(Statistics));
    }

    Renderer2D::Statistics Renderer2D::GetStats()
    {
        return sData.Stats;
    }

    void Renderer2D::StartBatch()
    {
        sData.QuadIndexCount = 0;
        sData.QuadVertexBufferPtr = sData.QuadVertexBufferBase;

        sData.CircleIndexCount = 0;
        sData.CircleVertexBufferPtr = sData.CircleVertexBufferBase;

        sData.LineIndexCount = 0;
        sData.LineVertexBufferPtr = sData.LineVertexBufferBase;

        sData.TextureSlotIndex = 1;
    }

    void Renderer2D::NextBatch()
    {
        Flush();
        StartBatch();
    }

    void Renderer2D::CreateRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation,
        const glm::vec4& color, float textureIndex, float tilingFactor)
    {
        ORG_PROFILE_FUNCTION();

        if (sData.QuadIndexCount >= sData.MaxIndices)
        {
            NextBatch();
        }
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) *
            glm::rotate(glm::mat4(1.0f), glm::radians(rotation), { 0.0f, 0.0f, 1.0f }) *
            glm::scale(glm::mat4(1.0f), { size.x, size.y, 1.0f });

        sData.QuadVertexBufferPtr->mPosition = transform * sData.QuadVertexPositions[0];
        sData.QuadVertexBufferPtr->mColor = color;
        sData.QuadVertexBufferPtr->mTexCoord = { 0.0f, 0.0f };
        sData.QuadVertexBufferPtr->mTexIndex = textureIndex;
        sData.QuadVertexBufferPtr->mTilingFactor = tilingFactor;
        sData.QuadVertexBufferPtr++;

        sData.QuadVertexBufferPtr->mPosition = transform * sData.QuadVertexPositions[1];
        sData.QuadVertexBufferPtr->mColor = color;
        sData.QuadVertexBufferPtr->mTexCoord = { 1.0f, 0.0f };
        sData.QuadVertexBufferPtr->mTexIndex = textureIndex;
        sData.QuadVertexBufferPtr->mTilingFactor = tilingFactor;
        sData.QuadVertexBufferPtr++;

        sData.QuadVertexBufferPtr->mPosition = transform * sData.QuadVertexPositions[2];
        sData.QuadVertexBufferPtr->mColor = color;
        sData.QuadVertexBufferPtr->mTexCoord = { 1.0f, 1.0f };
        sData.QuadVertexBufferPtr->mTexIndex = textureIndex;
        sData.QuadVertexBufferPtr->mTilingFactor = tilingFactor;
        sData.QuadVertexBufferPtr++;

        sData.QuadVertexBufferPtr->mPosition = transform * sData.QuadVertexPositions[3];
        sData.QuadVertexBufferPtr->mColor = color;
        sData.QuadVertexBufferPtr->mTexCoord = { 0.0f, 1.0f };
        sData.QuadVertexBufferPtr->mTexIndex = textureIndex;
        sData.QuadVertexBufferPtr->mTilingFactor = tilingFactor;
        sData.QuadVertexBufferPtr++;
        sData.QuadIndexCount += 6;

        sData.Stats.QuadCount++;
    }

    void Renderer2D::CreateQuad(const glm::mat4& transform, const glm::vec4& color, float textureIndex, 
        float tilingFactor, const glm::vec2 textureCoords[], int entityId)
    {
      ORG_PROFILE_FUNCTION();
      
      if (sData.QuadIndexCount >= sData.MaxIndices)
      {
          NextBatch();
      }

      constexpr size_t quadVertexCount = 4;
      for(int i = 0; i < quadVertexCount; i++)
      {
        sData.QuadVertexBufferPtr->mPosition = transform * sData.QuadVertexPositions[i];
        sData.QuadVertexBufferPtr->mColor = color;
        sData.QuadVertexBufferPtr->mTexCoord = textureCoords[i];
        sData.QuadVertexBufferPtr->mTexIndex = textureIndex;
        sData.QuadVertexBufferPtr->mTilingFactor = tilingFactor;
        sData.QuadVertexBufferPtr->mEntityID = entityId;
        sData.QuadVertexBufferPtr++;
      }
      sData.QuadIndexCount += 6;

      sData.Stats.QuadCount++;
    } 

}