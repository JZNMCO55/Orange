#ifndef RENDERER_2D_H
#define RENDERER_2D_H

#include "OrangeExport.h"

namespace Orange
{
    class OrthographicCamera;
    class Texture2D;
    class ORANGE_API Renderer2D
    {
    public:
        struct Statistics
        {
            uint32_t DrawCalls = 0;
            uint32_t QuadCount = 0;

            uint32_t GetTotalVertexCount() const { return QuadCount * 4; }
            uint32_t GetTotalIndexCount() const { return QuadCount * 6; }
        };
    public:
        static void Init();

        static void Shutdown();

        static void BeginScene(const Ref<OrthographicCamera>& camera);

        static void EndScene();

        static void Flush();

        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);

        static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);

        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& texture,
            float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));

        static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture,
            float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));

        static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size,
            float rotation, const glm::vec4& color);

        static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, 
            float rotation, const glm::vec4& color);
        static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation,
            const Ref<Texture2D>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));

        static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation,
            const Ref<Texture2D>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));

        static void ResetStats();

        static Statistics GetStats();
    private:
        // ToDo: Combind all the draw functions into one function to reduce function calls and improve performance
        static void CreateQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color,float textureIndex, float tilingFactor);

        static void CreateRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color,float textureIndex, float tilingFactor);


        static void FlushAndReset();
    };
}

#endif // RENDERER_2D_H