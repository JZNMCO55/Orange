// PostProcessChain 实现：vector<unique_ptr<IPostProcessPass>> 的薄包
// 装。CRUD 全部走标准 vector 语义；越界 / nullptr 输入按 silent-ignore
// 处理（不抛异常、不 log）——与 MaterialInstance 的
// silent-ignore 路径一致。

#include "orange/engine/render/PostProcessChain.h"

namespace Orange::Engine::Render
{

void PostProcessChain::AddPass(std::unique_ptr<IPostProcessPass> pass)
{
    if (!pass)
    {
        return;
    }
    mPasses.push_back(std::move(pass));
}

void PostProcessChain::RemoveAt(std::size_t index)
{
    if (index >= mPasses.size())
    {
        return;
    }
    mPasses.erase(mPasses.begin() + static_cast<std::ptrdiff_t>(index));
}

void PostProcessChain::Clear() noexcept
{
    mPasses.clear();
}

std::size_t PostProcessChain::PassCount() const noexcept
{
    return mPasses.size();
}

IPostProcessPass* PostProcessChain::PassAt(std::size_t index) noexcept
{
    if (index >= mPasses.size())
    {
        return nullptr;
    }
    return mPasses[index].get();
}

const IPostProcessPass* PostProcessChain::PassAt(std::size_t index) const noexcept
{
    if (index >= mPasses.size())
    {
        return nullptr;
    }
    return mPasses[index].get();
}

IPostProcessPass* PostProcessChain::FindByName(std::string_view name) noexcept
{
    for (auto& p : mPasses)
    {
        if (p && std::string_view(p->Name()) == name)
        {
            return p.get();
        }
    }
    return nullptr;
}

const IPostProcessPass* PostProcessChain::FindByName(std::string_view name) const noexcept
{
    for (const auto& p : mPasses)
    {
        if (p && std::string_view(p->Name()) == name)
        {
            return p.get();
        }
    }
    return nullptr;
}

}  // namespace Orange::Engine::Render
