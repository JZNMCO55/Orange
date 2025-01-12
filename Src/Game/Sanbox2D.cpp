#include "Sanbox2D.h"
#include "Platform/OpenGL/OpenGLShader.h"
#include "imgui/imgui.h"

#include <chrono>

template<typename Fn>
class Timer
{
public:
    Timer(const char* name, Fn&& func)
        : m_Name(name), mFunc(func), mStopped(false)
    {
        mStartTimepoint = std::chrono::high_resolution_clock::now();
    }
    ~Timer()
    {
        if (!mStopped)
            Stop();
    }
    void Stop()
    {
        auto endTimepoint = std::chrono::high_resolution_clock::now();
        long long start = std::chrono::time_point_cast<std::chrono::microseconds>(mStartTimepoint).time_since_epoch().count();
        long long end = std::chrono::time_point_cast<std::chrono::microseconds>(endTimepoint).time_since_epoch().count();
        mStopped = true;
        float duration = (end - start) * 0.001f;
        mFunc({ m_Name, duration });
    }
private:
    const char* m_Name;
    Fn mFunc;
    std::chrono::time_point<std::chrono::steady_clock> mStartTimepoint;
    bool mStopped;
};
#define PROFILE_SCOPE(name) Timer timer##__LINE__(name, [&](ProfileResult profileResult) { mProfileResults.push_back(profileResult); })


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
    mpCheckerboardTexture = Orange::Texture2D::Create(R"(..\..\Resource\Textures\Checkerboard.png)");
}

void Sandbox2D::OnDetach()
{

}

void Sandbox2D::OnUpdate(Orange::Timestep ts)
{
    PROFILE_SCOPE("Sandbox2D::OnUpdate");
    // Update
    {
        PROFILE_SCOPE("CameraController::OnUpdate");
        mpCameraController->OnUpdate(ts);
    }

    // Render
    {
        PROFILE_SCOPE("Render2D Preparation");
        Orange::RenderCommand::SetClearColor({ 0.2f, 0.3f, 0.3f, 1.0f });
        Orange::RenderCommand::Clear();
    }

    {
        PROFILE_SCOPE("Renderer2D Draw");
        Orange::Renderer2D::BeginScene(mpCameraController->GetCamera());
        Orange::Renderer2D::DrawQuad({ -1.0f, 0.0f }, { 0.8f, 0.8f }, { 0.8f, 0.2f, 0.3f, 1.0f });
        Orange::Renderer2D::DrawQuad({ 0.5f, -0.5f }, { 0.5f, 0.75f }, { 0.2f, 0.3f, 0.8f, 1.0f });
        Orange::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.1f }, { 10.0f, 10.0f }, mpCheckerboardTexture);
    
        Orange::Renderer2D::EndScene();
    }
}

void Sandbox2D::OnImGuiRender()
{
    ImGui::Begin("Settings");
    ImGui::ColorEdit4("Square Color", glm::value_ptr(mSquareColor));
    for (auto& result : mProfileResults)
    {
        char label[256];
        strcpy(label, "%.3fms ");
        strcat(label, result.Name);
        ImGui::Text(label, result.Time);
    }
    mProfileResults.clear();
    ImGui::End();
}

void Sandbox2D::OnEvent(Orange::Event& e)
{
    mpCameraController->OnEvent(e);
}
