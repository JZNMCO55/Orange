#ifndef EDITOR_LAYER_H
#define EDITOR_LAYER_H

#include "OrangeExport.h"
#include "Orange.h"
#include "Panels/SceneHierachyPanel.h"
#include "Panels/ContentBrowserPanel.h"

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
        bool OnKeyPressed(KeyPressedEvent& e);
        bool OnMouseButtonPressed(MouseButtonPressedEvent& e);

        void NewScene();
        void OpenScene();
        void SaveSceneAs();

    private:
        OrthographicCameraControler mCameraControler;

        // temp
        Ref<VertexArray> mSquareVA;
        Ref<Shader> mFlatColorShader;
        Ref<FrameBuffer> mpFrameBuffer;

        Ref<Scene> mpActiveScene;

        bool mPrimaryCamera = true;
        Ref<EditorCamera> mpEditorCamera;

        Ref<Texture2D> mpCheckerboardTexture;

        Entity mHoveredEntity;

        bool mViewportFocused = false;
        bool mViewportHovered = false;
        glm::vec2 mViewportSize = { 0.0f, 0.0f };
        glm::vec2 mViewportBounds[2];
        glm::vec4 mSquareColor = { 0.2f, 0.3f, 0.8f, 1.0f };

        int mGizmoType = -1;

        SceneHierachyPanel mSceneHierachyPanel;
        ContentBrowserPannel mContentBrowserPannel;
    };
}

#endif // EDITOR_LAYER_H