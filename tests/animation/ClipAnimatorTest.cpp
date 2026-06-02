// ClipAnimator 的 headless 单元测试（B2.2）。
// 锁住"AnimationClip 数据曲线 → 实体本地 TransformComponent"的运行时消费路径：
//   * targetName → Transform 字段的解析（ParseTransformTarget）
//   * position / rotation(euler) / scale / 单轴 / uniform 各通道的写入
//   * 播放控制（Play/Pause/Stop/Seek/loop/clamp）与 IsFinished
//   * 经 AnimatorComponent + TickAnimators 的端到端整合（World 真实 entity）
// 纯 CPU：栈上 TransformComponent + glm，无 GPU / 无 Vulkan。

#include "orange/engine/animation/ClipAnimator.h"

#include "orange/engine/animation/AnimationSystem.h"
#include "orange/engine/animation/AnimatorComponent.h"
#include "orange/engine/scene/TransformComponent.h"
#include "orange/engine/scene/World.h"

#include <glm/geometric.hpp>  // glm::length
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace Anim  = ::Orange::Engine::Animation;
namespace Scene = ::Orange::Engine::Scene;

namespace
{

bool Near(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) < eps;
}

bool NearV3(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
{
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

bool NearQuat(const glm::quat& a, const glm::quat& b, float eps = 1e-4f)
{
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps) && Near(a.w, b.w, eps);
}

Anim::Keyframe LinKey(float time, glm::vec4 value)
{
    Anim::Keyframe k;
    k.time   = time;
    k.value  = value;
    k.interp = Anim::InterpMode::Linear;
    return k;
}

// 造一条给定 valueType 的 track（关键帧已是 vec4，调用方按维度填）。
Anim::AnimationTrack MakeTrack(const char* targetName, Anim::TrackValueType vt,
                               std::vector<Anim::Keyframe> keys)
{
    Anim::AnimationTrack t;
    t.targetName = targetName;
    t.valueType  = vt;
    t.keys       = std::move(keys);
    return t;
}

}  // namespace

