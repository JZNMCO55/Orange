#include "Sanbox2D.h"
#include "Platform/OpenGL/OpenGLShader.h"
#include "imgui/imgui.h"

Sandbox2D::Sandbox2D()
    : Layer("Sandbox2D")
{
    mpCameraController = std::make_shared<Orange::OrthographicCameraControler>(1280.0f / 720.0f);
}

Sandbox2D::~Sandbox2D()
{

}

void Sandbox2D::OnAttach()
{
    mpVertexArray = Orange::VertexArray::Create();

    float squareVertices[5 * 4] = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.5f,  0.5f, 0.0f,
        -0.5f,  0.5f, 0.0f
    };

    Orange::Ref<Orange::VertexBuffer> squareVB;
    squareVB.reset(Orange::VertexBuffer::Create(squareVertices, sizeof(squareVertices)));
    squareVB->SetLayout({
        { Orange::EShaderDataType::Float3, "a_Position" }
        });
    mpVertexArray->AddVertexBuffer(squareVB);
    uint32_t squareIndices[6] = { 0, 1, 2, 2, 3, 0 };
    Orange::Ref<Orange::IndexBuffer> squareIB;
    squareIB.reset(Orange::IndexBuffer::Create(squareIndices, sizeof(squareIndices) / sizeof(uint32_t)));
    mpVertexArray->SetIndexBuffer(squareIB);
    mpFlatShader = Orange::Shader::Create(R"(../../Resource/Shaders/FlatShader.glsl)");;
}

void Sandbox2D::OnDetach()
{

}

void Sandbox2D::OnUpdate(Orange::Timestep ts)
{
    mpCameraController->OnUpdate(ts);
    
    Orange::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
    Orange::RenderCommand::Clear();

    Orange::Renderer::BeginScene(mpCameraController->GetCamera());

    std::dynamic_pointer_cast<Orange::OpenGLShader>(mpFlatShader)->Bind();
    std::dynamic_pointer_cast<Orange::OpenGLShader>(mpFlatShader)->UploadUniformFloat4("u_Color", mSquareColor);

    Orange::Renderer::Submit(mpFlatShader, mpVertexArray,
        glm::scale(glm::mat4(1.0f), glm::vec3(1.5f)));

    Orange::Renderer::EndScene();
}

void Sandbox2D::OnImGuiRender()
{
    ImGui::Begin("Settings");
    ImGui::ColorEdit4("Square Color", glm::value_ptr(mSquareColor));
    ImGui::End();
}

void Sandbox2D::OnEvent(Orange::Event& e)
{
    mpCameraController->OnEvent(e);
}
