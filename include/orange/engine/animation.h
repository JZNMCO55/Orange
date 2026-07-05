#ifndef ORANGE_ENGINE_ANIMATION_H
#define ORANGE_ENGINE_ANIMATION_H

// Animation 子系统便利聚合头：一次性引入 <orange/engine/animation/*> 全部公共头。
// 维护：animation/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/animation/AnimationClip.h>
#include <orange/engine/animation/AnimationClipLoader.h>
#include <orange/engine/animation/AnimationClipSerialization.h>
#include <orange/engine/animation/AnimationStateMachine.h>
#include <orange/engine/animation/AnimationSystem.h>
#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/AnimatorRegistry.h>
#include <orange/engine/animation/BlendSpace.h>
#include <orange/engine/animation/BlendSpaceAnimator.h>
#include <orange/engine/animation/ClipAnimator.h>
#include <orange/engine/animation/IAnimator.h>
#include <orange/engine/animation/ProceduralAnimator.h>
#include <orange/engine/animation/SkeletalAnimator.h>

#endif // ORANGE_ENGINE_ANIMATION_H
