// BlendSpace 的 headless 单元测试 —— 参数化姿势混合（blend space / blend tree）。
// 锁住纯数学层（BlendSpace.h header-only）+ BlendSpaceAnimator 运行时后端：
//   * BlendPoses：N-pose 加权混合（position/scale 线性、rotation 符号对齐 nlerp）
//   * EvaluateBlendSpace1D：参数轴 clamp / 邻接两样本插值 + phase-normalized 时间同步
//   * EvaluateBlendSpace2D：精确命中样本 + 反距离权重（IDW）混合
//   * ApplyAdditivePose：附加层叠（差量 position/scale + 差量旋转 slerp 右乘）
//   * BlendSpaceAnimator：SetTarget/SetBlendParameter/Tick 端到端驱动 TransformComponent
// 纯 CPU：栈上 TransformComponent + glm，无 GPU / 无 Vulkan。

#include "orange/engine/animation/BlendSpace.h"

#include "orange/engine/animation/BlendSpaceAnimator.h"

#include <glm/geometric.hpp>       // glm::length
#include <glm/gtc/quaternion.hpp>  // glm::angleAxis
#include <glm/trigonometric.hpp>   // glm::radians

#include <cassert>
#include <cmath>
#include <cstdio>
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

Anim::AnimationTrack PosYTrack(std::vector<Anim::Keyframe> keys)
{
    Anim::AnimationTrack t;
    t.targetName = "position.y";
    t.valueType  = Anim::TrackValueType::Float;
    t.keys       = std::move(keys);
    return t;
}

// 常量 clip：position.y 恒为 y（两端同值），用于验证参数轴混合（与 phase 无关）。
Anim::AnimationClip ConstPosY(float y, float dur = 1.0f)
{
    Anim::AnimationClip c;
    c.duration = dur;
    c.tracks.push_back(PosYTrack({LinKey(0.0f, glm::vec4(y, 0, 0, 0)),
                                  LinKey(dur, glm::vec4(y, 0, 0, 0))}));
    return c;
}

// 斜坡 clip：position.y 在 dur 内从 y0 线性到 y1，用于验证 phase-normalized 时间。
Anim::AnimationClip RampPosY(float y0, float y1, float dur = 1.0f)
{
    Anim::AnimationClip c;
    c.duration = dur;
    c.tracks.push_back(PosYTrack({LinKey(0.0f, glm::vec4(y0, 0, 0, 0)),
                                  LinKey(dur, glm::vec4(y1, 0, 0, 0))}));
    return c;
}

Scene::TransformComponent MakePose(float posY, const glm::quat& rot = glm::quat(1, 0, 0, 0))
{
    Scene::TransformComponent p;
    p.position = glm::vec3(0.0f, posY, 0.0f);
    p.rotation = rot;
    return p;
}

glm::quat RotZ(float degrees)
{
    return glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 0.0f, 1.0f));
}

}  // namespace

