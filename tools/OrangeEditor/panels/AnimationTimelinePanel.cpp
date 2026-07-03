// Animation timeline / dopesheet 面板（B2.3）—— 底部 Animation tab 从占位
// 升级成"编辑选中实体 ClipAnimator 的 clip"的时间轴。
//
// 范围（按 docs/b2.3-timeline-dopesheet-spec.md）：
//   1. 只读 timeline：轨道行（每 track 一行 targetName 标签）+ 关键帧点
//      （按 time 横向定位）+ playhead 游标 + 时间标尺（总长 ComputeClipDuration）。
//   2. playhead scrub：拖游标 → ClipAnimator::Seek(t)（Edit 模式直接写 Transform，
//      实体实时跟随）。
//   3. 打键 / 删键：playhead 处对某 track 打键（K / 按钮）→ UpsertKeyframe；
//      选中 key + Del → RemoveKeyframe。走命令栈。
//   4. 拖 key 改时间：水平拖关键帧点 → MoveKeyframeTime；命令栈 + 连续拖同
//      一 key merge 成一条。
//   5. ▶ 播放预览：复用 host.animPreview（B2.6 的 Edit 期单 animator tick），
//      ▶ 置 previewPlaying=true、⏸ 置 false。不新造 tick 路径。
//   6. 轨道增删 + .anim 写回：UpsertTrack/RemoveTrack；clip 来自 .anim 资产
//      （SourceAssetPath 非空）时显式 "Save to .anim" → SaveAnimationClip。
//   7. 事件轨道（marker 行）：clip.events 展示 + 加/删/拖。
//   8. 曲线编辑器（B2.4）：transport 行 Dopesheet ↔ Curve 模式切换；curve 模式
//      对选中 track 用 SampleTrack 密集采样画曲线（display 与 playback 完全一致——
//      不自己重算插值）+ 每 key 画点 + Bezier 段的 in/out 切线手柄；拖手柄反推改
//      该 key 的 inTangent/outTangent（与 CubicBezierEase 控制柄约定一致的逆运算）；
//      右键 key 切 InterpMode（Step/Linear/Bezier）。编辑同样走 SetAnimationClipCommand。
//
// 架构纪律：clip 编辑一律走 copy-modify-SetClip（拷当前 clip → 在副本上调
// AnimationClip.h 数据原语 → RecomputeClipDuration → 新建 SetAnimationClipCommand
// 压栈，见 command/SetAnimationClipCommand.h）。不给 ClipAnimator 加 MutableClip。
// 颜色 / 尺寸全走 Theme token + GetContentRegionAvail，禁止 hardcode 字面量
// RGBA / 像素（invariant lint 拦）。
//
// dogfood：所有 ImGui 像素 / 拖拽 / hit-test 交互（画得对 / scrub / 打键 /
// 拖键 / 删键 / Undo-Redo / ▶ 预览 / 事件 marker / .anim 写回）headless 测不
// 到，逐条登记 docs/dogfood-checklist.md 待人工 dogfood。

#include "../EditorRenderLayer.h"

#include "../EditorHost.h"
#include "../command/SetAnimationClipCommand.h"
#include "../theme/EditorTheme.h"

#include <orange/engine/animation/AnimationClip.h>
#include <orange/engine/animation/AnimationClipSerialization.h>
#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/ClipAnimator.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/scene/World.h>

