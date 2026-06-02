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
#include <string>
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

    // ===== 14. SetSpeed：播放速率（含倒放）=====
    {
        Anim::AnimationClip clip;
        clip.duration = 2.0f;
        clip.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                        {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                         LinKey(2.0f, glm::vec4(20, 0, 0, 0))}));
        Scene::TransformComponent tc;
        ClipAnimator anim(clip, &tc);
        assert(Near(anim.Speed(), 1.0f) && "默认速率 1.0");

        anim.SetSpeed(2.0f);
        anim.Tick(0.5f);  // 0.5 * 2 = 1.0 推进
        assert(Near(anim.ElapsedSeconds(), 1.0f) && "2x 速率：dt=0.5 → 推进 1.0");
        assert(Near(tc.position.x, 10.0f) && "elapsed 1.0 → position.x 10");

        // 倒放：speed=-1，从 elapsed 1.0 退回。
        anim.SetSpeed(-1.0f);
        anim.Tick(0.5f);  // 1.0 + 0.5*(-1) = 0.5
        assert(Near(anim.ElapsedSeconds(), 0.5f) && "倒放：elapsed 退回 0.5");

        // 慢放：speed=0.5。
        anim.SetSpeed(0.5f);
        anim.Tick(0.5f);  // 0.5 + 0.5*0.5 = 0.75
        assert(Near(anim.ElapsedSeconds(), 0.75f) && "0.5x 速率：推进 0.25");
        std::fprintf(stdout, "  [PASS] SetSpeed 播放速率（2x/倒放/慢放）\n");
    }

    // ===== 15. 动画事件：正向越过 time 触发；倒放/scrub 不触发；loop 跨界 =====
    {
        Anim::AnimationClip clip;
        clip.duration = 2.0f;
        clip.events.push_back(Anim::AnimationEvent{0.5f, "hit"});
        clip.events.push_back(Anim::AnimationEvent{1.5f, "recover"});

        std::vector<std::string> fired;
        ClipAnimator anim(clip, nullptr);
        anim.SetEventCallback([&fired](std::string_view n) { fired.emplace_back(n); });

        anim.Tick(0.6f);  // 0→0.6 越过 0.5 → "hit"
        assert(fired.size() == 1 && fired[0] == "hit" && "越过 0.5 → hit");
        anim.Tick(1.0f);  // 0.6→1.6 越过 1.5 → "recover"
        assert(fired.size() == 2 && fired[1] == "recover" && "越过 1.5 → recover");
        anim.Tick(1.0f);  // 1.6→2.0(clamp) 无事件
        assert(fired.size() == 2 && "末段无更多事件");

        // 倒放不触发。
        anim.SetSpeed(-1.0f);
        anim.Tick(2.0f);  // 倒回到 0，越过 0.5/1.5 但倒放不 fire
        assert(fired.size() == 2 && "倒放不触发事件");

        // scrub（Seek）不触发。
        fired.clear();
        anim.SetSpeed(1.0f);
        anim.Seek(1.6f);  // 直接跳过 0.5/1.5，不 fire
        assert(fired.empty() && "Seek scrub 不触发事件");

        std::fprintf(stdout, "  [PASS] 动画事件正向触发 + 倒放/scrub 不触发\n");
    }

    // ===== 16. 动画事件 loop 跨界分段触发 =====
    {
        Anim::AnimationClip clip;
        clip.duration = 2.0f;
        clip.loop     = true;
        clip.events.push_back(Anim::AnimationEvent{0.3f, "loopStart"});
        clip.events.push_back(Anim::AnimationEvent{1.8f, "loopEnd"});

        std::vector<std::string> fired;
        ClipAnimator anim(clip, nullptr);
        anim.SetEventCallback([&fired](std::string_view n) { fired.emplace_back(n); });

        anim.Seek(1.7f);                 // 落在 loopEnd 前（Seek 不触发）
        assert(fired.empty());
        anim.Tick(0.6f);  // 1.7→(2.3 raw, wrap)→0.3：越 1.8（段1 (1.7,2.0]）+ 越 0.3（段2 (0,0.3]）
        assert(fired.size() == 2 && fired[0] == "loopEnd" && fired[1] == "loopStart" &&
               "loop 跨界：先 (oldT,dur] 的 loopEnd 再 (0,newT] 的 loopStart");
        std::fprintf(stdout, "  [PASS] 动画事件 loop 跨界分段触发\n");
    }

    // ===== 17. CrossFadeTo：过渡混合（无 pop / 半程混合 / 瞬切）=====
    {
        Scene::TransformComponent tc;  // 起始姿势 A：position.x = 0（默认）
        ClipAnimator anim(Anim::AnimationClip{}, &tc);

        Anim::AnimationClip clipB;  // position.x 恒为 10
        clipB.duration = 1.0f;
        clipB.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                         {LinKey(0.0f, glm::vec4(10, 0, 0, 0)),
                                          LinKey(1.0f, glm::vec4(10, 0, 0, 0))}));

        anim.CrossFadeTo(clipB, 1.0f);
        assert(anim.IsFading() && "fade>0 → IsFading");
        assert(Near(tc.position.x, 0.0f) && "过渡起点 w=0 → 保持 from-pose（无 pop）");

        anim.Tick(0.5f);  // w=0.5 → mix(0,10,0.5)=5
        assert(Near(tc.position.x, 5.0f) && "过渡半程 → 混合 5");
        assert(anim.IsFading());

        anim.Tick(0.5f);  // fade 完，纯 clipB → 10
        assert(Near(tc.position.x, 10.0f) && "过渡完成 → 纯新 clip 10");
        assert(!anim.IsFading() && "fade 结束 → IsFading false");

        // 瞬切（fade<=0）：直接纯新 clip。
        Scene::TransformComponent tc2;
        ClipAnimator anim2(Anim::AnimationClip{}, &tc2);
        anim2.CrossFadeTo(clipB, 0.0f);
        assert(!anim2.IsFading() && Near(tc2.position.x, 10.0f) && "fade<=0 → 瞬切纯新 clip");
        std::fprintf(stdout, "  [PASS] CrossFadeTo 过渡混合（无 pop / 半程混合 / 瞬切）\n");

        // rotation slerp 路径：from=identity → clipC(绕 Y 90°)，半程应为 45°（slerp 走最短弧）。
        Scene::TransformComponent tc3;  // identity rotation
        ClipAnimator anim3(Anim::AnimationClip{}, &tc3);
        Anim::AnimationClip clipC;
        clipC.duration = 1.0f;
        clipC.tracks.push_back(MakeTrack("rotation", TrackValueType::Vec3,
                                         {LinKey(0.0f, glm::vec4(0, 90, 0, 0)),
                                          LinKey(1.0f, glm::vec4(0, 90, 0, 0))}));
        anim3.CrossFadeTo(clipC, 1.0f);
        anim3.Tick(0.5f);  // w=0.5：slerp(identity, 90°Y, 0.5) = 45°Y
        const glm::quat expected45 = glm::slerp(glm::quat(1, 0, 0, 0),
                                                glm::quat(glm::radians(glm::vec3(0, 90, 0))), 0.5f);
        assert(NearQuat(tc3.rotation, expected45, 1e-3f) && "rotation 过渡半程 = slerp 45°");
        // 旋转 +Z 向量验证朝向合理（45°Y：(0,0,1)→约(0.707,0,0.707)）。
        const glm::vec3 dir = tc3.rotation * glm::vec3(0, 0, 1);
        assert(Near(dir.x, 0.7071f, 2e-3f) && Near(dir.z, 0.7071f, 2e-3f) &&
               "45°Y 旋转把 +Z 转到约 (0.707,0,0.707)");
        std::fprintf(stdout, "  [PASS] CrossFadeTo rotation slerp 半程 45°\n");
    }

    // ===== 19. CrossFadeTo 真两-clip：出场 clip 在 fade 期间继续推进（非冻结一帧）=====
    {
        Scene::TransformComponent tc;
        // 出场 clip A：position.x 线性 0→100，duration 2（非 loop）→ x(t)=50t。
        Anim::AnimationClip clipA;
        clipA.duration = 2.0f;
        clipA.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                         {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                          LinKey(2.0f, glm::vec4(100, 0, 0, 0))}));
        ClipAnimator anim(clipA, &tc);
        anim.Tick(0.4f);  // 出场 clip 推进到 t=0.4 → x=20
        assert(Near(tc.position.x, 20.0f) && "出场 clip 起播 t=0.4 → x=20");

        // 入场 clip B：position.x 恒 0。CrossFade 1.0s。
        Anim::AnimationClip clipB;
        clipB.duration = 1.0f;
        clipB.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                         {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                          LinKey(1.0f, glm::vec4(0, 0, 0, 0))}));
        anim.CrossFadeTo(clipB, 1.0f);
        assert(Near(tc.position.x, 20.0f) && "过渡起点 w=0 → 纯出场姿势 20（无 pop）");

        anim.Tick(0.5f);  // 入场 elapsed=0.5；出场 playhead=0.4+0.5=0.9 → x=45；w=0.5
        // 真两-clip：mix(出场 45, 入场 0, 0.5)=22.5（出场 clip 推进了！）。
        // 若是旧冻结 MVP：mix(冻结 20, 0, 0.5)=10。22.5≠10 → 证明出场继续播放。
        assert(Near(tc.position.x, 22.5f, 0.1f) &&
               "fade 半程：出场 clip 推进到 x=45 → mix(45,0,0.5)=22.5（冻结 MVP 会得 10）");

        anim.Tick(0.5f);  // fade 完 → 纯入场 clipB x=0
        assert(!anim.IsFading() && Near(tc.position.x, 0.0f) && "fade 完 → 纯入场 0");
        std::fprintf(stdout, "  [PASS] CrossFadeTo 真两-clip：出场 fade 期间继续推进（x=22.5≠冻结 10）\n");
    }

    // ===== 20. CrossFadeTo：出场 clip 未驱动的字段回退冻结基线（无反馈）=====
    {
        Scene::TransformComponent tc;
        tc.scale = glm::vec3(3.0f);  // 起始 scale=3（无任何 clip 驱动 scale）
        // 出场 clip 只驱动 position.x；入场 clip 也只驱动 position.x。两者都不碰 scale。
        Anim::AnimationClip clipA;
        clipA.duration = 1.0f;
        clipA.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                         {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                          LinKey(1.0f, glm::vec4(0, 0, 0, 0))}));
        ClipAnimator anim(clipA, &tc);
        Anim::AnimationClip clipB;
        clipB.duration = 1.0f;
        clipB.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                         {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                          LinKey(1.0f, glm::vec4(0, 0, 0, 0))}));
        anim.CrossFadeTo(clipB, 1.0f);
        anim.Tick(0.5f);  // fade 半程
        // scale 两 clip 都不驱动 → 冻结基线 3 在 from/to 两端一致 → 混合后仍 3（不被位置反馈污染）。
        assert(NearV3(tc.scale, glm::vec3(3.0f)) &&
               "两 clip 都不驱动的 scale 在 fade 中保持基线 3（无反馈漂移）");
        std::fprintf(stdout, "  [PASS] CrossFadeTo 未驱动字段回退冻结基线（scale=3 不漂移）\n");
    }

    // ===== 21. CrossFadeTo fade-中-再切换：切换帧无 pop（出场源降级冻结基线）=====
    {
        Scene::TransformComponent tc;  // x=0
        ClipAnimator anim(Anim::AnimationClip{}, &tc);

        Anim::AnimationClip clipA;  // position.x 恒 100
        clipA.duration = 1.0f;
        clipA.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                         {LinKey(0.0f, glm::vec4(100, 0, 0, 0)),
                                          LinKey(1.0f, glm::vec4(100, 0, 0, 0))}));
        anim.CrossFadeTo(clipA, 1.0f);
        anim.Tick(0.5f);  // 从 0 淡入 100 半程 → x=50，仍在 fade
        assert(Near(tc.position.x, 50.0f) && "第一次 fade 半程 x=50");
        assert(anim.IsFading());

        // fade 中再切到 clipB（恒 0）。此刻 target=50（混合中含 clipA 50% 贡献）。
        Anim::AnimationClip clipB;
        clipB.duration = 1.0f;
        clipB.tracks.push_back(MakeTrack("position.x", TrackValueType::Float,
                                         {LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                          LinKey(1.0f, glm::vec4(0, 0, 0, 0))}));
        anim.CrossFadeTo(clipB, 1.0f);
        // 切换帧 w=0：应保持 50（出场源降级冻结基线）。**修复前** bug 会用 clipA 实时采样
        // 覆盖混合结果 → 瞬跳到 clipA 纯值 100（pop）。
        assert(Near(tc.position.x, 50.0f) &&
               "fade-中-再切换切换帧无 pop：保持 50（非跳到出场 clip 纯值 100）");

        anim.Tick(0.5f);  // w=0.5：mix(冻结 50, clipB 0, 0.5)=25，平滑
        assert(Near(tc.position.x, 25.0f) && "再切换半程平滑混合到 25");
        std::fprintf(stdout, "  [PASS] CrossFadeTo fade-中-再切换无 pop（保持 50→25，非跳 100）\n");
    }

    std::fprintf(stdout, "ClipAnimatorTest: all passed\n");
    return 0;
}
