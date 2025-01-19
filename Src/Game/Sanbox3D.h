#ifndef SANDBOX_2D_H
#define SANDBOX_2D_H

#include "Orange.h"

class Sandbox3D : public Orange::Layer
{
public:
    Sandbox3D();
    ~Sandbox3D();

    void OnAttach() override;
    void OnDetach() override;

    void OnUpdate(Orange::Timestep ts) override;
    void OnImGuiRender() override;
    void OnEvent(Orange::Event& event) override;

private:
    Orange::Ref<Orange::OrthographicCameraControler> mpCameraController;

    Orange::Ref<Orange::VertexArray> mpVertexArray;
    Orange::Ref<Orange::Shader> mpFlatShader;
    Orange::Ref<Orange::Texture2D> mpCheckerboardTexture;
    Orange::Ref<Orange::Mesh> mpMesh;
    Orange::Ref<Orange::RendererObject> mpRendererObject;
    glm::vec4 mSquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };
};

#endif //SANDBOX_2D_H