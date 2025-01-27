#include "Sanbox3D.h"
#include "imgui/imgui.h"

static std::vector<float> cubeVertices = {
    // 位置              // 颜色 (RGB)         // 透明度 (A)
    // 前表面
    -0.5f, -0.5f,  0.5f,    1.0f, 0.0f, 0.0f,  1.0f,  // 前左下，红色
     0.5f, -0.5f,  0.5f,    1.0f, 0.0f, 0.0f,  1.0f,  // 前右下，绿色
     0.5f,  0.5f,  0.5f,    1.0f, 0.0f, 0.0f,  1.0f,  // 前右上，蓝色
    -0.5f,  0.5f,  0.5f,    1.0f, 0.0f, 0.0f,  1.0f,  // 前左上，黄色

    // 后表面
    -0.5f, -0.5f, -0.5f,    0.0f, 1.0f, 0.0f,  1.0f,  // 后左下，紫色
    -0.5f,  0.5f, -0.5f,    0.0f, 1.0f, 0.0f,  1.0f,  // 后左上，青色
     0.5f,  0.5f, -0.5f,    0.0f, 1.0f, 0.0f,  1.0f,  // 后右上，橙色
     0.5f, -0.5f, -0.5f,    0.0f, 1.0f, 0.0f,  1.0f,  // 后右下，紫红色

     // 左表面
     -0.5f, -0.5f, -0.5f,    0.5f, 0.5f, 0.5f,  1.0f,  // 左后下，灰色
     -0.5f, -0.5f,  0.5f,    0.0f, 0.5f, 0.5f,  1.0f,  // 左前下，蓝绿色
     -0.5f,  0.5f,  0.5f,    0.5f, 0.0f, 0.5f,  1.0f,  // 左前上，紫红色
     -0.5f,  0.5f, -0.5f,    0.5f, 0.5f, 0.0f,  1.0f,  // 左后上，黄绿色

     // 右表面
      0.5f, -0.5f, -0.5f,    0.8f, 0.2f, 0.2f,  1.0f,  // 右后下，浅红色
      0.5f,  0.5f, -0.5f,    0.2f, 0.8f, 0.2f,  1.0f,  // 右后上，浅绿色
      0.5f,  0.5f,  0.5f,    0.2f, 0.2f, 0.8f,  1.0f,  // 右前上，浅蓝色
      0.5f, -0.5f,  0.5f,    0.8f, 0.8f, 0.2f,  1.0f,  // 右前下，浅黄色

      // 上表面
      -0.5f,  0.5f, -0.5f,    0.2f, 0.8f, 0.8f,  1.0f,  // 上后左，浅青色
      -0.5f,  0.5f,  0.5f,    0.8f, 0.2f, 0.8f,  1.0f,  // 上前左，浅紫色
       0.5f,  0.5f,  0.5f,    0.8f, 0.8f, 0.2f,  1.0f,  // 上前右，浅黄色
       0.5f,  0.5f, -0.5f,    0.2f, 0.2f, 0.8f,  1.0f,  // 上后右，浅蓝色

       // 下表面
       -0.5f, -0.5f, -0.5f,    0.8f, 0.2f, 0.2f,  1.0f,  // 下后左，浅红色
        0.5f, -0.5f, -0.5f,    0.2f, 0.8f, 0.2f,  1.0f,  // 下后右，浅绿色
        0.5f, -0.5f,  0.5f,    0.2f, 0.2f, 0.8f,  1.0f,  // 下前右，浅蓝色
       -0.5f, -0.5f,  0.5f,    0.8f, 0.8f, 0.2f,  1.0f   // 下前左，浅黄色
};

static std::vector<uint32_t> cubeIndices = {
    // 前表面
    0, 1, 2,
    0, 2, 3,

    // 后表面
    4, 5, 6,
    4, 6, 7,

    // 左表面
    8, 9, 10,
    8, 10, 11,

    // 右表面
    12, 13, 14,
    12, 14, 15,

    // 上表面
    16, 17, 18,
    16, 18, 19,

    // 下表面
    20, 21, 22,
    20, 22, 23
};
static std::vector<float> triangleVertices = {
        -0.5f, -0.5f, 0.0f, 0.8f, 0.2f, 0.8f, 1.0f,
         0.5f, -0.5f, 0.0f, 0.2f, 0.3f, 0.8f, 1.0f,
         0.0f,  0.5f, 0.0f, 0.8f, 0.8f, 0.2f, 1.0f
};

static std::vector<uint32_t> triangleIndices = { 0, 1, 2 };

