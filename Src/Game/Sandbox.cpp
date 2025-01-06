#include <iostream>
#include "Orange.h"

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
        std::shared_ptr<Orange::VertexBuffer> tpVertexBuffer(Orange::VertexBuffer::Create(vertices, sizeof(vertices)));

        Orange::BufferLayout layout = {
            {Orange::EShaderDataType::Float3, "a_Position"},
            {Orange::EShaderDataType::Float4, "a_Color"}
        };

        tpVertexBuffer->SetLayout(layout);
        mpVertexArray->AddVertexBuffer(tpVertexBuffer);

        uint32_t indices[3] = { 0, 1, 2 };
        std::shared_ptr<Orange::IndexBuffer> tpIndexBuffer;
        tpIndexBuffer.reset(Orange::IndexBuffer::Create(indices, sizeof(indices) / sizeof(uint32_t)));
        mpVertexArray->SetIndexBuffer(tpIndexBuffer);

        // Squares
        mpBlueVertexArray.reset(Orange::VertexArray::Create());
        float squareVertices[3 * 4] = {
            -0.75f, -0.75f, 0.0f,
             0.75f, -0.75f, 0.0f,
             0.75f,  0.75f, 0.0f,
            -0.75f,  0.75f, 0.0f
        };

        std::shared_ptr<Orange::VertexBuffer> squareVB;
        squareVB.reset(Orange::VertexBuffer::Create(squareVertices, sizeof(squareVertices)));
        squareVB->SetLayout({
            { Orange::EShaderDataType::Float3, "a_Position" }
            });
        mpBlueVertexArray->AddVertexBuffer(squareVB);

        uint32_t squareIndices[6] = { 0, 1, 2, 2, 3, 0 };
        std::shared_ptr<Orange::IndexBuffer> squareIB;
        squareIB.reset(Orange::IndexBuffer::Create(squareIndices, sizeof(squareIndices) / sizeof(uint32_t)));
        mpBlueVertexArray->SetIndexBuffer(squareIB);

        std::string vertexSrc = R"(
            #version 330 core
            
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec4 a_Color;
            uniform mat4 u_ViewProjection;

            out vec3 v_Position;
            out vec4 v_Color;

            void main()
            {
                v_Position = a_Position;
                v_Color = a_Color;
                gl_Position = u_ViewProjection * vec4(a_Position, 1.0);    
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

        mpShader = std::make_unique<Orange::Shader>(vertexSrc, fragmentSrc);

        std::string blueShaderVertexSrc = R"(
            #version 330 core
            
            layout(location = 0) in vec3 a_Position;
            uniform mat4 u_ViewProjection;
            out vec3 v_Position;

            void main()
            {
                v_Position = a_Position;
                gl_Position = u_ViewProjection * vec4(a_Position, 1.0);    
            }
        )";

        std::string blueShaderFragmentSrc = R"(
            #version 330 core
            
            layout(location = 0) out vec4 color;

            in vec3 v_Position;

            void main()
            {
                color = vec4(0.2, 0.3, 0.8, 1.0);
            }
        )";

        mpBlueShader = std::make_unique<Orange::Shader>(blueShaderVertexSrc, blueShaderFragmentSrc);

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
        mpBlueShader->Bind();
        Orange::Renderer::Submit(mpBlueShader, mpBlueVertexArray);

        mpShader->Bind();
        Orange::Renderer::Submit(mpShader, mpVertexArray);
        Orange::Renderer::EndScene();
    }

    void OnEvent(Orange::Event& event) override
    {

    }   

private:
    std::shared_ptr<Orange::Shader> mpShader{ nullptr };
    std::shared_ptr<Orange::VertexArray> mpVertexArray{ nullptr };
    std::shared_ptr<Orange::Shader> mpBlueShader{ nullptr };
    std::shared_ptr<Orange::VertexArray> mpBlueVertexArray{ nullptr };
    std::shared_ptr<Orange::OrthographicCamera> mpCamera;

    glm::vec3 mCameraPosition;
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