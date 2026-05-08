#ifndef ORANGE_ENGINE_ANIMATION_ANIMATOR_REGISTRY_H
#define ORANGE_ENGINE_ANIMATION_ANIMATOR_REGISTRY_H

// ---------------------------------------------------------------------------
// AnimatorRegistry —— Animator backend factory 注册表。
//
// 与 MaterialSystem::RegisterTemplate 同节奏（可扩展点）：游戏侧不修改
// 引擎源码地注册自己的 Animator backend。Phase 4 / Task 03 / Task 04 用
// 它注册"skeletal_dragonbones" / "procedural"两条内置 backend。
//
// MaterialSystem 内部要 SPIR-V 加载 + Pipeline 缓存；本类只是个
// `std::unordered_map<string, factory>`，不持 GPU 资源——更轻。
//
// factory 签名：`std::function<std::unique_ptr<IAnimator>()>`。游戏 backend
// 需要参数（如 SkeletonAsset / 起始 anim 名）就在调用方自己 capture 闭包，
// registry 不再加 args 形参——避免 `std::any` 之类的传染性类型走到公共面。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/animation/IAnimator.h>
#include <orange/engine/core/Result.h>

#include <functional>
#include <memory>
#include <string_view>

namespace Orange::Engine::Animation
{

class ORANGE_ENGINE_API AnimatorRegistry
{
public:
    using FactoryFn = std::function<std::unique_ptr<IAnimator>()>;

    AnimatorRegistry();
    ~AnimatorRegistry();

    AnimatorRegistry(const AnimatorRegistry&)            = delete;
    AnimatorRegistry& operator=(const AnimatorRegistry&) = delete;

    AnimatorRegistry(AnimatorRegistry&&) noexcept;
    AnimatorRegistry& operator=(AnimatorRegistry&&) noexcept;

    // 注册一个 backend factory。
    //   * name 重复 → 返回 ResultCode::AlreadyExists，表内不被覆盖；
    //   * factory 为空 → 返回 ResultCode::InvalidArgument；
    //   * 成功 → 返回 Ok。
    Result<void, ResultCode> RegisterBackend(std::string_view name, FactoryFn factory);

    // 创建一个 backend 实例。
    //   * name 未注册 → 返回 nullptr；
    //   * factory 调用结果为 nullptr → 也返回 nullptr（factory 自己决定是否
    //     fail-soft）。
    std::unique_ptr<IAnimator> Create(std::string_view name) const;

    // 是否已注册某 backend 名（诊断 / 单测用）。
    bool HasBackend(std::string_view name) const noexcept;

    std::size_t BackendCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Animation

#endif  // ORANGE_ENGINE_ANIMATION_ANIMATOR_REGISTRY_H
