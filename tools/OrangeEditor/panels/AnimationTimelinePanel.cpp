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

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace
{

namespace Anim = Orange::Engine::Animation;
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
    if (duration <= 0.0f) { return trackX0; }
    const float u = std::clamp(t / duration, 0.0f, 1.0f);
    return trackX0 + u * trackW;
}

// 反映射：时间轴区域内屏幕 x → clip 时间（秒），clamp 到 [0,duration]。
float ScreenXToTime(float screenX, float trackX0, float trackW, float duration)
{
    if (trackW <= 0.0f || duration <= 0.0f) { return 0.0f; }
    const float u = std::clamp((screenX - trackX0) / trackW, 0.0f, 1.0f);
    return u * duration;
}

// 取当前选中实体的 ClipAnimator（仅 backend=="clip" 时非空）。任一环空返回 nullptr。
ClipAnimator* ResolveSelectedClipAnimator(EditorHost& host)
{
    using AC = Anim::AnimatorComponent;
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return nullptr; }
    const Orange::Engine::Entity e = host.selection.selectedEntity;
    if (!e.IsValid() || !pWorld->IsValid(e)) { return nullptr; }
    auto* ac = pWorld->GetComponent<AC>(e);
    if (ac == nullptr || !ac->animator) { return nullptr; }
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
    bool        draggingKey   = false;
    std::size_t dragTrack     = static_cast<std::size_t>(-1);
    std::size_t dragKey       = static_cast<std::size_t>(-1);
    // 正在拖动的事件 marker。
    bool        draggingEvent = false;
    std::size_t dragEventIdx  = static_cast<std::size_t>(-1);

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

TimelineSelection sTimelineSel;
constexpr std::size_t kInvalidIdx = static_cast<std::size_t>(-1);

