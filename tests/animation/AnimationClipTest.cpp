// AnimationClip 数据模型 + 采样的 headless 单元测试（B2.1 地基）。
// 锁住 SampleTrack 的 step / linear / bezier 插值 + clamp 边界 + 二分多段，
// 是后续 timeline / 曲线编辑器 / ProceduralAnimator 数据 channel 的正确性基线。
// 纯数据 + 纯函数，无 GPU / 无 World 依赖。

#include <orange/engine/animation/AnimationClip.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace Anim = ::Orange::Engine::Animation;

namespace
{

bool Near(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) < eps;
}

// 造一条 Float track，给定 (time, value, interp) 列表（值放 vec4.x）。
Anim::AnimationTrack MakeFloatTrack(std::initializer_list<Anim::Keyframe> ks)
{
    Anim::AnimationTrack t;
    t.valueType = Anim::TrackValueType::Float;
    t.keys      = std::vector<Anim::Keyframe>(ks);
    return t;
}

Anim::Keyframe Key(float time, float v, Anim::InterpMode interp,
                   glm::vec2 inT = glm::vec2(0.0f), glm::vec2 outT = glm::vec2(0.0f))
{
    Anim::Keyframe k;
    k.time       = time;
    k.value      = glm::vec4(v, 0.0f, 0.0f, 0.0f);
    k.interp     = interp;
    k.inTangent  = inT;
    k.outTangent = outT;
    return k;
}

}  // namespace

