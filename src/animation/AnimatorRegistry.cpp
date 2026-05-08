// AnimatorRegistry impl ——
// name → factory 的简单查表注册器。与 MaterialSystem::RegisterTemplate
// 公共面同节奏，但实现远更轻：没有资源加载 / GPU pipeline 缓存。

#include "orange/engine/animation/AnimatorRegistry.h"

#include <string>
#include <unordered_map>
#include <utility>

namespace Orange::Engine::Animation
{

struct AnimatorRegistry::Impl
{
    std::unordered_map<std::string, FactoryFn> backends;
};

AnimatorRegistry::AnimatorRegistry()
    : mpImpl(std::make_unique<Impl>())
{
}

AnimatorRegistry::~AnimatorRegistry() = default;

AnimatorRegistry::AnimatorRegistry(AnimatorRegistry&&) noexcept            = default;
AnimatorRegistry& AnimatorRegistry::operator=(AnimatorRegistry&&) noexcept = default;

Result<void, ResultCode>
AnimatorRegistry::RegisterBackend(std::string_view name, FactoryFn factory)
{
    if (!mpImpl)
    {
        return ResultCode::NotInitialized;
    }
    if (!factory)
    {
        return ResultCode::InvalidArgument;
    }
    std::string key{name};
    if (mpImpl->backends.find(key) != mpImpl->backends.end())
    {
        return ResultCode::AlreadyExists;
    }
    mpImpl->backends.emplace(std::move(key), std::move(factory));
    return {};
}

std::unique_ptr<IAnimator>
AnimatorRegistry::Create(std::string_view name) const
{
    if (!mpImpl)
    {
        return nullptr;
    }
    const auto it = mpImpl->backends.find(std::string(name));
    if (it == mpImpl->backends.end())
    {
        return nullptr;
    }
    return it->second();  // factory 自身决定是否返 nullptr
}

bool AnimatorRegistry::HasBackend(std::string_view name) const noexcept
{
    if (!mpImpl)
    {
        return false;
    }
    return mpImpl->backends.find(std::string(name)) != mpImpl->backends.end();
}

std::size_t AnimatorRegistry::BackendCount() const noexcept
{
    return mpImpl ? mpImpl->backends.size() : 0;
}

}  // namespace Orange::Engine::Animation
