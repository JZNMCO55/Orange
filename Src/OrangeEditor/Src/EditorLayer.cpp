#include "EditorLayer.h"
#include "imgui/imgui.h"

#include "Scene/SceneSerializer.h"
#include "Utils/PlatformUtils.h"
#include "ImGui/ImGuizmo/ImGuizmo.h"
#include "Math/Math.h"

#define BIND_EDITOR_EVENT_FN(x) std::bind(&EditorLayer::x, this, std::placeholders::_1)

namespace Orange
{
    extern std::filesystem::path gAssetDirectory;
    EditorLayer::EditorLayer() : Layer("EditorLayer"),
        mCameraControler(1280.0f / 720.0)
    {

    }

    void EditorLayer::OnAttach()
    {
        ORG_PROFILE_FUNCTION();

        mpCheckerboardTexture = Orange::Texture2D::Create(R"(..\..\Resource\Textures\Checkerboard.png)");
        mpIconPlay = Orange::Texture2D::Create(R"(..\..\Resource\Icons\PlayButton.png)");
        mpIconStop = Orange::Texture2D::Create(R"(..\..\Resource\Icons\StopButton.png)");

        Orange::FrameBufferSpecification fbSpec;
        fbSpec.width = 1280;
        fbSpec.height = 720;
        fbSpec.attachment = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
        mpFrameBuffer = Orange::FrameBuffer::Create(fbSpec);

        mpEditorScene = CreateRef<Scene>();
        mpActiveScene = mpEditorScene;
        
        auto commandLineArgs = Application::GetInstance()->GetCommandLineArgs();
        if (commandLineArgs.Count > 1)
        {
            auto sceneFilePath = commandLineArgs[1];
            SceneSerializer serializer(mpActiveScene);
            serializer.Deserialize(sceneFilePath);
        }

        mpEditorCamera = CreateRef<EditorCamera>(30.0f, 1.778f, 0.1f, 1000.0f);

        mSceneHierachyPanel.SetContext(mpActiveScene);

        mEditorScenePath = std::filesystem::path();
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
            mpEditorCamera->SetViewportSize(mViewportSize.x, mViewportSize.y);
            mpActiveScene->OnViewportResize(mViewportSize.x, mViewportSize.y);
        }

        // Update
        if (mViewportFocused)
        {
            mCameraControler.OnUpdate(ts);
            mpEditorCamera->OnUpdate(ts);
        }

        Orange::Renderer2D::ResetStats();

        mpFrameBuffer->Bind();
        // Render
        {
            ORG_PROFILE_SCOPE("Render2D Preparation");
            Orange::RenderCommand::SetClearColor({ 0.2f, 0.3f, 0.3f, 1.0f });
            Orange::RenderCommand::Clear();

            // clear the entity ID attachment to -1
            mpFrameBuffer->ClearAttachment(1, -1);

            switch (mSceneState)
            {
            case SceneState::Edit:
            {
                if (mViewportFocused)
                {
                    mCameraControler.OnUpdate(ts);
                }

                mpEditorCamera->OnUpdate(ts);
                mpActiveScene->OnUpdateEditor(ts, mpEditorCamera);
                break;
            }
            case SceneState::Play:
            {
                mpActiveScene->OnUpdateRuntime(ts);
                break;
            }
            default:
                break;
            }

            mpActiveScene->OnUpdateEditor(ts, mpEditorCamera);
            //mpActiveScene->OnUpdateRuntime(ts);
        }

        auto [mx, my] = ImGui::GetMousePos();
        mx -= mViewportBounds[0].x;
        my -= mViewportBounds[0].y;
        glm::vec2 viewportSize = mViewportSize;
        my = viewportSize.y - my;
        int mouseX = (int)mx;
        int mouseY = (int)my;

        if (mouseX >= 0 && mouseY >= 0 && mouseX < (int)viewportSize.x && mouseY < (int)viewportSize.y)
        {
            int pixelData = mpFrameBuffer->ReadPixel(1, mouseX, mouseY);
            if (pixelData != -1)
            {
                mHoveredEntity = Entity((entt::entity)pixelData, mpActiveScene);
            }
            else
            {
                mHoveredEntity = Entity();
            }
        }