int main()
{
    using Anim::InterpMode;

    // ===== 1. 空 track → 零 =====
    {
        Anim::AnimationTrack empty;
        const glm::vec4 v = Anim::SampleTrack(empty, 0.5f);
        assert(Near(v.x, 0.0f) && Near(v.y, 0.0f) && Near(v.z, 0.0f) && Near(v.w, 0.0f) &&
               "空 track 采样应返回 (0,0,0,0)");
        std::fprintf(stdout, "  [PASS] 空 track → 零\n");
    }

    // ===== 2. clamp：t 在首 key 前 / 末 key 后 =====
    {
        auto tr = MakeFloatTrack({Key(1.0f, 10.0f, InterpMode::Linear),
                                  Key(3.0f, 30.0f, InterpMode::Linear)});
        assert(Near(Anim::SampleTrack(tr, 0.0f).x, 10.0f) && "t<首key → 首key值（无外插）");
        assert(Near(Anim::SampleTrack(tr, 5.0f).x, 30.0f) && "t>末key → 末key值（无外插）");
        assert(Near(Anim::SampleTrack(tr, 1.0f).x, 10.0f) && "t==首key → 首key值");
        assert(Near(Anim::SampleTrack(tr, 3.0f).x, 30.0f) && "t==末key → 末key值");
        std::fprintf(stdout, "  [PASS] clamp：首key前/末key后/端点\n");
    }

    // ===== 3. 线性插值中点 =====
    {
        auto tr = MakeFloatTrack({Key(0.0f, 0.0f, InterpMode::Linear),
                                  Key(2.0f, 100.0f, InterpMode::Linear)});
        assert(Near(Anim::SampleTrack(tr, 1.0f).x, 50.0f) && "线性 t=1（区间中点）→ 50");
        assert(Near(Anim::SampleTrack(tr, 0.5f).x, 25.0f) && "线性 t=0.5 → 25");
        std::fprintf(stdout, "  [PASS] 线性插值：中点/四分点\n");
    }

    // ===== 4. step 插值：保持 k0 值直到 k1 =====
    {
        auto tr = MakeFloatTrack({Key(0.0f, 7.0f, InterpMode::Step),
                                  Key(2.0f, 99.0f, InterpMode::Step)});
        assert(Near(Anim::SampleTrack(tr, 1.999f).x, 7.0f) && "step：区间内保持 k0=7");
        assert(Near(Anim::SampleTrack(tr, 2.0f).x, 99.0f) && "step：到 k1 跳到 99（末key clamp）");
        std::fprintf(stdout, "  [PASS] step：区间内保持 k0\n");
    }

    // ===== 5. Bezier 零切线 = 中点退化为线性中点 =====
    // 切线 m0=m1=0 时 Hermite → h00*P0 + h01*P1；u=0.5 时 h00=h01=0.5 → 中点。
    {
        auto tr = MakeFloatTrack({Key(0.0f, 0.0f, InterpMode::Bezier),
                                  Key(2.0f, 80.0f, InterpMode::Bezier)});
        assert(Near(Anim::SampleTrack(tr, 1.0f).x, 40.0f) &&
               "Bezier 零切线 t=1（中点）→ 40（退化为线性中点）");
        // 端点必须精确命中 P0 / P1（u=0 → h00=1；u→1 → h01=1）。
        assert(Near(Anim::SampleTrack(tr, 0.0f).x, 0.0f) && "Bezier u=0 → P0");
        std::fprintf(stdout, "  [PASS] Bezier 零切线退化为线性中点 + 端点命中\n");
    }

    // ===== 6. Bezier 正出切线 → 中点高于线性（ease-out 抬升）=====
    {
        auto tr = MakeFloatTrack({Key(0.0f, 0.0f, InterpMode::Bezier, {}, glm::vec2(0.0f, 1.0f)),
                                  Key(2.0f, 80.0f, InterpMode::Bezier)});
        const float mid = Anim::SampleTrack(tr, 1.0f).x;
        assert(mid > 40.0f && "Bezier outTangent.y>0 → 中点高于线性中点 40（缓动抬升）");
        std::fprintf(stdout, "  [PASS] Bezier 正切线抬升中点（mid=%.2f > 40）\n", mid);
    }

    // ===== 7. 多 key 二分：3 段中采样中间段 =====
    {
        auto tr = MakeFloatTrack({Key(0.0f, 0.0f, InterpMode::Linear),
                                  Key(1.0f, 10.0f, InterpMode::Linear),
                                  Key(2.0f, 10.0f, InterpMode::Linear),
                                  Key(3.0f, 40.0f, InterpMode::Linear)});
        assert(Near(Anim::SampleTrack(tr, 0.5f).x, 5.0f) && "段[0,1] t=0.5 → 5");
        assert(Near(Anim::SampleTrack(tr, 1.5f).x, 10.0f) && "段[1,2] 平台 t=1.5 → 10");
        assert(Near(Anim::SampleTrack(tr, 2.5f).x, 25.0f) && "段[2,3] t=2.5 → 25");
        std::fprintf(stdout, "  [PASS] 多 key 二分：3 段各自命中正确区间\n");
    }

    // ===== 8. Vec3 track：多维同时插值 =====
    {
        Anim::AnimationTrack tr;
        tr.valueType = Anim::TrackValueType::Vec3;
        Anim::Keyframe a; a.time = 0.0f; a.value = glm::vec4(0, 0, 0, 0); a.interp = InterpMode::Linear;
        Anim::Keyframe b; b.time = 1.0f; b.value = glm::vec4(2, 4, 6, 0); b.interp = InterpMode::Linear;
        tr.keys = {a, b};
        const glm::vec4 v = Anim::SampleTrack(tr, 0.5f);
        assert(Near(v.x, 1.0f) && Near(v.y, 2.0f) && Near(v.z, 3.0f) &&
               "Vec3 track t=0.5 → (1,2,3)（各维独立线性）");
        std::fprintf(stdout, "  [PASS] Vec3 track 多维同时线性插值\n");
    }

    // ===== 9. SortTrackKeys / IsTrackSorted：维护升序不变量 =====
    {
        auto tr = MakeFloatTrack({Key(2.0f, 20.0f, InterpMode::Linear),
                                  Key(0.0f, 0.0f, InterpMode::Linear),
                                  Key(1.0f, 10.0f, InterpMode::Linear)});
        assert(!Anim::IsTrackSorted(tr) && "乱序插入 → IsTrackSorted=false");
        Anim::SortTrackKeys(tr);
        assert(Anim::IsTrackSorted(tr) && "SortTrackKeys 后 → 升序");
        // 排序后采样正确（乱序时二分会失效）。
        assert(Near(Anim::SampleTrack(tr, 0.5f).x, 5.0f) && "排序后段[0,1] t=0.5 → 5");
        assert(Near(Anim::SampleTrack(tr, 1.5f).x, 15.0f) && "排序后段[1,2] t=1.5 → 15");
        std::fprintf(stdout, "  [PASS] SortTrackKeys/IsTrackSorted：维护升序不变量\n");
    }

    // ===== 10. ComputeClipDuration / WrapClipTime（loop / clamp）=====
    {
        Anim::AnimationClip clip;
        clip.duration = 4.0f;
        // track A 末 key t=2，track B 末 key t=3 → 内容时长 max = 3。
        clip.tracks.push_back(MakeFloatTrack({Key(0.0f, 0.0f, InterpMode::Linear),
                                              Key(2.0f, 1.0f, InterpMode::Linear)}));
        clip.tracks.push_back(MakeFloatTrack({Key(1.0f, 0.0f, InterpMode::Linear),
                                              Key(3.0f, 1.0f, InterpMode::Linear)}));
        assert(Near(Anim::ComputeClipDuration(clip), 3.0f) && "内容时长 = max 末 key = 3");

        clip.loop = false;
        assert(Near(Anim::WrapClipTime(clip, 5.0f), 4.0f) && "非 loop t=5 → clamp 到 duration 4");
        assert(Near(Anim::WrapClipTime(clip, -1.0f), 0.0f) && "非 loop t=-1 → clamp 0");
        clip.loop = true;
        assert(Near(Anim::WrapClipTime(clip, 5.0f), 1.0f) && "loop t=5 → 5 mod 4 = 1");
        assert(Near(Anim::WrapClipTime(clip, -1.0f), 3.0f) && "loop t=-1 → 负值规整到 3");
        std::fprintf(stdout, "  [PASS] ComputeClipDuration + WrapClipTime（loop/clamp）\n");
    }

    // ===== 11. AddKeyframeSorted：编辑器打键维持升序 + 同时间覆盖 =====
    {
        Anim::AnimationTrack tr;
        tr.valueType = Anim::TrackValueType::Float;
        Anim::AddKeyframeSorted(tr, Key(2.0f, 20.0f, InterpMode::Linear));
        Anim::AddKeyframeSorted(tr, Key(0.0f, 0.0f, InterpMode::Linear));   // 插到最前
        Anim::AddKeyframeSorted(tr, Key(1.0f, 10.0f, InterpMode::Linear));  // 插中间
        assert(tr.keys.size() == 3 && Anim::IsTrackSorted(tr) && "3 个乱序打键 → 有序");
        assert(Near(tr.keys[0].time, 0.0f) && Near(tr.keys[1].time, 1.0f) &&
               Near(tr.keys[2].time, 2.0f) && "打键后 time 升序排列");
        // 同时间重打键 → 覆盖值，不新增 key。
        Anim::AddKeyframeSorted(tr, Key(1.0f, 99.0f, InterpMode::Step));
        assert(tr.keys.size() == 3 && "同 time 重打键 → 覆盖不新增");
        assert(Near(Anim::SampleTrack(tr, 1.0f).x, 99.0f) && "重打键值覆盖为 99（Step 保持）");
        std::fprintf(stdout, "  [PASS] AddKeyframeSorted：打键维持升序 + 同时间覆盖\n");
    }

    // ===== 12. FindKeyframeIndexNear / RemoveKeyframe：编辑器选键 + 删键 =====
    {
        auto tr = MakeFloatTrack({Key(0.0f, 0.0f, InterpMode::Linear),
                                  Key(1.0f, 10.0f, InterpMode::Linear),
                                  Key(2.0f, 20.0f, InterpMode::Linear)});
        assert(Anim::FindKeyframeIndexNear(tr, 1.05f, 0.1f) == 1 && "近 t=1（点1.05）→ index 1");
        assert(Anim::FindKeyframeIndexNear(tr, 0.5f, 0.1f) == tr.keys.size() &&
               "无近邻（点0.5容差0.1）→ size 哨兵");
        assert(Anim::RemoveKeyframe(tr, 1) && tr.keys.size() == 2 && Anim::IsTrackSorted(tr) &&
               "删 index 1 → 剩 2 仍升序");
        assert(Near(tr.keys[1].time, 2.0f) && "删中间 key 后 index1 = 原末 key t=2");
        assert(!Anim::RemoveKeyframe(tr, 99) && tr.keys.size() == 2 && "越界删 → no-op false");
        std::fprintf(stdout, "  [PASS] FindKeyframeIndexNear/RemoveKeyframe：选键 + 删键\n");
    }

    std::fprintf(stdout, "[AnimationClipTest] all tests passed.\n");
    return 0;
}
