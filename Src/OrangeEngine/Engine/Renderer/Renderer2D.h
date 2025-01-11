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
        static void Init();

        static void Shutdown();

        static void BeginScene(const Ref<OrthographicCamera>& camera);

        static void EndScene();

        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);

        static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);

        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& texture);
        static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture);
    };
}

#endif // RENDERER_2D_H