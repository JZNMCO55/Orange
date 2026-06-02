#ifndef ORANGE_ENGINE_ANIMATION_ANIMATION_CLIP_H
#define ORANGE_ENGINE_ANIMATION_ANIMATION_CLIP_H

// ---------------------------------------------------------------------------
// AnimationClip —— 引擎原生、可序列化的关键帧动画 clip 数据模型（B2.1 地基）。
//
// 背景：现有 ProceduralAnimator 的 channel 是 std::function<T(float)>（C++
// lambda）—— 曲线是代码不是数据，编辑器无法编、无法序列化。AnimationClip
// 把"时间 → 值"的曲线表示成**数据**（track + keyframe + 插值模式），让
// timeline / 曲线编辑器能创作、让 .anim 文件能序列化。
//
// 本头是**纯数据 + 纯采样函数**（无 GPU / 无 OrangeRender 依赖），采样
// 是 header-only inline 的语言无关算法，headless 可测。如何把采样接到
// ProceduralAnimator（数据 channel 与现有 lambda channel 并存）/ 接到一个
// 写 TransformComponent 的 ClipAnimator，是后续 increment——本头不绑定消费者。
//
// 设计文档：docs/b2-animation-authoring-design.md。
// ---------------------------------------------------------------------------

#include <glm/common.hpp>      // glm::clamp / glm::mix
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Orange::Engine::Animation
{

// 关键帧之间的插值方式。
enum class InterpMode : std::uint8_t
{
    Step,    // 阶梯：保持 k0 值直到 k1（无过渡）
    Linear,  // 线性插值
    Bezier,  // 三次 Bezier（用 outTangent / inTangent）
};

// track 的值维度——决定 value 用前几维。采样按 vec4 统一算，消费者按
// valueType 取前 N 维（避免按 type 分支采样逻辑）。
enum class TrackValueType : std::uint8_t
{
    Float,  // value.x
    Vec2,   // value.xy
    Vec3,   // value.xyz
    Vec4,   // value.xyzw
};

// 单个关键帧。value 按 track 的 valueType 用前 N 维；其余维忽略。
// inTangent / outTangent 仅 Bezier 用——(dx, dy) 控制柄，dx 为时间方向、
// dy 为值方向（这里值取 .x 维近似；多维 Bezier 后续可扩成每维独立切线）。
struct Keyframe
{
    float      time{0.0f};
    glm::vec4  value{0.0f};
    InterpMode interp{InterpMode::Linear};
    glm::vec2  inTangent{0.0f};
    glm::vec2  outTangent{0.0f};
};

// 一条轨道：一组按 time 升序的关键帧，驱动一个具名目标。
// targetName 语义由消费者解释（material uniform 名 / transform 字段路径等），
// 本数据层不绑定具体目标种类。
struct AnimationTrack
{
    std::string           targetName;
    TrackValueType        valueType{TrackValueType::Float};
    std::vector<Keyframe> keys;
};

// 一个动画 clip：时长 + 是否循环 + 多条 track。
struct AnimationClip
{
    std::string                 name;
    float                       duration{0.0f};
    bool                        loop{false};
    std::vector<AnimationTrack> tracks;
};

// ---- 采样（header-only inline，语言无关算法，headless 可测）-------------
//
// sample(track, t)：返回 track 在时间 t 的 vec4 值。
//   * 空 track → 返回 (0,0,0,0)。
//   * t ≤ 第一个 key → 返回第一个 key 值（clamp，无外插）。
//   * t ≥ 最后一个 key → 返回最后一个 key 值（clamp）。
//   * 否则二分找包住 t 的相邻 k0,k1，按 k0.interp 插值。
// 不变量：keys 必须按 time 升序（编辑器插入时维持；本函数不排序）。
// 复杂度：O(log N)（N = key 数）的二分。
inline glm::vec4 SampleTrack(const AnimationTrack& track, float t) noexcept
{
    const auto& keys = track.keys;
    if (keys.empty())              { return glm::vec4(0.0f); }
    if (t <= keys.front().time)    { return keys.front().value; }
    if (t >= keys.back().time)     { return keys.back().value; }

    // 二分：找第一个 time > t 的 key，其前一个即 k0。
    std::size_t lo = 0;
    std::size_t hi = keys.size();  // [lo, hi) 半开
    while (lo + 1 < hi)
    {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (keys[mid].time <= t) { lo = mid; }
        else                     { hi = mid; }
    }
    const Keyframe& k0 = keys[lo];
    const Keyframe& k1 = keys[lo + 1];

    const float span = k1.time - k0.time;
    if (span <= 0.0f) { return k0.value; }  // 退化（重复 time）防除零
    const float u = (t - k0.time) / span;

    switch (k0.interp)
    {
        case InterpMode::Step:
            return k0.value;
        case InterpMode::Linear:
            return glm::mix(k0.value, k1.value, u);
        case InterpMode::Bezier:
        {
            // 三次 Bezier 的标准 de Casteljau 权重。控制点：
            //   P0 = k0.value，P3 = k1.value；
            //   P1 = P0 + outTangent（值方向用 .y 抬升所有维，时间方向用 .x
            //        缩放——MVP 先用标量参数 u 直接走 Bezier 基函数，切线的
            //        值分量(.y)按比例作用到全维差值上，够 timeline 缓动用）。
            // MVP：用 outTangent.y / inTangent.y 作为进/出缓动强度，落在
            // [P0,P3] 区间内的三次 Bezier（Hermite 等价形式）。
            const float u2 = u * u;
            const float u3 = u2 * u;
            const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;  // P0 基
            const float h10 = u3 - 2.0f * u2 + u;            // m0 基
            const float h01 = -2.0f * u3 + 3.0f * u2;        // P1 基
            const float h11 = u3 - u2;                       // m1 基
            const glm::vec4 m0 = (k1.value - k0.value) * k0.outTangent.y;
            const glm::vec4 m1 = (k1.value - k0.value) * k1.inTangent.y;
            return h00 * k0.value + h10 * m0 + h01 * k1.value + h11 * m1;
        }
    }
    return k0.value;  // 不可达（switch 全覆盖），守编译器
}

// 把 track 关键帧按 time 升序稳定排序——维护 SampleTrack 依赖的升序不变量。
// 编辑器在任意时间插入 / 拖动 key 后调用。stable_sort 保 time 相等时相对顺序。
inline void SortTrackKeys(AnimationTrack& track)
{
    std::stable_sort(track.keys.begin(), track.keys.end(),
                     [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

// 校验 track 是否已按 time 非降序——供 assert / 编辑器保存前校验。空 / 单 key 视为有序。
inline bool IsTrackSorted(const AnimationTrack& track) noexcept
{
    for (std::size_t i = 1; i < track.keys.size(); ++i)
    {
        if (track.keys[i].time < track.keys[i - 1].time) { return false; }
    }
    return true;
}

// 在维持 time 升序的前提下插入一个 keyframe——编辑器在 playhead 处「打键」（K）用。
// 若已存在相同 time 的 key（容差内）则覆盖其值/插值（同时间重打键 = 覆盖，与 Unity
// 一致），否则插到正确有序位置。保持 SampleTrack 依赖的升序不变量，免去整轨 re-sort。
inline void AddKeyframeSorted(AnimationTrack& track, const Keyframe& key)
{
    constexpr float kEps = 1e-6f;
    for (std::size_t i = 0; i < track.keys.size(); ++i)
    {
        if (std::fabs(track.keys[i].time - key.time) <= kEps)
        {
            track.keys[i] = key;  // 同时间重打键 → 覆盖
            return;
        }
        if (track.keys[i].time > key.time)
        {
            track.keys.insert(track.keys.begin() + static_cast<std::ptrdiff_t>(i), key);
            return;
        }
    }
    track.keys.push_back(key);  // 比所有 key 都晚 → 追加末尾
}

// 各 track 末 key 时间的最大值——clip 的"内容时长"。供编辑器校验 / 派生
// AnimationClip::duration（用户可能手设 duration 与 key 不符，本函数给真实下界）。
// 假设各 track 已升序（末 key 时间最大）；空 track 贡献 0。
inline float ComputeClipDuration(const AnimationClip& clip) noexcept
{
    float maxT = 0.0f;
    for (const AnimationTrack& tr : clip.tracks)
    {
        if (!tr.keys.empty()) { maxT = std::max(maxT, tr.keys.back().time); }
    }
    return maxT;
}

// 把播放 elapsed 时间换算到 clip-local 采样时间：loop 时按 duration 取模
//（含负值规整到 [0,duration)），非 loop 时 clamp 到 [0,duration]。供 playhead /
// ClipAnimator 把累计时间喂给 SampleTrack 前调用。duration<=0 直接返 0（退化）。
inline float WrapClipTime(const AnimationClip& clip, float t) noexcept
{
    const float dur = clip.duration;
    if (dur <= 0.0f) { return 0.0f; }
    if (!clip.loop)  { return glm::clamp(t, 0.0f, dur); }
    float wrapped = std::fmod(t, dur);
    if (wrapped < 0.0f) { wrapped += dur; }
    return wrapped;
}

}  // namespace Orange::Engine::Animation

#endif  // ORANGE_ENGINE_ANIMATION_ANIMATION_CLIP_H
