// BuiltinPostProcessChain::CreateDefault 实现：按 HDR → Bloom → Tonemap
// → LUT 顺序填 4 个内置 pass，全 default 参数。

#include "orange/engine/render/BuiltinPostProcessChain.h"

#include "orange/engine/render/PostProcessPasses.h"

#include <memory>

namespace Orange::Engine::Render::BuiltinPostProcessChain
{

PostProcessChain CreateDefault()
{
    PostProcessChain chain;
    chain.AddPass(std::make_unique<HdrPass>());
    chain.AddPass(std::make_unique<BloomPass>());
    chain.AddPass(std::make_unique<TonemapPass>());
    chain.AddPass(std::make_unique<LutPass>());
    return chain;
}

}  // namespace Orange::Engine::Render::BuiltinPostProcessChain
