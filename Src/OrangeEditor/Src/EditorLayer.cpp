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

        auto squareEntity = mpActiveScene->CreateEntity();
        mpActiveScene->GetRegistry().emplace<TransformComponent>(squareEntity);
        mpActiveScene->GetRegistry().emplace<SpriteRendererComponent>(squareEntity, glm::vec4{ 0.0f, 1.0f, 0.0f, 1.0f });

        mSquareEntity = squareEntity;
    }

    void EditorLayer::OnDetach()
    {
        ORG_PROFILE_FUNCTION();
    }

    void EditorLayer::OnUpdate(Timestep ts)
    {
        ORG_PROFILE_FUNCTION();

        //ORG_PROFILE_SCOPE("CameraController::OnUpdate");

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

            Orange::Renderer2D::BeginScene(mCameraControler.GetCamera()); 
            mpActiveScene->OnUpdate(ts);
            Orange::Renderer2D::EndScene();
        }


        //{
        //    ORG_PROFILE_SCOPE("Renderer2D Draw");
        //    Orange::Renderer2D::BeginScene(mCameraControler.GetCamera());

        //    Orange::Renderer2D::DrawQuad({ -1.0f, 0.0f }, { 0.8f, 0.8f }, { 0.8f, 0.2f, 0.3f, 1.0f });
        //    Orange::Renderer2D::DrawQuad({ 0.5f, -0.5f }, { 0.5f, 0.75f }, mSquareColor);
        //    Orange::Renderer2D::DrawQuad({ -5.f, -5.f, -0.1f }, { 20.0f, 20.0f }, mpCheckerboardTexture, 10.0f);
        //    Orange::Renderer2D::DrawRotatedQuad({ 0.0f, 0.0f, 0.1f }, { 1.0f, 1.0f }, 45.0f, mSquareColor);

        //    Orange::Renderer2D::EndScene();

        //    Orange::Renderer2D::BeginScene(mCameraControler.GetCamera());
        //    for (float y = -5.0f; y < 5.0f; y += 0.05f)
        //    {
        //        for (float x = -5.0f; x < 5.0f; x += 0.05f)
        //        {
        //            glm::vec4 color = { (x + 5.0f) / 10.0f, 0.4f, (y + 5.0f) / 10.0f, 0.7f };
        //            Orange::Renderer2D::DrawQuad({ x, y }, { 0.45f, 0.45f }, color);
        //        }
        //    }
        //    Orange::Renderer2D::EndScene();
        //}
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
        ImGui::Begin("Settings");
        auto stats = Orange::Renderer2D::GetStats();
        ImGui::Text("Renderer2D Stats:");
        ImGui::Text("Draw Calls: %d", stats.DrawCalls);
        ImGui::Text("Quads: %d", stats.QuadCount);
        ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
        ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

        auto& squreColor = mpActiveScene->GetRegistry().get<SpriteRendererComponent>(mSquareEntity).Color;
        ImGui::ColorEdit4("Square Color", glm::value_ptr(squreColor));
        ImGui::End();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
        ImGui::Begin("Viewport");
        mViewportFocused = ImGui::IsWindowFocused();
        mViewportHovered = ImGui::IsWindowHovered();
        Orange::Application::GetInstance()->GetImGuiLayer()->BlockEvents(!mViewportFocused || !mViewportHovered);
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
    

        if ((mViewportSize != *((glm::vec2*)&viewportPanelSize)) && viewportPanelSize.x > 0 && viewportPanelSize.y > 0)
        {
            mpFrameBuffer->Resize((uint32_t)viewportPanelSize.x, (uint32_t)viewportPanelSize.y);
            mViewportSize = { viewportPanelSize.x, viewportPanelSize.y };
            mCameraControler.OnResize(viewportPanelSize.x, viewportPanelSize.y);
        }

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