// 提交一次 clip 编辑：拷 oldClip→在 newClip 上已被调用方改好→RecomputeDuration
// → 压 SetAnimationClipCommand。mergeKey 决定是否与后续命令合并（拖动用稳定
// key，离散编辑用唯一 key），label 是 Undo 菜单展示名。
void PushClipEdit(EditorHost& host, ClipAnimator& clip, AnimationClip newClip,
                  std::string mergeKey, std::string label)
{
    AnimationClip oldClip = clip.Clip();  // 当前快照（do/undo 对称基线）
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
    const Orange::Engine::Entity e = host.selection.selectedEntity;
    const bool canPreview = (host.scene.playState == PlayState::Edit);

    const bool isPreviewTarget =
        host.animPreview.previewEntity.IsValid()
        && host.animPreview.previewEntity == e;
    const bool isPlaying = isPreviewTarget && host.animPreview.previewPlaying;

    ImGui::BeginDisabled(!canPreview);

    // ▶：把预览指向本 entity + 启动（复用 B2.6——EditorRenderLayer Edit 模式
    // OnUpdate 据 previewPlaying 每帧 Tick 单 animator，不在此另起 tick 路径）。
    if (ImGui::Button(isPlaying ? Theme::Icon::GetPause() : Theme::Icon::GetPlay()))
    {
        if (isPlaying)
        {
            clip.Pause();
            if (isPreviewTarget) { host.animPreview.previewPlaying = false; }
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
        clip.Stop();  // mPlaying=false + elapsed=0 + 立即应用 t0 pose
        if (isPreviewTarget) { host.animPreview.previewPlaying = false; }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    // loop：直接桥接 ClipAnimator（与 Inspector loop checkbox 同语义）。
    bool loop = clip.IsLooping();
    if (ImGui::Checkbox("Loop", &loop)) { clip.SetLoop(loop); }

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
    const float t = clip.ElapsedSeconds();
    AnimationClip newClip = clip.Clip();
    AnimationTrack* tr = Anim::FindTrack(newClip, track.targetName);
    if (tr == nullptr) { return; }

    Keyframe key;
    key.time   = t;
    key.value  = Anim::SampleTrack(*tr, t);  // 当前时刻曲线值，打键不跳变
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
    if (sTimelineSel.selTrack == kInvalidIdx || sTimelineSel.selKey == kInvalidIdx) { return; }
    AnimationClip newClip = clip.Clip();
    if (sTimelineSel.selTrack >= newClip.tracks.size()) { return; }
    AnimationTrack& tr = newClip.tracks[sTimelineSel.selTrack];
    if (!Anim::RemoveKeyframe(tr, sTimelineSel.selKey)) { return; }
    PushClipEdit(host, clip, std::move(newClip), "anim_key_delete", "Delete Keyframe");
    sTimelineSel.ClearKeySel();
}

}  // namespace

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

    // ---- 键盘快捷键：K 打选中轨道键 / Del 删选中 key（仅面板聚焦时）-------
    const bool panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const AnimationClip& curClip = clip.Clip();

    // ---- timeline 主体绘制 ------------------------------------------------
    // 用 ImDrawList 自绘标尺 + 轨道行 + 关键帧 + playhead，覆盖一个 InvisibleButton
    // 区域捕获点击 / 拖拽（与 ScenePanel / ColliderVertexEdit 自绘 + hit-test 同款）。
    const float labelW   = LabelColumnWidth();
    const ImVec2 avail    = ImGui::GetContentRegionAvail();
    const float trackAreaW = std::max(avail.x - labelW, ImGui::GetFontSize() * 4.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 colRuler     = ImGui::GetColorU32(Theme::Color::GetSeparator());
    const ImU32 colRowBg      = ImGui::GetColorU32(Theme::Color::GetBackgroundSecondary());
    const ImU32 colRowAltBg   = ImGui::GetColorU32(Theme::Color::GetControlBg());
    const ImU32 colKey        = ImGui::GetColorU32(Theme::Color::GetTextSecondary());
    const ImU32 colKeySel     = ImGui::GetColorU32(Theme::Color::GetAccentPrimary());
    const ImU32 colPlayhead   = ImGui::GetColorU32(Theme::Color::GetAccentPrimary());
    const ImU32 colEvent      = ImGui::GetColorU32(Theme::Color::GetAlertWarn());
    const ImU32 colEventSel   = ImGui::GetColorU32(Theme::Color::GetAccentPrimary());

    const float rowH    = RowHeight();
    const float keyR    = KeyRadius();
    const float hitR    = HitRadius();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float trackX0 = origin.x + labelW;

    // 事件 marker 行 + 标尺行高度。
    const float rulerH  = rowH;       // 顶部时间标尺 + 事件 marker 共用一行
    const std::size_t trackCount = curClip.tracks.size();
    const float bodyH   = rulerH + rowH * static_cast<float>(std::max<std::size_t>(trackCount, 1));

    // 覆盖整块 timeline 的 InvisibleButton——吃掉 ImGui 默认 item 行为，自己解析
    // 点击落在哪条轨 / 哪个 key / 标尺。必须先于自绘调用，拿到 hovered/active。
    ImGui::InvisibleButton("##anim_timeline_canvas", ImVec2(avail.x, bodyH));
    const bool canvasHovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;

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
        const AnimationEvent& ev = curClip.events[ei];
        const float x = TimeToScreenX(ev.time, trackX0, trackAreaW, duration);
        const float cy = origin.y + rulerH * 0.5f;
        const bool selected = (sTimelineSel.selEvent == ei);
        const ImU32 c = selected ? colEventSel : colEvent;
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
        const AnimationTrack& tr = curClip.tracks[ti];
        const float rowY  = origin.y + rulerH + rowH * static_cast<float>(ti);
        const float keyCy = rowY + rowH * 0.5f;

        // 轨道标签（左列）——用 ImDrawList 直绘，不占 ImGui item（整块已是
        // InvisibleButton）。
        dl->AddText(ImVec2(origin.x + 2.0f, rowY + 2.0f),
                    ImGui::GetColorU32(Theme::Color::GetTextPrimary()),
                    tr.targetName.c_str());

        // 关键帧菱形。
        for (std::size_t ki = 0; ki < tr.keys.size(); ++ki)
        {
            const float kx = TimeToScreenX(tr.keys[ki].time, trackX0, trackAreaW, duration);
            const bool selected = (sTimelineSel.selTrack == ti && sTimelineSel.selKey == ki);
            const ImU32 c = selected ? colKeySel : colKey;
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
                const AnimationTrack& tr = curClip.tracks[ti];
                const float rowY  = origin.y + rulerH + rowH * static_cast<float>(ti);
                const float keyCy = rowY + rowH * 0.5f;
                if (std::fabs(mouse.y - keyCy) > rowH * 0.5f) { continue; }
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
                        hitSomething      = true;
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
                            hitSomething        = true;
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
                const float newT = ScreenXToTime(mouse.x, trackX0, trackAreaW, duration);
                AnimationClip newClip = clip.Clip();
                if (sTimelineSel.dragTrack < newClip.tracks.size()
                    && sTimelineSel.dragKey < newClip.tracks[sTimelineSel.dragTrack].keys.size())
                {
                    // 拖动后该 key 可能换索引（排序维持升序）—— MoveKeyframeTime
                    // 在副本上做，push 后从 clip 回查新索引（newClip 已被 move 走，
                    // 不能再读它，故索引回查走 clip.Clip() 新真相）。
                    Anim::MoveKeyframeTime(newClip.tracks[sTimelineSel.dragTrack],
                                           sTimelineSel.dragKey, newT);
                    char mergeKey[128];
                    std::snprintf(mergeKey, sizeof(mergeKey), "anim_drag_key:%zu",
                                  sTimelineSel.dragTrack);
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
            const float newT = ScreenXToTime(mouse.x, trackX0, trackAreaW, duration);
            AnimationClip newClip = clip.Clip();
            if (sTimelineSel.dragEventIdx < newClip.events.size())
            {
                AnimationEvent ev = newClip.events[sTimelineSel.dragEventIdx];
                ev.time = newT;
                Anim::RemoveClipEvent(newClip, sTimelineSel.dragEventIdx);
                Anim::AddClipEvent(newClip, ev);
                PushClipEdit(mHost, clip, std::move(newClip), "anim_drag_event",
                             "Move Event");
                // 回查新索引（events 升序，取 time 最近的）。
                std::size_t bestIdx = kInvalidIdx;
                float bestDist = 1e-3f;
                const AnimationClip& after = clip.Clip();
                for (std::size_t i = 0; i < after.events.size(); ++i)
                {
                    const float d = std::fabs(after.events[i].time - newT);
                    if (d <= bestDist) { bestDist = d; bestIdx = i; }
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
            if (sTimelineSel.selEvent != kInvalidIdx
                && sTimelineSel.selEvent < kbClip.events.size())
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
                    ? sTimelineSel.selTrack : 0;
            KeyTrackAtPlayhead(mHost, clip, kbClip.tracks[ti]);
        }
    }

    // ---- 底部工具行：加轨道 / 删选中轨道 / 加事件 / 打键按钮 -------------
    ImGui::Dummy(ImVec2(0.0f, bodyH));  // 占位，让下面控件落在 timeline 下方
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
        "position", "position.x", "position.y", "position.z",
        "rotation", "scale", "scale.x", "scale.y", "scale.z", "scale.uniform",
    };
    static const Anim::TrackValueType kTrackTypes[] = {
        Anim::TrackValueType::Vec3, Anim::TrackValueType::Float,
        Anim::TrackValueType::Float, Anim::TrackValueType::Float,
        Anim::TrackValueType::Vec3, Anim::TrackValueType::Vec3,
        Anim::TrackValueType::Float, Anim::TrackValueType::Float,
        Anim::TrackValueType::Float, Anim::TrackValueType::Float,
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
                ? sTimelineSel.selTrack : 0;
        KeyTrackAtPlayhead(mHost, clip, curClip.tracks[ti]);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    // 加事件：在 playhead 时刻加一个空名事件。
    if (ImGui::Button("Add Event"))
    {
        AnimationClip newClip = curClip;
        AnimationEvent ev;
        ev.time = clip.ElapsedSeconds();
        ev.name = "event";
        Anim::AddClipEvent(newClip, ev);
        PushClipEdit(mHost, clip, std::move(newClip), "anim_add_event", "Add Event");
    }

    // 选中事件重命名 InputText（在工具行下方一行，便于改 event.name）。
    if (sTimelineSel.selEvent != kInvalidIdx && sTimelineSel.selEvent < curClip.events.size())
    {
        char nameBuf[128];
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
