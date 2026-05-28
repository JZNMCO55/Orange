#ifndef ORANGE_ENGINE_APP_LAYER_H
#define ORANGE_ENGINE_APP_LAYER_H

// ---------------------------------------------------------------------------
// Layer —— 游戏侧 / 工具侧把逻辑挂进引擎主循环的唯一扩展点。AppHost 拥
// 有一个 LayerStack；每帧由它依序遍历整个 stack：事件自上而下分发，更
// 新自下而上调用。
//
// 生命周期：
//   OnAttach          —— Layer 进入 stack 时调用一次
//   OnUpdate(frame)   —— 每帧调用；Layer 不应在这里阻塞
//   OnImGui()         —— 每帧 debug-UI 提交点（可选）。仅当宿主接通了引擎
//                       托管的 ImGui overlay（`Pipeline::EnableImGui` +
//                       把 `AppHost::DispatchImGui` 接到 `Pipeline::
//                       SetImGuiSubmit`）时才会被调用，时机在引擎内部
//                       `ImGui::NewFrame` 之后、`ImGui::Render` 之前。
//                       Layer 在此直接调 ImGui API（消费者自行 `#include
//                       <imgui.h>`）提交调参 / 调试窗口。未接通 overlay
//                       时本回调永不触发，default 空实现零开销。
//   OnEvent(event)    —— 每个平台事件都会调用；返回 true 表示已消费、
//                       不再向更低优先级的 Layer 传递
//   OnDetach          —— Layer 离开 stack 时调用一次
//
// Layer 不可拷贝、不可移动。稳定的对象身份很重要，因为 LayerStack::Pop
// 接收的是 PushLayer 当时返回的裸指针，而且 Layer 自身可能在外部以
// `this` 为 key 注册了回调。
//
// 事件传播：
//   OnEvent 返回 `true` 表示 "我已经处理了这个事件，不要继续转发"；
//   返回 `false`（默认）则让 LayerStack 继续往下分发。这与 "overlay
//   永远处在 layer 之上、可以在 layer 看到事件之前先消费" 的契约一致。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/platform/WindowEvent.h>

#include <string>
#include <string_view>

namespace Orange::Engine
{

class ORANGE_ENGINE_API Layer
{
public:
    explicit Layer(std::string_view name = "Layer");
    virtual ~Layer();

    Layer(const Layer&)            = delete;
    Layer& operator=(const Layer&) = delete;
    Layer(Layer&&)                 = delete;
    Layer& operator=(Layer&&)      = delete;

    virtual void OnAttach() {}
    virtual void OnDetach() {}
    virtual void OnUpdate(const FrameContext& /*frame*/) {}

    // debug-UI 提交点。签名刻意不出现任何 ImGui 类型——公共头不漏第三方
    // （consumer 在 .cpp 里自行 #include <imgui.h>）。见上方生命周期说明。
    virtual void OnImGui() {}

    virtual bool OnEvent(const Platform::WindowEvent& /*event*/) { return false; }

    const std::string& GetName() const noexcept { return mName; }

private:
    std::string mName;
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_APP_LAYER_H
