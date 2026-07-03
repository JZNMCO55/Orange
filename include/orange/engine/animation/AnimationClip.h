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

#include <glm/common.hpp>         // glm::clamp / glm::mix
#include <glm/gtc/quaternion.hpp> // glm::quat / glm::slerp / glm::dot（Quat track 最短弧）
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Orange::Engine::Animation
{

    // 关键帧之间的插值方式。
    enum class InterpMode : std::uint8_t
    {
        Step,   // 阶梯：保持 k0 值直到 k1（无过渡）
        Linear, // 线性插值
        Bezier, // 三次 Bezier（用 outTangent / inTangent）
    };

    // track 的值维度——决定 value 用前几维。采样按 vec4 统一算，消费者按
    // valueType 取前 N 维（避免按 type 分支采样逻辑）。
    enum class TrackValueType : std::uint8_t
    {
        Float, // value.x
        Vec2,  // value.xy
        Vec3,  // value.xyz
        Vec4,  // value.xyzw
        Quat,  // value.xyzw 解释为四元数 (x,y,z,w)；采样走最短弧 slerp/nlerp 而非逐分量 mix
    };

    // 单个关键帧。value 按 track 的 valueType 用前 N 维；其余维忽略。
    // inTangent / outTangent 仅 Bezier 用——单位方框内的归一化控制柄 (dx, dy)，
    // 语义对标 CSS cubic-bezier / After Effects：把相邻两帧的过渡看成 (0,0)→(1,1)
    // 的单位三次 Bezier，outTangent 是从本帧 (0,0) 出发的控制点偏移 c1，inTangent 是
    // 落到下一帧 (1,1) 的控制点偏移（c2 = (1,1) + inTangent，故 inTangent.x 通常为负，
    // 指回前一帧方向）。dx 控制 **时间方向** 缓动（ease-in/out 的非线性时序，夹到 [0,1]
    // 保 X 单调）、dy 控制值方向抬升（可超出 [0,1] 实现 overshoot / 回弹）。多维 value
    // 共享同一标量时序曲线（各维同步缓动）。
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

    // 动画事件：在 clip 时间线上某时刻触发的具名通知（gameplay 用，如"攻击判定生成"
    // "脚步声"）。ClipAnimator 正向播放越过该 time 时回调消费者。name 语义由游戏侧解释。
    struct AnimationEvent
    {
        float       time{0.0f}; // 触发时刻（秒，clip-local）
        std::string name;       // 事件名（游戏侧 dispatch 键）
    };

    // 一个动画 clip：时长 + 是否循环 + 多条 track + 事件。
    struct AnimationClip
    {
        std::string                 name;
        float                       duration{0.0f};
        bool                        loop{false};
        std::vector<AnimationTrack> tracks;
        std::vector<AnimationEvent> events; // 按 time 升序（非强制；ClipAnimator 不假设有序）
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
    // 单位三次 Bezier 缓动求解（CSS cubic-bezier / After Effects 时间缓动模型）。
    // 控制点 P0=(0,0)、P3=(1,1)，c1=(c1x,c1y)、c2=(c2x,c2y) 落在单位方框内。给定线性
    // 时间分数 x∈[0,1]，先反解参数 s 使 BezierX(s)=x（Newton-Raphson 起步 + bisection
    // 兜底），再取 BezierY(s) 作缓动后的值分数。这让控制柄的 **时间方向（.x）** 真正参与，
    // 可表达 ease-in / ease-out / ease-in-out 的非线性 *时序*（旧 MVP 只用 .y 抬升值、时间
    // 恒线性）。c1x/c2x 夹到 [0,1] 保证 X(s) 单调（合法缓动函数前提）；c1y/c2y 不夹，允许
    // overshoot / 回弹。x 越界端点精确返回 0 / 1（端点命中性质）。
    inline float CubicBezierEase(float c1x, float c1y, float c2x, float c2y, float x) noexcept
    {
        c1x = glm::clamp(c1x, 0.0f, 1.0f);
        c2x = glm::clamp(c2x, 0.0f, 1.0f);
        if (x <= 0.0f)
        {
            return 0.0f;
        }
        if (x >= 1.0f)
        {
            return 1.0f;
        }

        // P0=0、P3=1 的三次 Bezier 分量值与其对 s 的导数（a=c1 分量、b=c2 分量）。
        const auto bez = [](float s, float a, float b) noexcept
        {
            const float oms = 1.0f - s;
            return 3.0f * oms * oms * s * a + 3.0f * oms * s * s * b + s * s * s;
        };
        const auto dbez = [](float s, float a, float b) noexcept
        {
            const float oms = 1.0f - s;
            return 3.0f * oms * oms * a + 6.0f * oms * s * (b - a) + 3.0f * s * s * (1.0f - b);
        };

        // Newton-Raphson 反解 BezierX(s)=x，初值 s=x（X 近线性时几次即收敛）。
        float s = x;
        for (int i = 0; i < 8; ++i)
        {
            const float err = bez(s, c1x, c2x) - x;
            if (std::fabs(err) < 1e-6f)
            {
                return bez(s, c1y, c2y);
            }
            const float d = dbez(s, c1x, c2x);
            if (std::fabs(d) < 1e-6f)
            {
                break;
            } // 导数过小 → 转 bisection
            s -= err / d;
        }
        // bisection 兜底：Newton 不收敛 / 跑出 [0,1] 时稳收敛。
        float lo = 0.0f;
        float hi = 1.0f;
        s        = glm::clamp(s, 0.0f, 1.0f);
        for (int i = 0; i < 40; ++i)
        {
            const float xs = bez(s, c1x, c2x);
            if (std::fabs(xs - x) < 1e-6f)
            {
                break;
            }
            if (xs < x)
            {
                lo = s;
            }
            else
            {
                hi = s;
            }
            s = 0.5f * (lo + hi);
        }
        return bez(s, c1y, c2y);
    }

    inline glm::vec4 SampleTrack(const AnimationTrack& track, float t) noexcept
    {
        const auto& keys = track.keys;
        if (keys.empty())
        {
            return glm::vec4(0.0f);
        }
        if (t <= keys.front().time)
        {
            return keys.front().value;
        }
        if (t >= keys.back().time)
        {
            return keys.back().value;
        }

        // 二分：找第一个 time > t 的 key，其前一个即 k0。
        std::size_t lo = 0;
        std::size_t hi = keys.size(); // [lo, hi) 半开
        while (lo + 1 < hi)
        {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (keys[mid].time <= t)
            {
                lo = mid;
            }
            else
            {
                hi = mid;
            }
        }
        const Keyframe& k0 = keys[lo];
        const Keyframe& k1 = keys[lo + 1];

        const float span = k1.time - k0.time;
        if (span <= 0.0f)
        {
            return k0.value;
        } // 退化（重复 time）防除零
        const float u = (t - k0.time) / span;

        // 求插值分数 frac：Step 走 k0（下方提前返回）、Linear 直接 u、Bezier 反解缓动。
        // 切线作单位方框 (0,0)→(1,1) 内的 2D 控制柄：c1 = k0.outTangent（从 (0,0)
        // 出发的控制点偏移）、c2 = (1,1) + k1.inTangent（落到 (1,1) 的控制点偏移，
        // inTangent.x 通常为负）。先按线性时间分数 u 反解 Bezier 参数得 **缓动后的
        // 值分数**（时间方向 .x 真正参与 → 支持 ease-in/out 时序），再用它在 k0→k1
        // 值之间插值；多维值共享同一标量缓动分数（CSS 式：一条时序曲线作用整段过渡）。
        if (k0.interp == InterpMode::Step)
        {
            return k0.value;
        }
        const float frac = (k0.interp == InterpMode::Bezier)
                               ? CubicBezierEase(k0.outTangent.x, k0.outTangent.y,
                                                 1.0f + k1.inTangent.x, 1.0f + k1.inTangent.y, u)
                               : u; // Linear

        // Quat track：value.xyzw 是四元数，按最短弧 slerp（dot<0 取反保最短弧），不能逐
        // 分量 mix（会得非单位 / 绕远弧）。从 value 构造 glm::quat 注意内存序——glm 构造
        // 取 (w,x,y,z)，成员是 .x/.y/.z/.w；约定 value 存 (x,y,z,w)。结果打包回 vec4(xyzw)。
        if (track.valueType == TrackValueType::Quat)
        {
            glm::quat q0(k0.value.w, k0.value.x, k0.value.y, k0.value.z);
            glm::quat q1(k1.value.w, k1.value.x, k1.value.y, k1.value.z);
            if (glm::dot(q0, q1) < 0.0f)
            {
                q1 = -q1;
            } // 取反邻接四元数 → 走最短弧
            const glm::quat q = glm::normalize(glm::slerp(q0, q1, frac));
            return glm::vec4(q.x, q.y, q.z, q.w);
        }

        return glm::mix(k0.value, k1.value, frac);
    }

    // 把 track 关键帧按 time 升序稳定排序——维护 SampleTrack 依赖的升序不变量。
    // 编辑器在任意时间插入 / 拖动 key 后调用。stable_sort 保 time 相等时相对顺序。
    inline void SortTrackKeys(AnimationTrack& track)
    {
        std::stable_sort(track.keys.begin(), track.keys.end(),
                         [](const Keyframe& a, const Keyframe& b)
                         { return a.time < b.time; });
    }

    // 校验 track 是否已按 time 非降序——供 assert / 编辑器保存前校验。空 / 单 key 视为有序。
    inline bool IsTrackSorted(const AnimationTrack& track) noexcept
    {
        for (std::size_t i = 1; i < track.keys.size(); ++i)
        {
            if (track.keys[i].time < track.keys[i - 1].time)
            {
                return false;
            }
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
                track.keys[i] = key; // 同时间重打键 → 覆盖
                return;
            }
            if (track.keys[i].time > key.time)
            {
                track.keys.insert(track.keys.begin() + static_cast<std::ptrdiff_t>(i), key);
                return;
            }
        }
        track.keys.push_back(key); // 比所有 key 都晚 → 追加末尾
    }

    // 找到 time 最接近 t（容差内）的 keyframe 索引——编辑器点选 dopesheet 上的 key 用。
    // 无命中返回 keys.size()（"未找到"哨兵，调用方与 size 比较判定）。多个等距取先到的。
    inline std::size_t FindKeyframeIndexNear(const AnimationTrack& track, float t,
                                             float tolerance) noexcept
    {
        std::size_t best     = track.keys.size();
        float       bestDist = tolerance;
        for (std::size_t i = 0; i < track.keys.size(); ++i)
        {
            const float d = std::fabs(track.keys[i].time - t);
            if (d <= bestDist)
            {
                bestDist = d;
                best     = i;
            }
        }
        return best;
    }

    // 删除 index 处的 keyframe——编辑器删键用。index 越界 → no-op 返 false。
    // vector erase 保序，删除不破坏剩余 key 的升序不变量。
    inline bool RemoveKeyframe(AnimationTrack& track, std::size_t index)
    {
        if (index >= track.keys.size())
        {
            return false;
        }
        track.keys.erase(track.keys.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    // 把 index 处 keyframe 移到新 time——编辑器在 dopesheet 上水平拖 key 用。移动后重新
    // 维持升序（该 key 索引位置可能变）；若新 time 与另一 key 重合则覆盖那个（沿用
    // AddKeyframeSorted 的同时间覆盖语义）。index 越界 → no-op false。value/插值/切线随迁。
    inline bool MoveKeyframeTime(AnimationTrack& track, std::size_t index, float newTime)
    {
        if (index >= track.keys.size())
        {
            return false;
        }
        Keyframe moved = track.keys[index];
        moved.time     = newTime;
        track.keys.erase(track.keys.begin() + static_cast<std::ptrdiff_t>(index));
        AddKeyframeSorted(track, moved);
        return true;
    }

    // 各 track 末 key 时间的最大值——clip 的"内容时长"。供编辑器校验 / 派生
    // AnimationClip::duration（用户可能手设 duration 与 key 不符，本函数给真实下界）。
    // 假设各 track 已升序（末 key 时间最大）；空 track 贡献 0。
    inline float ComputeClipDuration(const AnimationClip& clip) noexcept
    {
        float maxT = 0.0f;
        for (const AnimationTrack& tr : clip.tracks)
        {
            if (!tr.keys.empty())
            {
                maxT = std::max(maxT, tr.keys.back().time);
            }
        }
        // 事件也是 clip 的"内容"——末尾事件（或纯事件 clip，无任何 track）晚于末关键帧时，
        // duration 必须覆盖它：否则 ClipAnimator 在 duration<=0 时据此推导出过小的 duration，
        // 非 loop 播放被 WrapClipTime clamp、loop 播放被取模，该事件永不被 FireEvents 越过触发。
        // events 不保证有序（见 AnimationClip::events 注释），故遍历取 max 而非取 back。
        for (const AnimationEvent& ev : clip.events)
        {
            maxT = std::max(maxT, ev.time);
        }
        return maxT;
    }

    // 把 clip.duration 刷新为各 track 关键帧的实际时长（ComputeClipDuration）。编辑器在
    // 增 / 删 / 拖 key 改变了关键帧分布后调用，使 duration 与内容同步（dopesheet 编辑流
    // 直接用，见 docs/b2.3-timeline-dopesheet-spec.md）。注：这会**覆盖**用户手设的
    // trailing-hold duration——只在"按内容自动定时长"语义下调用。
    inline void RecomputeClipDuration(AnimationClip& clip) noexcept
    {
        clip.duration = ComputeClipDuration(clip);
    }

    // 把播放 elapsed 时间换算到 clip-local 采样时间：loop 时按 duration 取模
    //（含负值规整到 [0,duration)），非 loop 时 clamp 到 [0,duration]。供 playhead /
    // ClipAnimator 把累计时间喂给 SampleTrack 前调用。duration<=0 直接返 0（退化）。
    inline float WrapClipTime(const AnimationClip& clip, float t) noexcept
    {
        const float dur = clip.duration;
        if (dur <= 0.0f)
        {
            return 0.0f;
        }
        if (!clip.loop)
        {
            return glm::clamp(t, 0.0f, dur);
        }
        float wrapped = std::fmod(t, dur);
        if (wrapped < 0.0f)
        {
            wrapped += dur;
        }
        return wrapped;
    }

    // ---- clip 级 track 管理（编辑器创作 API，承接上面的 keyframe 级原语）----
    // 注：C++17 无 std::string / std::string_view 异构 operator==，统一把 targetName
    // 提升成 string_view 比较。

    // 按 targetName 找首个匹配 track。不存在 → nullptr。返回的指针在对 clip.tracks
    // 做增删（可能 reallocate）前有效。
    inline AnimationTrack* FindTrack(AnimationClip& clip, std::string_view targetName) noexcept
    {
        for (AnimationTrack& tr : clip.tracks)
        {
            if (std::string_view(tr.targetName) == targetName)
            {
                return &tr;
            }
        }
        return nullptr;
    }

    inline const AnimationTrack* FindTrack(const AnimationClip& clip,
                                           std::string_view     targetName) noexcept
    {
        for (const AnimationTrack& tr : clip.tracks)
        {
            if (std::string_view(tr.targetName) == targetName)
            {
                return &tr;
            }
        }
        return nullptr;
    }

    // find-or-create：返回 targetName 对应 track 的引用。已存在则直接返回（**不**改其
    // valueType——避免静默改写已有轨道类型）；不存在则用给定 valueType 新建并 push。
    inline AnimationTrack& UpsertTrack(AnimationClip& clip, std::string_view targetName,
                                       TrackValueType valueType)
    {
        if (AnimationTrack* existing = FindTrack(clip, targetName))
        {
            return *existing;
        }
        AnimationTrack tr;
        tr.targetName = std::string(targetName);
        tr.valueType  = valueType;
        clip.tracks.push_back(std::move(tr));
        return clip.tracks.back();
    }

    // 删除首个 targetName 匹配的 track（编辑器删轨道用）。不存在 → false。
    inline bool RemoveTrack(AnimationClip& clip, std::string_view targetName)
    {
        for (std::size_t i = 0; i < clip.tracks.size(); ++i)
        {
            if (std::string_view(clip.tracks[i].targetName) == targetName)
            {
                clip.tracks.erase(clip.tracks.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
        }
        return false;
    }

    // 在 targetName 轨道上打一个 key —— dopesheet "K 插入键"在 clip 级的入口：
    // UpsertTrack（按需建轨）+ AddKeyframeSorted（同时间覆盖，维持升序）。返回该 track。
    inline AnimationTrack& UpsertKeyframe(AnimationClip& clip, std::string_view targetName,
                                          TrackValueType valueType, const Keyframe& key)
    {
        AnimationTrack& tr = UpsertTrack(clip, targetName, valueType);
        AddKeyframeSorted(tr, key);
        return tr;
    }

    // ---- clip 事件管理（timeline 事件轨道创作；与 keyframe CRUD 同款）----
    // 按 time 升序稳定排序事件（同 time 保持插入序，允许同时刻多事件）。ClipAnimator
    // 触发不要求有序，但编辑器展示 / 二分查找受益。
    inline void SortClipEvents(AnimationClip& clip)
    {
        std::stable_sort(clip.events.begin(), clip.events.end(),
                         [](const AnimationEvent& a, const AnimationEvent& b)
                         { return a.time < b.time; });
    }

    // 插入一个事件并维持升序（同 time 允许并存——不同 name 的多事件可同时触发）。
    inline void AddClipEvent(AnimationClip& clip, const AnimationEvent& ev)
    {
        auto pos = std::upper_bound(clip.events.begin(), clip.events.end(), ev.time,
                                    [](float t, const AnimationEvent& e)
                                    { return t < e.time; });
        clip.events.insert(pos, ev);
    }

    // 删除 index 处事件。越界 → no-op false。
    inline bool RemoveClipEvent(AnimationClip& clip, std::size_t index)
    {
        if (index >= clip.events.size())
        {
            return false;
        }
        clip.events.erase(clip.events.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

} // namespace Orange::Engine::Animation

#endif // ORANGE_ENGINE_ANIMATION_ANIMATION_CLIP_H
