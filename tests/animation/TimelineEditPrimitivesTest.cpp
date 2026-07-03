// Timeline / dopesheet 编辑的数据层 round-trip 测试（B2.3）。
//
// 编辑器 timeline 面板的命令（SetAnimationClipCommand）+ 工具行交互最终都归
// 结为一组**纯数据操作**：拷当前 clip → 在副本上调 AnimationClip.h 原语
// （UpsertKeyframe / MoveKeyframeTime / RemoveKeyframe / UpsertTrack /
// RemoveTrack / AddClipEvent / RemoveClipEvent）→ RecomputeClipDuration →
// ClipAnimator::SetClip（do/undo 在 old/new clip 之间对称切换）→ 资产化
// clip 时 SaveAnimationClip 写回 .anim。
//
// ImGui 像素 / 拖拽 / hit-test headless 测不到（逐条 dogfood），但上述数据
// 语义可在引擎层直接锁住——本测试复刻 do/undo 对称性 + .anim round-trip 保真。
// 不链接编辑器 TU（SetAnimationClipCommand 含 EditorHost / ImGui 依赖），只测
// 其等价的引擎层操作链，覆盖命令真正承载的数据逻辑。

#include "orange/engine/animation/AnimationClip.h"
#include "orange/engine/animation/AnimationClipSerialization.h"
#include "orange/engine/animation/ClipAnimator.h"
#include "orange/engine/scene/TransformComponent.h"

#include <glm/vec4.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace Anim  = ::Orange::Engine::Animation;
namespace Scene = ::Orange::Engine::Scene;

namespace
{

    bool Near(float a, float b, float eps = 1e-4f)
    {
        return std::fabs(a - b) < eps;
    }

    Anim::Keyframe Key(float time, float v)
    {
        Anim::Keyframe k;
        k.time   = time;
        k.value  = glm::vec4(v, 0.0f, 0.0f, 0.0f);
        k.interp = Anim::InterpMode::Linear;
        return k;
    }

    // 复刻 SetAnimationClipCommand 的 do/undo：do = SetClip(newClip)、undo =
    // SetClip(oldClip)，二者都保持 SourceAssetPath（SetClip 不动它）。
    struct ClipEdit
    {
        Anim::AnimationClip oldClip;
        Anim::AnimationClip newClip;

        void Do(Anim::ClipAnimator& anim) const { anim.SetClip(newClip); }
        void Undo(Anim::ClipAnimator& anim) const { anim.SetClip(oldClip); }
    };

    // 拷当前 clip → 在副本上调原语（调用方填）→ RecomputeDuration → 返回 ClipEdit。
    template <typename Mutate>
    ClipEdit MakeEdit(const Anim::ClipAnimator& anim, Mutate mutate)
    {
        ClipEdit e;
        e.oldClip = anim.Clip();
        e.newClip = anim.Clip();
        mutate(e.newClip);
        Anim::RecomputeClipDuration(e.newClip);
        return e;
    }

} // namespace