int main()
{
    using Anim::BlendPoses;
    using Anim::BlendSample1D;
    using Anim::BlendSample2D;
    using Anim::BlendSpace1D;
    using Anim::BlendSpace2D;

    // ===== 1. BlendPoses：端点 / 中点 / 权重归一化 / N=3 / rotation nlerp =====
    {
        const Scene::TransformComponent poseA = MakePose(0.0f);
        const Scene::TransformComponent poseB = MakePose(10.0f);
        const Scene::TransformComponent poses[2] = {poseA, poseB};
        Scene::TransformComponent       out;

        const float wA[2] = {1.0f, 0.0f};
        BlendPoses(poses, wA, 2, out);
        assert(NearV3(out.position, poseA.position) && "weights{1,0} → poseA");

        const float wB[2] = {0.0f, 1.0f};
        BlendPoses(poses, wB, 2, out);
        assert(NearV3(out.position, poseB.position) && "weights{0,1} → poseB");

        const float wMid[2] = {0.5f, 0.5f};
        BlendPoses(poses, wMid, 2, out);
        assert(Near(out.position.y, 5.0f) && "weights{0.5,0.5} → 中点 5");

        // 非归一化权重：BlendPoses 内部按 sumW 归一化。{3,1} → 0.75/0.25 → 2.5。
        const float wUn[2] = {3.0f, 1.0f};
        BlendPoses(poses, wUn, 2, out);
        assert(Near(out.position.y, 2.5f) && "非归一化权重{3,1} → 归一化后 2.5");

        // count==0 → 默认 pose（identity）。
        BlendPoses(poses, wMid, 0, out);
        assert(NearV3(out.position, glm::vec3(0.0f)) && NearV3(out.scale, glm::vec3(1.0f)) &&
               NearQuat(out.rotation, glm::quat(1, 0, 0, 0)) && "count==0 → 默认 TransformComponent");

        // count==1 → poses[0]。
        BlendPoses(poses, wMid, 1, out);
        assert(NearV3(out.position, poseA.position) && "count==1 → poses[0]");

        // sumW<=eps 退化 → poses[0]。
        const Scene::TransformComponent posesD[2] = {MakePose(7.0f), MakePose(3.0f)};
        const float                     wZero[2]  = {0.0f, 0.0f};
        BlendPoses(posesD, wZero, 2, out);
        assert(Near(out.position.y, 7.0f) && "sumW<=eps → 退化回 poses[0]");

        // N=3 加权和位置。posY 0/3/9，weights 0.2/0.3/0.5 → 0.9+4.5=5.4。
        const Scene::TransformComponent poses3[3] = {MakePose(0.0f), MakePose(3.0f), MakePose(9.0f)};
        const float                     w3[3]     = {0.2f, 0.3f, 0.5f};
        BlendPoses(poses3, w3, 3, out);
        assert(Near(out.position.y, 5.4f) && "N=3 加权和 posY = 5.4");

        // rotation nlerp：identity 与 90°Z 中点 → 45°Z（nlerp 中点 == slerp 中点，精确）。
        const Scene::TransformComponent posesR[2] = {MakePose(0.0f, glm::quat(1, 0, 0, 0)),
                                                     MakePose(0.0f, RotZ(90.0f))};
        BlendPoses(posesR, wMid, 2, out);
        const glm::vec3 dir = out.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
        assert(NearV3(dir, glm::vec3(0.7071f, 0.7071f, 0.0f), 1e-3f) &&
               "nlerp(identity,90°Z,0.5) → 45°Z：+X 转到约 (0.707,0.707,0)");
        std::fprintf(stdout, "  [PASS] BlendPoses 端点/中点/归一化/N=3/rotation nlerp\n");
    }

    // ===== 2. EvaluateBlendSpace1D：参数轴 clamp / 邻接插值 / phase 同步 =====
    {
        BlendSpace1D space;
        space.samples.push_back(BlendSample1D{0.0f, ConstPosY(0.0f)});   // idle
        space.samples.push_back(BlendSample1D{5.0f, ConstPosY(5.0f)});   // walk
        space.samples.push_back(BlendSample1D{10.0f, ConstPosY(10.0f)}); // run

        const Scene::TransformComponent baseline;  // 默认 identity
        Scene::TransformComponent       out;

        // 精确命中样本点。
        Anim::EvaluateBlendSpace1D(space, 0.0f, 0.5f, baseline, out);
        assert(Near(out.position.y, 0.0f) && "x=0 → idle 样本 posY 0");
        Anim::EvaluateBlendSpace1D(space, 5.0f, 0.5f, baseline, out);
        assert(Near(out.position.y, 5.0f) && "x=5 → walk 样本 posY 5");
        Anim::EvaluateBlendSpace1D(space, 10.0f, 0.5f, baseline, out);
        assert(Near(out.position.y, 10.0f) && "x=10 → run 样本 posY 10");

        // 区间内 t01=0.5 混合。
        Anim::EvaluateBlendSpace1D(space, 2.5f, 0.5f, baseline, out);
        assert(Near(out.position.y, 2.5f) && "x=2.5 → idle/walk 中点 posY 2.5");
        Anim::EvaluateBlendSpace1D(space, 7.5f, 0.5f, baseline, out);
        assert(Near(out.position.y, 7.5f) && "x=7.5 → walk/run 中点 posY 7.5");

        // 越界 clamp。
        Anim::EvaluateBlendSpace1D(space, -1.0f, 0.5f, baseline, out);
        assert(Near(out.position.y, 0.0f) && "x=-1 → clamp 首样本 posY 0");
        Anim::EvaluateBlendSpace1D(space, 99.0f, 0.5f, baseline, out);
        assert(Near(out.position.y, 10.0f) && "x=99 → clamp 末样本 posY 10");
        std::fprintf(stdout, "  [PASS] EvaluateBlendSpace1D 参数轴 clamp/邻接插值\n");

        // phase 同步：单样本 ramp clip posY 0→10（duration 1）；phase=0.5 → posY≈5。
        BlendSpace1D rampSpace;
        rampSpace.samples.push_back(BlendSample1D{0.0f, RampPosY(0.0f, 10.0f, 1.0f)});
        Anim::EvaluateBlendSpace1D(rampSpace, 0.0f, 0.5f, baseline, out);
        assert(Near(out.position.y, 5.0f) && "phase=0.5 → ramp clip 采样在 0.5*dur → posY 5");
        Anim::EvaluateBlendSpace1D(rampSpace, 0.0f, 0.0f, baseline, out);
        assert(Near(out.position.y, 0.0f) && "phase=0 → ramp 起点 posY 0");
        Anim::EvaluateBlendSpace1D(rampSpace, 0.0f, 1.0f, baseline, out);
        assert(Near(out.position.y, 10.0f) && "phase=1 → ramp 终点 posY 10");
        std::fprintf(stdout, "  [PASS] EvaluateBlendSpace1D phase-normalized 时间同步\n");
    }

    // ===== 3. EvaluateBlendSpace2D：精确命中 + IDW 等距均值 =====
    {
        BlendSpace2D space;
        space.samples.push_back(BlendSample2D{glm::vec2(0, 0), ConstPosY(0.0f)});
        space.samples.push_back(BlendSample2D{glm::vec2(1, 0), ConstPosY(1.0f)});
        space.samples.push_back(BlendSample2D{glm::vec2(0, 1), ConstPosY(2.0f)});
        space.samples.push_back(BlendSample2D{glm::vec2(1, 1), ConstPosY(3.0f)});

        const Scene::TransformComponent baseline;
        Scene::TransformComponent       out;

        Anim::EvaluateBlendSpace2D(space, glm::vec2(0, 0), 0.5f, baseline, out);
        assert(Near(out.position.y, 0.0f) && "p=(0,0) → 精确命中 posY 0");
        Anim::EvaluateBlendSpace2D(space, glm::vec2(1, 1), 0.5f, baseline, out);
        assert(Near(out.position.y, 3.0f) && "p=(1,1) → 精确命中 posY 3");

        // 中心点四样本等距 → IDW 等权 → 算术平均 (0+1+2+3)/4 = 1.5。
        Anim::EvaluateBlendSpace2D(space, glm::vec2(0.5f, 0.5f), 0.5f, baseline, out);
        assert(Near(out.position.y, 1.5f, 0.01f) && "p=(0.5,0.5) → IDW 等距均值 1.5");
        std::fprintf(stdout, "  [PASS] EvaluateBlendSpace2D 精确命中 + IDW 等距均值\n");
    }

    // ===== 4. ApplyAdditivePose：差量 position + 差量旋转 slerp =====
    {
        const Scene::TransformComponent base    = MakePose(10.0f);
        const Scene::TransformComponent additive = MakePose(3.0f);
        const Scene::TransformComponent ref     = MakePose(1.0f);
        Scene::TransformComponent       out;

        Anim::ApplyAdditivePose(base, additive, ref, 1.0f, out);
        assert(Near(out.position.y, 12.0f) && "weight=1 → 10+(3-1)=12");
        Anim::ApplyAdditivePose(base, additive, ref, 0.5f, out);
        assert(Near(out.position.y, 11.0f) && "weight=0.5 → 10+0.5*(3-1)=11");

        // rotation：base identity + additive 90°Z + ref identity + weight=1 → out ≈ 90°Z。
        const Scene::TransformComponent baseR    = MakePose(0.0f, glm::quat(1, 0, 0, 0));
        const Scene::TransformComponent additiveR = MakePose(0.0f, RotZ(90.0f));
        const Scene::TransformComponent refR     = MakePose(0.0f, glm::quat(1, 0, 0, 0));
        Anim::ApplyAdditivePose(baseR, additiveR, refR, 1.0f, out);
        const glm::vec3 dir = out.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
        assert(NearV3(dir, glm::vec3(0.0f, 1.0f, 0.0f), 1e-3f) &&
               "additive 90°Z（weight=1）→ +X 转到 +Y");
        std::fprintf(stdout, "  [PASS] ApplyAdditivePose 差量 position + 差量旋转\n");
    }

    // ===== 5. BlendSpaceAnimator 端到端：SetTarget/SetBlendParameter/Tick 驱动 =====
    {
        BlendSpace1D space;
        space.samples.push_back(BlendSample1D{0.0f, ConstPosY(0.0f, 1.0f)});
        space.samples.push_back(BlendSample1D{10.0f, ConstPosY(10.0f, 1.0f)});

        Scene::TransformComponent tc;
        Anim::BlendSpaceAnimator  animator(space, &tc);
        assert(animator.BackendName() == "blendspace" && "BackendName");
        assert(!animator.IsFinished() && "loop 默认 → 永不 finished");

        animator.SetBlendParameter(5.0f);
        animator.Tick(0.1f);
        assert(Near(tc.position.y, 5.0f) && "param=5 → 混合中点 posY 5");

        animator.SetBlendParameter(10.0f);
        animator.Tick(0.1f);
        assert(Near(tc.position.y, 10.0f) && "param=10 → clamp 末样本 posY 10");

        animator.SetBlendParameter(0.0f);
        animator.Tick(0.1f);
        assert(Near(tc.position.y, 0.0f) && "param=0 → 首样本 posY 0（SetBlendParameter 改变输出）");

        // phase 推进：refDuration=1，speed=1，累计 dt=0.3 → phase≈0.3。
        assert(Near(animator.Phase(), 0.3f, 1e-3f) && "累计 dt=0.3 / refDur=1 → phase≈0.3");

        // 非 loop + Tick 越界 → clamp phase=1 → IsFinished。
        Anim::BlendSpaceAnimator animator2(space, &tc);
        animator2.SetLoop(false);
        animator2.SetBlendParameter(5.0f);
        animator2.Tick(5.0f);  // phase = 5/1 clamp 1.0
        assert(Near(animator2.Phase(), 1.0f) && "非 loop → phase clamp 到 1");
        assert(animator2.IsFinished() && "非 loop phase>=1 → finished");

        // Stop 复位 phase。
        animator2.Stop();
        assert(Near(animator2.Phase(), 0.0f) && "Stop → phase 复位 0");
        assert(!animator2.IsPlaying() && "Stop → 非播放");

        // Pause：Tick 不推进、不写 target。
        Scene::TransformComponent tc3;
        tc3.position.y = 42.0f;
        Anim::BlendSpaceAnimator animator3(space, &tc3);
        animator3.SetBlendParameter(5.0f);
        animator3.Pause();
        animator3.Tick(0.5f);
        assert(Near(tc3.position.y, 42.0f) && "Pause → 不写 target");
        assert(Near(animator3.Phase(), 0.0f) && "Pause → phase 不推进");

        // null target 安全：Tick 仍推进 phase、不崩。
        Anim::BlendSpaceAnimator animator4(space, nullptr);
        animator4.SetBlendParameter(5.0f);
        animator4.Tick(0.2f);
        assert(Near(animator4.Phase(), 0.2f, 1e-3f) && "null target Tick 仍推进 phase");
        std::fprintf(stdout, "  [PASS] BlendSpaceAnimator 端到端 SetBlendParameter/Tick/Stop/Pause\n");
    }

    // ===== 6. NaN 防护：NaN 参数不越界 / 不崩（复核逮到的 bracketing 越界）=====
    {
        BlendSpace1D space;
        space.samples.push_back({0.0f, ConstPosY(0.0f)});
        space.samples.push_back({5.0f, ConstPosY(5.0f)});
        space.samples.push_back({10.0f, ConstPosY(10.0f)});
        const Scene::TransformComponent baseline;

        const float kNan = std::nanf("");
        // NaN 的 x：修复前 bracketing while 会把 i 递进到越界读 samples[n]。
        // 修复后归一化为首样本 → 不崩、输出首样本姿势（posY=0）。
        Scene::TransformComponent out;
        Anim::EvaluateBlendSpace1D(space, kNan, 0.5f, baseline, out);
        assert(Near(out.position.y, 0.0f) && "NaN x → 归一化到首样本（不越界）");
        // BlendedClipDuration1D 同款循环 → 也不能越界。
        const float dur = Anim::BlendedClipDuration1D(space, kNan);
        assert(Near(dur, 1.0f) && "NaN x → 首样本 duration（不越界）");

        // 2D：NaN p → 首样本（不产 NaN 输出）。
        BlendSpace2D space2d;
        space2d.samples.push_back({glm::vec2(0.0f, 0.0f), ConstPosY(0.0f)});
        space2d.samples.push_back({glm::vec2(1.0f, 1.0f), ConstPosY(3.0f)});
        Scene::TransformComponent out2d;
        Anim::EvaluateBlendSpace2D(space2d, glm::vec2(kNan, kNan), 0.5f, baseline, out2d);
        assert(Near(out2d.position.y, 0.0f) && "2D NaN p → 首样本（不产 NaN）");

        // animator：SetBlendParameter(NaN) + Tick 不崩。
        Scene::TransformComponent tc;
        Anim::BlendSpaceAnimator animator(space, &tc);
        animator.SetBlendParameter(kNan);
        animator.Tick(0.1f);  // 修复前此处经 BlendedClipDuration1D + EvaluateBlendSpace1D 越界崩
        assert(tc.position.y == tc.position.y && "NaN 参数 Tick 输出非 NaN（不崩）");
        std::fprintf(stdout, "  [PASS] NaN 参数防护（1D/2D/animator 不越界不崩）\n");
    }

    // ===== 7. 样本 clip duration<=0 自动补（与 ClipAnimator 契约一致，防冻结）=====
    {
        // 只填 keyframe、不设 duration（默认 0）——修复前 refDuration=0 令 blend space
        // 永久冻结（phase 不推进 + 恒 t=0 采样）；修复后构造时自动 ComputeClipDuration。
        Anim::AnimationClip clipNoDur;
        clipNoDur.tracks.push_back(PosYTrack({LinKey(0.0f, glm::vec4(0, 0, 0, 0)),
                                              LinKey(1.0f, glm::vec4(10, 0, 0, 0))}));
        assert(clipNoDur.duration == 0.0f && "前置：clip 未设 duration");

        BlendSpace1D space;
        space.samples.push_back({0.0f, clipNoDur});

        Scene::TransformComponent tc;
        Anim::BlendSpaceAnimator animator(space, &tc);
        animator.SetBlendParameter(0.0f);
        animator.Tick(0.5f);
        // 自动补 duration=1.0 → phase 推进 0.5、采样 t=0.5 → ramp posY=5（非冻结的 0）。
        assert(Near(animator.Phase(), 0.5f, 1e-3f) && "duration 自动补 → phase 推进（非冻结）");
        assert(Near(tc.position.y, 5.0f) && "duration 自动补 → 采样 t=0.5 → posY=5");
        std::fprintf(stdout, "  [PASS] 样本 clip duration<=0 自动补（防冻结）\n");
    }

    std::fprintf(stdout, "BlendSpaceTest: all passed\n");
    return 0;
}