#include <imgui.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace
{

    namespace Anim  = Orange::Engine::Animation;
    namespace Theme = Orange::Editor::Theme;

    using Anim::AnimationClip;
    using Anim::AnimationEvent;
    using Anim::AnimationTrack;
    using Anim::ClipAnimator;
    using Anim::Keyframe;

    // ---- 几何常量（派生自字号 / content region，非 hardcode 像素字面量）------
    // 行高 / 关键帧菱形半径 / 命中容差都按当前字号缩放，跟随 DPI scale，避免
    // 写死像素（与 lint SetNextItemWidth 像素字面量规则同源纪律：用语义量推导）。
    float RowHeight()
    {
        // 1.8 倍正文行高：留出菱形 + 上下 padding。GetTextLineHeightWithSpacing
        // 已含 ImGui item spacing，按 DPI 缩放。
        return ImGui::GetTextLineHeightWithSpacing() * 1.8f;
    }

    float KeyRadius()
    {
        // 关键帧菱形半径：约 0.3 个字高，clamp 出一个不至于太小的下界。
        return ImGui::GetFontSize() * 0.32f;
    }

    float HitRadius()
    {
        // 命中容差略大于绘制半径，便于点中（与 ColliderVertexEdit 同款手法）。
        return KeyRadius() * 1.8f;
    }

    // 左侧轨道标签列宽——按一段示意文本宽度派生（禁像素字面量；与 Console
    // 面板用 CalcTextSize 派生列宽同款）。
    float LabelColumnWidth()
    {
        return ImGui::CalcTextSize("position.uniform  ").x;
    }

    // 把 clip 时间 t（秒）映射到时间轴区域内的屏幕 x 像素。
    float TimeToScreenX(float t, float trackX0, float trackW, float duration)
    {
        if (duration <= 0.0f)
        {
            return trackX0;
        }
        const float u = std::clamp(t / duration, 0.0f, 1.0f);
        return trackX0 + u * trackW;
    }

    // 反映射：时间轴区域内屏幕 x → clip 时间（秒），clamp 到 [0,duration]。
    float ScreenXToTime(float screenX, float trackX0, float trackW, float duration)
    {
        if (trackW <= 0.0f || duration <= 0.0f)
        {
            return 0.0f;
        }
        const float u = std::clamp((screenX - trackX0) / trackW, 0.0f, 1.0f);
        return u * duration;
    }

    // 取当前选中实体的 ClipAnimator（仅 backend=="clip" 时非空）。任一环空返回 nullptr。
    ClipAnimator* ResolveSelectedClipAnimator(EditorHost& host)
    {
        using AC     = Anim::AnimatorComponent;
        auto* pWorld = host.scene.pWorld.get();
        if (pWorld == nullptr)
        {
            return nullptr;
        }
        const Orange::Engine::Entity e = host.selection.selectedEntity;
        if (!e.IsValid() || !pWorld->IsValid(e))
        {
            return nullptr;
        }
        auto* ac = pWorld->GetComponent<AC>(e);
        if (ac == nullptr || !ac->animator)
        {
            return nullptr;
        }
        return dynamic_cast<ClipAnimator*>(ac->animator.get());
    }

    // timeline 面板的 per-frame 选中态（哪条轨 / 哪个 key 被选 + 拖动状态）。
    // 纯 view 态，挂 file-scope static——单一面板单一实例，不序列化、不跨实体
    // 持久（切实体时 owner 不同自然失效，下面用 owner entity 校验）。
    struct TimelineSelection
    {
        Orange::Engine::Entity owner = Orange::Engine::Entity::Invalid();
        // 选中的 keyframe：trackIndex / keyIndex（kInvalid = 无选中）。
        std::size_t selTrack = static_cast<std::size_t>(-1);
        std::size_t selKey   = static_cast<std::size_t>(-1);
        // 选中的事件 index（kInvalid = 无选中）。
        std::size_t selEvent = static_cast<std::size_t>(-1);
        // 正在拖动的 key（拖动期间用稳定 merge key 合并命令）。
        bool        draggingKey = false;
        std::size_t dragTrack   = static_cast<std::size_t>(-1);
        std::size_t dragKey     = static_cast<std::size_t>(-1);
        // 正在拖动的事件 marker。
        bool        draggingEvent = false;
        std::size_t dragEventIdx  = static_cast<std::size_t>(-1);
        // 单调递增的拖动会话 id：每次开始拖 key / event 自增一次，作命令 merge key
        // 的稳定后缀。不能用 dragKey/dragEventIdx 作 merge key——它们在拖动中会因
        // 升序重排而变（同一次拖动跨帧 key 不再合并），且 track-only 的旧 merge key
        // 会把"同一轨先后拖两个不同 key"误并成一条 Undo（bug-hunt 发现）。会话 id
        // 在一次拖动内稳定、跨拖动唯一，既能合并单次拖动的逐帧命令、又能区分两次拖动。
        std::uint64_t dragSession = 0;

        void ResetIfOwnerChanged(Orange::Engine::Entity e)
        {
            if (owner != e)
            {
                *this = TimelineSelection{};
                owner = e;
            }
        }
        void ClearKeySel()
        {
            selTrack = selKey = static_cast<std::size_t>(-1);
        }
        void ClearEventSel() { selEvent = static_cast<std::size_t>(-1); }
    };

    TimelineSelection     sTimelineSel;
    constexpr std::size_t kInvalidIdx = static_cast<std::size_t>(-1);

    // 面板视图模式：dopesheet（key 时间编辑）vs curve（key 值/缓动编辑，B2.4）。
    // transport 行的切换按钮在两者间切，curve 模式复用同一选中实体 / track / 命令栈。
    enum class TimelineMode
    {
        Dopesheet,
        Curve,
    };

    TimelineMode sTimelineMode = TimelineMode::Dopesheet;

    // curve 模式专属拖动态：正在拖某 key 的哪个 Bezier 切线手柄。两套句柄
    // （out = 控制本 key 出发段的缓动起手柄；in = 控制落到本 key 的段的收手柄）。
    enum class CurveHandle
    {
        None,
        Out, // k.outTangent（从本 key (0,0) 出发的控制柄 c1）
        In,  // k.inTangent（落到本 key (1,1) 的控制柄 c2 偏移）
    };

    struct CurveDragState
    {
        bool        dragging = false;
        std::size_t track    = kInvalidIdx;
        std::size_t key      = kInvalidIdx;
        CurveHandle handle   = CurveHandle::None;
    };

    CurveDragState sCurveDrag;

    // 提交一次 clip 编辑：拷 oldClip→在 newClip 上已被调用方改好→RecomputeDuration
    // → 压 SetAnimationClipCommand。mergeKey 决定是否与后续命令合并（拖动用稳定
    // key，离散编辑用唯一 key），label 是 Undo 菜单展示名。
    void PushClipEdit(EditorHost& host, ClipAnimator& clip, AnimationClip newClip,
                      std::string mergeKey, std::string label)
    {
        AnimationClip oldClip = clip.Clip(); // 当前快照（do/undo 对称基线）
        Anim::RecomputeClipDuration(newClip);
        host.cmdStack.Push(std::make_unique<SetAnimationClipCommand>(
            host, host.selection.selectedEntity, std::move(oldClip),
            std::move(newClip), std::move(mergeKey), std::move(label)));
    }

    // 空态提示：无选中 / 选中实体没有 clip backend 的友好提示。
    void DrawEmptyState(EditorHost& host)
    {
        if (!host.selection.selectedEntity.IsValid())
        {
            ImGui::TextDisabled("未选中实体。");
            ImGui::TextDisabled("在 Hierarchy / Scene 选中带 Animator(Clip) 的实体后，"
                                "此处显示其动画时间轴。");
            return;
        }
        ImGui::TextDisabled("选中实体没有 clip 动画后端。");
        ImGui::TextDisabled("给它 Add Component → Animator(Clip)，并在 Inspector "
                            "引用一个 .anim 资产或直接在此打键创作。");
    }

    // 顶部 transport 行：▶/⏸ 预览（复用 host.animPreview）+ loop + speed + 当前
    // 时间 / 总长 readout。返回当前 duration（供下方布局用）。
    float DrawTransportRow(EditorHost& host, ClipAnimator& clip)
    {
        const Orange::Engine::Entity e          = host.selection.selectedEntity;
        const bool                   canPreview = (host.scene.playState == PlayState::Edit);

        const bool isPreviewTarget =
            host.animPreview.previewEntity.IsValid() && host.animPreview.previewEntity == e;
        const bool isPlaying = isPreviewTarget && host.animPreview.previewPlaying;

        ImGui::BeginDisabled(!canPreview);

        // ▶：把预览指向本 entity + 启动（复用 B2.6——EditorRenderLayer Edit 模式
        // OnUpdate 据 previewPlaying 每帧 Tick 单 animator，不在此另起 tick 路径）。
        if (ImGui::Button(isPlaying ? Theme::Icon::GetPause() : Theme::Icon::GetPlay()))
        {
            if (isPlaying)
            {
                clip.Pause();
                if (isPreviewTarget)
                {
                    host.animPreview.previewPlaying = false;
                }
            }
            else
            {
                host.animPreview.previewEntity  = e;
                host.animPreview.previewPlaying = true;
                clip.Play();
            }
        }
        ImGui::SameLine();
        // ⏹ Stop：归位 t0 + 停预览（与 ▶ 区分；Pause 留 pose、Stop 回起点）。
        if (ImGui::Button(Theme::Icon::GetStop()))
        {
            clip.Stop(); // mPlaying=false + elapsed=0 + 立即应用 t0 pose
            if (isPreviewTarget)
            {
                host.animPreview.previewPlaying = false;
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        // loop：直接桥接 ClipAnimator（与 Inspector loop checkbox 同语义）。
        bool loop = clip.IsLooping();
        if (ImGui::Checkbox("Loop", &loop))
        {
            clip.SetLoop(loop);
        }

        ImGui::SameLine();
        // speed：预览速率（不改 clip 数据，不进命令栈——纯 runtime 倍率）。
        float speed = clip.Speed();
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("0.000x").x * 2.0f);
        if (ImGui::DragFloat("##anim_speed", &speed, 0.01f, -4.0f, 4.0f, "%.2fx"))
        {
            clip.SetSpeed(speed);
        }

        const float duration = Anim::ComputeClipDuration(clip.Clip());
        ImGui::SameLine();
        ImGui::TextDisabled("t = %.3fs / %.3fs", clip.ElapsedSeconds(), duration);

        // 视图模式切换：Dopesheet（key 时间）↔ Curve（key 值 / 缓动，B2.4）。同一选中
        // 实体 / track / 命令栈，仅换可视化与编辑维度。
        ImGui::SameLine();
        if (ImGui::Button(sTimelineMode == TimelineMode::Dopesheet ? "Curve >" : "< Dopesheet"))
        {
            sTimelineMode = (sTimelineMode == TimelineMode::Dopesheet) ? TimelineMode::Curve
                                                                       : TimelineMode::Dopesheet;
        }

        if (!canPreview)
        {
            ImGui::TextDisabled("(预览 / scrub 仅 Edit 模式可用；Play 模式由全量 tick 驱动)");
        }
        return duration;
    }

    // 当前 playhead 时刻对某 track 打键：采当前 SampleTrack 值（保持视觉连续——
    // 新键值 = 当前曲线在该时刻的值）→ UpsertKeyframe → 压命令栈（离散编辑、唯一 key）。
    void KeyTrackAtPlayhead(EditorHost& host, ClipAnimator& clip,
                            const AnimationTrack& track)
    {
        const float     t       = clip.ElapsedSeconds();
        AnimationClip   newClip = clip.Clip();
        AnimationTrack* tr      = Anim::FindTrack(newClip, track.targetName);
        if (tr == nullptr)
        {
            return;
        }

        Keyframe key;
        key.time   = t;
        key.value  = Anim::SampleTrack(*tr, t); // 当前时刻曲线值，打键不跳变
        key.interp = Anim::InterpMode::Linear;
        Anim::UpsertKeyframe(newClip, track.targetName, tr->valueType, key);

        char mergeKey[128];
        std::snprintf(mergeKey, sizeof(mergeKey), "anim_key_insert:%s",
                      track.targetName.c_str());
        PushClipEdit(host, clip, std::move(newClip), mergeKey, "Insert Keyframe");
    }

    // 删除选中 key（离散编辑、唯一 key）。
    void DeleteSelectedKey(EditorHost& host, ClipAnimator& clip)
    {
        if (sTimelineSel.selTrack == kInvalidIdx || sTimelineSel.selKey == kInvalidIdx)
        {
            return;
        }
        AnimationClip newClip = clip.Clip();
        if (sTimelineSel.selTrack >= newClip.tracks.size())
        {
            return;
        }
        AnimationTrack& tr = newClip.tracks[sTimelineSel.selTrack];
        if (!Anim::RemoveKeyframe(tr, sTimelineSel.selKey))
        {
            return;
        }
        PushClipEdit(host, clip, std::move(newClip), "anim_key_delete", "Delete Keyframe");
        sTimelineSel.ClearKeySel();
    }

    // ---- 曲线编辑器（B2.4）几何 + 切线手柄数学 ------------------------------
    //
    // 取 track 当前用于绘制 / 编辑的标量分量索引（0=x / 1=y / 2=z / 3=w）。多分量
    // track（Vec2/3/4）目前画首个驱动分量的曲线作主曲线 + 编辑它的缓动（spec 现状：
    // track 是单值序列就画单曲线；多分量共享同一标量时序缓动，故编辑任一分量的切线即
    // 改整段时序）。Float track 恒取 .x。切线（inTangent/outTangent）是整段共享的 2D
    // 控制柄，与具体 value 分量无关——值方向 .y 抬升按"被绘制分量"的值跨度可视化。
    int TrackPrimaryComponent(const AnimationTrack& tr)
    {
        // 选值跨度最大的标量分量作主曲线显示。rotation.euler 这类多分量 track 真实动画
        // 常落在 .y（如绕 Y 自旋 0→360），恒返回 0 会画出 .x 的平直线（无意义、误导）。
        // Float track（position.y 等）只有 .x 承载值（.y/.z/.w 恒 0），自然选回 0。切线是
        // 整段共享的 2D 控制柄（编辑任一分量即改共享时序缓动），故按"最有信息量的分量"
        // 显示不破坏编辑语义。
        if (tr.keys.size() < 2)
        {
            return 0;
        }
        int   best      = 0;
        float bestRange = -1.0f;
        for (int c = 0; c < 4; ++c)
        {
            float lo = tr.keys.front().value[c];
            float hi = lo;
            for (const Keyframe& k : tr.keys)
            {
                lo = std::min(lo, k.value[c]);
                hi = std::max(hi, k.value[c]);
            }
            const float range = hi - lo;
            if (range > bestRange)
            {
                bestRange = range;
                best      = c;
            }
        }
        return best;
    }

    // 取 track 的值范围（被绘制分量在所有 key 上的 min/max），用于纵轴映射。空 / 单值
    // 退化时给一个对称小区间避免除零。pad 留出上下边距让曲线不贴边。
    void TrackValueRange(const AnimationTrack& tr, int comp, float& outMin, float& outMax)
    {
        if (tr.keys.empty())
        {
            outMin = -1.0f;
            outMax = 1.0f;
            return;
        }
        float lo = tr.keys.front().value[comp];
        float hi = lo;
        for (const Keyframe& k : tr.keys)
        {
            lo = std::min(lo, k.value[comp]);
            hi = std::max(hi, k.value[comp]);
        }
        // 还要把 Bezier 值方向 overshoot（切线 .y 超出 [0,1]）的控制柄纳入范围，否则
        // overshoot 手柄会画到视图外拖不到。逐相邻段把控制点的值估进 min/max。
        for (std::size_t i = 0; i + 1 < tr.keys.size(); ++i)
        {
            const Keyframe& k0 = tr.keys[i];
            const Keyframe& k1 = tr.keys[i + 1];
            if (k0.interp != Anim::InterpMode::Bezier)
            {
                continue;
            }
            const float span = k1.value[comp] - k0.value[comp];
            const float c1   = k0.value[comp] + k0.outTangent.y * span; // out 手柄值
            const float c2   = k1.value[comp] + k1.inTangent.y * span;  // in 手柄值
            lo               = std::min({lo, c1, c2});
            hi               = std::max({hi, c1, c2});
        }
        if (hi - lo < 1e-4f)
        {
            lo -= 1.0f;
            hi += 1.0f;
        } // 平直曲线给个对称区间
        const float pad = (hi - lo) * 0.12f;
        outMin          = lo - pad;
        outMax          = hi + pad;
    }

    // 值 → 屏幕 y：值大的在上方（y 小），按 [valMin,valMax] 线性映射到 [areaY1, areaY0]。
    float ValueToScreenY(float v, float areaY0, float areaH, float valMin, float valMax)
    {
        if (valMax - valMin < 1e-6f)
        {
            return areaY0 + areaH * 0.5f;
        }
        const float u = (v - valMin) / (valMax - valMin);
        return areaY0 + (1.0f - std::clamp(u, -0.5f, 1.5f)) * areaH; // 留一点越界余量画 overshoot
    }

    // 反映射：屏幕 y → 值。
    float ScreenYToValue(float y, float areaY0, float areaH, float valMin, float valMax)
    {
        if (areaH <= 0.0f)
        {
            return valMin;
        }
        const float u = 1.0f - (y - areaY0) / areaH;
        return valMin + u * (valMax - valMin);
    }

    // 把某 key 的 Bezier 切线手柄换算到屏幕坐标。约定（与 CubicBezierEase 一致）：
    // 段 k0→k1 的单位方框 (0,0)=(k0.time,k0.value)、(1,1)=(k1.time,k1.value)；
    //   out 手柄（k0 出发，控制点 c1 = k0.outTangent）：
    //     time  = k0.time  + outTangent.x · (k1.time  - k0.time)
    //     value = k0.value + outTangent.y · (k1.value - k0.value)
    //   in 手柄（落到 k1，控制点 c2 = (1,1)+k1.inTangent）：
    //     time  = k1.time  + inTangent.x · (k1.time  - k0.time)
    //     value = k1.value + inTangent.y · (k1.value - k0.value)
    // dt/dv 是该段的 time / value 跨度。返回手柄的 (time, value)。
    struct HandleTV
    {
        float time;
        float value;
    };

    HandleTV OutHandleTV(const Keyframe& k0, int comp, float dt, float dv)
    {
        return {k0.time + k0.outTangent.x * dt, k0.value[comp] + k0.outTangent.y * dv};
    }
    HandleTV InHandleTV(const Keyframe& k1, int comp, float dt, float dv)
    {
        return {k1.time + k1.inTangent.x * dt, k1.value[comp] + k1.inTangent.y * dv};
    }

    // 逆运算：把手柄落点 (time,value) 反推回切线 (x,y)。dt/dv 是段跨度。
    // 时间方向 .x 由 CubicBezierEase 内部夹到 [0,1] 保 X 单调，这里也夹（out 取
    // [0,1]、in 取 [-1,0]，与"in 指回前一帧"约定一致）；值方向 .y 不夹（允许 overshoot）。
    // dt<=0（段退化）时不改 .x（除零保护）；dv≈0（值平直段）时不改 .y。
    glm::vec2 SolveOutTangent(const Keyframe& k0, int comp, float handleTime, float handleValue,
                              float dt, float dv)
    {
        glm::vec2 t = k0.outTangent;
        if (dt > 1e-6f)
        {
            t.x = std::clamp((handleTime - k0.time) / dt, 0.0f, 1.0f);
        }
        if (std::fabs(dv) > 1e-6f)
        {
            t.y = (handleValue - k0.value[comp]) / dv;
        }
        return t;
    }
    glm::vec2 SolveInTangent(const Keyframe& k1, int comp, float handleTime, float handleValue,
                             float dt, float dv)
    {
        glm::vec2 t = k1.inTangent;
        if (dt > 1e-6f)
        {
            t.x = std::clamp((handleTime - k1.time) / dt, -1.0f, 0.0f);
        }
        if (std::fabs(dv) > 1e-6f)
        {
            t.y = (handleValue - k1.value[comp]) / dv;
        }
        return t;
    }

    // 给 key 切到 Bezier 时一组合理的默认平滑切线（CSS ease-in-out 同款时序、值方向不
    // 抬升）。这样右键切 Bezier 后曲线立刻有可拖的手柄而非退化成线性。
    void AssignSmoothBezierDefault(Keyframe& k)
    {
        k.outTangent = glm::vec2(0.42f, 0.0f);
        k.inTangent  = glm::vec2(-0.42f, 0.0f);
    }

} // namespace

// ---------------------------------------------------------------------------
// EditorRenderLayer::DrawAnimationPanel —— 面板入口
// ---------------------------------------------------------------------------
void EditorRenderLayer::DrawAnimationPanel()
{
    ImGui::Begin("Animation");

    ClipAnimator* pClip = ResolveSelectedClipAnimator(mHost);
    if (pClip == nullptr)
    {
        DrawEmptyState(mHost);
        ImGui::End();
        return;
    }
    ClipAnimator& clip = *pClip;
    sTimelineSel.ResetIfOwnerChanged(mHost.selection.selectedEntity);

    const bool canEdit = (mHost.scene.playState == PlayState::Edit);

    // ---- transport 行（▶/⏸/⏹ + loop + speed + readout）--------------------
    const float duration = DrawTransportRow(mHost, clip);

    // ---- .anim 写回行（仅资产化 clip）------------------------------------
    // SourceAssetPath 非空 = clip 来自 .anim 资产；in-memory 编辑后须显式写回
    // 文件，否则 scene 只存 clipSource 引用、改动丢。内联 clip（无 source）随
    // scene 存 clipJson，不需写回。
    const std::string sourcePath = std::string(clip.SourceAssetPath());
    if (!sourcePath.empty())
    {
        ImGui::SameLine();
        if (ImGui::Button("Save to .anim"))
        {
            auto r = Anim::SaveAnimationClip(clip.Clip(), sourcePath);
            if (r.IsErr())
            {
                ORANGE_LOG_ERROR("Animation timeline: 写回 .anim 失败：{}", sourcePath);
            }
            else
            {
                ORANGE_LOG_INFO("Animation timeline: 已写回 {}", sourcePath);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(资产化 clip：编辑后须写回 .anim)");
    }

    ImGui::Separator();

    // ---- Curve 模式（B2.4）：另走曲线编辑器视图，dopesheet 主体不绘制 --------
    if (sTimelineMode == TimelineMode::Curve)
    {
        DrawCurveEditor(clip, duration);
        ImGui::End();
        return;
    }

    // ---- 键盘快捷键：K 打选中轨道键 / Del 删选中 key（仅面板聚焦时）-------
    const bool           panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const AnimationClip& curClip      = clip.Clip();

    // ---- timeline 主体绘制 ------------------------------------------------
    // 用 ImDrawList 自绘标尺 + 轨道行 + 关键帧 + playhead，覆盖一个 InvisibleButton
    // 区域捕获点击 / 拖拽（与 ScenePanel / ColliderVertexEdit 自绘 + hit-test 同款）。
    const float  labelW     = LabelColumnWidth();
    const ImVec2 avail      = ImGui::GetContentRegionAvail();
    const float  trackAreaW = std::max(avail.x - labelW, ImGui::GetFontSize() * 4.0f);

    ImDrawList* dl          = ImGui::GetWindowDrawList();
    const ImU32 colRuler    = ImGui::GetColorU32(Theme::Color::GetSeparator());
    const ImU32 colRowBg    = ImGui::GetColorU32(Theme::Color::GetBackgroundSecondary());
    const ImU32 colRowAltBg = ImGui::GetColorU32(Theme::Color::GetControlBg());
    const ImU32 colKey      = ImGui::GetColorU32(Theme::Color::GetTextSecondary());
    const ImU32 colKeySel   = ImGui::GetColorU32(Theme::Color::GetAccentPrimary());
    const ImU32 colPlayhead = ImGui::GetColorU32(Theme::Color::GetAccentPrimary());
    const ImU32 colEvent    = ImGui::GetColorU32(Theme::Color::GetAlertWarn());
    const ImU32 colEventSel = ImGui::GetColorU32(Theme::Color::GetAccentPrimary());

    const float rowH = RowHeight();
    const float keyR = KeyRadius();
    const float hitR = HitRadius();

    const ImVec2 origin  = ImGui::GetCursorScreenPos();
    const float  trackX0 = origin.x + labelW;

    // 事件 marker 行 + 标尺行高度。
    const float       rulerH     = rowH; // 顶部时间标尺 + 事件 marker 共用一行
    const std::size_t trackCount = curClip.tracks.size();
    const float       bodyH      = rulerH + rowH * static_cast<float>(std::max<std::size_t>(trackCount, 1));

    // 覆盖整块 timeline 的 InvisibleButton——吃掉 ImGui 默认 item 行为，自己解析
    // 点击落在哪条轨 / 哪个 key / 标尺。必须先于自绘调用，拿到 hovered/active。
    ImGui::InvisibleButton("##anim_timeline_canvas", ImVec2(avail.x, bodyH));
    const bool   canvasHovered = ImGui::IsItemHovered();
    const ImVec2 mouse         = ImGui::GetIO().MousePos;

    // 背景：标尺行 + 各轨道行交替底色。
    dl->AddRectFilled(ImVec2(origin.x, origin.y),
                      ImVec2(origin.x + avail.x, origin.y + rulerH), colRowAltBg);
    for (std::size_t i = 0; i < std::max<std::size_t>(trackCount, 1); ++i)
    {
        const float y0 = origin.y + rulerH + rowH * static_cast<float>(i);
        const ImU32 bg = (i % 2 == 0) ? colRowBg : colRowAltBg;
        dl->AddRectFilled(ImVec2(origin.x, y0),
                          ImVec2(origin.x + avail.x, y0 + rowH), bg);
    }
    // 标签列与时间轴列分隔线。
    dl->AddLine(ImVec2(trackX0, origin.y), ImVec2(trackX0, origin.y + bodyH), colRuler);

    // 时间标尺刻度（0 / 中 / 末三档，省去过密刻度计算；标尺数字用 ImGui text）。
    auto drawTick = [&](float t)
    {
        const float x = TimeToScreenX(t, trackX0, trackAreaW, duration);
        dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + rulerH * 0.5f), colRuler);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", t);
        dl->AddText(ImVec2(x + 2.0f, origin.y + 1.0f),
                    ImGui::GetColorU32(Theme::Color::GetTextDisabled()), buf);
    };
    if (duration > 0.0f)
    {
        drawTick(0.0f);
        drawTick(duration * 0.5f);
        drawTick(duration);
    }

    // ---- 事件 marker（标尺行内的小三角，clip.events 全部展示）-------------
    // 命中半径内点击选中；选中后可拖动改 time / Del 删（在下方键盘段处理）。
    for (std::size_t ei = 0; ei < curClip.events.size(); ++ei)
    {
        const AnimationEvent& ev       = curClip.events[ei];
        const float           x        = TimeToScreenX(ev.time, trackX0, trackAreaW, duration);
        const float           cy       = origin.y + rulerH * 0.5f;
        const bool            selected = (sTimelineSel.selEvent == ei);
        const ImU32           c        = selected ? colEventSel : colEvent;
        dl->AddTriangleFilled(ImVec2(x - keyR, cy - keyR),
                              ImVec2(x + keyR, cy - keyR),
                              ImVec2(x, cy + keyR), c);
        if (!ev.name.empty())
        {
            dl->AddText(ImVec2(x + keyR + 1.0f, cy - keyR), c, ev.name.c_str());
        }
    }

    // ---- 各轨道：标签 + 关键帧菱形 ---------------------------------------
    for (std::size_t ti = 0; ti < trackCount; ++ti)
    {
        const AnimationTrack& tr    = curClip.tracks[ti];
        const float           rowY  = origin.y + rulerH + rowH * static_cast<float>(ti);
        const float           keyCy = rowY + rowH * 0.5f;

        // 轨道标签（左列）——用 ImDrawList 直绘，不占 ImGui item（整块已是
        // InvisibleButton）。
        dl->AddText(ImVec2(origin.x + 2.0f, rowY + 2.0f),
                    ImGui::GetColorU32(Theme::Color::GetTextPrimary()),
                    tr.targetName.c_str());

        // 关键帧菱形。
        for (std::size_t ki = 0; ki < tr.keys.size(); ++ki)
        {
            const float kx       = TimeToScreenX(tr.keys[ki].time, trackX0, trackAreaW, duration);
            const bool  selected = (sTimelineSel.selTrack == ti && sTimelineSel.selKey == ki);
            const ImU32 c        = selected ? colKeySel : colKey;
            // 菱形（45° 方块）：四个顶点。
            dl->AddQuadFilled(ImVec2(kx, keyCy - keyR), ImVec2(kx + keyR, keyCy),
                              ImVec2(kx, keyCy + keyR), ImVec2(kx - keyR, keyCy), c);
        }
    }

    // ---- playhead 游标（竖线，覆盖整块）----------------------------------
    if (duration > 0.0f)
    {
        const float px = TimeToScreenX(clip.ElapsedSeconds(), trackX0, trackAreaW, duration);
        dl->AddLine(ImVec2(px, origin.y), ImVec2(px, origin.y + bodyH), colPlayhead, 2.0f);
        dl->AddTriangleFilled(ImVec2(px - keyR, origin.y),
                              ImVec2(px + keyR, origin.y),
                              ImVec2(px, origin.y + keyR), colPlayhead);
    }

    // ---- 交互处理：点击 / 拖拽（仅 Edit 模式）----------------------------
    if (canEdit)
    {
        // 鼠标按下瞬间：判定点中了 key / event / 还是空白（scrub）。
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            bool hitSomething = false;

            // 命中 key？（遍历各轨各 key，取屏幕距离最近且在命中半径内的）
            for (std::size_t ti = 0; ti < trackCount && !hitSomething; ++ti)
            {
                const AnimationTrack& tr    = curClip.tracks[ti];
                const float           rowY  = origin.y + rulerH + rowH * static_cast<float>(ti);
                const float           keyCy = rowY + rowH * 0.5f;
                if (std::fabs(mouse.y - keyCy) > rowH * 0.5f)
                {
                    continue;
                }
                for (std::size_t ki = 0; ki < tr.keys.size(); ++ki)
                {
                    const float kx =
                        TimeToScreenX(tr.keys[ki].time, trackX0, trackAreaW, duration);
                    if (std::fabs(mouse.x - kx) <= hitR)
                    {
                        sTimelineSel.selTrack = ti;
                        sTimelineSel.selKey   = ki;
                        sTimelineSel.ClearEventSel();
                        sTimelineSel.draggingKey = true;
                        sTimelineSel.dragTrack   = ti;
                        sTimelineSel.dragKey     = ki;
                        ++sTimelineSel.dragSession; // 新拖动会话（merge key 用）
                        hitSomething = true;
                        break;
                    }
                }
            }

            // 命中事件 marker？
            if (!hitSomething)
            {
                const float cy = origin.y + rulerH * 0.5f;
                if (std::fabs(mouse.y - cy) <= rowH * 0.5f)
                {
                    for (std::size_t ei = 0; ei < curClip.events.size(); ++ei)
                    {
                        const float x = TimeToScreenX(curClip.events[ei].time, trackX0,
                                                      trackAreaW, duration);
                        if (std::fabs(mouse.x - x) <= hitR)
                        {
                            sTimelineSel.selEvent = ei;
                            sTimelineSel.ClearKeySel();
                            sTimelineSel.draggingEvent = true;
                            sTimelineSel.dragEventIdx  = ei;
                            ++sTimelineSel.dragSession; // 新拖动会话（merge key 用）
                            hitSomething = true;
                            break;
                        }
                    }
                }
            }

            // 空白：scrub —— 直接 Seek 到点击时刻（实体实时跟随）。
            if (!hitSomething && mouse.x >= trackX0)
            {
                sTimelineSel.ClearKeySel();
                sTimelineSel.ClearEventSel();
                clip.Seek(ScreenXToTime(mouse.x, trackX0, trackAreaW, duration));
            }
        }

        // 拖动 key：水平拖 → MoveKeyframeTime；连续帧合并成一条命令（merge key
        // 含 track/key index，整段拖动撤销一步回到拖前）。注意 MoveKeyframeTime
        // 可能因排序变更 key 索引——拖动期用 dragTrack/dragKey 跟踪最新位置。
        if (sTimelineSel.draggingKey && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            if (sTimelineSel.dragTrack < curClip.tracks.size())
            {
                const float   newT    = ScreenXToTime(mouse.x, trackX0, trackAreaW, duration);
                AnimationClip newClip = clip.Clip();
                if (sTimelineSel.dragTrack < newClip.tracks.size() && sTimelineSel.dragKey < newClip.tracks[sTimelineSel.dragTrack].keys.size())
                {
                    // 拖动后该 key 可能换索引（排序维持升序）—— MoveKeyframeTime
                    // 在副本上做，push 后从 clip 回查新索引（newClip 已被 move 走，
                    // 不能再读它，故索引回查走 clip.Clip() 新真相）。
                    Anim::MoveKeyframeTime(newClip.tracks[sTimelineSel.dragTrack],
                                           sTimelineSel.dragKey, newT);
                    char mergeKey[128];
                    // merge key 用稳定的拖动会话 id（非 dragTrack）——同一轨先后拖
                    // 两个不同 key 是两次会话、两条 Undo，不被错误合并成一条。
                    std::snprintf(mergeKey, sizeof(mergeKey), "anim_drag_key:%llu",
                                  static_cast<unsigned long long>(sTimelineSel.dragSession));
                    PushClipEdit(mHost, clip, std::move(newClip), mergeKey, "Move Keyframe");
                    const AnimationClip& after = clip.Clip();
                    if (sTimelineSel.dragTrack < after.tracks.size())
                    {
                        const std::size_t newIdx = Anim::FindKeyframeIndexNear(
                            after.tracks[sTimelineSel.dragTrack], newT, 1e-3f);
                        if (newIdx != after.tracks[sTimelineSel.dragTrack].keys.size())
                        {
                            sTimelineSel.dragKey  = newIdx;
                            sTimelineSel.selKey   = newIdx;
                            sTimelineSel.selTrack = sTimelineSel.dragTrack;
                        }
                    }
                }
            }
        }

        // 拖动事件 marker：水平拖 → 改 time（events 用 AddClipEvent 维持升序，
        // 拖动后回查新索引）。
        if (sTimelineSel.draggingEvent && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            const float   newT    = ScreenXToTime(mouse.x, trackX0, trackAreaW, duration);
            AnimationClip newClip = clip.Clip();
            if (sTimelineSel.dragEventIdx < newClip.events.size())
            {
                AnimationEvent ev = newClip.events[sTimelineSel.dragEventIdx];
                ev.time           = newT;
                Anim::RemoveClipEvent(newClip, sTimelineSel.dragEventIdx);
                Anim::AddClipEvent(newClip, ev);
                char evMergeKey[128];
                std::snprintf(evMergeKey, sizeof(evMergeKey), "anim_drag_event:%llu",
                              static_cast<unsigned long long>(sTimelineSel.dragSession));
                PushClipEdit(mHost, clip, std::move(newClip), evMergeKey,
                             "Move Event");
                // 回查新索引（events 升序，取 time 最近的）。
                std::size_t          bestIdx  = kInvalidIdx;
                float                bestDist = 1e-3f;
                const AnimationClip& after    = clip.Clip();
                for (std::size_t i = 0; i < after.events.size(); ++i)
                {
                    const float d = std::fabs(after.events[i].time - newT);
                    if (d <= bestDist)
                    {
                        bestDist = d;
                        bestIdx  = i;
                    }
                }
                if (bestIdx != kInvalidIdx)
                {
                    sTimelineSel.dragEventIdx = bestIdx;
                    sTimelineSel.selEvent     = bestIdx;
                }
            }
        }

        // 松开鼠标：结束拖动（命令栈靠 merge key 已把整段合成一条）。
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            sTimelineSel.draggingKey   = false;
            sTimelineSel.draggingEvent = false;
        }
    }

    // ---- 键盘：K 打键（选中轨道）/ Del 删选中 key / 选中 event ------------
    // 重新取 clip 真相：上方拖拽分支可能已 SetClip（curClip 引用随之失效），
    // 键盘分支用新鲜引用，避免同帧拖+按键的悬空读。
    if (canEdit && panelFocused)
    {
        const AnimationClip& kbClip = clip.Clip();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            if (sTimelineSel.selEvent != kInvalidIdx && sTimelineSel.selEvent < kbClip.events.size())
            {
                AnimationClip newClip = kbClip;
                if (Anim::RemoveClipEvent(newClip, sTimelineSel.selEvent))
                {
                    PushClipEdit(mHost, clip, std::move(newClip), "anim_event_delete",
                                 "Delete Event");
                    sTimelineSel.ClearEventSel();
                }
            }
            else
            {
                DeleteSelectedKey(mHost, clip);
            }
        }
        // K：在选中轨道（selTrack）playhead 处打键；无选中轨道则对第一条轨道打。
        if (ImGui::IsKeyPressed(ImGuiKey_K, false) && !kbClip.tracks.empty())
        {
            const std::size_t ti =
                (sTimelineSel.selTrack != kInvalidIdx && sTimelineSel.selTrack < kbClip.tracks.size())
                    ? sTimelineSel.selTrack
                    : 0;
            KeyTrackAtPlayhead(mHost, clip, kbClip.tracks[ti]);
        }
    }

    // ---- 底部工具行：加轨道 / 删选中轨道 / 加事件 / 打键按钮 -------------
    ImGui::Dummy(ImVec2(0.0f, bodyH)); // 占位，让下面控件落在 timeline 下方
    ImGui::Separator();

    ImGui::BeginDisabled(!canEdit);
    DrawTimelineToolbar(clip);
    ImGui::EndDisabled();

    ImGui::End();
}

