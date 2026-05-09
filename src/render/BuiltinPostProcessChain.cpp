// BuiltinPostProcessChain::CreateDefault 实现：按 HDR → Bloom → GodRays
// → Tonemap → LUT 顺序填 5 个内置 pass，全 default 参数。GodRaysPass
// 默认 enabled = false——既有 sample 不需要 god rays 时视觉零变化；
// sample 端 dynamic_cast<GodRaysPass*>(chain.FindByName("god_rays"))
// 拿到后改 enabled / 调参即可。

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
    chain.AddPass(std::make_unique<GodRaysPass>());     // enabled = false 默认
    chain.AddPass(std::make_unique<TonemapPass>());
    chain.AddPass(std::make_unique<LutPass>());
    return chain;
}

}  // namespace Orange::Engine::Render::BuiltinPostProcessChain
