// 曲线编辑器（B2.4）的数据层等价测试。
//
// 编辑器曲线视图拖 Bezier 切线手柄最终归结为**纯数据操作**：拷当前 clip → 在副本
// 上改某 keyframe 的 inTangent / outTangent（屏幕落点经逆运算反推切线 x/y）→
// RecomputeClipDuration → ClipAnimator::SetClip（do/undo 对称）。曲线视图用
// SampleTrack 密集采样画线（display==playback），所以"改切线 → 曲线变"等价于"改切线
// → SampleTrack 值变"。ImGui 拖拽 / hit-test / 手柄屏幕映射 headless 测不到（逐条
// dogfood），但**切线编辑 → SampleTrack 中点值按预期变**这条正确性核心可直接锁住。
//
// 本测试不链接编辑器 TU（DrawCurveEditor 含 EditorHost / ImGui 依赖），复刻其切线编辑
// 的等价引擎层数据链 + 复用 SampleTrack 断言（参 AnimationClipTest 的 Bezier 时序断言）。

#include "orange/engine/animation/AnimationClip.h"
#include "orange/engine/animation/ClipAnimator.h"
#include "orange/engine/scene/TransformComponent.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <utility>

namespace Anim  = ::Orange::Engine::Animation;
namespace Scene = ::Orange::Engine::Scene;

namespace
{

bool Near(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) < eps;
}

Anim::Keyframe BezKey(float time, float v, glm::vec2 inT = glm::vec2(0.0f),
                      glm::vec2 outT = glm::vec2(0.0f))
{
    Anim::Keyframe k;
    k.time       = time;
    k.value      = glm::vec4(v, 0.0f, 0.0f, 0.0f);
    k.interp     = Anim::InterpMode::Bezier;
    k.inTangent  = inT;
    k.outTangent = outT;
    return k;
}

// 复刻 DrawCurveEditor 的逆运算（SolveOutTangent / SolveInTangent）：把"手柄落点
// (time,value)"反推回切线 (x,y)。dt/dv = 段跨度。time 方向夹（out [0,1] / in
// [-1,0]）；value 方向不夹（overshoot）。dt<=0 / dv≈0 时该维不改。
glm::vec2 SolveOut(const Anim::Keyframe& k0, float handleTime, float handleValue,
                   float dt, float dv)
{
    glm::vec2 t = k0.outTangent;
    if (dt > 1e-6f) { t.x = std::fmin(std::fmax((handleTime - k0.time) / dt, 0.0f), 1.0f); }
    if (std::fabs(dv) > 1e-6f) { t.y = (handleValue - k0.value.x) / dv; }
    return t;
}
glm::vec2 SolveIn(const Anim::Keyframe& k1, float handleTime, float handleValue,
                  float dt, float dv)
{
    glm::vec2 t = k1.inTangent;
    if (dt > 1e-6f) { t.x = std::fmin(std::fmax((handleTime - k1.time) / dt, -1.0f), 0.0f); }
    if (std::fabs(dv) > 1e-6f) { t.y = (handleValue - k1.value.x) / dv; }
    return t;
}

}  // namespace

