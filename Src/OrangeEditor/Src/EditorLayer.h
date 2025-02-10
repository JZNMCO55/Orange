#ifndef EDITOR_LAYER_H
#define EDITOR_LAYER_H

#include "OrangeExport.h"
#include "Orange.h"

namespace Orange
{
    class ORANGE_API EditorLayer : public Layer
    {
    public:
        EditorLayer();
        virtual ~EditorLayer() = default;

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnUpdate(Timestep ts) override;
        virtual void OnImGuiRender() override;
        virtual void OnEvent(Event& event) override;

    private:
        OrthographicCameraControler mCameraControler;

        // temp
        Ref<VertexArray> mSquareVA;
        Ref<Shader> mFlatColorShader;
        Ref<FrameBuffer> mpFrameBuffer;

        Ref<Texture2D> mpCheckerboardTexture;

        bool mViewportFocused = false;
        bool mViewportHovered = false;
        glm::vec2 mViewportSize = { 0.0f, 0.0f };
        glm::vec4 mSquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };
    };
}

#endif // EDITOR_LAYER_H