        OnOverlayRender();

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
        ImGuiStyle& style = ImGui::GetStyle();
        float minWinSizeX = style.WindowMinSize.x;
        style.WindowMinSize.x = 370.0f;
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }

        style.WindowMinSize.x = minWinSizeX;

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                // Disabling fullscreen would allow the window to be moved to the front of other windows, 
                // which we can't undo at the moment without finer window depth/z control.
                //ImGui::MenuItem("Fullscreen", NULL, &opt_fullscreen_persistant);

                if (ImGui::MenuItem("New", "Ctrl+N"))
                {
                    NewScene();
                }

                if (ImGui::MenuItem("Open...", "Ctrl+O"))
                {
                    OpenScene();
                }

                if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
                {
                    SaveSceneAs();
                }

                if (ImGui::MenuItem("Exit"))
                {
                    Orange::Application::GetInstance()->Close();
                };
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        mSceneHierachyPanel.OnImGuiRender();
        mContentBrowserPannel.OnImGuiRender();

        ImGui::Begin("Statuts");
        std::string name = "None";
        if (mHoveredEntity)
        {
            name = mHoveredEntity.GetComponent<TagComponent>().Tag;
        }
        ImGui::Text("Hovered Entity: %s", name.c_str());
        auto stats = Orange::Renderer2D::GetStats();
        ImGui::Text("Renderer2D Stats:");
        ImGui::Text("Draw Calls: %d", stats.DrawCalls);
        ImGui::Text("Quads: %d", stats.QuadCount);
        ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
        ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

        ImGui::End();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });

        ImGui::Begin("Settings");
        ImGui::Checkbox("Show physics colliders", &mbShowPhysicsColliders);
        ImGui::End();

        ImGui::Begin("Viewport");
        auto viewportOffset = ImGui::GetCursorPos();

        mViewportFocused = ImGui::IsWindowFocused();
        mViewportHovered = ImGui::IsWindowHovered();

        Orange::Application::GetInstance()->GetImGuiLayer()->BlockEvents(!mViewportFocused && !mViewportHovered);
        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        mViewportSize = { viewportPanelSize.x, viewportPanelSize.y };
    
        uint32_t textureID = mpFrameBuffer->GetColorAttachmentRendererID();
        ImGui::Image(textureID, ImVec2{ mViewportSize.x, mViewportSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

        auto windowSize = ImGui::GetWindowSize();
        ImVec2 minBound = ImGui::GetWindowPos();
        minBound.x += viewportOffset.x;
        minBound.y += viewportOffset.y;

        ImVec2 maxBound = { minBound.x + windowSize.x, minBound.y + windowSize.y };
        mViewportBounds[0] = { minBound.x, minBound.y };
        mViewportBounds[1] = { maxBound.x, maxBound.y };

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                const wchar_t* path = (const wchar_t*)payload->Data;
                OpenScene(gAssetDirectory / path);
            }
        }

        // Gizmos
        Entity selectedEntity = mSceneHierachyPanel.GetSelectedEntity();
        if (selectedEntity && mGizmoType != -1)
        {
            ImGuizmo::Enable(true);
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();

            float windowWidth = (float)ImGui::GetWindowWidth();
            float windowHeight = (float)ImGui::GetWindowHeight();
            ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);

            ImGuizmo::SetGizmoSizeClipSpace(.15f);
            // Camera
            const glm::mat4& cameraProjection = mpEditorCamera->GetProjectionMatrix();
            glm::mat4 cameraView = mpEditorCamera->GetViewMatrix();

            // Entity transform
            auto& tc = selectedEntity.GetComponent<TransformComponent>();
            glm::mat4 transform = tc.GetTransform();

            // Snapping
            bool snap = Input::IsKeyPressed(OrgKeyCodes::LeftControl);
            float snapValue = 0.5f; // Snap to 0.5m for translation/scale
            // Snap to 45 degrees for rotation
            if (mGizmoType == ImGuizmo::OPERATION::ROTATE)
                snapValue = 45.0f;

            float snapValues[3] = { snapValue, snapValue, snapValue };

            ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                (ImGuizmo::OPERATION)mGizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform),
                nullptr, snap ? snapValues : nullptr);

            if (ImGuizmo::IsUsing())
            {
                glm::vec3 translation, rotation, scale;
                Math::DecomposeTransform(transform, translation, rotation, scale);

                glm::vec3 deltaRotation = rotation - tc.Rotation;
                tc.Translation = translation;
                tc.Rotation += deltaRotation;
                tc.Scale = scale;
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();

        UIToolbar();

        ImGui::End();
    }

    void EditorLayer::OnEvent(Event& event)
    {
        mCameraControler.OnEvent(event);
        mpEditorCamera->OnEvent(event);

        EventDispatcher dispatcher(event);
        dispatcher.Dispatch<KeyPressedEvent>(BIND_EDITOR_EVENT_FN(EditorLayer::OnKeyPressed));
        dispatcher.Dispatch<MouseButtonPressedEvent>(BIND_EDITOR_EVENT_FN(EditorLayer::OnMouseButtonPressed));
    }

    bool EditorLayer::OnKeyPressed(KeyPressedEvent& e)
    {
        // shortcuts
        if (e.GetRepeatCount() > 0)
        {
            return false;
        }

        bool bControl = Input::IsKeyPressed(OrgKeyCodes::LeftControl) || Input::IsKeyPressed(OrgKeyCodes::RightControl);
        bool bShift = Input::IsKeyPressed(OrgKeyCodes::LeftShift) || Input::IsKeyPressed(OrgKeyCodes::RightShift);

        auto keyCode = e.GetKeyCode();
        switch (OrgKeyCodes(keyCode))
        {
            case OrgKeyCodes::N:
            {
                if (bControl)
                {
                    NewScene();
                }
                break;
            }
            case OrgKeyCodes::O:
            {
                if (bControl)
                {
                    OpenScene();
                }
                break;
            case OrgKeyCodes::S:
            {
                if (bControl)
                {
                    bShift ? SaveSceneAs() : SaveScene();
                }
                
                break;
            }
            case OrgKeyCodes::Q:
            {
                if (bControl)
                {
                    mGizmoType = -1;
                }
                break;
            }
            case OrgKeyCodes::D:
            {
                if (bControl)
                {
                    OnDuplicateEntity();
                }
                break;
            }
            case OrgKeyCodes::W:
            {
                if (bControl)
                {
                    mGizmoType = ImGuizmo::OPERATION::TRANSLATE;
                }
                break;
            }
            case OrgKeyCodes::E:
            {
                if (bControl)
                {
                    mGizmoType = ImGuizmo::OPERATION::ROTATE;
                }
                break;
            }
            case OrgKeyCodes::R:
            {
                if (bControl)
                {
                    mGizmoType = ImGuizmo::OPERATION::SCALE;
                }
                break;
            }
            default:
                break;
            }
        }
    }

    bool EditorLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (e.GetButton() == MouseButton::LeftButton)
        {
            if (mViewportHovered && !ImGui::IsWindowHovered() && !Input::IsKeyPressed(OrgKeyCodes::LeftAlt))
            {
                mSceneHierachyPanel.SetSelectedEntity(mHoveredEntity);
            }
        }
        return false;
    }

    void EditorLayer::OnOverlayRender()
    {
        if (mSceneState == SceneState::Play)
        {
            Entity camera = mpActiveScene->GetPrimaryCameraEntity();
            Renderer2D::BeginScene(CreateRef<SceneCamera>(camera.GetComponent<CameraComponent>().Camera),
                camera.GetComponent<TransformComponent>().GetTransform());
        }
        else
        {
            Renderer2D::BeginScene(mpEditorCamera);
        }

        if (mbShowPhysicsColliders)
        {
            // Box Colliders
            {
                auto view = mpActiveScene->GetAllEntitiesWith<TransformComponent, BoxCollider2DComponent>();
                for (auto entity : view)
                {
                    auto [tc, bc2d] = view.get<TransformComponent, BoxCollider2DComponent>(entity);

                    glm::vec3 translation = tc.Translation + glm::vec3(bc2d.Offset, 0.001f);
                    glm::vec3 scale = tc.Scale * glm::vec3(bc2d.Size * 2.0f, 1.0f);

                    glm::mat4 transform = glm::translate(glm::mat4(1.0f), translation)
                        * glm::rotate(glm::mat4(1.0f), tc.Rotation.z, glm::vec3(0.0f, 0.0f, 1.0f))
                        * glm::scale(glm::mat4(1.0f), scale);

                    Renderer2D::DrawRect(transform, glm::vec4(0, 1, 0, 1));
                }
            }

            // Circle Colliders
            {
                auto view = mpActiveScene->GetAllEntitiesWith<TransformComponent, CircleCollider2DComponent>();
                for (auto entity : view)
                {
                    auto [tc, cc2d] = view.get<TransformComponent, CircleCollider2DComponent>(entity);

                    glm::vec3 translation = tc.Translation + glm::vec3(cc2d.Offset, 0.001f);
                    glm::vec3 scale = tc.Scale * glm::vec3(cc2d.Radius * 2.0f);

                    glm::mat4 transform = glm::translate(glm::mat4(1.0f), translation)
                        * glm::scale(glm::mat4(1.0f), scale);

                    Renderer2D::DrawCircle(transform, glm::vec4(0, 1, 0, 1), 0.01f);
                }
            }
        }
        Renderer2D::EndScene();
    }

    void EditorLayer::NewScene()
    {
        mpActiveScene = CreateRef<Scene>();
        mpActiveScene->OnViewportResize(mViewportSize.x, mViewportSize.y);
        mSceneHierachyPanel.SetContext(mpActiveScene);
    }

    void EditorLayer::OpenScene()
    {
        std::string filepath = FileDialog::OpenFile("Scene (*.oescn)*.oescn");

        if(!filepath.empty())
        {
            OpenScene(filepath);
        }
    }

    void EditorLayer::OpenScene(const std::filesystem::path& path)
    {
        if (mSceneState != SceneState::Edit)
        {
            OnSceneStop();
        }

        Ref<Scene> newScene = CreateRef<Scene>();

        SceneSerializer serializer(newScene);
        if (serializer.Deserialize(path.string()))
        {
            mpEditorScene = newScene;
            mpEditorScene->OnViewportResize(mViewportSize.x, mViewportSize.y);
            mSceneHierachyPanel.SetContext(mpEditorScene);

            mpActiveScene = mpEditorScene;
            mEditorScenePath = path;
        }
    }

    void EditorLayer::SaveScene()
    {
        if (!mEditorScenePath.empty())
        {
            SerilizeScene(mpActiveScene, mEditorScenePath);
        }
        else
        {
            SaveSceneAs();
        }
    }

    void EditorLayer::SaveSceneAs()
    {
        std::string filepath = FileDialog::SaveFile("Scene (*.oescn)\\0*.oescn\\0");

        if (!filepath.empty())
        {
            // check extension
            if (filepath.size() < 6 || filepath.substr(filepath.size() - 6) != ".oescn")
            {
                filepath += ".oescn";
            }
            SerilizeScene(mpActiveScene, filepath);
            mEditorScenePath = filepath;
        }
    }

    void EditorLayer::SerilizeScene(Ref<Scene> tpScene, const std::filesystem::path& path)
    {
        SceneSerializer serializer(tpScene);
        serializer.Serialize(path.string());
    }

    void EditorLayer::OnScenePlay()
    {
        mSceneState = SceneState::Play;

        mpActiveScene = Scene::Copy(mpEditorScene);
        mpActiveScene->OnRuntimeStart();

        mSceneHierachyPanel.SetContext(mpActiveScene);
    }

    void EditorLayer::OnSceneStop()
    {
        mSceneState = SceneState::Edit;
        mpActiveScene->OnRuntimeStop();
        mpActiveScene = mpEditorScene;
        mSceneHierachyPanel.SetContext(mpActiveScene);
    }

    void EditorLayer::OnDuplicateEntity()
    {
        if (mSceneState != SceneState::Edit)
        {
            return;
        }

        Entity selectedEntity = mSceneHierachyPanel.GetSelectedEntity();
        if (selectedEntity)
        {
            mpEditorScene->DuplicateEntity(selectedEntity);
        }
    }

    void EditorLayer::UIToolbar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 2));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        auto& colors = ImGui::GetStyle().Colors;
        const auto& buttonHovered = colors[ImGuiCol_ButtonHovered];
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(buttonHovered.x, buttonHovered.y, buttonHovered.z, 0.5f));
        const auto& buttonActive = colors[ImGuiCol_ButtonActive];
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(buttonActive.x, buttonActive.y, buttonActive.z, 0.5f));

        ImGui::Begin("##toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        float size = ImGui::GetWindowHeight() - 4.0f;
        Ref<Texture2D> icon = mSceneState == SceneState::Edit ? mpIconPlay : mpIconStop;
        std::string buttonName = mSceneState == SceneState::Edit ? "Play" : "Stop";
        ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x * 0.5f) - (size * 0.5f));
        if (ImGui::ImageButton(buttonName.c_str(),(ImTextureID)icon->GetRendererID(), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1)))
        {
            if (mSceneState == SceneState::Edit)
            {
                OnScenePlay();
            }
            else if (mSceneState == SceneState::Play)
            {
                OnSceneStop();
            }
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        ImGui::End();
    }

}