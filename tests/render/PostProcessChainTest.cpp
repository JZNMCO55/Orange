// PostProcessChain 与 BuiltinPostProcessChain::CreateDefault 的最小单元
// 测试。Phase 3 / Task 03 仅交付接口与默认链描述符——Setup / Execute
// 是空 stub，所以本测试只验"容器语义 + 默认链装配 + 参数可改"。
//
// 5 条路径：
//   1. 默认空 chain + AddPass / PassCount 行为；
//   2. RemoveAt / Clear 后 PassCount 归零、原指针不再被容器持有；
//   3. FindByName 命中第一个 + 不命中 → nullptr；
//   4. BuiltinPostProcessChain::CreateDefault 返回 4-pass chain，顺序为
//      hdr / bloom / tonemap / lut；每个 pass 的 Name() 与具体类型
//      （dynamic_cast）匹配；
//   5. 取出 BloomPass* / TonemapPass* 后修改 public 参数能持久——验证
//      "可改链" 承诺（不是写时复制 / 不是值类型副本）。

#include <orange/engine/render/BuiltinPostProcessChain.h>
#include <orange/engine/render/IPostProcessPass.h>
#include <orange/engine/render/PostProcessChain.h>
#include <orange/engine/render/PostProcessPasses.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>

using Orange::Engine::Render::BloomPass;
using Orange::Engine::Render::GodRaysPass;
using Orange::Engine::Render::HdrPass;
using Orange::Engine::Render::IPostProcessPass;
using Orange::Engine::Render::LutPass;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::TonemapPass;
namespace BuiltinPostProcessChain = Orange::Engine::Render::BuiltinPostProcessChain;