// ---------------------------------------------------------------------------
// 底部工具行 —— 加轨道 / 删选中轨道 / 加事件 / 打键。拆成独立成员函数避免
// DrawAnimationPanel 巨函数化（架构纪律）。
// ---------------------------------------------------------------------------
void EditorRenderLayer::DrawTimelineToolbar(Orange::Engine::Animation::ClipAnimator& clip)
{
    const AnimationClip& curClip = clip.Clip();

    // 加轨道：下拉选 targetName（Transform 字段约定名）→ UpsertTrack。
    static const char* kTrackNames[] = {
        "position",
        "position.x",
        "position.y",
        "position.z",
        "rotation",
        "scale",
        "scale.x",
        "scale.y",
        "scale.z",
        "scale.uniform",
    };
    static const Anim::TrackValueType kTrackTypes[] = {
        Anim::TrackValueType::Vec3,
        Anim::TrackValueType::Float,
        Anim::TrackValueType::Float,
        Anim::TrackValueType::Float,
        Anim::TrackValueType::Vec3,
        Anim::TrackValueType::Vec3,
        Anim::TrackValueType::Float,
        Anim::TrackValueType::Float,
        Anim::TrackValueType::Float,
        Anim::TrackValueType::Float,
    };
    static int sAddTrackIdx = 0;

    ImGui::SetNextItemWidth(LabelColumnWidth());
    ImGui::Combo("##anim_add_track_kind", &sAddTrackIdx, kTrackNames,
                 IM_ARRAYSIZE(kTrackNames));
    ImGui::SameLine();
    if (ImGui::Button("Add Track"))
    {
        AnimationClip newClip = curClip;
        Anim::UpsertTrack(newClip, kTrackNames[sAddTrackIdx], kTrackTypes[sAddTrackIdx]);
        PushClipEdit(mHost, clip, std::move(newClip), "anim_add_track", "Add Track");
    }

    ImGui::SameLine();
    // 删选中轨道。
    const bool haveTrackSel =
        (sTimelineSel.selTrack != kInvalidIdx && sTimelineSel.selTrack < curClip.tracks.size());
    ImGui::BeginDisabled(!haveTrackSel);
    if (ImGui::Button("Remove Track"))
    {
        AnimationClip newClip = curClip;
        if (sTimelineSel.selTrack < newClip.tracks.size())
        {
            Anim::RemoveTrack(newClip, newClip.tracks[sTimelineSel.selTrack].targetName);
            PushClipEdit(mHost, clip, std::move(newClip), "anim_remove_track",
                         "Remove Track");
            sTimelineSel.ClearKeySel();
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    // 打键（按钮版，等价 K）：对选中轨道在 playhead 打键。
    const bool haveTrack = !curClip.tracks.empty();
    ImGui::BeginDisabled(!haveTrack);
    if (ImGui::Button("Key (K)"))
    {
        const std::size_t ti =
            (sTimelineSel.selTrack != kInvalidIdx && sTimelineSel.selTrack < curClip.tracks.size())
                ? sTimelineSel.selTrack
                : 0;
        KeyTrackAtPlayhead(mHost, clip, curClip.tracks[ti]);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    // 加事件：在 playhead 时刻加一个空名事件。
    if (ImGui::Button("Add Event"))
    {
        AnimationClip  newClip = curClip;
        AnimationEvent ev;
        ev.time = clip.ElapsedSeconds();
        ev.name = "event";
        Anim::AddClipEvent(newClip, ev);
        PushClipEdit(mHost, clip, std::move(newClip), "anim_add_event", "Add Event");
    }

    // 选中事件重命名 InputText（在工具行下方一行，便于改 event.name）。
    if (sTimelineSel.selEvent != kInvalidIdx && sTimelineSel.selEvent < curClip.events.size())
    {
        char               nameBuf[128];
        const std::string& curName = curClip.events[sTimelineSel.selEvent].name;
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", curName.c_str());
        ImGui::SetNextItemWidth(LabelColumnWidth() * 1.5f);
        if (ImGui::InputText("Event Name", nameBuf, sizeof(nameBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue))
        {
            AnimationClip newClip = curClip;
            if (sTimelineSel.selEvent < newClip.events.size())
            {
                newClip.events[sTimelineSel.selEvent].name = nameBuf;
                PushClipEdit(mHost, clip, std::move(newClip), "anim_event_rename",
                             "Rename Event");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// EditorRenderLayer::DrawCurveEditor —— 曲线编辑器视图（B2.4）
//
// 对"选中 track"（selTrack；无则取第 0 条）画一条曲线：横轴 time、纵轴 value。
// **关键正确性约束**：曲线用 SampleTrack 在时间范围内密集采样画折线——display 与
// playback 完全一致（不自己重算插值）。每个 key 画点；Bezier 段的 in/out 切线作
// 单位方框 2D 控制柄（约定见本 TU 顶 OutHandleTV/InHandleTV 注释）画可拖手柄。拖
// 手柄反推 inTangent/outTangent（SolveOut/InTangent），走 SetAnimationClipCommand
// （连续拖同手柄 merge 一条），改完曲线实时重画（SampleTrack 读新切线）。右键 key
// 弹菜单切 InterpMode（Step / Linear / Bezier）。
// ---------------------------------------------------------------------------
void EditorRenderLayer::DrawCurveEditor(Orange::Engine::Animation::ClipAnimator& clip,
                                        float                                    duration)
{
    const AnimationClip& curClip = clip.Clip();
    const bool           canEdit = (mHost.scene.playState == PlayState::Edit);

    if (curClip.tracks.empty())
    {
        ImGui::TextDisabled("当前 clip 无轨道。切回 Dopesheet 用 Add Track 加一条，"
                            "或在 dopesheet 打键创作后回曲线视图编辑缓动。");
        return;
    }

    // ---- 选哪条 track 画：track 选择下拉（沿用选中 track，可在此切）-----------
    std::size_t curTrack =
        (sTimelineSel.selTrack != kInvalidIdx && sTimelineSel.selTrack < curClip.tracks.size())
            ? sTimelineSel.selTrack
            : 0;
    {
        const char* preview = curClip.tracks[curTrack].targetName.c_str();
        ImGui::SetNextItemWidth(LabelColumnWidth());
        if (ImGui::BeginCombo("##curve_track", preview))
        {
            for (std::size_t ti = 0; ti < curClip.tracks.size(); ++ti)
            {
                const bool sel = (ti == curTrack);
                if (ImGui::Selectable(curClip.tracks[ti].targetName.c_str(), sel))
                {
                    sTimelineSel.ClearKeySel(); // 切 track 清旧 key 选中（含 selTrack）
                    sTimelineSel.selTrack = ti; // 重设为新 track（曲线视图按它画）
                    curTrack              = ti;
                }
                if (sel)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("右键 key 切插值模式 · 拖手柄改 Bezier 缓动");
    }

    const AnimationTrack& track = curClip.tracks[curTrack];
    const int             comp  = TrackPrimaryComponent(track);

    // ---- 画布几何（全派生，无像素字面量）------------------------------------
    ImDrawList*  dl     = ImGui::GetWindowDrawList();
    const ImVec2 avail  = ImGui::GetContentRegionAvail();
    const float  bodyH  = std::max(avail.y - RowHeight() * 1.5f, ImGui::GetFontSize() * 8.0f);
    const float  leftW  = LabelColumnWidth() * 0.6f; // 纵轴值标签列
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float  areaX0 = origin.x + leftW;
    const float  areaY0 = origin.y;
    const float  areaW  = std::max(avail.x - leftW, ImGui::GetFontSize() * 4.0f);
    const float  areaH  = bodyH;

    const float keyR = KeyRadius();
    const float hitR = HitRadius();

    // 颜色 token（与 dopesheet 同源，禁 hardcode RGBA）。
    const ImU32 colBg       = ImGui::GetColorU32(Theme::Color::GetBackgroundSecondary());
    const ImU32 colGrid     = ImGui::GetColorU32(Theme::Color::GetSeparator());
    const ImU32 colCurve    = ImGui::GetColorU32(Theme::Color::GetTextPrimary());
    const ImU32 colKey      = ImGui::GetColorU32(Theme::Color::GetTextSecondary());
    const ImU32 colKeySel   = ImGui::GetColorU32(Theme::Color::GetAccentPrimary());
    const ImU32 colHandle   = ImGui::GetColorU32(Theme::Color::GetAlertWarn());
    const ImU32 colPlayhead = ImGui::GetColorU32(Theme::Color::GetAccentPrimary());
    const ImU32 colLabel    = ImGui::GetColorU32(Theme::Color::GetTextDisabled());

    // 覆盖整块的 InvisibleButton 捕获点击 / 拖拽（先于自绘，与 dopesheet 同款）。
    ImGui::InvisibleButton("##anim_curve_canvas", ImVec2(avail.x, bodyH));
    const bool   canvasHovered = ImGui::IsItemHovered();
    const ImVec2 mouse         = ImGui::GetIO().MousePos;

    // 背景 + 边框。
    dl->AddRectFilled(ImVec2(areaX0, areaY0), ImVec2(areaX0 + areaW, areaY0 + areaH), colBg);
    dl->AddRect(ImVec2(areaX0, areaY0), ImVec2(areaX0 + areaW, areaY0 + areaH), colGrid);

    // 值范围 + 纵轴 min/mid/max 标签。
    float valMin = 0.0f;
    float valMax = 1.0f;
    TrackValueRange(track, comp, valMin, valMax);
    auto drawValueLabel = [&](float v)
    {
        const float y = ValueToScreenY(v, areaY0, areaH, valMin, valMax);
        dl->AddLine(ImVec2(areaX0, y), ImVec2(areaX0 + areaW, y), colGrid);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        dl->AddText(ImVec2(origin.x + 1.0f, y - ImGui::GetFontSize() * 0.5f), colLabel, buf);
    };
    drawValueLabel(valMax);
    drawValueLabel((valMin + valMax) * 0.5f);
    drawValueLabel(valMin);

    // 时间 → 屏幕 x（沿用 dopesheet 的 TimeToScreenX，统一 time→x 语义）。
    auto timeX = [&](float t)
    { return TimeToScreenX(t, areaX0, areaW, duration); };
    auto valY = [&](float v)
    { return ValueToScreenY(v, areaY0, areaH, valMin, valMax); };

    // ---- 曲线折线：用 SampleTrack 密集采样（display==playback 的正确性核心）----
    // 采样数按画布宽派生（约每 2px 一个采样点），最少 32 段。
    if (duration > 0.0f && track.keys.size() >= 1)
    {
        const int samples = std::max(32, static_cast<int>(areaW * 0.5f));
        ImVec2    prev(0.0f, 0.0f);
        for (int i = 0; i <= samples; ++i)
        {
            const float  t = duration * static_cast<float>(i) / static_cast<float>(samples);
            const float  v = Anim::SampleTrack(track, t)[comp];
            const ImVec2 p(timeX(t), valY(v));
            if (i > 0)
            {
                dl->AddLine(prev, p, colCurve, 1.5f);
            }
            prev = p;
        }
    }

    // ---- 每个 key：点 + （Bezier 段）切线手柄 -------------------------------
    // 段跨度（time / value）：out 手柄看 [ki, ki+1] 段，in 手柄看 [ki-1, ki] 段。
    for (std::size_t ki = 0; ki < track.keys.size(); ++ki)
    {
        const Keyframe& k = track.keys[ki];
        const ImVec2    kp(timeX(k.time), valY(k.value[comp]));
        const bool      selected = (sTimelineSel.selTrack == curTrack && sTimelineSel.selKey == ki);
        const ImU32     kc       = selected ? colKeySel : colKey;

        // out 手柄：当前 key 是某 Bezier 段的起点（k.interp==Bezier 且有后继）。
        if (k.interp == Anim::InterpMode::Bezier && ki + 1 < track.keys.size())
        {
            const Keyframe& k1 = track.keys[ki + 1];
            const float     dt = k1.time - k.time;
            const float     dv = k1.value[comp] - k.value[comp];
            const HandleTV  h  = OutHandleTV(k, comp, dt, dv);
            const ImVec2    hp(timeX(h.time), valY(h.value));
            dl->AddLine(kp, hp, colHandle, 1.0f);
            dl->AddCircleFilled(hp, keyR * 0.7f, colHandle);
        }
        // in 手柄：当前 key 是某 Bezier 段的终点（前一 key.interp==Bezier）。
        if (ki > 0 && track.keys[ki - 1].interp == Anim::InterpMode::Bezier)
        {
            const Keyframe& k0 = track.keys[ki - 1];
            const float     dt = k.time - k0.time;
            const float     dv = k.value[comp] - k0.value[comp];
            const HandleTV  h  = InHandleTV(k, comp, dt, dv);
            const ImVec2    hp(timeX(h.time), valY(h.value));
            dl->AddLine(kp, hp, colHandle, 1.0f);
            dl->AddCircleFilled(hp, keyR * 0.7f, colHandle);
        }

        // key 点（菱形，与 dopesheet 一致的视觉）。
        dl->AddQuadFilled(ImVec2(kp.x, kp.y - keyR), ImVec2(kp.x + keyR, kp.y),
                          ImVec2(kp.x, kp.y + keyR), ImVec2(kp.x - keyR, kp.y), kc);
    }

    // ---- playhead 竖线 ------------------------------------------------------
    if (duration > 0.0f)
    {
        const float px = timeX(clip.ElapsedSeconds());
        dl->AddLine(ImVec2(px, areaY0), ImVec2(px, areaY0 + areaH), colPlayhead, 1.5f);
    }

    // ---- 交互：拖手柄改缓动 / 点 key 选中 / 空白 scrub / 右键切模式 ----------
    if (canEdit)
    {
        // 鼠标按下：优先命中手柄（拖缓动），再命中 key（选中），最后空白 scrub。
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            bool hit = false;

            // 命中手柄？遍历各 key 的 out / in 手柄屏幕位置。
            for (std::size_t ki = 0; ki < track.keys.size() && !hit; ++ki)
            {
                const Keyframe& k = track.keys[ki];
                // out 手柄
                if (k.interp == Anim::InterpMode::Bezier && ki + 1 < track.keys.size())
                {
                    const Keyframe& k1 = track.keys[ki + 1];
                    const float     dt = k1.time - k.time;
                    const float     dv = k1.value[comp] - k.value[comp];
                    const HandleTV  h  = OutHandleTV(k, comp, dt, dv);
                    const ImVec2    hp(timeX(h.time), valY(h.value));
                    if (std::fabs(mouse.x - hp.x) <= hitR && std::fabs(mouse.y - hp.y) <= hitR)
                    {
                        sCurveDrag            = {true, curTrack, ki, CurveHandle::Out};
                        sTimelineSel.selTrack = curTrack;
                        sTimelineSel.selKey   = ki;
                        hit                   = true;
                        break;
                    }
                }
                // in 手柄
                if (ki > 0 && track.keys[ki - 1].interp == Anim::InterpMode::Bezier)
                {
                    const Keyframe& k0 = track.keys[ki - 1];
                    const float     dt = k.time - k0.time;
                    const float     dv = k.value[comp] - k0.value[comp];
                    const HandleTV  h  = InHandleTV(k, comp, dt, dv);
                    const ImVec2    hp(timeX(h.time), valY(h.value));
                    if (std::fabs(mouse.x - hp.x) <= hitR && std::fabs(mouse.y - hp.y) <= hitR)
                    {
                        sCurveDrag            = {true, curTrack, ki, CurveHandle::In};
                        sTimelineSel.selTrack = curTrack;
                        sTimelineSel.selKey   = ki;
                        hit                   = true;
                        break;
                    }
                }
            }

            // 命中 key 点？（选中，不拖——曲线视图改值/缓动，时间编辑留 dopesheet）
            if (!hit)
            {
                for (std::size_t ki = 0; ki < track.keys.size(); ++ki)
                {
                    const Keyframe& k = track.keys[ki];
                    const ImVec2    kp(timeX(k.time), valY(k.value[comp]));
                    if (std::fabs(mouse.x - kp.x) <= hitR && std::fabs(mouse.y - kp.y) <= hitR)
                    {
                        sTimelineSel.selTrack = curTrack;
                        sTimelineSel.selKey   = ki;
                        hit                   = true;
                        break;
                    }
                }
            }

            // 空白：scrub（横轴时间，与 dopesheet 一致）。
            if (!hit && mouse.x >= areaX0)
            {
                clip.Seek(ScreenXToTime(mouse.x, areaX0, areaW, duration));
            }
        }

        // 拖手柄：屏幕落点 → (time,value) → 反推切线 → 改该 key → 命令栈（merge）。
        if (sCurveDrag.dragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            AnimationClip newClip = clip.Clip();
            if (sCurveDrag.track < newClip.tracks.size())
            {
                AnimationTrack&   tr          = newClip.tracks[sCurveDrag.track];
                const std::size_t ki          = sCurveDrag.key;
                const float       handleTime  = ScreenXToTime(mouse.x, areaX0, areaW, duration);
                const float       handleValue = ScreenYToValue(mouse.y, areaY0, areaH, valMin, valMax);

                bool changed = false;
                if (sCurveDrag.handle == CurveHandle::Out && ki < tr.keys.size() && ki + 1 < tr.keys.size())
                {
                    Keyframe&   k0 = tr.keys[ki];
                    Keyframe&   k1 = tr.keys[ki + 1];
                    const float dt = k1.time - k0.time;
                    const float dv = k1.value[comp] - k0.value[comp];
                    k0.outTangent  = SolveOutTangent(k0, comp, handleTime, handleValue, dt, dv);
                    changed        = true;
                }
                else if (sCurveDrag.handle == CurveHandle::In && ki < tr.keys.size() && ki > 0)
                {
                    Keyframe&   k1 = tr.keys[ki];
                    Keyframe&   k0 = tr.keys[ki - 1];
                    const float dt = k1.time - k0.time;
                    const float dv = k1.value[comp] - k0.value[comp];
                    k1.inTangent   = SolveInTangent(k1, comp, handleTime, handleValue, dt, dv);
                    changed        = true;
                }

                if (changed)
                {
                    char mergeKey[128];
                    std::snprintf(mergeKey, sizeof(mergeKey), "anim_curve_handle:%zu:%zu:%d",
                                  sCurveDrag.track, sCurveDrag.key,
                                  static_cast<int>(sCurveDrag.handle));
                    PushClipEdit(mHost, clip, std::move(newClip), mergeKey, "Edit Bezier Handle");
                }
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            sCurveDrag = CurveDragState{};
        }

        // 右键 key → 弹菜单切 InterpMode（在选中 key 上；命中谁就对谁开）。
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            for (std::size_t ki = 0; ki < track.keys.size(); ++ki)
            {
                const Keyframe& k = track.keys[ki];
                const ImVec2    kp(timeX(k.time), valY(k.value[comp]));
                if (std::fabs(mouse.x - kp.x) <= hitR && std::fabs(mouse.y - kp.y) <= hitR)
                {
                    sTimelineSel.selTrack = curTrack;
                    sTimelineSel.selKey   = ki;
                    ImGui::OpenPopup("##curve_key_interp");
                    break;
                }
            }
        }
    }

    // 重新取 clip 真相：上方拖手柄分支可能已 SetClip（curClip / track 引用随之
    // 失效，B2.3 已踩过的 stale-clip 坑）。popup + 底部 readout 用新鲜引用，避免
    // 同帧"拖完手柄 + 读旧 track"的悬空读。track 索引 curTrack 不变（拖手柄不增删轨）。
    const AnimationClip&  freshClip = clip.Clip();
    const AnimationTrack* freshTrack =
        (curTrack < freshClip.tracks.size()) ? &freshClip.tracks[curTrack] : nullptr;

    // InterpMode 右键菜单：对选中 key 切 Step / Linear / Bezier（走命令栈）。
    if (freshTrack != nullptr && ImGui::BeginPopup("##curve_key_interp"))
    {
        const AnimationTrack& popupTrack = *freshTrack;
        const std::size_t     ki         = sTimelineSel.selKey;
        const bool            valid      = (sTimelineSel.selTrack == curTrack && ki != kInvalidIdx && ki < popupTrack.keys.size());
        ImGui::TextDisabled("Interpolation");
        ImGui::Separator();
        auto setMode = [&](Anim::InterpMode mode, const char* label)
        {
            const bool active = valid && popupTrack.keys[ki].interp == mode;
            if (ImGui::MenuItem(label, nullptr, active, valid && !active))
            {
                AnimationClip newClip = clip.Clip();
                if (sTimelineSel.selTrack < newClip.tracks.size() && ki < newClip.tracks[sTimelineSel.selTrack].keys.size())
                {
                    Keyframe& nk = newClip.tracks[sTimelineSel.selTrack].keys[ki];
                    nk.interp    = mode;
                    // 切到 Bezier 且当前切线全零 → 给个平滑默认，否则曲线退化无手柄可拖。
                    if (mode == Anim::InterpMode::Bezier && nk.outTangent == glm::vec2(0.0f) && nk.inTangent == glm::vec2(0.0f))
                    {
                        AssignSmoothBezierDefault(nk);
                    }
                    PushClipEdit(mHost, clip, std::move(newClip), "anim_curve_interp",
                                 "Set Interp Mode");
                }
            }
        };
        setMode(Anim::InterpMode::Step, "Step");
        setMode(Anim::InterpMode::Linear, "Linear");
        setMode(Anim::InterpMode::Bezier, "Bezier");
        ImGui::EndPopup();
    }

    // ---- 占位推进 cursor + 底部提示（与 dopesheet 的 Dummy 同款）------------
    ImGui::Dummy(ImVec2(0.0f, bodyH));
    if (freshTrack != nullptr && sTimelineSel.selKey != kInvalidIdx && sTimelineSel.selTrack == curTrack && sTimelineSel.selKey < freshTrack->keys.size())
    {
        const Keyframe& sk       = freshTrack->keys[sTimelineSel.selKey];
        const char*     modeName = (sk.interp == Anim::InterpMode::Step)     ? "Step"
                                   : (sk.interp == Anim::InterpMode::Linear) ? "Linear"
                                                                             : "Bezier";
        ImGui::TextDisabled("选中 key  t=%.3f  value=%.3f  interp=%s", sk.time,
                            sk.value[comp], modeName);
        if (sk.interp != Anim::InterpMode::Bezier)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(非 Bezier 段无切线手柄；右键 key 切 Bezier)");
        }
    }
    else
    {
        ImGui::TextDisabled("点 key 选中 · 右键切插值 · 拖橙色手柄改 Bezier 缓动");
    }
}
