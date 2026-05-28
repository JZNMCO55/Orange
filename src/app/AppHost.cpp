#include "orange/engine/app/AppHost.h"

#include "orange/engine/app/FrameContext.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/core/Profiler.h"
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

    // v0.9 c4：声明引擎内置 sample bin 树。根 "Frame" 下挂 PollEvents /
    // LayerUpdate / Present 三个一级子项；Layer 子系统（Render / Physics
    // / Audio 等）按各自模块的 PROFILE_SCOPE 自然形成 "LayerUpdate" 的子
    // 树。游戏侧 / 编辑器侧可继续 DeclareSampleBin 把自定义热点挂回任一
    // 已知 parent。
    static const bool sProfilerBinsDeclared = []() {
        using namespace ::Orange::Engine::Core;
        Profiler::DeclareSampleBin("Frame");
        Profiler::DeclareSampleBin("PollEvents", "Frame");
        Profiler::DeclareSampleBin("LayerUpdate", "Frame");
        return true;
    }();
    (void)sProfilerBinsDeclared;

    while (!impl.pWindow->ShouldClose()
        && !impl.exitRequested.load(std::memory_order_relaxed))
    {
        // "Frame" scope 用内层 block 包整帧主体，让其 dtor 在 FinalizeFrame
        // 之前跑——这样 FinalizeFrame 看到的 inclusive_ms 包含整帧。
        {
        ORANGE_PROFILE_SCOPE("Frame");

        {
            ORANGE_PROFILE_SCOPE("PollEvents");
            Platform::Window::PollEvents();
        }

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

        {
            ORANGE_PROFILE_SCOPE("LayerUpdate");
            for (auto& layer : impl.stack)
            {
                if (layer)
                {
                    layer->OnUpdate(frame);
                }
            }
        }

        // Render / present 阶段当前为空，待 OrangeRender 在后续阶段
        // 接通后填充。
        }  // "Frame" scope dtor 在此跑完，inclusive_ms 已累加

        // 帧末汇总 profiler：上一行 "Frame" scope 已 dtor，本调用看到的
        // inclusive_ms 是整帧累加值。FinalizeFrame 本身的开销不计入 "Frame"
        // bin（在 scope 之外），但下一帧 PollEvents 之前会有 ~μs 间隙；
        // 想精确测 FinalizeFrame 自身耗时可单独声明 "ProfilerFinalize" bin
        // 再 wrap，本期不做。
        ::Orange::Engine::Core::Profiler::FinalizeFrame();

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

void AppHost::DispatchImGui()
{
    for (auto& layer : mpImpl->stack)
    {
        if (layer)
        {
            layer->OnImGui();
        }
    }
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
