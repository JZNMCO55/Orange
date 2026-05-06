#include "orange/engine/app/AppHost.h"

#include "orange/engine/app/FrameContext.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/core/Time.h"

#include <atomic>
#include <chrono>
#include <utility>
#include <variant>

namespace Orange::Engine
{

struct AppHost::Impl
{
    std::unique_ptr<Platform::Window> pWindow;
    LayerStack                        stack;

    std::atomic<bool> exitRequested{false};
    FrameIndex        frameIndex{0};

    using SteadyClock = std::chrono::steady_clock;
    SteadyClock::time_point startTime{};
    SteadyClock::time_point lastFrameTime{};
    bool                    firstFrame{true};
};

AppHost::AppHost() : mpImpl(std::make_unique<Impl>()) {}

AppHost::~AppHost() = default;

Result<std::unique_ptr<AppHost>, ResultCode> AppHost::Create(const AppConfig& config)
{
    auto windowResult = Platform::Window::Create(config.window);
    if (windowResult.IsErr())
    {
        ORANGE_LOG_ERROR("AppHost::Create: Window::Create failed");
        return windowResult.Error();
    }

    auto host    = std::unique_ptr<AppHost>(new AppHost{});
    auto& impl   = *host->mpImpl;
    impl.pWindow = std::move(windowResult).Value();

    // 事件回调：按 reverse 顺序分发给 LayerStack——overlay 在普通
    // layer 之上优先拿到事件，任一返回 true 即视为已消费。AppHost
    // 自己不需要在这里截 close 事件：GLFW 已经在内部 set should-close
    // 了，主循环顶部的 ShouldClose() 会看到它。
    auto* implPtr = &impl;
    impl.pWindow->SetEventCallback([implPtr](const Platform::WindowEvent& event) {
        for (auto it = implPtr->stack.rbegin(); it != implPtr->stack.rend(); ++it)
        {
            if ((*it) && (*it)->OnEvent(event))
            {
                return;
            }
        }
    });

    return host;
}

int AppHost::Run()
{
    auto& impl = *mpImpl;
    if (!impl.pWindow)
    {
        ORANGE_LOG_ERROR("AppHost::Run: window is not available");
        return -1;
    }

    impl.startTime     = Impl::SteadyClock::now();
    impl.lastFrameTime = impl.startTime;
    impl.firstFrame    = true;
    impl.frameIndex    = 0;
    impl.exitRequested.store(false, std::memory_order_relaxed);

    while (!impl.pWindow->ShouldClose()
        && !impl.exitRequested.load(std::memory_order_relaxed))
    {
        Platform::Window::PollEvents();

        const auto now = Impl::SteadyClock::now();

        // 第一帧的 dt 钳到 0，避免把 startup 时间算进首帧。
        DeltaSeconds delta = 0.0f;
        if (!impl.firstFrame)
        {
            const auto frameDur = std::chrono::duration<DeltaSeconds>(now - impl.lastFrameTime);
            delta               = frameDur.count();
        }
        impl.firstFrame    = false;
        impl.lastFrameTime = now;

        const auto totalDur = std::chrono::duration<DeltaSeconds>(now - impl.startTime);

        std::uint32_t fbWidth  = 0;
        std::uint32_t fbHeight = 0;
        impl.pWindow->GetFramebufferSize(fbWidth, fbHeight);

        FrameContext frame{};
        frame.time.deltaSeconds  = delta;
        frame.time.totalSeconds  = totalDur.count();
        frame.time.frameIndex    = impl.frameIndex;
        frame.framebufferWidth   = fbWidth;
        frame.framebufferHeight  = fbHeight;
        frame.pWindow            = impl.pWindow.get();

        for (auto& layer : impl.stack)
        {
            if (layer)
            {
                layer->OnUpdate(frame);
            }
        }

        // Render / present 阶段当前为空，待 OrangeRender 在后续阶段
        // 接通后填充。

        ++impl.frameIndex;
    }

    return 0;
}

void AppHost::RequestExit() noexcept
{
    mpImpl->exitRequested.store(true, std::memory_order_relaxed);
}

Layer* AppHost::PushLayer(std::unique_ptr<Layer> layer)
{
    return mpImpl->stack.PushLayer(std::move(layer));
}

Layer* AppHost::PushOverlay(std::unique_ptr<Layer> overlay)
{
    return mpImpl->stack.PushOverlay(std::move(overlay));
}

Platform::Window& AppHost::GetWindow() noexcept
{
    return *mpImpl->pWindow;
}

LayerStack& AppHost::GetLayerStack() noexcept
{
    return mpImpl->stack;
}

}  // namespace Orange::Engine
