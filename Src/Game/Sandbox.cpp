#include <iostream>
#include "imgui/imgui.h"
#include "Orange.h"
#include "OpenGL/OpenGLShader.h"
#include <filesystem>

class ExampleLayer : public Orange::Layer
{
public:
    ExampleLayer()
        : Layer("Example")
        ,mCameraPosition(0.f)
    {
        mpCamera = std::make_shared<Orange::OrthographicCamera>(-1.6f, 1.6f, -0.9f, 0.9f);
        //Triangle
        float vertices[3 * 7] = {
            -0.5f, -0.5f, 0.0f, 0.8f, 0.2f, 0.8f, 1.0f,
             0.5f, -0.5f, 0.0f, 0.2f, 0.3f, 0.8f, 1.0f,
             0.0f,  0.5f, 0.0f, 0.8f, 0.8f, 0.2f, 1.0f
        };

        mpVertexArray.reset(Orange::VertexArray::Create());
        Orange::Ref<Orange::VertexBuffer> tpVertexBuffer(Orange::VertexBuffer::Create(vertices, sizeof(vertices)));

        Orange::BufferLayout layout = {
            {Orange::EShaderDataType::Float3, "a_Position"},
            {Orange::EShaderDataType::Float4, "a_Color"}
        };

        tpVertexBuffer->SetLayout(layout);
        mpVertexArray->AddVertexBuffer(tpVertexBuffer);

        uint32_t indices[3] = { 0, 1, 2 };
        Orange::Ref<Orange::IndexBuffer> tpIndexBuffer;
        tpIndexBuffer.reset(Orange::IndexBuffer::Create(indices, sizeof(indices) / sizeof(uint32_t)));
        mpVertexArray->SetIndexBuffer(tpIndexBuffer);

        // Squares
        mpBlueVertexArray.reset(Orange::VertexArray::Create());
        float squareVertices[5 * 4] = {
            -0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
             0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
             0.5f,  0.5f, 0.0f, 1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f, 0.0f, 1.0f
        };

        Orange::Ref<Orange::VertexBuffer> squareVB;
        squareVB.reset(Orange::VertexBuffer::Create(squareVertices, sizeof(squareVertices)));
        squareVB->SetLayout({
            { Orange::EShaderDataType::Float3, "a_Position" },
            { Orange::EShaderDataType::Float2, "a_TexCoord" }
            });
        mpBlueVertexArray->AddVertexBuffer(squareVB);

        uint32_t squareIndices[6] = { 0, 1, 2, 2, 3, 0 };
        Orange::Ref<Orange::IndexBuffer> squareIB;
        squareIB.reset(Orange::IndexBuffer::Create(squareIndices, sizeof(squareIndices) / sizeof(uint32_t)));
        mpBlueVertexArray->SetIndexBuffer(squareIB);

        std::string vertexSrc = R"(
            #version 330 core
            
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec4 a_Color;
            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;

            out vec3 v_Position;
            out vec4 v_Color;

            void main()
            {
                v_Position = a_Position;
                v_Color = a_Color;
                gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);
            }
        )";

        std::string fragmentSrc = R"(
            #version 330 core
            
            layout(location = 0) out vec4 color;

            in vec3 v_Position;
            in vec4 v_Color;

            void main()
            {
                color = vec4(v_Position * 0.5 + 0.5, 1.0);
                color = v_Color;
            }
        )";

        mpShader.reset(Orange::Shader::Create(vertexSrc, fragmentSrc));

        std::string flatColorShaderVertexSrc = R"(
            #version 330 core
            
            layout(location = 0) in vec3 a_Position;

            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;

            out vec3 v_Position;

            void main()
            {
                v_Position = a_Position;
                gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);    
            }
        )";

        std::string flatColorShaderFragmentSrc = R"(
            #version 330 core
            
            layout(location = 0) out vec4 color;

            in vec3 v_Position;
            
            uniform vec3 u_Color;

            void main()
            {
                color = vec4(u_Color, 1.0);
            }
        )";


        mpFlatShader.reset(Orange::Shader::Create(flatColorShaderVertexSrc, flatColorShaderFragmentSrc));

        std::string textureShaderVertexSrc = R"(
            #version 330 core
            
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec2 a_TexCoord;

            uniform mat4 u_ViewProjection;
            uniform mat4 u_Transform;

            out vec2 v_TexCoord;

            void main()
            {
                v_TexCoord = a_TexCoord;
                gl_Position = u_ViewProjection * u_Transform * vec4(a_Position, 1.0);    
            }
        )";

        std::string textureShaderFragmentSrc = R"(
            #version 330 core
            
            layout(location = 0) out vec4 color;

            in vec2 v_TexCoord;
            
            uniform sampler2D u_Texture;

            void main()
            {
                color = texture(u_Texture, v_TexCoord);
            }
        )";

        mpTextureShader.reset(Orange::Shader::Create(textureShaderVertexSrc, textureShaderFragmentSrc));
        std::filesystem::path currentPath = std::filesystem::current_path();

        // 打印当前工作目录
        std::cout << "Current working directory: " << currentPath << std::endl;
        mpTexture = Orange::Texture2D::Create(R"(..\..\Resource\Textures\Checkerboard.png)");

        std::dynamic_pointer_cast<Orange::OpenGLShader>(mpTextureShader)->Bind();
        std::dynamic_pointer_cast<Orange::OpenGLShader>(mpTextureShader)->UploadUniformInt("u_Texture", 0);

    }
    ~ExampleLayer() {}

    void OnUpdate(Orange::Timestep ts) override
    {
        if (Orange::Input::IsKeyPressed(ORG_KEY_LEFT))
        {
            mCameraPosition.x += mCameraMoveSpeed * ts;
        }
        else if(Orange::Input::IsKeyPressed(ORG_KEY_RIGHT))
        {
            mCameraPosition.x -= mCameraMoveSpeed * ts;
        }

        if (Orange::Input::IsKeyPressed(ORG_KEY_UP))
        {
            mCameraPosition.y -= mCameraMoveSpeed * ts;
        }
        else if (Orange::Input::IsKeyPressed(ORG_KEY_DOWN))
        {
            mCameraPosition.y += mCameraMoveSpeed * ts;
        }

        if (Orange::Input::IsKeyPressed(ORG_KEY_A))
        {
            mCameraRotation -= mCameraRotationSpeed * ts;
        }
        else if (Orange::Input::IsKeyPressed(ORG_KEY_D))
        {
            mCameraRotation += mCameraRotationSpeed * ts;
        }

        Orange::RenderCommand::SetClearColor({ 0.2f, 0.3f, 0.3f, 1.0f });
        Orange::RenderCommand::Clear();

        mpCamera->SetPosition(mCameraPosition);
        mpCamera->SetRotation(mCameraRotation);

        Orange::Renderer::BeginScene(mpCamera);
        
        glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));
        std::dynamic_pointer_cast<Orange::OpenGLShader>(mpFlatShader)->Bind();
        std::dynamic_pointer_cast<Orange::OpenGLShader>(mpFlatShader)->UploadUniformFloat3("u_Color", mSquareColor);

        for (int y = 0; y < 20; y++)
        {
            for (int x = 0; x < 20; x++)
            {
                glm::vec3 pos(x * 0.11f, y * 0.11f, 0.0f);
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos) * scale;
                Orange::Renderer::Submit(mpFlatShader, mpBlueVertexArray, transform);
            }
        }

        mpTexture->Bind();
        Orange::Renderer::Submit(mpTextureShader, mpBlueVertexArray);
        
        // triangle
        //Orange::Renderer::Submit(mpShader, mpVertexArray);
        Orange::Renderer::EndScene();
    }

    void OnImGuiRender() override
    {
        ImGui::Begin("Settings");
        ImGui::ColorEdit3("Square Color", glm::value_ptr(mSquareColor));
        ImGui::End();
    }

    void OnEvent(Orange::Event& event) override
    {

    }   

private:
    Orange::Ref<Orange::Shader> mpShader{ nullptr };
    Orange::Ref<Orange::VertexArray> mpVertexArray{ nullptr };
    Orange::Ref<Orange::Shader> mpFlatShader{ nullptr };
    Orange::Ref<Orange::VertexArray> mpBlueVertexArray{ nullptr };
    Orange::Ref<Orange::Shader> mpTextureShader{ nullptr };
    Orange::Ref<Orange::OrthographicCamera> mpCamera;
    Orange::Ref<Orange::Texture2D> mpTexture{ nullptr };

    glm::vec3 mCameraPosition;
    glm::vec3 mSquareColor;
    float mCameraMoveSpeed = 5.0f;
    float mCameraRotation = 0.0f;
    float mCameraRotationSpeed = 180.0f;
};
class Sandbox : public Orange::Application
{
    public:
        Sandbox()
        {
             PushLayer(new ExampleLayer());
        }
        
        ~Sandbox()
        {

        }
        
};

Orange::Application* Orange::CreateApplication()
{
    return new Sandbox();
}