int main()
{
    using Anim::ClipAnimator;
    using Anim::TransformTarget;
    using Anim::TrackValueType;

    // ===== 1. ParseTransformTarget 名字映射 =====
    {
        assert(Anim::ParseTransformTarget("position") == TransformTarget::Position);
        assert(Anim::ParseTransformTarget("position.x") == TransformTarget::PositionX);
        assert(Anim::ParseTransformTarget("position.y") == TransformTarget::PositionY);
        assert(Anim::ParseTransformTarget("position.z") == TransformTarget::PositionZ);
        assert(Anim::ParseTransformTarget("rotation") == TransformTarget::RotationEuler);
        assert(Anim::ParseTransformTarget("rotation.euler") == TransformTarget::RotationEuler);
        assert(Anim::ParseTransformTarget("scale") == TransformTarget::Scale);
        assert(Anim::ParseTransformTarget("scale.x") == TransformTarget::ScaleX);
        assert(Anim::ParseTransformTarget("scale.uniform") == TransformTarget::ScaleUniform);
        assert(Anim::ParseTransformTarget("bogus") == TransformTarget::Unknown);
        assert(Anim::ParseTransformTarget("") == TransformTarget::Unknown);
        std::fprintf(stdout, "  [PASS] ParseTransformTarget 名字映射\n");
    }

    // ===== 2. position Vec3 track 线性中点驱动 position =====
    {
        Anim::AnimationClip clip;
        clip.name = "move_x";
        clip.tracks.push_back(MakeTrack("position", TrackValueType::Vec3,
                                        {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                         LinKey(2.0f, glm::vec4(10, 0, 0, 0))}));
        Scene::TransformComponent tc;
        ClipAnimator anim(std::move(clip), &tc);
        anim.Tick(1.0f);  // t=1 → 线性中点
        assert(NearV3(tc.position, glm::vec3(5.0f, 0.0f, 0.0f)) && "position track 中点应为 5");
        std::fprintf(stdout, "  [PASS] position Vec3 track 线性驱动\n");
    }

    // ===== 3. position.y 标量（Float track）单轴驱动 =====
    {
        Anim::AnimationClip clip;
        clip.tracks.push_back(MakeTrack("position.y", TrackValueType::Float,
                                        {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                         LinKey(1.0f, glm::vec4(8, 0, 0, 0))}));
        Scene::TransformComponent tc;
        tc.position = glm::vec3(3.0f, 0.0f, 7.0f);  // x/z 应保持不动
        ClipAnimator anim(std::move(clip), &tc);
        anim.Tick(0.5f);
        assert(Near(tc.position.y, 4.0f) && "position.y 应到 4");
        assert(Near(tc.position.x, 3.0f) && Near(tc.position.z, 7.0f) && "x/z 不受单轴 track 影响");
        std::fprintf(stdout, "  [PASS] position.y 标量单轴驱动\n");
    }

    // ===== 4. scale Vec3 track =====
    {
        Anim::AnimationClip clip;
        clip.tracks.push_back(MakeTrack("scale", TrackValueType::Vec3,
                                        {LinKey(0.0f, glm::vec4(1, 1, 1, 0)),
                                         LinKey(1.0f, glm::vec4(2, 3, 4, 0))}));
        Scene::TransformComponent tc;
        ClipAnimator anim(std::move(clip), &tc);
        anim.Seek(1.0f);  // 末帧
        assert(NearV3(tc.scale, glm::vec3(2.0f, 3.0f, 4.0f)) && "scale 应到末帧值");
        std::fprintf(stdout, "  [PASS] scale Vec3 track\n");
    }

    // ===== 5. scale.uniform 标量驱动三轴 =====
    {
        Anim::AnimationClip clip;
        clip.tracks.push_back(MakeTrack("scale.uniform", TrackValueType::Float,
                                        {LinKey(0.0f, glm::vec4(1, 0, 0, 0)),
                                         LinKey(1.0f, glm::vec4(5, 0, 0, 0))}));
        Scene::TransformComponent tc;
        ClipAnimator anim(std::move(clip), &tc);
        anim.Seek(0.5f);
        assert(NearV3(tc.scale, glm::vec3(3.0f, 3.0f, 3.0f)) && "uniform 标量应均匀写三轴");
        std::fprintf(stdout, "  [PASS] scale.uniform 标量驱动三轴\n");
    }

    // ===== 6. rotation.euler → 四元数（与 glm::quat(radians(euler)) 同序合成）=====
    {
        Anim::AnimationClip clip;
        clip.tracks.push_back(MakeTrack("rotation", TrackValueType::Vec3,
                                        {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                         LinKey(1.0f, glm::vec4(0, 90, 0, 0))}));
        Scene::TransformComponent tc;
        ClipAnimator anim(std::move(clip), &tc);
        anim.Seek(1.0f);
        const glm::quat expected = glm::quat(glm::radians(glm::vec3(0.0f, 90.0f, 0.0f)));
        assert(NearQuat(tc.rotation, expected) && "euler track 应合成与 glm 同序的四元数");
        std::fprintf(stdout, "  [PASS] rotation.euler → 四元数\n");
    }

    // ===== 7. loop wrap：dur=2 的 loop clip，Tick 超界回卷 =====
    {
        Anim::AnimationClip clip;
        clip.loop = true;
        clip.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                        {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                         LinKey(2.0f, glm::vec4(20, 0, 0, 0))}));
        Scene::TransformComponent tc;
        ClipAnimator anim(std::move(clip), &tc);
        anim.Tick(3.0f);  // 3 % 2 = 1 → position.x = 10
        assert(Near(anim.ElapsedSeconds(), 1.0f) && "loop 应回卷到 1.0");
        assert(Near(tc.position.x, 10.0f) && "回卷后采样应为中点 10");
        assert(!anim.IsFinished() && "loop clip 永不 finished");
        std::fprintf(stdout, "  [PASS] loop wrap 回卷\n");
    }

    // ===== 8. 非 loop clamp + IsFinished =====
    {
        Anim::AnimationClip clip;
        clip.loop = false;
        clip.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                        {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                         LinKey(1.0f, glm::vec4(10, 0, 0, 0))}));
        Scene::TransformComponent tc;
        ClipAnimator anim(std::move(clip), &tc);
        assert(!anim.IsFinished() && "起始未结束");
        anim.Tick(5.0f);  // clamp 到 dur=1
        assert(Near(anim.ElapsedSeconds(), 1.0f) && "非 loop 应 clamp 到 duration");
        assert(Near(tc.position.x, 10.0f) && "clamp 末帧值");
        assert(anim.IsFinished() && "非 loop 到末尾应 finished");
        std::fprintf(stdout, "  [PASS] 非 loop clamp + IsFinished\n");
    }

    // ===== 9. Pause：不推进、不应用 =====
    {
        Anim::AnimationClip clip;
        clip.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                        {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                         LinKey(1.0f, glm::vec4(10, 0, 0, 0))}));
        Scene::TransformComponent tc;
        ClipAnimator anim(std::move(clip), &tc);
        anim.Pause();
        anim.Tick(1.0f);
        assert(Near(anim.ElapsedSeconds(), 0.0f) && "pause 后 Tick 不推进");
        assert(Near(tc.position.x, 0.0f) && "pause 后不写目标");
        assert(!anim.IsPlaying() && "Pause 后 IsPlaying=false");
        std::fprintf(stdout, "  [PASS] Pause 冻结\n");
    }

    // ===== 10. Stop：elapsed 归零 + 应用 t0 pose =====
    {
        Anim::AnimationClip clip;
        clip.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                        {LinKey(0.0f, glm::vec4(2, 0, 0, 0)),
                                         LinKey(1.0f, glm::vec4(10, 0, 0, 0))}));
        Scene::TransformComponent tc;
        ClipAnimator anim(std::move(clip), &tc);
        anim.Tick(1.0f);  // 到末帧
        assert(Near(tc.position.x, 10.0f));
        anim.Stop();
        assert(Near(anim.ElapsedSeconds(), 0.0f) && "Stop 后 elapsed=0");
        assert(Near(tc.position.x, 2.0f) && "Stop 后应用 t0 pose");
        assert(!anim.IsPlaying() && "Stop 后非播放");
        std::fprintf(stdout, "  [PASS] Stop 归零 + t0 pose\n");
    }

    // ===== 11. null target 安全（Tick 仍推进 elapsed，不崩）=====
    {
        Anim::AnimationClip clip;
        clip.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                        {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                         LinKey(1.0f, glm::vec4(10, 0, 0, 0))}));
        ClipAnimator anim(std::move(clip), nullptr);
        anim.Tick(0.5f);  // 无 target，不应崩
        assert(Near(anim.ElapsedSeconds(), 0.5f) && "无 target Tick 仍推进 elapsed");
        Scene::TransformComponent tc;
        anim.SetTarget(&tc);
        anim.ApplyPose();  // 后接上 target → 立即按当前 elapsed 写
        assert(Near(tc.position.x, 5.0f) && "SetTarget 后 ApplyPose 写当前 pose");
        std::fprintf(stdout, "  [PASS] null target 安全 + 后接 target\n");
    }

    // ===== 12. 端到端：World + AnimatorComponent + TickAnimators =====
    {
        Orange::Engine::World world;
        Orange::Engine::Entity e = world.CreateEntity();
        world.AddComponent<Scene::TransformComponent>(e, Scene::TransformComponent{});

        Anim::AnimationClip clip;
        clip.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                        {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                         LinKey(1.0f, glm::vec4(4, 0, 0, 0))}));
        auto up = std::make_unique<ClipAnimator>(std::move(clip));
        ClipAnimator* raw = up.get();  // unique_ptr move 不搬 pointee，raw 跨 move 有效

        Anim::AnimatorComponent ac{};
        ac.animator = std::move(up);
        world.AddComponent<Anim::AnimatorComponent>(e, std::move(ac));

        // TransformComponent 与 AnimatorComponent 不同 pool，上面的 AddComponent 不搬动
        // 它的存储 —— 此时取指针稳定。
        raw->SetTarget(world.GetComponent<Scene::TransformComponent>(e));

        const std::size_t ticked = Anim::TickAnimators(world, 0.25f);
        assert(ticked == 1 && "应 tick 到 1 个 animator");

        const Scene::TransformComponent* tc = world.GetComponent<Scene::TransformComponent>(e);
        assert(tc != nullptr);
        assert(Near(tc->position.x, 1.0f) && "TickAnimators 经 ClipAnimator 写实体 Transform");
        std::fprintf(stdout, "  [PASS] World + TickAnimators 端到端\n");
    }

    // ===== 13. Progress：播放进度 [0,1] =====
    {
        Anim::AnimationClip clip;
        clip.duration = 2.0f;  // 显式时长
        clip.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                        {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                         LinKey(2.0f, glm::vec4(20, 0, 0, 0))}));
        ClipAnimator anim(clip, nullptr);  // 无 target，仅测进度
        assert(Near(anim.Progress(), 0.0f) && "起始进度 0");
        anim.Tick(1.0f);
        assert(Near(anim.Progress(), 0.5f) && "t=1/dur=2 → 0.5");
        anim.Tick(5.0f);  // 非 loop clamp 到 2.0
        assert(Near(anim.Progress(), 1.0f) && "非 loop 末尾 → 1.0");

        // loop clip：回卷后进度落在 [0,1)。
        Anim::AnimationClip lclip = clip;
        lclip.loop = true;
        ClipAnimator lanim(lclip, nullptr);
        lanim.Tick(3.0f);  // 3 % 2 = 1 → progress 0.5
        assert(Near(lanim.Progress(), 0.5f) && "loop 回卷进度 0.5");

        // 空 / 零时长 clip → 0（退化）。
        ClipAnimator eanim(Anim::AnimationClip{}, nullptr);
        eanim.Tick(1.0f);
        assert(Near(eanim.Progress(), 0.0f) && "duration<=0 → 进度 0");
        std::fprintf(stdout, "  [PASS] Progress 播放进度 [0,1]\n");
    }

    std::fprintf(stdout, "ClipAnimatorTest: all passed\n");
    return 0;
}
