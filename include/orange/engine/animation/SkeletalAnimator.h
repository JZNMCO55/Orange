#ifndef ORANGE_ENGINE_ANIMATION_SKELETAL_ANIMATOR_H
#define ORANGE_ENGINE_ANIMATION_SKELETAL_ANIMATOR_H

// ---------------------------------------------------------------------------
// SkeletalAnimator —— IAnimator 的 DragonBones 后端。
//
// 输入：SkeletonAsset（loader 解析出的骨架数据 + 工厂索引）+ armatureName
//      （asset 内某个 armature 的名字）。运行期内部维护一个 dragonBones::
//      Armature 实例，由 DragonBonesContext 拿其 BaseFactory 创建。
//
// 行为：
//   * Tick(dt)：armature->advanceTime(dt) 推进动画 + 重算 bone 全局变换；
//   * Pose() const：返回 std::span<const glm::mat4>，元素 = sortedBones
//     顺序的 per-bone world transform（DragonBones 是 2D，所以 mat4 的
//     z 维退化：[a c 0 tx; b d 0 ty; 0 0 1 0; 0 0 0 1]）；
//   * Play(animName, fadeIn, playTimes)：切动画；playTimes = 0 → 用 anim
//     默认（通常 loop），= -1 → 永远 loop，> 0 → 播 N 次后 IsFinished();
//   * IsFinished()：armature->getAnimation()->isCompleted();
//   * BackendName() = "skeletal_dragonbones"。
//
// 头隔离：本头不暴露 dragonBones 类型——通过前向声明 +
// DragonBonesContext 公共面承接 runtime；实现在 src/animation/dragonbones/
// SkeletalAnimator.cpp 单点 include <dragonBones/...>。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/animation/IAnimator.h>
#include <orange/engine/asset/SkeletonAsset.h>

#include <glm/mat4x4.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace Orange::Engine::Animation::DragonBonesBackend
{
class DragonBonesContext;
}

namespace Orange::Engine::Animation
{

class ORANGE_ENGINE_API SkeletalAnimator final : public IAnimator
{
public:
    // ctx 的生命周期必须包住 SkeletalAnimator；asset 必须由 AssetRegistry
    // / 调用方保证在 SkeletalAnimator 存活期间不释放。
    // armatureName 不在 asset 里 → 后续 Tick / Pose / Play 全部 no-op，
    // BoneCount() == 0；调用方可用 BoneCount > 0 判 ctor 是否成功。
    SkeletalAnimator(DragonBonesBackend::DragonBonesContext& ctx,
                     const Orange::Engine::Asset::SkeletonAsset& asset,
                     std::string_view armatureName);
    ~SkeletalAnimator() override;

    SkeletalAnimator(const SkeletalAnimator&)            = delete;
    SkeletalAnimator& operator=(const SkeletalAnimator&) = delete;
    SkeletalAnimator(SkeletalAnimator&&)                 = delete;
    SkeletalAnimator& operator=(SkeletalAnimator&&)      = delete;

    // IAnimator
    void             Tick(float dt) override;
    bool             IsFinished() const noexcept override;
    std::string_view BackendName() const noexcept override;

    // 切动画。playTimes 语义对齐 DragonBones：
    //   * -1 → 用 animation data 默认（通常 looping clip 走 0 = 无限）
    //   *  0 → 永远 loop
    //   *  N>0 → 播 N 次后 isCompleted == true
    // animName 不存在 → no-op。fadeInSeconds < 0 → 0。
    void Play(std::string_view animName,
              float            fadeInSeconds = 0.0f,
              int              playTimes     = -1);

    // 当前 pose（per-bone world transform）。span 指向内部缓冲，下一次
    // Tick 之前有效——调用方 Tick 之间保留指针都不安全。
    // ctor 失败 / armature 未建好 → 返回空 span。
    std::span<const glm::mat4> Pose() const noexcept;

    // 当前 armature 的 bone 数量。ctor 失败 → 0。
    std::size_t BoneCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Animation

#endif  // ORANGE_ENGINE_ANIMATION_SKELETAL_ANIMATOR_H
