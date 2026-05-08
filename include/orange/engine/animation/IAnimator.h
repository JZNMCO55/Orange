#ifndef ORANGE_ENGINE_ANIMATION_I_ANIMATOR_H
#define ORANGE_ENGINE_ANIMATION_I_ANIMATOR_H

// ---------------------------------------------------------------------------
// IAnimator —— Animation 后端抽象基类。
//
// Animation 子系统在 Phase 4 即承诺 Skeletal + Procedural 双后端并列。
// 两条路径输出形态完全不同（matrix palette / shader uniform），因此
// IAnimator 故意**不**规定 pose / uniform 输出 API——只暴露最小驱动入
// 口 `Tick(dt)`：
//   * Skeletal 后端（DragonBones）通过 SkeletonAsset 内部 palette + Render
//     模块的 SkinningMatrixPalette uniform 路径把 pose 上 GPU；
//   * Procedural 后端通过 MaterialInstance::SetUniform 把曲线 / 噪声值
//     写进 Material 内表，Pipeline 在主 pass 用现有 push-constant /
//     Material UBO 通路下发。
// 两条路径**通过 ECS 自然解耦**——IAnimator 上层不需要知道下游消费方。
//
// 生命周期：IAnimator 由调用方（典型：游戏 main / sample 端）持有 unique_ptr，
// AnimatorComponent 持有同一指针的 std::unique_ptr 转移过来。析构无需特殊
// 顺序，IAnimator 不持 GPU 资源。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <string_view>

namespace Orange::Engine::Animation
{

class ORANGE_ENGINE_API IAnimator
{
public:
    virtual ~IAnimator() = default;

    IAnimator() = default;

    IAnimator(const IAnimator&)            = delete;
    IAnimator& operator=(const IAnimator&) = delete;
    IAnimator(IAnimator&&)                 = delete;
    IAnimator& operator=(IAnimator&&)      = delete;

    // 推进动画时间。dt = 本帧间隔（秒）。后端按需更新内部 elapsed clock /
    // pose / uniform 值。dt < 0 由后端自行决定（典型：clamp 到 0；不强制）。
    virtual void Tick(float dt) = 0;

    // 当前动画是否已自然结束（非 looping clip 走完最后一帧）。looping 后端
    // 永远返回 false。AnimationStateMachine 用它判断"clip done"型自动过渡。
    virtual bool IsFinished() const noexcept = 0;

    // backend 标识符（"skeletal_dragonbones" / "procedural" / 游戏自注册的
    // 名字等）。AnimatorRegistry 按这个名索引；调用方 / 调试日志按这个名识别。
    virtual std::string_view BackendName() const noexcept = 0;
};

}  // namespace Orange::Engine::Animation

#endif  // ORANGE_ENGINE_ANIMATION_I_ANIMATOR_H
