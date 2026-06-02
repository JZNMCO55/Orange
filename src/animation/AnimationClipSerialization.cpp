// AnimationClip .anim JSON 序列化实现 —— 见同名 .h 头注释。

#include "orange/engine/animation/AnimationClipSerialization.h"

#include <string>
#include <utility>

namespace Orange::Engine::Animation
{

namespace
{

constexpr std::string_view kSchemaVersionPath = "schemaVersion";

// reader 端期望 schema（namespace bit-for-bit + major 硬墙 + minor 向后兼容）。
const SchemaVersion& ExpectedSchema()
{
    static const SchemaVersion kVersion{kAnimationClipSchemaNamespace, kAnimationClipSchemaMajor,
                                        kAnimationClipSchemaMinor};
    return kVersion;
}

// path 段拼接：base + "/" + leaf。
std::string Join(const std::string& base, std::string_view leaf)
{
    std::string s = base;
    s += '/';
    s.append(leaf.data(), leaf.size());
    return s;
}

// 把 clip 写进一个 JsonWriter —— ToJson / SaveAnimationClip 共用，避免双份漂移。
void WriteClipToWriter(JsonWriter& writer, const AnimationClip& clip)
{
    writer.WriteSchemaVersion(kSchemaVersionPath, ExpectedSchema());
    writer.WriteString("name", clip.name);
    writer.WriteFloat("duration", static_cast<double>(clip.duration));
    writer.WriteBool("loop", clip.loop);

    writer.BeginArray("tracks", clip.tracks.size());
    for (std::size_t i = 0; i < clip.tracks.size(); ++i)
    {
        const AnimationTrack& track = clip.tracks[i];
        const std::string     tp    = "tracks/" + std::to_string(i);

        writer.WriteString(Join(tp, "targetName"), track.targetName);
        writer.WriteString(Join(tp, "valueType"), ToString(track.valueType));

        const std::string keysPath = Join(tp, "keys");
        writer.BeginArray(keysPath, track.keys.size());
        for (std::size_t j = 0; j < track.keys.size(); ++j)
        {
            const Keyframe&   key = track.keys[j];
            const std::string kp  = keysPath + "/" + std::to_string(j);

            writer.WriteFloat(Join(kp, "time"), static_cast<double>(key.time));
            writer.WriteFloatArray(Join(kp, "value"), &key.value.x, 4);
            writer.WriteString(Join(kp, "interp"), ToString(key.interp));
            writer.WriteFloatArray(Join(kp, "inTangent"), &key.inTangent.x, 2);
            writer.WriteFloatArray(Join(kp, "outTangent"), &key.outTangent.x, 2);
        }
    }

    // events（schema minor 1）。
    writer.BeginArray("events", clip.events.size());
    for (std::size_t i = 0; i < clip.events.size(); ++i)
    {
        const AnimationEvent& ev = clip.events[i];
        const std::string     ep = "events/" + std::to_string(i);
        writer.WriteFloat(Join(ep, "time"), static_cast<double>(ev.time));
        writer.WriteString(Join(ep, "name"), ev.name);
    }
}

// 从已解析的 reader 读出 clip —— FromJson / LoadAnimationClip 共用。
// schema 校验已由调用方先行做过（此处只读 payload）。
AnimationClip ParseClipPayload(const JsonReader& reader)
{
    AnimationClip clip;
    clip.name     = reader.GetString("name", "");
    clip.duration = static_cast<float>(reader.GetFloat("duration", 0.0));
    clip.loop     = reader.GetBool("loop", false);

    const std::size_t trackCount = reader.ArraySize("tracks");
    clip.tracks.reserve(trackCount);
    for (std::size_t i = 0; i < trackCount; ++i)
    {
        const std::string tp = "tracks/" + std::to_string(i);

        AnimationTrack track;
        track.targetName = reader.GetString(Join(tp, "targetName"), "");

        // 未知 valueType 串 fail-soft：保留默认 Float。
        std::string vtStr;
        if (reader.ReadString(Join(tp, "valueType"), vtStr))
        {
            TrackValueType vt = TrackValueType::Float;
            if (TrackValueTypeFromString(vtStr, vt)) { track.valueType = vt; }
        }

        const std::string keysPath = Join(tp, "keys");
        const std::size_t keyCount = reader.ArraySize(keysPath);
        track.keys.reserve(keyCount);
        for (std::size_t j = 0; j < keyCount; ++j)
        {
            const std::string kp = keysPath + "/" + std::to_string(j);

            Keyframe key;
            key.time = static_cast<float>(reader.GetFloat(Join(kp, "time"), 0.0));
            reader.ReadFloatArray(Join(kp, "value"), &key.value.x, 4);

            std::string interpStr;
            if (reader.ReadString(Join(kp, "interp"), interpStr))
            {
                InterpMode mode = InterpMode::Linear;
                if (InterpModeFromString(interpStr, mode)) { key.interp = mode; }
            }

            reader.ReadFloatArray(Join(kp, "inTangent"), &key.inTangent.x, 2);
            reader.ReadFloatArray(Join(kp, "outTangent"), &key.outTangent.x, 2);

            track.keys.push_back(key);
        }

        clip.tracks.push_back(std::move(track));
    }

    // events（schema minor 1；旧 minor 0 文件无此字段 → ArraySize 0 → 空）。
    const std::size_t eventCount = reader.ArraySize("events");
    clip.events.reserve(eventCount);
    for (std::size_t i = 0; i < eventCount; ++i)
    {
        const std::string ep = "events/" + std::to_string(i);
        AnimationEvent    ev;
        ev.time = static_cast<float>(reader.GetFloat(Join(ep, "time"), 0.0));
        ev.name = reader.GetString(Join(ep, "name"), "");
        clip.events.push_back(std::move(ev));
    }

    return clip;
}

// schema 校验（缺失 / 不兼容 → 填 err 返回 false）。
bool ValidateSchema(const JsonReader& reader, ParseError& err)
{
    auto verResult = reader.ReadSchemaVersion(kSchemaVersionPath);
    if (verResult.IsErr())
    {
        err.code    = ResultCode::SchemaMismatch;
        err.path    = std::string(kSchemaVersionPath);
        err.message = "AnimationClip: 缺少或非法的 schemaVersion";
        return false;
    }
    if (!ExpectedSchema().CanRead(verResult.Value()))
    {
        err.code    = ResultCode::SchemaMismatch;
        err.path    = std::string(kSchemaVersionPath);
        err.message = "AnimationClip: schemaVersion 不兼容（namespace/major 不匹配）";
        return false;
    }
    return true;
}

}  // namespace

std::string_view ToString(TrackValueType type) noexcept
{
    switch (type)
    {
        case TrackValueType::Float: return "Float";
        case TrackValueType::Vec2:  return "Vec2";
        case TrackValueType::Vec3:  return "Vec3";
        case TrackValueType::Vec4:  return "Vec4";
    }
    return "Float";
}

std::string_view ToString(InterpMode mode) noexcept
{
    switch (mode)
    {
        case InterpMode::Step:   return "Step";
        case InterpMode::Linear: return "Linear";
        case InterpMode::Bezier: return "Bezier";
    }
    return "Linear";
}

bool TrackValueTypeFromString(std::string_view text, TrackValueType& out) noexcept
{
    if (text == "Float") { out = TrackValueType::Float; return true; }
    if (text == "Vec2")  { out = TrackValueType::Vec2;  return true; }
    if (text == "Vec3")  { out = TrackValueType::Vec3;  return true; }
    if (text == "Vec4")  { out = TrackValueType::Vec4;  return true; }
    return false;
}

bool InterpModeFromString(std::string_view text, InterpMode& out) noexcept
{
    if (text == "Step")   { out = InterpMode::Step;   return true; }
    if (text == "Linear") { out = InterpMode::Linear; return true; }
    if (text == "Bezier") { out = InterpMode::Bezier; return true; }
    return false;
}

std::string AnimationClipToJson(const AnimationClip& clip, int indent)
{
    JsonWriter writer;
    WriteClipToWriter(writer, clip);
    return writer.Dump(indent);
}

Result<AnimationClip, ParseError> AnimationClipFromJson(std::string_view jsonText)
{
    auto readerResult = JsonReader::FromString(jsonText);
    if (readerResult.IsErr())
    {
        return readerResult.Error();
    }
    const JsonReader& reader = readerResult.Value();

    ParseError err;
    if (!ValidateSchema(reader, err))
    {
        return err;
    }
    return ParseClipPayload(reader);
}

Result<void, ResultCode> SaveAnimationClip(const AnimationClip& clip, std::string_view path)
{
    JsonWriter writer;
    WriteClipToWriter(writer, clip);
    return writer.SaveToFile(path, 2);
}

Result<AnimationClip, ParseError> LoadAnimationClip(std::string_view path)
{
    auto readerResult = JsonReader::FromFile(path);
    if (readerResult.IsErr())
    {
        return readerResult.Error();
    }
    const JsonReader& reader = readerResult.Value();

    ParseError err;
    if (!ValidateSchema(reader, err))
    {
        return err;
    }
    return ParseClipPayload(reader);
}

}  // namespace Orange::Engine::Animation
