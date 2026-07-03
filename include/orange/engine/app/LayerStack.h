#ifndef ORANGE_ENGINE_APP_LAYER_STACK_H
#define ORANGE_ENGINE_APP_LAYER_STACK_H

// ---------------------------------------------------------------------------
// LayerStack —— AppHost 每帧 tick 的 Layer 容器。
//
// 概念上是同一段连续 vector 中的两个分区：
//
//   [ 普通 layer ... ][ overlay ... ]
//                     ^
//                     mLayerInsertIndex —— 第一个 overlay 槽位
//
// 为什么用单个 vector 而不是两个？因为遍历是热点：AppHost 在 OnUpdate
// 时按 front-to-back 跑（普通 layer 在前、overlay 在后渲染），在
// OnEvent 时按 reverse 跑（overlay 优先拿到事件）。单 vector 让两条
// 路径都不需要分支判断当前在哪一段。
//
// 所有权：
//   LayerStack 用 unique_ptr 持有 Layer 的所有权。Push* 拿走所有权，
//   返回一个非拥有指针给调用方做后续 Pop* 的身份引用。析构时按反向
//   插入顺序 detach，与构造时序对称。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace Orange::Engine
{

    class Layer;

    class ORANGE_ENGINE_API LayerStack
    {
    public:
        using LayerPtr  = std::unique_ptr<Layer>;
        using Container = std::vector<LayerPtr>;

        LayerStack();
        ~LayerStack();

        LayerStack(const LayerStack&)            = delete;
        LayerStack& operator=(const LayerStack&) = delete;
        LayerStack(LayerStack&&)                 = delete;
        LayerStack& operator=(LayerStack&&)      = delete;

        Layer* PushLayer(LayerPtr layer);
        Layer* PushOverlay(LayerPtr overlay);

        LayerPtr PopLayer(Layer* layer);
        LayerPtr PopOverlay(Layer* overlay);

        std::size_t Size() const noexcept { return mLayers.size(); }
        bool        Empty() const noexcept { return mLayers.empty(); }

        Container::iterator       begin() noexcept { return mLayers.begin(); }
        Container::iterator       end() noexcept { return mLayers.end(); }
        Container::const_iterator begin() const noexcept { return mLayers.begin(); }
        Container::const_iterator end() const noexcept { return mLayers.end(); }

        Container::reverse_iterator       rbegin() noexcept { return mLayers.rbegin(); }
        Container::reverse_iterator       rend() noexcept { return mLayers.rend(); }
        Container::const_reverse_iterator rbegin() const noexcept { return mLayers.rbegin(); }
        Container::const_reverse_iterator rend() const noexcept { return mLayers.rend(); }

    private:
        Container   mLayers;
        std::size_t mLayerInsertIndex{0};
    };

} // namespace Orange::Engine

#endif // ORANGE_ENGINE_APP_LAYER_STACK_H
