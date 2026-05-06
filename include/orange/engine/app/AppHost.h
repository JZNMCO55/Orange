#ifndef ORANGE_ENGINE_APP_APP_HOST_H
#define ORANGE_ENGINE_APP_APP_HOST_H

// ---------------------------------------------------------------------------
// AppHost —— 引擎运行时的所有者 / 主循环驱动。
//
// 它把骨架阶段需要的几个 sub-system 拢到一起：
//   * 一个 Platform::Window；
//   * 一个 LayerStack（游戏侧 / 工具侧通过 Push 注入逻辑）；
//   * 一份每帧产出的 FrameContext。
//
// 主循环每帧的固定顺序：
//   PollEvents → 事件按 reverse 分发到 LayerStack（overlay 优先消费）
//   → 计算 deltaSeconds / 总时长 / 帧序号 → LayerStack 正向 OnUpdate
//   → 渲染（当前为空，待 OrangeRender 接通后填充）→ present。
//
// 退出条件：Window 收到 close 事件（GLFW 已自动 set should-close），
// 或者外部调用 RequestExit。两者任一满足即跳出循环。
//
// 当前帧节拍：vsync 由 swap-chain 保证；不引入 fixed timestep，物理
// 模块上线时再补。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/app/LayerStack.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/platform/Window.h>

#include <memory>

namespace Orange::Engine
{

class ORANGE_ENGINE_API AppHost
{
public:
    static Result<std::unique_ptr<AppHost>, ResultCode> Create(const AppConfig& config);

    AppHost(const AppHost&)            = delete;
    AppHost& operator=(const AppHost&) = delete;
    AppHost(AppHost&&)                 = delete;
    AppHost& operator=(AppHost&&)      = delete;

    ~AppHost();

    // 跑主循环。返回值约定：0 表示正常退出，非 0 留给后续阶段表示异
    // 常退出码（当前永远返回 0）。
    int Run();

    // 任意 Layer / 外部线程都可以调用，下一帧顶部检测后退出循环。
    void RequestExit() noexcept;

    // Layer / overlay 注入入口。返回的裸指针非拥有，仅作身份引用，
    // 用于后续 PopLayer / PopOverlay。
    Layer* PushLayer(std::unique_ptr<Layer> layer);
    Layer* PushOverlay(std::unique_ptr<Layer> overlay);

    Platform::Window& GetWindow() noexcept;
    LayerStack&       GetLayerStack() noexcept;

private:
    AppHost();

    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_APP_APP_HOST_H
