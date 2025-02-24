#include "EditorLayer.h"
#include "imgui/imgui.h"
namespace Orange
{
    EditorLayer::EditorLayer() : Layer("EditorLayer"),
        mCameraControler(1280.0f / 720.0)
    {

    }

    void EditorLayer::OnAttach()
    {
        ORG_PROFILE_FUNCTION();

        mpCheckerboardTexture = Orange::Texture2D::Create(R"(..\..\Resource\Textures\Checkerboard.png)");

        Orange::FrameBufferSpecification fbSpec;
        fbSpec.width = 1280;
        fbSpec.height = 720;
        mpFrameBuffer = Orange::FrameBuffer::Create(fbSpec);

        mpActiveScene = CreateRef<Scene>();

        // Create a square entity
        auto squareEntity = mpActiveScene->CreateEntity("Green Square");
        squareEntity.AddComponent<SpriteRendererComponent>(glm::vec4{ 0.0f, 1.0f, 0.0f, 1.0f });

        // Create a red square entity
        auto redSquare = mpActiveScene->CreateEntity("Red Square");
        redSquare.AddComponent<SpriteRendererComponent>(glm::vec4{ 1.0f, 0.0f, 0.0f, 1.0f });

        mSquareEntity = squareEntity;
        mCameraEntity = mpActiveScene->CreateEntity("CameraA");
        mCameraEntity.AddComponent<CameraComponent>();
        mSecondCamera = mpActiveScene->CreateEntity("CameraB");
        auto& cc = mSecondCamera.AddComponent<CameraComponent>();
        cc.Primary = false;

        class CameraController : public ScriptableEntity
        {
        public:
            virtual void OnCreate() override
            {
                auto& translation = GetComponent<TransformComponent>().Translation;
                translation.x = rand() % 10 - 5.0f;
            }

            virtual void OnDestroy() override
            {
            }

            virtual void OnUpdate(Timestep ts) override
            {
                auto& translation = GetComponent<TransformComponent>().Translation;
                float speed = 5.0f;

                if (Input::IsKeyPressed(ORG_KEY_A))
                    translation.x += speed * ts;
                if (Input::IsKeyPressed(ORG_KEY_D))
                    translation.x -= speed * ts;
                if (Input::IsKeyPressed(ORG_KEY_W))
                    translation.y -= speed * ts;
                if (Input::IsKeyPressed(ORG_KEY_S))
                    translation.y += speed * ts;
            }
        };

        mCameraEntity.AddComponent<NativeScriptComponent>().Bind<CameraController>();
        mSecondCamera.AddComponent<NativeScriptComponent>().Bind<CameraController>();

        mSceneHierachyPanel.SetContext(mpActiveScene);
    }

    void EditorLayer::OnDetach()
    {
        ORG_PROFILE_FUNCTION();
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        ORG_PROFILE_FUNCTION();

        //ORG_PROFILE_SCOPE("CameraController::OnUpdate");

        if (Orange::FrameBufferSpecification spec = mpFrameBuffer->GetSpecification();
            mViewportSize.x > 0 && mViewportSize.y > 0 &&
            (spec.width != mViewportSize.x || spec.height != mViewportSize.y))
        {
            mpFrameBuffer->Resize(mViewportSize.x, mViewportSize.y);
            mCameraControler.OnResize(mViewportSize.x, mViewportSize.y);
            mpActiveScene->OnViewportResize(mViewportSize.x, mViewportSize.y);
        }
        if (mViewportFocused)
        {
            mCameraControler.OnUpdate(ts);
        }


        Orange::Renderer2D::ResetStats();

        mpFrameBuffer->Bind();
        // Render
        {
            ORG_PROFILE_SCOPE("Render2D Preparation");
            Orange::RenderCommand::SetClearColor({ 0.2f, 0.3f, 0.3f, 1.0f });
            Orange::RenderCommand::Clear();

            mpActiveScene->OnUpdate(ts);
        }

        mpFrameBuffer->Unbind();
    }

    void EditorLayer::OnImGuiRender()
    {
        // Note: Switch this to true to enable dockspace
        static bool dockspaceOpen = true;
        static bool opt_fullscreen_persistant = true;
        bool opt_fullscreen = opt_fullscreen_persistant;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
        // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
        // because it would be confusing to have two docking targets within each others.
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if (opt_fullscreen)
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }
        // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background and handle the pass-thru hole, so we ask Begin() to not render a background.
        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            window_flags |= ImGuiWindowFlags_NoBackground;
        // Important: note that we proceed even if Begin() returns false (aka window is collapsed).
        // This is because we want to keep our DockSpace() active. If a DockSpace() is inactive, 
        // all active windows docked into it will lose their parent and become undocked.
        // We cannot preserve the docking relationship between an active window and an inactive docking, otherwise 
        // any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpace Demo", &dockspaceOpen, window_flags);
        ImGui::PopStyleVar();
        if (opt_fullscreen)
            ImGui::PopStyleVar(2);
        // DockSpace
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                // Disabling fullscreen would allow the window to be moved to the front of other windows, 
                // which we can't undo at the moment without finer window depth/z control.
                //ImGui::MenuItem("Fullscreen", NULL, &opt_fullscreen_persistant);
                if (ImGui::MenuItem("Exit"))
                {
                    Orange::Application::GetInstance()->Close();
                };
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        mSceneHierachyPanel.OnImGuiRender();

        ImGui::Begin("Settings");
        auto stats = Orange::Renderer2D::GetStats();
        ImGui::Text("Renderer2D Stats:");
        ImGui::Text("Draw Calls: %d", stats.DrawCalls);
        ImGui::Text("Quads: %d", stats.QuadCount);
        ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
        ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

        ImGui::End();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
        ImGui::Begin("Viewport");
        mViewportFocused = ImGui::IsWindowFocused();
        mViewportHovered = ImGui::IsWindowHovered();
        Orange::Application::GetInstance()->GetImGuiLayer()->BlockEvents(!mViewportFocused || !mViewportHovered);
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        mViewportSize = { viewportPanelSize.x, viewportPanelSize.y };
    
        uint32_t textureID = mpFrameBuffer->GetColorAttachmentRendererID();
        ImGui::Image(textureID, ImVec2{ mViewportSize.x, mViewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::End();
    }

    void EditorLayer::OnEvent(Event& event)
    {
        mCameraControler.OnEvent(event);
    }
}