#ifndef ORANGE_ENGINE_APP_FRAME_CONTEXT_H
#define ORANGE_ENGINE_APP_FRAME_CONTEXT_H

// ---------------------------------------------------------------------------
// FrameContext —— 每帧只读快照，传给每个 Layer 的 `OnUpdate`。契约刻
// 意保持狭窄：
//
//   * 内部各字段在一帧内部保持不变。Layer 可以在自己的 `OnUpdate` 内
//     缓存对字段的引用，但禁止跨帧持有指针；
//   * `pWindow` 是非拥有的 observer。Layer 可以通过它查询 window 状
//     态（尺寸、关闭请求），但不应销毁它。Window 的生命周期由 AppHost
//     拥有。
//
// 为什么不直接传 Window？
//   绝大多数 Layer 只需读取时间 + framebuffer 尺寸；把这些做成 plain
//   value 既能让常见路径少一次间接寻址，也让 Layer 单测变得很简单
//   （直接构造一个 FrameContext literal、`pWindow` 留 null 即可）。指
//   针只为少数确实需要回话平台层（比如 request close）的 Layer 提供。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Time.h>

#include <cstdint>

namespace Orange::Engine
{

    namespace Platform
    {
        class Window;
    }

    struct FrameContext
    {
        TimeStamp         time{};
        std::uint32_t     framebufferWidth{0};
        std::uint32_t     framebufferHeight{0};
        Platform::Window* pWindow{nullptr};
    };

} // namespace Orange::Engine

#endif // ORANGE_ENGINE_APP_FRAME_CONTEXT_H
