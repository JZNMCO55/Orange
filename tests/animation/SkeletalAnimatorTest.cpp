// SkeletalAnimatorTest —— Phase 4 / Task 03 验收：
// SkeletalAnimator 的 DragonBones 后端能：
//   1. 通过 SkeletonLoader + AssetRegistry 加载 .json skeleton；
//   2. 构造 armature；BoneCount = sortedBones.size；
//   3. Play(animName) 切动画；Tick(dt) 推进 + 输出 pose；
//   4. 10 帧 Tick 后首 dynamic bone（"bullet"）的 tx 在已知曲线上变大；
//   5. playTimes=1（非循环）时 Tick 超过 anim duration 后 IsFinished() == true。
//
// 测试资源用 vendor/DragonBones/Cocos2DX_3.x/Demos/Resources/bullet_01/
// bullet_01_ske.json：
//   * 24 fps、12 帧（0.5 秒）的 "idle" 动画；
//   * 2 根 bone（root / bullet）；
//   * "bullet" bone 在动画里 translate.x 从 0 → 100 线性插值。
// 路径由 ORANGE_ENGINE_TEST_DATA_DIR 在 CMake 注入。
//
// 头隔离：本 test 不直接 include <dragonBones/...>。DragonBonesContext.h
// 是 src 内部头（不在公共 include/ 下），通过 target_include_directories
// 把 src/ 加进 test 的 include path 来访问；context 的方法面里也不暴露
// dragonBones 类型——本 test 只看公共行为。

#include "animation/dragonbones/DragonBonesContext.h"
#include "orange/engine/animation/SkeletalAnimator.h"
#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/asset/SkeletonAsset.h"
#include "orange/engine/asset/SkeletonLoader.h"

#include <cassert>
#include <cstdio>
#include <memory>
#include <string>

namespace DBB = Orange::Engine::Animation::DragonBonesBackend;
namespace Ast = Orange::Engine::Asset;
namespace Ani = Orange::Engine::Animation;

#ifndef ORANGE_ENGINE_TEST_DATA_DIR
#  error "ORANGE_ENGINE_TEST_DATA_DIR must be defined by CMake"
#endif

