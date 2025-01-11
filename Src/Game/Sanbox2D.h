#ifndef SANDBOX_2D_H
#define SANDBOX_2D_H

#include "Orange.h"

class Sandbox2D : public Orange::Layer
{
public:
    Sandbox2D();
    ~Sandbox2D();

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
    glm::vec4 mSquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };
};

#endif //SANDBOX_2D_H