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
    // 零切线 → 控制点 c1=(0,0)、c2=(1,1)，即 cubic-bezier(0,0,1,1) 恒等于线性缓动
    //（X(s)≡Y(s) → eased=u）；u=0.5 时 eased=0.5 → 中点 40。
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

    // ===== 6b. ease-in（cubic-bezier(0.42,0,1,1)）：纯 *时间* 切线（.y=0）改变时序 =====
    // 关键：两端切线值方向 .y 全 0 —— 旧 Hermite-MVP 只看 .y，会退化成线性中点 50；
    // 新模型用 .x 反解时间 → 慢启动，时间分数 0.5 处值仍落后（< 50）。这是旧实现做不到的。
    {
        auto tr = MakeFloatTrack({Key(0.0f, 0.0f, InterpMode::Bezier, {}, glm::vec2(0.42f, 0.0f)),
                                  Key(2.0f, 100.0f, InterpMode::Bezier)});
        const float mid = Anim::SampleTrack(tr, 1.0f).x;  // 时间分数 u=0.5
        assert(mid < 50.0f &&
               "ease-in（时间柄 .x>0、值柄 .y=0）中点应低于线性 50（慢启动；旧 MVP 退化线性）");
        std::fprintf(stdout, "  [PASS] Bezier ease-in 时序缓动（mid=%.2f < 50）\n", mid);
    }

    // ===== 6c. ease-out（cubic-bezier(0,0,0.58,1)）：快启动 → 中点高于线性 =====
    {
        auto tr = MakeFloatTrack({Key(0.0f, 0.0f, InterpMode::Bezier),
                                  Key(2.0f, 100.0f, InterpMode::Bezier, glm::vec2(-0.42f, 0.0f), {})});
        const float mid = Anim::SampleTrack(tr, 1.0f).x;
        assert(mid > 50.0f &&
               "ease-out（下一帧 inTangent.x=-0.42、.y=0）中点应高于线性 50（快启动）");
        std::fprintf(stdout, "  [PASS] Bezier ease-out 时序缓动（mid=%.2f > 50）\n", mid);
    }

    // ===== 6d. ease-in-out 对称（cubic-bezier(0.42,0,0.58,1)）：中点精确 50、两侧对称 =====
    {
        auto tr = MakeFloatTrack({Key(0.0f, 0.0f, InterpMode::Bezier, {}, glm::vec2(0.42f, 0.0f)),
                                  Key(2.0f, 100.0f, InterpMode::Bezier, glm::vec2(-0.42f, 0.0f), {})});
        const float q1  = Anim::SampleTrack(tr, 0.5f).x;  // u=0.25
        const float mid = Anim::SampleTrack(tr, 1.0f).x;  // u=0.5
        const float q3  = Anim::SampleTrack(tr, 1.5f).x;  // u=0.75
        assert(Near(mid, 50.0f, 0.5f) && "对称缓动中点精确 50");
        assert(q1 < 50.0f && q3 > 50.0f && "ease-in-out：前慢（q1<50）后快（q3>50）");
        assert(Near(q1 + q3, 100.0f, 1.0f) && "前后四分点关于中点对称（q1+q3≈100）");
        std::fprintf(stdout, "  [PASS] Bezier ease-in-out 对称（q1=%.2f mid=%.2f q3=%.2f）\n",
                     q1, mid, q3);
    }

    // ===== 6e. 纯时间缓动是单调映射 + 端点精确（合法 [0,1]→[0,1] 缓动函数）=====
    {
        auto tr = MakeFloatTrack({Key(0.0f, 0.0f, InterpMode::Bezier, {}, glm::vec2(0.25f, 0.0f)),
                                  Key(4.0f, 100.0f, InterpMode::Bezier, glm::vec2(-0.25f, 0.0f), {})});
        assert(Near(Anim::SampleTrack(tr, 0.0f).x, 0.0f) && "Bezier 端点 u=0 → 0");
        assert(Near(Anim::SampleTrack(tr, 4.0f).x, 100.0f) && "Bezier 端点 u=1 → 100");
        float prev = -1.0f;
        for (int i = 0; i <= 40; ++i)
        {
            const float t = 4.0f * static_cast<float>(i) / 40.0f;
            const float v = Anim::SampleTrack(tr, t).x;
            assert(v >= prev - 1e-3f && "纯时间缓动：采样值随 t 单调非降");
            prev = v;
        }
        std::fprintf(stdout, "  [PASS] Bezier 纯时间缓动单调 + 端点精确\n");
    }

    // ===== 6f. 值方向 overshoot（.y 超出 [0,1]）→ 中段越过端点值（anticipation / 回弹）=====
    // squash-stretch 的"juice"：过渡中段值临时冲过目标再回落。值柄 .y>1 由 glm::mix 外插实现。
    {
        auto tr = MakeFloatTrack({Key(0.0f, 0.0f, InterpMode::Bezier, {}, glm::vec2(0.3f, 1.6f)),
                                  Key(2.0f, 100.0f, InterpMode::Bezier, glm::vec2(-0.3f, 1.6f), {})});
        float peak = 0.0f;
        for (int i = 0; i <= 40; ++i)
        {
            const float t = 2.0f * static_cast<float>(i) / 40.0f;
            peak = std::max(peak, Anim::SampleTrack(tr, t).x);
        }
        assert(peak > 100.0f && "值方向 overshoot 柄 → 中段峰值越过端点 100（回弹）");
        std::fprintf(stdout, "  [PASS] Bezier 值方向 overshoot 回弹（peak=%.2f > 100）\n", peak);
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

    // ===== 10b. ComputeClipDuration 纳入 events（修复：末尾/纯事件 clip 不被截断）=====
    {
        // 事件晚于末关键帧：duration 必须覆盖事件，否则 ClipAnimator 据此推导出过小
        // duration、WrapClipTime clamp 掉、该事件永不触发（bug 回归锁）。
        Anim::AnimationClip clip;
        clip.tracks.push_back(MakeFloatTrack({Key(0.0f, 0.0f, InterpMode::Linear),
                                              Key(1.0f, 1.0f, InterpMode::Linear)}));
        Anim::AddClipEvent(clip, Anim::AnimationEvent{1.5f, "late"});
        assert(Near(Anim::ComputeClipDuration(clip), 1.5f) &&
               "ComputeClipDuration 取 max(末 key 1.0, 事件 1.5)=1.5");

        // 纯事件 clip（无 track）：duration = max 事件 time，非 0。
        Anim::AnimationClip pureEvents;
        Anim::AddClipEvent(pureEvents, Anim::AnimationEvent{2.0f, "boom"});
        assert(Near(Anim::ComputeClipDuration(pureEvents), 2.0f) &&
               "纯事件 clip 内容时长 = max 事件 time = 2（非 0）");
        std::fprintf(stdout, "  [PASS] ComputeClipDuration 纳入 events（末尾/纯事件不截断）\n");
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

    // ===== 13. MoveKeyframeTime：dopesheet 水平拖 key 改时间 =====
    {
        auto tr = MakeFloatTrack({Key(0.0f, 0.0f, InterpMode::Linear),
                                  Key(1.0f, 10.0f, InterpMode::Linear),
                                  Key(2.0f, 20.0f, InterpMode::Linear)});
        // 把 index 1（t=1,val=10）拖到 t=2.5 → 应重排到末尾、仍升序。
        assert(Anim::MoveKeyframeTime(tr, 1, 2.5f) && Anim::IsTrackSorted(tr) &&
               "拖 key 到 t=2.5 → 重排仍升序");
        assert(tr.keys.size() == 3 && Near(tr.keys[2].time, 2.5f) &&
               Near(tr.keys[2].value.x, 10.0f) && "拖后末 key t=2.5 且 value 随迁=10");
        // 越界拖 → no-op。
        assert(!Anim::MoveKeyframeTime(tr, 9, 0.0f) && "越界拖 → no-op false");
        std::fprintf(stdout, "  [PASS] MoveKeyframeTime：拖 key 改时间维持升序 + value 随迁\n");
    }

    // ===== 14. clip 级 track 管理：Find/Upsert/Remove/UpsertKeyframe =====
    {
        Anim::AnimationClip clip;

        // FindTrack 空 clip → nullptr。
        assert(Anim::FindTrack(clip, "position") == nullptr && "空 clip 找不到 track");

        // UpsertTrack 新建。
        Anim::AnimationTrack& pos = Anim::UpsertTrack(clip, "position", Anim::TrackValueType::Vec3);
        assert(clip.tracks.size() == 1 && pos.targetName == "position" &&
               pos.valueType == Anim::TrackValueType::Vec3 && "UpsertTrack 新建 Vec3 轨道");

        // UpsertTrack 已存在 → 返回同一轨道、不改 valueType、不新增。
        Anim::AnimationTrack& pos2 = Anim::UpsertTrack(clip, "position", Anim::TrackValueType::Float);
        assert(clip.tracks.size() == 1 && &pos2 == &clip.tracks[0] &&
               pos2.valueType == Anim::TrackValueType::Vec3 && "已存在 → 复用且不改类型");

        // FindTrack const 重载命中。
        const Anim::AnimationClip& cclip = clip;
        assert(Anim::FindTrack(cclip, "position") != nullptr && "const FindTrack 命中");

        // UpsertKeyframe：在 rotation 轨道（自动建）上打 2 个键，维持升序。
        Anim::UpsertKeyframe(clip, "rotation", Anim::TrackValueType::Vec3,
                             Key(1.0f, 90.0f, InterpMode::Linear));
        Anim::UpsertKeyframe(clip, "rotation", Anim::TrackValueType::Vec3,
                             Key(0.0f, 0.0f, InterpMode::Linear));
        const Anim::AnimationTrack* rot = Anim::FindTrack(clip, "rotation");
        assert(clip.tracks.size() == 2 && rot != nullptr && rot->keys.size() == 2 &&
               Anim::IsTrackSorted(*rot) && "UpsertKeyframe 自动建轨 + 打键升序");
        // 同时间覆盖（沿用 AddKeyframeSorted 语义）。
        Anim::UpsertKeyframe(clip, "rotation", Anim::TrackValueType::Vec3,
                             Key(1.0f, 45.0f, InterpMode::Step));
        rot = Anim::FindTrack(clip, "rotation");
        assert(rot->keys.size() == 2 && Near(rot->keys[1].value.x, 45.0f) &&
               rot->keys[1].interp == InterpMode::Step && "同时间打键覆盖");

        // RemoveTrack。
        assert(Anim::RemoveTrack(clip, "position") && clip.tracks.size() == 1 &&
               "RemoveTrack 删 position");
        assert(!Anim::RemoveTrack(clip, "nonexistent") && "删不存在 track → false");
        assert(Anim::FindTrack(clip, "rotation") != nullptr && "rotation 仍在");
        std::fprintf(stdout, "  [PASS] clip 级 track 管理：Find/Upsert/Remove/UpsertKeyframe\n");
    }

    // ===== 15. RecomputeClipDuration：按内容刷新 duration =====
    {
        Anim::AnimationClip clip;
        clip.duration = 0.0f;
        Anim::UpsertKeyframe(clip, "position.x", Anim::TrackValueType::Float,
                             Key(0.0f, 0.0f, InterpMode::Linear));
        Anim::UpsertKeyframe(clip, "position.x", Anim::TrackValueType::Float,
                             Key(3.5f, 10.0f, InterpMode::Linear));
        Anim::RecomputeClipDuration(clip);
        assert(Near(clip.duration, 3.5f) && "duration 应刷新为末 key 时间 3.5");
        // 删掉末 key 后再刷新 → 缩短。
        auto* tr = Anim::FindTrack(clip, "position.x");
        Anim::RemoveKeyframe(*tr, 1);
        Anim::RecomputeClipDuration(clip);
        assert(Near(clip.duration, 0.0f) && "删末 key 后 duration 回到 0（仅剩 t=0 key）");
        std::fprintf(stdout, "  [PASS] RecomputeClipDuration 按内容刷新\n");
    }

    // ===== 16. clip 事件管理：AddClipEvent 升序 / Sort / Remove =====
    {
        Anim::AnimationClip clip;
        Anim::AddClipEvent(clip, Anim::AnimationEvent{1.0f, "b"});
        Anim::AddClipEvent(clip, Anim::AnimationEvent{0.5f, "a"});
        Anim::AddClipEvent(clip, Anim::AnimationEvent{2.0f, "d"});
        Anim::AddClipEvent(clip, Anim::AnimationEvent{0.5f, "a2"});  // 同 time 并存
        assert(clip.events.size() == 4 && "同 time 事件并存");
        // 升序（同 time 保持插入序：a 在 a2 前）。
        assert(Near(clip.events[0].time, 0.5f) && clip.events[0].name == "a");
        assert(clip.events[1].name == "a2" && "同 time 插入序保持");
        assert(Near(clip.events[2].time, 1.0f) && Near(clip.events[3].time, 2.0f) &&
               "AddClipEvent 维持升序");

        // RemoveClipEvent 越界 → false。
        assert(!Anim::RemoveClipEvent(clip, 99) && "越界删 → false");
        assert(Anim::RemoveClipEvent(clip, 0) && clip.events.size() == 3 && "删 index 0");

        // SortClipEvents：乱序后排序。
        Anim::AnimationClip c2;
        c2.events = {{3.0f, "x"}, {1.0f, "y"}, {2.0f, "z"}};
        Anim::SortClipEvents(c2);
        assert(Near(c2.events[0].time, 1.0f) && Near(c2.events[1].time, 2.0f) &&
               Near(c2.events[2].time, 3.0f) && "SortClipEvents 升序");
        std::fprintf(stdout, "  [PASS] clip 事件管理：Add/Sort/Remove\n");
    }

    std::fprintf(stdout, "[AnimationClipTest] all tests passed.\n");
    return 0;
}
