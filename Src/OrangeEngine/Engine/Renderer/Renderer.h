#ifndef RENDERER_H
#define RENDERER_H

#include "OrangeExport.h"
#include "RenderCommand.h"
namespace Orange
{
    class OrthographicCamera;
    class Shader;
    class ORANGE_API Renderer
    {
    public:
        static void Init();
        static void BeginScene(std::shared_ptr<OrthographicCamera>& camera);
        static void EndScene();

        static void Submit(const std::shared_ptr<Shader>& shader,
            const std::shared_ptr<VertexArray>& vertexArray ,
            const glm::mat4& transform = glm::mat4(1.0f));

        inline static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }

    private:
        struct SceneData
        {
            glm::mat4 ViewProjectionMatrix;
        };

        static SceneData* mpSceneData;
    };
}

#endif // RENDERER_H