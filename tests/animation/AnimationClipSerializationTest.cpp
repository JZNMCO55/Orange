// AnimationClip .anim JSON 序列化的 headless round-trip 测试（B2.2）。
// 锁住：enum 字符串映射、clip→JSON→clip 全字段还原、schema 校验 fail-fast、
// 未知 enum 串 fail-soft、文件 Save/Load。纯 CPU，无 GPU。

#include "orange/engine/animation/AnimationClipSerialization.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

namespace Anim = ::Orange::Engine::Animation;

namespace
{

bool Near(float a, float b, float eps = 1e-5f)
{
    return std::fabs(a - b) < eps;
}

Anim::Keyframe MakeKey(float time, glm::vec4 value, Anim::InterpMode interp,
                       glm::vec2 inT = glm::vec2(0.0f), glm::vec2 outT = glm::vec2(0.0f))
{
    Anim::Keyframe k;
    k.time       = time;
    k.value      = value;
    k.interp     = interp;
    k.inTangent  = inT;
    k.outTangent = outT;
    return k;
}

}  // namespace

int main()
{
    using Anim::InterpMode;
    using Anim::TrackValueType;

    // ===== 1. enum ↔ 字符串 =====
    {
        assert(Anim::ToString(TrackValueType::Vec3) == "Vec3");
        assert(Anim::ToString(InterpMode::Bezier) == "Bezier");
        TrackValueType vt = TrackValueType::Float;
        assert(Anim::TrackValueTypeFromString("Vec4", vt) && vt == TrackValueType::Vec4);
        InterpMode im = InterpMode::Linear;
        assert(Anim::InterpModeFromString("Step", im) && im == InterpMode::Step);
        assert(!Anim::TrackValueTypeFromString("Nonsense", vt) && "未知串返回 false");
        assert(vt == TrackValueType::Vec4 && "失败时不改 out");
        std::fprintf(stdout, "  [PASS] enum ↔ 字符串\n");
    }

    // ===== 2. clip → JSON → clip 全字段 round-trip =====
    {
        Anim::AnimationClip clip;
        clip.name     = "bob_and_spin";
        clip.duration = 2.5f;
        clip.loop     = true;

        Anim::AnimationTrack posTrack;
        posTrack.targetName = "position";
        posTrack.valueType  = TrackValueType::Vec3;
        posTrack.keys.push_back(MakeKey(0.0f, glm::vec4(0, 0, 0, 0), InterpMode::Linear));
        posTrack.keys.push_back(
            MakeKey(1.25f, glm::vec4(1.5f, 3.0f, -2.0f, 0), InterpMode::Bezier,
                    glm::vec2(0.1f, 0.2f), glm::vec2(0.3f, 0.4f)));
        clip.tracks.push_back(posTrack);

        Anim::AnimationTrack rotTrack;
        rotTrack.targetName = "rotation";
        rotTrack.valueType  = TrackValueType::Vec3;
        rotTrack.keys.push_back(MakeKey(0.0f, glm::vec4(0, 0, 0, 0), InterpMode::Step));
        rotTrack.keys.push_back(MakeKey(2.5f, glm::vec4(0, 90, 0, 0), InterpMode::Linear));
        clip.tracks.push_back(rotTrack);

        const std::string json   = Anim::AnimationClipToJson(clip);
        auto              parsed = Anim::AnimationClipFromJson(json);
        assert(parsed.IsOk() && "合法 JSON 应解析成功");
        const Anim::AnimationClip& got = parsed.Value();

        assert(got.name == "bob_and_spin");
        assert(Near(got.duration, 2.5f));
        assert(got.loop == true);
        assert(got.tracks.size() == 2);

        assert(got.tracks[0].targetName == "position");
        assert(got.tracks[0].valueType == TrackValueType::Vec3);
        assert(got.tracks[0].keys.size() == 2);
        assert(Near(got.tracks[0].keys[1].time, 1.25f));
        assert(Near(got.tracks[0].keys[1].value.x, 1.5f) &&
               Near(got.tracks[0].keys[1].value.y, 3.0f) &&
               Near(got.tracks[0].keys[1].value.z, -2.0f));
        assert(got.tracks[0].keys[1].interp == InterpMode::Bezier);
        assert(Near(got.tracks[0].keys[1].inTangent.x, 0.1f) &&
               Near(got.tracks[0].keys[1].inTangent.y, 0.2f));
        assert(Near(got.tracks[0].keys[1].outTangent.x, 0.3f) &&
               Near(got.tracks[0].keys[1].outTangent.y, 0.4f));

        assert(got.tracks[1].targetName == "rotation");
        assert(got.tracks[1].keys[0].interp == InterpMode::Step);
        assert(Near(got.tracks[1].keys[1].value.y, 90.0f));
        std::fprintf(stdout, "  [PASS] clip → JSON → clip 全字段 round-trip\n");
    }

    // ===== 3. 空 clip round-trip（0 track）=====
    {
        Anim::AnimationClip empty;
        empty.name = "empty";
        auto parsed = Anim::AnimationClipFromJson(Anim::AnimationClipToJson(empty));
        assert(parsed.IsOk() && parsed.Value().tracks.empty() && "空 clip 也应 round-trip");
        std::fprintf(stdout, "  [PASS] 空 clip round-trip\n");
    }

    // ===== 4. schema 校验：缺 schemaVersion → Err =====
    {
        auto parsed = Anim::AnimationClipFromJson(R"({"name":"x","tracks":[]})");
        assert(parsed.IsErr() && "缺 schemaVersion 应 fail-fast");
        assert(parsed.Error().code == Orange::Engine::ResultCode::SchemaMismatch);
        std::fprintf(stdout, "  [PASS] 缺 schemaVersion → SchemaMismatch\n");
    }

    // ===== 5. schema 校验：错 namespace → Err =====
    {
        const char* wrongNs =
            R"({"schemaVersion":{"namespace":"scene/Transform","major":1,"minor":0},"tracks":[]})";
        auto parsed = Anim::AnimationClipFromJson(wrongNs);
        assert(parsed.IsErr() && "namespace 不匹配应拒绝");
        std::fprintf(stdout, "  [PASS] 错 namespace → 拒绝\n");
    }

    // ===== 6. 非法 JSON → Err（不崩）=====
    {
        auto parsed = Anim::AnimationClipFromJson("{ not valid json ");
        assert(parsed.IsErr() && "非法 JSON 应返回 Err");
        std::fprintf(stdout, "  [PASS] 非法 JSON → Err\n");
    }

    // ===== 7. 未知 enum 串 fail-soft（落默认）=====
    {
        const char* unknownEnum =
            R"({"schemaVersion":{"namespace":"animation/Clip","major":1,"minor":0},)"
            R"("name":"u","duration":1.0,"loop":false,"tracks":[)"
            R"({"targetName":"position","valueType":"Matrix9000","keys":[)"
            R"({"time":0.0,"value":[1,2,3,0],"interp":"Quantum","inTangent":[0,0],"outTangent":[0,0]}]}]})";
        auto parsed = Anim::AnimationClipFromJson(unknownEnum);
        assert(parsed.IsOk() && "未知 enum 串不应整体失败");
        const auto& c = parsed.Value();
        assert(c.tracks.size() == 1);
        assert(c.tracks[0].valueType == TrackValueType::Float && "未知 valueType 落默认 Float");
        assert(c.tracks[0].keys[0].interp == InterpMode::Linear && "未知 interp 落默认 Linear");
        assert(Near(c.tracks[0].keys[0].value.x, 1.0f) && "value 仍正确读出");
        std::fprintf(stdout, "  [PASS] 未知 enum 串 fail-soft\n");
    }

    // ===== 8. 文件 Save / Load round-trip =====
    {
        Anim::AnimationClip clip;
        clip.name     = "file_clip";
        clip.duration = 1.0f;
        Anim::AnimationTrack t;
        t.targetName = "scale.uniform";
        t.valueType  = TrackValueType::Float;
        t.keys.push_back(MakeKey(0.0f, glm::vec4(1, 0, 0, 0), InterpMode::Linear));
        t.keys.push_back(MakeKey(1.0f, glm::vec4(2, 0, 0, 0), InterpMode::Linear));
        clip.tracks.push_back(t);

        const std::string path = "clip_serialization_test_tmp.anim";
        auto saveRes = Anim::SaveAnimationClip(clip, path);
        assert(saveRes.IsOk() && "Save 应成功");

        auto loadRes = Anim::LoadAnimationClip(path);
        assert(loadRes.IsOk() && "Load 应成功");
        const auto& c = loadRes.Value();
        assert(c.name == "file_clip");
        assert(c.tracks.size() == 1 && c.tracks[0].targetName == "scale.uniform");
        assert(Near(c.tracks[0].keys[1].value.x, 2.0f));

        std::remove(path.c_str());
        std::fprintf(stdout, "  [PASS] 文件 Save / Load round-trip\n");
    }

    // ===== 9. events round-trip（schema minor 1）=====
    {
        Anim::AnimationClip clip;
        clip.name     = "with_events";
        clip.duration = 2.0f;
        clip.events.push_back(Anim::AnimationEvent{0.5f, "hit"});
        clip.events.push_back(Anim::AnimationEvent{1.5f, "recover"});

        auto parsed = Anim::AnimationClipFromJson(Anim::AnimationClipToJson(clip));
        assert(parsed.IsOk());
        const auto& got = parsed.Value();
        assert(got.events.size() == 2);
        assert(Near(got.events[0].time, 0.5f) && got.events[0].name == "hit");
        assert(Near(got.events[1].time, 1.5f) && got.events[1].name == "recover");
        std::fprintf(stdout, "  [PASS] events round-trip\n");
    }

    // ===== 10. 旧 minor 0 文件（无 events）→ 读为空（向后兼容）=====
    {
        const char* oldFile =
            R"({"schemaVersion":{"namespace":"animation/Clip","major":1,"minor":0},)"
            R"("name":"legacy","duration":1.0,"loop":false,"tracks":[]})";
        auto parsed = Anim::AnimationClipFromJson(oldFile);
        assert(parsed.IsOk() && "minor 0 文件应被 minor 1 reader 接受");
        assert(parsed.Value().events.empty() && "无 events 字段 → 空");
        std::fprintf(stdout, "  [PASS] 旧 minor 0 文件向后兼容（events 空）\n");
    }

    std::fprintf(stdout, "AnimationClipSerializationTest: all passed\n");
    return 0;
}
