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

    virtual bool OnEvent(const Platform::WindowEvent& /*event*/) { return false; }

    const std::string& GetName() const noexcept { return mName; }

private:
    std::string mName;
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_APP_LAYER_H