int main()
{
    using Anim::AnimationClip;
    using Anim::ClipAnimator;
    using Anim::TrackValueType;

    // ===== 1. 打键 do/undo 对称（UpsertKeyframe）=====
    {
        AnimationClip clip;
        clip.name = "edit_test";
        clip.tracks.push_back({});
        clip.tracks[0].targetName = "position.x";
        clip.tracks[0].valueType  = TrackValueType::Float;
        clip.tracks[0].keys.push_back(Key(0.0f, 0.0f));
        clip.tracks[0].keys.push_back(Key(2.0f, 10.0f));

        Scene::TransformComponent tc;
        ClipAnimator              anim(clip, &tc);

        const std::size_t before = anim.Clip().tracks[0].keys.size();
        ClipEdit          e      = MakeEdit(anim, [](AnimationClip& c)
                                            { Anim::UpsertKeyframe(c, "position.x", TrackValueType::Float, Key(1.0f, 5.0f)); });
        e.Do(anim);
        assert(anim.Clip().tracks[0].keys.size() == before + 1 && "打键后 key+1");
        // 中间帧值正确 + 升序维持。
        assert(Near(anim.Clip().tracks[0].keys[1].time, 1.0f) && "新键插在中间");

        e.Undo(anim);
        assert(anim.Clip().tracks[0].keys.size() == before && "Undo 回到打键前");
        e.Do(anim); // Redo 等价
        assert(anim.Clip().tracks[0].keys.size() == before + 1 && "Redo 恢复");
        std::fprintf(stdout, "  [PASS] 打键 do/undo 对称\n");
    }

    // ===== 2. 拖键改时间 do/undo（MoveKeyframeTime）+ duration 重算 =====
    {
        AnimationClip clip;
        clip.tracks.push_back({});
        clip.tracks[0].targetName = "position.x";
        clip.tracks[0].valueType  = TrackValueType::Float;
        clip.tracks[0].keys.push_back(Key(0.0f, 0.0f));
        clip.tracks[0].keys.push_back(Key(2.0f, 10.0f));
        Anim::RecomputeClipDuration(clip);

        Scene::TransformComponent tc;
        ClipAnimator              anim(clip, &tc);
        assert(Near(anim.Clip().duration, 2.0f) && "初始 duration=2");

        // 把末键从 t=2 拖到 t=5 → duration 应重算到 5。
        ClipEdit e = MakeEdit(anim, [](AnimationClip& c)
                              { Anim::MoveKeyframeTime(c.tracks[0], 1, 5.0f); });
        e.Do(anim);
        assert(Near(anim.Clip().duration, 5.0f) && "拖键后 duration 重算到 5");
        assert(Near(anim.Clip().tracks[0].keys[1].time, 5.0f) && "末键 time=5");

        e.Undo(anim);
        assert(Near(anim.Clip().duration, 2.0f) && "Undo 回 duration=2");
        assert(Near(anim.Clip().tracks[0].keys[1].time, 2.0f) && "Undo 回末键 time=2");
        std::fprintf(stdout, "  [PASS] 拖键改时间 do/undo + duration 重算\n");
    }

    // ===== 3. 删键 do/undo（RemoveKeyframe）=====
    {
        AnimationClip clip;
        clip.tracks.push_back({});
        clip.tracks[0].targetName = "scale.uniform";
        clip.tracks[0].valueType  = TrackValueType::Float;
        clip.tracks[0].keys.push_back(Key(0.0f, 1.0f));
        clip.tracks[0].keys.push_back(Key(1.0f, 2.0f));
        clip.tracks[0].keys.push_back(Key(2.0f, 1.0f));

        Scene::TransformComponent tc;
        ClipAnimator              anim(clip, &tc);

        ClipEdit e = MakeEdit(anim, [](AnimationClip& c)
                              {
                                  Anim::RemoveKeyframe(c.tracks[0], 1); // 删中间键
                              });
        e.Do(anim);
        assert(anim.Clip().tracks[0].keys.size() == 2 && "删键后 key=2");
        e.Undo(anim);
        assert(anim.Clip().tracks[0].keys.size() == 3 && "Undo 回 key=3");
        assert(Near(anim.Clip().tracks[0].keys[1].time, 1.0f) && "Undo 恢复中间键");
        std::fprintf(stdout, "  [PASS] 删键 do/undo\n");
    }

    // ===== 4. 加/删轨道 do/undo（UpsertTrack / RemoveTrack）=====
    {
        AnimationClip clip;
        clip.tracks.push_back({});
        clip.tracks[0].targetName = "position.x";
        clip.tracks[0].valueType  = TrackValueType::Float;

        Scene::TransformComponent tc;
        ClipAnimator              anim(clip, &tc);

        ClipEdit addEdit = MakeEdit(anim, [](AnimationClip& c)
                                    { Anim::UpsertTrack(c, "rotation", TrackValueType::Vec3); });
        addEdit.Do(anim);
        assert(anim.Clip().tracks.size() == 2 && "加轨道后 2 条");
        assert(Anim::FindTrack(anim.Clip(), "rotation") != nullptr && "新轨道存在");
        addEdit.Undo(anim);
        assert(anim.Clip().tracks.size() == 1 && "Undo 回 1 条");

        // 删轨道：先把 add 应用回去再删它。
        addEdit.Do(anim);
        ClipEdit removeEdit = MakeEdit(anim, [](AnimationClip& c)
                                       { Anim::RemoveTrack(c, "position.x"); });
        removeEdit.Do(anim);
        assert(anim.Clip().tracks.size() == 1 && "删轨道后 1 条");
        assert(Anim::FindTrack(anim.Clip(), "position.x") == nullptr && "被删轨道不存在");
        removeEdit.Undo(anim);
        assert(Anim::FindTrack(anim.Clip(), "position.x") != nullptr && "Undo 恢复轨道");
        std::fprintf(stdout, "  [PASS] 加/删轨道 do/undo\n");
    }

    // ===== 5. 事件加/删/改时间 do/undo（AddClipEvent / RemoveClipEvent）=====
    {
        AnimationClip clip;
        clip.tracks.push_back({});
        clip.tracks[0].targetName = "position.x";
        clip.tracks[0].valueType  = TrackValueType::Float;
        clip.tracks[0].keys.push_back(Key(0.0f, 0.0f));
        clip.tracks[0].keys.push_back(Key(3.0f, 10.0f));

        Scene::TransformComponent tc;
        ClipAnimator              anim(clip, &tc);

        ClipEdit addEv = MakeEdit(anim, [](AnimationClip& c)
                                  { Anim::AddClipEvent(c, Anim::AnimationEvent{1.5f, "hit"}); });
        addEv.Do(anim);
        assert(anim.Clip().events.size() == 1 && "加事件后 1 个");
        assert(anim.Clip().events[0].name == "hit" && "事件名正确");

        // 拖事件改时间：删旧 + 加新（面板拖动逻辑）。
        ClipEdit moveEv = MakeEdit(anim, [](AnimationClip& c)
                                   {
            Anim::RemoveClipEvent(c, 0);
            Anim::AddClipEvent(c, Anim::AnimationEvent{2.5f, "hit"}); });
        moveEv.Do(anim);
        assert(Near(anim.Clip().events[0].time, 2.5f) && "事件拖到 t=2.5");
        moveEv.Undo(anim);
        assert(Near(anim.Clip().events[0].time, 1.5f) && "Undo 回 t=1.5");

        // 删事件。
        ClipEdit delEv = MakeEdit(anim, [](AnimationClip& c)
                                  { Anim::RemoveClipEvent(c, 0); });
        delEv.Do(anim);
        assert(anim.Clip().events.empty() && "删事件后空");
        delEv.Undo(anim);
        assert(anim.Clip().events.size() == 1 && "Undo 恢复事件");
        std::fprintf(stdout, "  [PASS] 事件加/删/拖 do/undo\n");
    }

    // ===== 6. .anim 写回 round-trip 保真（SaveAnimationClip → Load）=====
    {
        AnimationClip clip;
        clip.name = "roundtrip";
        clip.loop = true;
        clip.tracks.push_back({});
        clip.tracks[0].targetName = "position";
        clip.tracks[0].valueType  = TrackValueType::Vec3;
        clip.tracks[0].keys.push_back(Anim::Keyframe{0.0f, glm::vec4(1, 2, 3, 0), Anim::InterpMode::Linear, {}, {}});
        clip.tracks[0].keys.push_back(Anim::Keyframe{2.0f, glm::vec4(4, 5, 6, 0), Anim::InterpMode::Step, {}, {}});
        clip.events.push_back(Anim::AnimationEvent{1.0f, "footstep"});
        Anim::RecomputeClipDuration(clip);

        // 模拟面板"Save to .anim"：写盘。
        const std::string path  = "timeline_roundtrip_test.anim";
        auto              saveR = Anim::SaveAnimationClip(clip, path);
        assert(saveR.IsOk() && "SaveAnimationClip 成功");

        // 重新加载，逐字段比对保真（编辑后写回 .anim 再开仍在）。
        auto loadR = Anim::LoadAnimationClip(path);
        assert(loadR.IsOk() && "LoadAnimationClip 成功");
        const AnimationClip& back = loadR.Value();

        assert(back.name == "roundtrip" && "name 保真");
        assert(back.loop == true && "loop 保真");
        assert(Near(back.duration, 2.0f) && "duration 保真");
        assert(back.tracks.size() == 1 && "track 数保真");
        assert(back.tracks[0].targetName == "position" && "targetName 保真");
        assert(back.tracks[0].valueType == TrackValueType::Vec3 && "valueType 保真");
        assert(back.tracks[0].keys.size() == 2 && "key 数保真");
        assert(Near(back.tracks[0].keys[0].value.y, 2.0f) && "key0 value 保真");
        assert(back.tracks[0].keys[1].interp == Anim::InterpMode::Step && "interp 保真");
        assert(back.events.size() == 1 && "event 数保真");
        assert(back.events[0].name == "footstep" && "event name 保真");
        assert(Near(back.events[0].time, 1.0f) && "event time 保真");

        std::remove(path.c_str());
        std::fprintf(stdout, "  [PASS] .anim 写回 round-trip 保真\n");
    }

    std::fprintf(stdout, "TimelineEditPrimitivesTest: 全部通过\n");
    return 0;
}