Sandbox3D::Sandbox3D()
    : Layer("Sandbox3D")
{
    mpCameraController = std::make_shared<Orange::OrthographicCameraControler>(1280.0f / 720.0f);

    float vertices[3 * 7] = {
        -0.5f, -0.5f, 0.0f, 0.8f, 0.2f, 0.8f, 1.0f,
         0.5f, -0.5f, 0.0f, 0.2f, 0.3f, 0.8f, 1.0f,
         0.0f,  0.5f, 0.0f, 0.8f, 0.8f, 0.2f, 1.0f
    };

    mpVertexArray = Orange::VertexArray::Create();
    Orange::Ref<Orange::VertexBuffer> tpVertexBuffer(Orange::VertexBuffer::Create(triangleVertices, sizeof(float) * triangleVertices.size()));

    Orange::BufferLayout layout = {
        {Orange::EShaderDataType::Float3, "a_Position"},
        {Orange::EShaderDataType::Float4, "a_Color"}
    };


    tpVertexBuffer->SetLayout(layout);
    //mpVertexArray->AddVertexBuffer(tpVertexBuffer);

    uint32_t indices[3] = { 0, 1, 2 };
    Orange::Ref<Orange::IndexBuffer> tpIndexBuffer;
    tpIndexBuffer = Orange::IndexBuffer::Create(triangleIndices.data(), triangleIndices.size());
    //mpVertexArray->SetIndexBuffer(tpIndexBuffer);

    //mpMesh = Orange::CreateRef<Orange::Mesh>(tpVertexBuffer, tpIndexBuffer);
    mpMesh = Orange::CreateRef<Orange::Mesh>(cubeVertices, cubeIndices);
    mpRendererObject = Orange::CreateRef<Orange::RendererObject>(mpMesh);

    mpVertexArray->AddVertexBuffer(mpMesh->GetVertexBuffer());
    mpVertexArray->SetIndexBuffer(mpMesh->GetIndexBuffer());

    //mpFlatShader = Orange::Shader::Create("FlatColor", flatColorShaderVertexSrc, flatColorShaderFragmentSrc);
    mpFlatShader = Orange::Shader::Create(R"(../../Resource/Shaders/FlatShader.glsl)");
}

Sandbox3D::~Sandbox3D()
{
}

void Sandbox3D::OnAttach()
{
    ORG_PROFILE_FUNCTION();

    mpCheckerboardTexture = Orange::Texture2D::Create(R"(..\..\Resource\Textures\Checkerboard.png)");
}

void Sandbox3D::OnDetach()
{
    ORG_PROFILE_FUNCTION();

}

void Sandbox3D::OnUpdate(Orange::Timestep ts)
{
    ORG_PROFILE_FUNCTION();

    //ORG_PROFILE_SCOPE("CameraController::OnUpdate");
    mpCameraController->OnUpdate(ts);

    Orange::Renderer2D::ResetStats();

    // Render
    {
        ORG_PROFILE_SCOPE("Render3D Preparation");
        Orange::RenderCommand::SetClearColor({ 0.2f, 0.3f, 0.3f, 1.0f });
        Orange::RenderCommand::Clear();
    }

    {
        Orange::Renderer3D::BeginScene(mpCameraController->GetCamera());
        Orange::Renderer3D::RenderObject(mpRendererObject);
        Orange::Renderer3D::EndScene();
        //Orange::Renderer::BeginScene(mpCameraController->GetCamera());

        //Orange::Renderer::Submit(mpFlatShader, mpVertexArray);
        //Orange::Renderer::EndScene();
        ORG_PROFILE_SCOPE("Renderer3D Draw");
    }
}

void Sandbox3D::OnImGuiRender()
{
    static bool dockspaceOpen = true;
    static bool opt_fullscreen_persistant = false;
    bool opt_fullscreen = opt_fullscreen_persistant;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
    
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
    
    if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
        window_flags |= ImGuiWindowFlags_NoBackground;
    
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
            if (ImGui::MenuItem("Exit"))
            {
                Orange::Application::GetInstance()->Close();
            }
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
    
    ImGui::ColorEdit4("Square Color", glm::value_ptr(mSquareColor));
    
    uint32_t textureID = mpCheckerboardTexture->GetRendererID();
    ImGui::Image(textureID, ImVec2{ 256.0f, 256.0f });
    ImGui::End();
    
    ImGui::End();
}

void Sandbox3D::OnEvent(Orange::Event& e)
{
    mpCameraController->OnEvent(e);
}
