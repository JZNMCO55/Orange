#ifndef ORANGE_ENGINE_ANIMATION_BLEND_SPACE_ANIMATOR_H
#define ORANGE_ENGINE_ANIMATION_BLEND_SPACE_ANIMATOR_H

// ---------------------------------------------------------------------------
// BlendSpaceAnimator —— IAnimator 的 1D blend space 后端（参数化 locomotion 混合）。
//
// 与 ClipAnimator 配对：后者播放单个 clip；本后端持一个 BlendSpace1D（一组带
// 参数坐标的样本 clip），逐帧按外部连续参数（如 speed）在样本间空间混合，同时
// 用归一化 phase 保持各 clip 步态同步——典型 idle↔walk↔run 无缝过渡。数据原语
// 与混合数学在 BlendSpace.h（header-only，headless 可测），本类只负责"推进 phase +
// 按当前参数采样写 TransformComponent"的运行时驱动。
//
// 1D vs 2D：本 animator 先只做 1D（locomotion 最常见用例）。2D blend space 的数据
// 原语（BlendSpace2D + EvaluateBlendSpace2D）已在 BlendSpace.h 就绪，可被游戏 /
// 未来 2D animator 直接使用，无需本类。
//
// 与 FSM 的关系：blend space 是"状态内的连续混合层"，与 AnimationStateMachine 的
// "状态间离散切换"正交。典型接线：FSM 的某个 locomotion 状态 OnEnter 挂一个
// BlendSpaceAnimator，gameplay 每帧把角色速度喂进 SetBlendParameter；离开该状态
// 时切走。写目标的持有方式沿用 ClipAnimator：持非拥有 TransformComponent*，由调用
// 方 SetTarget 接上，生命周期约定一致（animator 与 target 同 entity）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/animation/BlendSpace.h>
#include <orange/engine/animation/IAnimator.h>
#include <orange/engine/scene/TransformComponent.h>

#include <string_view>
#include <utility>

namespace Orange::Engine::Animation
{

class ORANGE_ENGINE_API BlendSpaceAnimator final : public IAnimator
{
public:
    // space 按值拷入并被 animator 拥有（自包含、可独立测试，与 ClipAnimator 按值持
    // clip 同一心智）。target 可为 nullptr——半构造态，Tick 仍推进 phase、只是不写
    // 任何字段，之后 SetTarget 接上即可。构造时若 target 非空，捕获其当前姿势作
    // baseline（各样本 clip 未驱动字段的基值）。
    explicit BlendSpaceAnimator(BlendSpace1D space = {}, Scene::TransformComponent* target = nullptr);
    ~BlendSpaceAnimator() override;

    BlendSpaceAnimator(const BlendSpaceAnimator&)            = delete;
    BlendSpaceAnimator& operator=(const BlendSpaceAnimator&) = delete;
    BlendSpaceAnimator(BlendSpaceAnimator&&)                 = delete;
    BlendSpaceAnimator& operator=(BlendSpaceAnimator&&)      = delete;

    // 切换写目标。非 nullptr 时捕获其当前姿势作 baseline（供样本 clip 未驱动的字段）。
    // nullptr → 暂停写入（Tick 仍推进 phase）。
    void                       SetTarget(Scene::TransformComponent* target);
    Scene::TransformComponent* GetTarget() const noexcept;

    // 混合参数（连续输入，如角色 speed）。落在样本参数轴外 → clamp 到端点样本。
    void  SetBlendParameter(float x) noexcept;
    float BlendParameter() const noexcept;

    // 归一化步态相位 [0,1)（loop）/ [0,1]（非 loop）。由 Tick 推进；只读访问器供调试 /
    // 编辑器进度显示。
    float Phase() const noexcept;

    // 播放速率倍数（默认 1.0）。Tick 按 dt*speed 推进 phase。
    void  SetSpeed(float speed) noexcept;
    float Speed() const noexcept;

    // 循环：loop（默认）→ phase 到 1 回卷到 0，IsFinished 永远 false；非 loop → phase
    // clamp 到 [0,1]，到 1 视作 finished。
    void  SetLoop(bool loop) noexcept;
    bool  IsLooping() const noexcept;

    // 播放控制。Play/Pause 只切 mPlaying（Tick 据此决定是否推进）。Stop 复位 phase=0。
    void  Play() noexcept;
    void  Pause() noexcept;
    void  Stop() noexcept;
    bool  IsPlaying() const noexcept;

    const BlendSpace1D& Space() const noexcept;

    // IAnimator
    // mPlaying 且 dt>=0 时：按当前参数的混合时长归一化推进 phase，再对 blend space
    // 求值写 target。否则不动。
    void             Tick(float dt) override;
    // loop → 永远 false；非 loop → phase >= 1。
    bool             IsFinished() const noexcept override;
    std::string_view BackendName() const noexcept override;

private:
    BlendSpace1D               mSpace;
    Scene::TransformComponent* mpTarget{nullptr};
    Scene::TransformComponent  mBaseline;  // SetTarget 时捕获（未驱动字段基值）；无 target 用默认
    float                      mBlendParam{0.0f};
    float                      mPhase{0.0f};
    float                      mSpeed{1.0f};
    bool                       mPlaying{true};
    bool                       mLoop{true};
};

}  // namespace Orange::Engine::Animation

#endif  // ORANGE_ENGINE_ANIMATION_BLEND_SPACE_ANIMATOR_H
