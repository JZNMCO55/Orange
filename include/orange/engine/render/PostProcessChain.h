#ifndef ORANGE_ENGINE_RENDER_POST_PROCESS_CHAIN_H
#define ORANGE_ENGINE_RENDER_POST_PROCESS_CHAIN_H

// ---------------------------------------------------------------------------
// PostProcessChain —— 顺序敏感的 IPostProcessPass 容器。
//
// 后处理链按 chain[0] → chain[1] → ... → chain[N-1] 顺序执行；上一 pass
// 的输出 color 是下一 pass 的输入。"是否跑某 pass" 由它是否在 chain 里
// 决定——不在 pass 上加 enable bool，避免 `enable=false` 与 "remove 掉"
// 两种禁用方式语义重叠。
//
// 默认构造的 chain 为空。通常调用方走两条路径之一：
//   * `BuiltinPostProcessChain::CreateDefault()` 拿到引擎内置 4-pass 链
//     （HDR → Bloom → Tonemap → LUT），按需 Remove / Add 自由调整；
//   * `PostProcessChain chain;` + 一连串 `AddPass(make_unique<XxxPass>())`
//     从零搭自定义链。
//
// 取出某个 pass 调整参数：`chain.PassAt(1)` 拿 IPostProcessPass*，再
// `dynamic_cast<BloomPass*>` 落到具体类。0.x 阶段 RTTI 是可接受的成本，
// 不在 IPostProcessPass 上加 As<T> 模板 helper（那会绑死 RTTI 风格 +
// 增加 surface area）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/render/IPostProcessPass.h>

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace Orange::Engine::Render
{

class ORANGE_ENGINE_API PostProcessChain
{
public:
    PostProcessChain()  = default;
    ~PostProcessChain() = default;

    PostProcessChain(const PostProcessChain&)            = delete;
    PostProcessChain& operator=(const PostProcessChain&) = delete;

    PostProcessChain(PostProcessChain&&) noexcept            = default;
    PostProcessChain& operator=(PostProcessChain&&) noexcept = default;

    // pass == nullptr 视为 no-op（与 MaterialInstance silent-ignore 同
    // 思路——容器不抛异常，调用方靠 PassCount 增减做断言）。
    void AddPass(std::unique_ptr<IPostProcessPass> pass);

    // 越界视为 no-op。chain 内位置稳定——RemoveAt 不影响其它 pass 的
    // 索引（vector::erase 之后的元素往前移，因此索引会变）。这是 vector
    // 的标准语义，调用方按"删完后从 0 重新拿索引"使用。
    void RemoveAt(std::size_t index);

    void Clear() noexcept;

    std::size_t PassCount() const noexcept;
    bool        Empty()     const noexcept { return PassCount() == 0; }

    IPostProcessPass*       PassAt(std::size_t index) noexcept;
    const IPostProcessPass* PassAt(std::size_t index) const noexcept;

    // 命中第一个 Name() 与 name 相等的 pass；不命中返回 nullptr。
    IPostProcessPass*       FindByName(std::string_view name) noexcept;
    const IPostProcessPass* FindByName(std::string_view name) const noexcept;

private:
    std::vector<std::unique_ptr<IPostProcessPass>> mPasses;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_POST_PROCESS_CHAIN_H