int main()
{
    using Anim::AnimationClip;
    using Anim::AnimationTrack;
    using Anim::ClipAnimator;
    using Anim::TrackValueType;

    // ===== 1. 改 outTangent.y（值方向抬升）→ SampleTrack 中点抬高 =====
    // 对应曲线视图：把 out 手柄往上拖 → outTangent.y 增大 → 缓动抬升 → 中点变高。
    {
        AnimationTrack tr;
        tr.valueType = TrackValueType::Float;
        tr.keys.push_back(BezKey(0.0f, 0.0f));   // 零切线 = 退化线性
        tr.keys.push_back(BezKey(2.0f, 80.0f));

        const float midBefore = Anim::SampleTrack(tr, 1.0f).x;
        assert(Near(midBefore, 40.0f) && "零切线中点退化为线性中点 40");

        // 改 k0.outTangent.y = 1.0（手柄抬到下一帧值高度）。
        tr.keys[0].outTangent.y = 1.0f;
        const float midAfter = Anim::SampleTrack(tr, 1.0f).x;
        assert(midAfter > midBefore && "outTangent.y>0 → 中点抬高（缓动抬升）");
        std::fprintf(stdout, "  [PASS] outTangent.y 抬升 → 中点 %.2f → %.2f\n",
                     midBefore, midAfter);
    }

    // ===== 2. 逆运算 round-trip：手柄落点 → 切线 → 手柄落点一致 =====
    // 取一组手柄屏幕落点对应的 (time,value)，反推切线，再正推手柄位置应回到原落点。
    {
        AnimationTrack tr;
        tr.valueType = TrackValueType::Float;
        tr.keys.push_back(BezKey(0.0f, 0.0f));
        tr.keys.push_back(BezKey(4.0f, 100.0f));

        const Anim::Keyframe& k0 = tr.keys[0];
        const Anim::Keyframe& k1 = tr.keys[1];
        const float dt = k1.time - k0.time;        // 4
        const float dv = k1.value.x - k0.value.x;  // 100

        // out 手柄目标落点：time=k0.time+0.42*dt, value=k0.value+0.3*dv（含值抬升）。
        const float hTime  = k0.time + 0.42f * dt;
        const float hValue = k0.value.x + 0.3f * dv;
        const glm::vec2 solved = SolveOut(k0, hTime, hValue, dt, dv);
        assert(Near(solved.x, 0.42f) && "逆推 outTangent.x 还原");
        assert(Near(solved.y, 0.3f) && "逆推 outTangent.y 还原");

        // 正推回手柄落点（OutHandleTV）：time=k0.time+x*dt, value=k0.value+y*dv。
        const float backTime  = k0.time + solved.x * dt;
        const float backValue = k0.value.x + solved.y * dv;
        assert(Near(backTime, hTime) && "手柄 time round-trip");
        assert(Near(backValue, hValue) && "手柄 value round-trip");
        std::fprintf(stdout, "  [PASS] 手柄落点 → 切线 → 手柄落点 round-trip\n");
    }

    // ===== 3. in 手柄逆运算：inTangent.x 夹到 [-1,0]（指回前一帧）=====
    {
        AnimationTrack tr;
        tr.valueType = TrackValueType::Float;
        tr.keys.push_back(BezKey(0.0f, 0.0f));
        tr.keys.push_back(BezKey(2.0f, 100.0f));
        const Anim::Keyframe& k0 = tr.keys[0];
        Anim::Keyframe&       k1 = tr.keys[1];
        const float dt = k1.time - k0.time;
        const float dv = k1.value.x - k0.value.x;

        // in 手柄拖到 k1 之前（time < k1.time）→ inTangent.x 应为负。
        const glm::vec2 solved = SolveIn(k1, k1.time - 0.42f * dt, k1.value.x, dt, dv);
        assert(Near(solved.x, -0.42f) && "in 手柄 time<k1 → inTangent.x 负（指回前帧）");
        assert(solved.x >= -1.0f && solved.x <= 0.0f && "inTangent.x 夹 [-1,0]");

        // 应用后中点应 > 50（ease-out 快启动，与 AnimationClipTest 6c 同款）。
        k1.inTangent = solved;
        const float mid = Anim::SampleTrack(tr, 1.0f).x;
        assert(mid > 50.0f && "ease-out（inTangent.x=-0.42）中点高于线性 50");
        std::fprintf(stdout, "  [PASS] in 手柄逆运算 + ease-out 时序（mid=%.2f > 50）\n", mid);
    }

    // ===== 4. 完整数据链 do/undo：改切线经 SetClip 对称切换 =====
    // 复刻 SetAnimationClipCommand：拷 clip → 改副本 key 切线 → SetClip(new) / SetClip(old)。
    {
        AnimationClip clip;
        clip.tracks.push_back({});
        clip.tracks[0].targetName = "position.y";
        clip.tracks[0].valueType  = TrackValueType::Float;
        clip.tracks[0].keys.push_back(BezKey(0.0f, 0.0f));
        clip.tracks[0].keys.push_back(BezKey(2.0f, 100.0f));
        Anim::RecomputeClipDuration(clip);

        Scene::TransformComponent tc;
        ClipAnimator anim(clip, &tc);

        const float midOld = Anim::SampleTrack(anim.Clip().tracks[0], 1.0f).x;  // 50（线性退化）

        AnimationClip oldClip = anim.Clip();
        AnimationClip newClip = anim.Clip();
        newClip.tracks[0].keys[0].outTangent = glm::vec2(0.0f, 1.6f);  // overshoot 抬升
        newClip.tracks[0].keys[1].inTangent  = glm::vec2(0.0f, 1.6f);
        Anim::RecomputeClipDuration(newClip);

        anim.SetClip(newClip);  // do
        // overshoot 切线 → 中段峰值越过端点（参 AnimationClipTest 6f）。
        float peak = 0.0f;
        for (int i = 0; i <= 40; ++i)
        {
            const float t = 2.0f * static_cast<float>(i) / 40.0f;
            peak = std::fmax(peak, Anim::SampleTrack(anim.Clip().tracks[0], t).x);
        }
        assert(peak > 100.0f && "do：overshoot 切线 → 峰值越过端点 100");

        anim.SetClip(oldClip);  // undo
        const float midBack = Anim::SampleTrack(anim.Clip().tracks[0], 1.0f).x;
        assert(Near(midBack, midOld) && "undo：切线还原 → 中点回退");
        std::fprintf(stdout, "  [PASS] 切线编辑 do/undo（peak=%.2f > 100；undo 回 %.2f）\n",
                     peak, midBack);
    }

    // ===== 5. 多 key：编辑中间 key 的切线只影响相邻两段 =====
    {
        AnimationTrack tr;
        tr.valueType = TrackValueType::Float;
        tr.keys.push_back(BezKey(0.0f, 0.0f));
        tr.keys.push_back(BezKey(2.0f, 50.0f));
        tr.keys.push_back(BezKey(4.0f, 50.0f));

        // 段[1,2] 是平直段（dv=0）：改中间 key.outTangent.y 不影响该段（dv≈0 时
        // SolveOut 不改 .y；且 mix 端点同值，缓动无论如何中点都 50）。
        const float seg2MidBefore = Anim::SampleTrack(tr, 3.0f).x;
        tr.keys[1].outTangent = glm::vec2(0.42f, 0.5f);  // 改中 key out（控制段[1,2]）
        const float seg2MidAfter = Anim::SampleTrack(tr, 3.0f).x;
        assert(Near(seg2MidBefore, 50.0f) && Near(seg2MidAfter, 50.0f)
               && "平直段（端点同值）中点恒为该值，切线不改值");

        // 段[0,1] 不受中 key out 切线影响（out 控制的是它后面那段）。
        const float seg1Mid = Anim::SampleTrack(tr, 1.0f).x;
        assert(Near(seg1Mid, 25.0f, 1.0f) && "段[0,1] 仍由 k0.out（零）退化线性 → 25");
        std::fprintf(stdout, "  [PASS] 多 key 切线编辑段隔离\n");
    }

    std::fprintf(stdout, "CurveEditorPrimitivesTest: 全部通过\n");
    return 0;
}