namespace
{

const std::string kBulletSkePath = std::string(ORANGE_ENGINE_TEST_DATA_DIR) +
                                   "/bullet_01_ske.json";

// 把一个 mat4（column-major）的 translation tx 取出来；与
// SkeletalAnimator::Pose() 的 2D-affine-嵌入-mat4 约定对齐
// （tx 落在 m[3][0]）。inline 模板让 mat4 / mat4x4 typedef 都能进。
template <class M>
float GetTx(const M& m) noexcept { return m[3][0]; }

void TestLoadAndConstruct()
{
    DBB::DragonBonesContext ctx;
    Ast::AssetRegistry      registry;

    auto regResult = registry.RegisterLoader<Ast::SkeletonAsset>(
        std::make_unique<Ast::SkeletonLoader>(ctx));
    assert(regResult.IsOk());

    auto handleR = registry.Load<Ast::SkeletonAsset>(kBulletSkePath);
    if (handleR.IsErr())
    {
        std::fprintf(stderr, "[SkeletalAnimatorTest] Load failed: %s\n",
                     kBulletSkePath.c_str());
    }
    assert(handleR.IsOk());

    const Ast::SkeletonAsset* asset = registry.Get(handleR.Value());
    assert(asset != nullptr);
    assert(!asset->Empty());

    // bullet_01_ske.json 内只有 1 个 armature "bullet_01"，含 root + bullet。
    auto armList = asset->Armatures();
    assert(armList.size() == 1);
    assert(armList[0].name == "bullet_01");
    assert(armList[0].boneNames.size() == 2);
    // 期望第 0 根是 root（顶层），第 1 根是 bullet（root 的子）；
    // sortedBones 按 parent-before-child 顺序。
    assert(armList[0].boneNames[0] == "root");
    assert(armList[0].boneNames[1] == "bullet");

    // armature 内含 1 条动画 "idle"。
    assert(armList[0].animationNames.size() == 1);
    assert(armList[0].animationNames[0] == "idle");
}

void TestTickAdvancesBoneTranslation()
{
    DBB::DragonBonesContext ctx;
    Ast::AssetRegistry      registry;
    auto _ = registry.RegisterLoader<Ast::SkeletonAsset>(
        std::make_unique<Ast::SkeletonLoader>(ctx));
    (void)_;
    auto handleR = registry.Load<Ast::SkeletonAsset>(kBulletSkePath);
    assert(handleR.IsOk());
    const Ast::SkeletonAsset* asset = registry.Get(handleR.Value());
    assert(asset != nullptr);

    Ani::SkeletalAnimator animator(ctx, *asset, "bullet_01");
    assert(animator.BoneCount() == 2);

    // Play "idle"，loop 0 次（用 anim 默认行为：DragonBones 内默认 loop）。
    animator.Play("idle", /*fadeIn=*/0.0f, /*playTimes=*/0);

    // Tick(0) 让 armature 把 bind pose / 第 0 帧 pose 拉到 globalTransformMatrix。
    animator.Tick(0.0f);
    auto pose0 = animator.Pose();
    assert(pose0.size() == 2);
    const float startTx = GetTx(pose0[1]);

    // Tick 10 帧，每帧 1/24 秒（与 .json frameRate=24 对齐）。
    for (int i = 0; i < 10; ++i)
    {
        animator.Tick(1.0f / 24.0f);
    }
    auto pose10 = animator.Pose();
    const float endTx = GetTx(pose10[1]);

    if (!(endTx > startTx + 1.0f))
    {
        std::fprintf(stderr,
                     "[SkeletalAnimatorTest] bullet tx not advancing: start=%.3f end=%.3f\n",
                     startTx, endTx);
    }
    // bullet bone 在 0.5 秒里 tx 0 → 100；10 帧 ≈ 10/12 ≈ 83% 进度，
    // 期望 endTx 至少 > startTx + 1（远高于浮点噪声）；上限 < 110 防爆。
    assert(endTx > startTx + 1.0f);
    assert(endTx < 110.0f);
}

void TestNonLoopingFinishes()
{
    DBB::DragonBonesContext ctx;
    Ast::AssetRegistry      registry;
    auto _ = registry.RegisterLoader<Ast::SkeletonAsset>(
        std::make_unique<Ast::SkeletonLoader>(ctx));
    (void)_;
    auto handleR = registry.Load<Ast::SkeletonAsset>(kBulletSkePath);
    assert(handleR.IsOk());
    const Ast::SkeletonAsset* asset = registry.Get(handleR.Value());
    assert(asset != nullptr);

    Ani::SkeletalAnimator animator(ctx, *asset, "bullet_01");
    animator.Play("idle", /*fadeIn=*/0.0f, /*playTimes=*/1);  // 播一次

    // Tick 远超 0.5 秒的 anim duration（30 帧 = 1.25 秒，足够）。
    for (int i = 0; i < 30; ++i)
    {
        animator.Tick(1.0f / 24.0f);
    }
    if (!animator.IsFinished())
    {
        std::fprintf(stderr,
                     "[SkeletalAnimatorTest] non-looping anim should be finished after 30 ticks\n");
    }
    assert(animator.IsFinished());
}

void TestMissingArmatureNameStaysSafe()
{
    DBB::DragonBonesContext ctx;
    Ast::AssetRegistry      registry;
    auto _ = registry.RegisterLoader<Ast::SkeletonAsset>(
        std::make_unique<Ast::SkeletonLoader>(ctx));
    (void)_;
    auto handleR = registry.Load<Ast::SkeletonAsset>(kBulletSkePath);
    assert(handleR.IsOk());
    const Ast::SkeletonAsset* asset = registry.Get(handleR.Value());
    assert(asset != nullptr);

    // armature 名错 → SkeletalAnimator 构造仍合法，但 BoneCount = 0；
    // Tick / Play 应全部安全 no-op。
    Ani::SkeletalAnimator bad(ctx, *asset, "no_such_armature");
    assert(bad.BoneCount() == 0);
    assert(bad.IsFinished());  // 空 animator 视为已完成
    bad.Play("idle", 0.0f, 0);
    bad.Tick(1.0f / 60.0f);    // 不崩
}

}  // namespace

int main()
{
    TestLoadAndConstruct();
    TestTickAdvancesBoneTranslation();
    TestNonLoopingFinishes();
    TestMissingArmatureNameStaysSafe();
    return 0;
}
