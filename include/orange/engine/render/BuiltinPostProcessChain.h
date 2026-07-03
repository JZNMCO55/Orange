#ifndef ORANGE_ENGINE_RENDER_BUILTIN_POST_PROCESS_CHAIN_H
#define ORANGE_ENGINE_RENDER_BUILTIN_POST_PROCESS_CHAIN_H

// ---------------------------------------------------------------------------
// BuiltinPostProcessChain —— 引擎内置默认后处理链工厂。
//
// `CreateDefault()` 返回按 HDR → Bloom → Tonemap → LUT 顺序填好的
// PostProcessChain。每个 pass 用 default 参数构造，调用方接到链后通过
// `chain.PassAt(i)` + dynamic_cast 调整具体参数：
//
//     auto chain = BuiltinPostProcessChain::CreateDefault();
//     if (auto* bloom = dynamic_cast<BloomPass*>(chain.PassAt(1)))
//     {
//         bloom->intensity = 0.7f;
//     }
//
// 工厂返回值是 chain（值类型移动语义），调用方拥有它的全部 pass。chain
// 析构时把 unique_ptr<IPostProcessPass> 全部释放。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/render/PostProcessChain.h>

namespace Orange::Engine::Render::BuiltinPostProcessChain
{

    ORANGE_ENGINE_API PostProcessChain CreateDefault();

} // namespace Orange::Engine::Render::BuiltinPostProcessChain

#endif // ORANGE_ENGINE_RENDER_BUILTIN_POST_PROCESS_CHAIN_H