namespace
{

// 1. 默认空 + AddPass / PassCount。
void TestEmptyAndAdd()
{
    PostProcessChain chain;
    assert(chain.PassCount() == 0);
    assert(chain.Empty());
    assert(chain.PassAt(0) == nullptr);

    chain.AddPass(std::make_unique<HdrPass>());
    chain.AddPass(std::make_unique<BloomPass>());
    assert(chain.PassCount() == 2);
    assert(!chain.Empty());

    // 顺序保留：先 add 的 pass 落在前面。
    assert(chain.PassAt(0) != nullptr);
    assert(chain.PassAt(1) != nullptr);
    assert(std::string_view(chain.PassAt(0)->Name()) == "hdr");
    assert(std::string_view(chain.PassAt(1)->Name()) == "bloom");

    // nullptr 输入 silent-ignore（与 MaterialInstance 同思路）。
    chain.AddPass(nullptr);
    assert(chain.PassCount() == 2);

    // 越界 PassAt 返回 nullptr。
    assert(chain.PassAt(2) == nullptr);
    assert(chain.PassAt(99) == nullptr);
}

// 2. RemoveAt / Clear。
void TestRemoveAndClear()
{
    PostProcessChain chain;
    chain.AddPass(std::make_unique<HdrPass>());
    chain.AddPass(std::make_unique<BloomPass>());
    chain.AddPass(std::make_unique<TonemapPass>());
    assert(chain.PassCount() == 3);

    // RemoveAt(1) 删 bloom，剩 hdr / tonemap，索引 1 现在指向 tonemap。
    chain.RemoveAt(1);
    assert(chain.PassCount() == 2);
    assert(std::string_view(chain.PassAt(0)->Name()) == "hdr");
    assert(std::string_view(chain.PassAt(1)->Name()) == "tonemap");

    // 越界 RemoveAt 是 no-op。
    chain.RemoveAt(99);
    assert(chain.PassCount() == 2);

    chain.Clear();
    assert(chain.PassCount() == 0);
    assert(chain.Empty());
    assert(chain.PassAt(0) == nullptr);
}

// 3. FindByName。
void TestFindByName()
{
    PostProcessChain chain;
    chain.AddPass(std::make_unique<HdrPass>());
    chain.AddPass(std::make_unique<BloomPass>());
    chain.AddPass(std::make_unique<TonemapPass>());

    IPostProcessPass* bloom = chain.FindByName("bloom");
    assert(bloom != nullptr);
    assert(bloom == chain.PassAt(1));
    assert(dynamic_cast<BloomPass*>(bloom) != nullptr);

    assert(chain.FindByName("nonexistent") == nullptr);
    assert(chain.FindByName("") == nullptr);

    // const 路径。
    const PostProcessChain& constChain = chain;
    const IPostProcessPass* hdrConst = constChain.FindByName("hdr");
    assert(hdrConst != nullptr);
    assert(std::string_view(hdrConst->Name()) == "hdr");
    assert(constChain.FindByName("nonexistent") == nullptr);
}

// 4. CreateDefault 顺序与类型。
void TestCreateDefault()
{
    PostProcessChain chain = BuiltinPostProcessChain::CreateDefault();
    // hdr / bloom / god_rays / tonemap / lut 五件内置 pass。
    assert(chain.PassCount() == 5);

    IPostProcessPass* p0 = chain.PassAt(0);
    IPostProcessPass* p1 = chain.PassAt(1);
    IPostProcessPass* p2 = chain.PassAt(2);
    IPostProcessPass* p3 = chain.PassAt(3);
    IPostProcessPass* p4 = chain.PassAt(4);
    assert(p0 && p1 && p2 && p3 && p4);

    // Name() 与文档一致。
    assert(std::string_view(p0->Name()) == "hdr");
    assert(std::string_view(p1->Name()) == "bloom");
    assert(std::string_view(p2->Name()) == "god_rays");
    assert(std::string_view(p3->Name()) == "tonemap");
    assert(std::string_view(p4->Name()) == "lut");

    // 具体类型 dynamic_cast 落得到。
    assert(dynamic_cast<HdrPass*>(p0) != nullptr);
    assert(dynamic_cast<BloomPass*>(p1) != nullptr);
    assert(dynamic_cast<GodRaysPass*>(p2) != nullptr);
    assert(dynamic_cast<TonemapPass*>(p3) != nullptr);
    assert(dynamic_cast<LutPass*>(p4) != nullptr);

    // 类型不匹配返回 nullptr。
    assert(dynamic_cast<BloomPass*>(p0) == nullptr);
}

// 5. 默认链上的参数可读可改。
void TestDefaultChainParametersMutable()
{
    PostProcessChain chain = BuiltinPostProcessChain::CreateDefault();

    auto* bloom    = dynamic_cast<BloomPass*>(chain.PassAt(1));
    auto* godRays  = dynamic_cast<GodRaysPass*>(chain.PassAt(2));
    auto* tonemap  = dynamic_cast<TonemapPass*>(chain.PassAt(3));
    auto* lut      = dynamic_cast<LutPass*>(chain.PassAt(4));
    assert(bloom && godRays && tonemap && lut);

    // 默认值与 PostProcessPasses.h 注释一致。
    assert(bloom->threshold == 1.0f);
    assert(bloom->intensity == 0.5f);
    assert(tonemap->exposure == 1.0f);
    assert(lut->strength == 1.0f);
    assert(!lut->lut.IsValid());

    // GodRaysPass 默认 enabled=false—— sample 主动开启再调参，避免
    // 既有 sample 视觉漂移；其他默认值保留 PostProcessPasses.h 头文件
    // 注释里写的合理值。
    assert(godRays->enabled == false);
    assert(godRays->numSamples == 64);
    assert(godRays->density == 1.2f);
    assert(godRays->decay == 0.97f);
    assert(godRays->weight == 0.04f);
    assert(godRays->exposure == 1.0f);

    // 改完后再次取出同一 pass，参数应持久（验证 chain 持有 pass 而不
    // 是返回拷贝）。
    bloom->threshold     = 0.8f;
    bloom->intensity     = 0.7f;
    tonemap->exposure    = 2.0f;
    lut->strength        = 0.25f;
    godRays->enabled     = true;
    godRays->density     = 2.0f;
    godRays->numSamples  = 128;

    auto* bloom2   = dynamic_cast<BloomPass*>(chain.PassAt(1));
    auto* godRays2 = dynamic_cast<GodRaysPass*>(chain.PassAt(2));
    auto* tonemap2 = dynamic_cast<TonemapPass*>(chain.PassAt(3));
    auto* lut2     = dynamic_cast<LutPass*>(chain.PassAt(4));
    assert(bloom2 == bloom);
    assert(godRays2 == godRays);
    assert(tonemap2 == tonemap);
    assert(lut2 == lut);
    assert(bloom2->threshold == 0.8f);
    assert(bloom2->intensity == 0.7f);
    assert(tonemap2->exposure == 2.0f);
    assert(lut2->strength == 0.25f);
    assert(godRays2->enabled == true);
    assert(godRays2->density == 2.0f);
    assert(godRays2->numSamples == 128);
}

// 6. GodRaysPass 在 chain 里查找 + 与 BloomPass 类型隔离。
void TestGodRaysFindAndIsolation()
{
    PostProcessChain chain = BuiltinPostProcessChain::CreateDefault();

    IPostProcessPass* p = chain.FindByName("god_rays");
    assert(p != nullptr);
    assert(std::string_view(p->Name()) == "god_rays");

    GodRaysPass* gr = dynamic_cast<GodRaysPass*>(p);
    assert(gr != nullptr);

    // BloomPass / TonemapPass 等其它 pass 不会被错认成 GodRaysPass。
    auto* bloom = chain.FindByName("bloom");
    assert(bloom != nullptr);
    assert(dynamic_cast<GodRaysPass*>(bloom) == nullptr);
}

}  // namespace

int main()
{
    TestEmptyAndAdd();
    TestRemoveAndClear();
    TestFindByName();
    TestCreateDefault();
    TestDefaultChainParametersMutable();
    TestGodRaysFindAndIsolation();
    std::printf("postprocess_chain_test: OK\n");
    return 0;
}
