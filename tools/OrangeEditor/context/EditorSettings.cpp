#include "EditorSettings.h"

#include <orange/engine/core/Serialization.h>

#include <string>
#include <string_view>

using Orange::Engine::JsonReader;
using Orange::Engine::JsonWriter;

namespace
{

// 读 4 元素 float 数组，缺失 / size 不对则保留 default
void ReadVec4(const JsonReader& in, std::string_view path, glm::vec4& out)
{
    float buf[4];
    if (in.ReadFloatArray(path, buf, 4))
    {
        out = glm::vec4(buf[0], buf[1], buf[2], buf[3]);
    }
}

void WriteVec4(JsonWriter& out, std::string_view path, const glm::vec4& v)
{
    const float arr[4] = { v.x, v.y, v.z, v.w };
    out.WriteFloatArray(path, arr, 4);
}

// 读 float（用便利接口 GetFloat 自带默认值；本路径下默认值由 caller
// 传入 settings 字段已存在的当前值）
void ReadFloat(const JsonReader& in, std::string_view path, float& out)
{
    double v = static_cast<double>(out);
    if (in.ReadFloat(path, v))
    {
        out = static_cast<float>(v);
    }
}

}  // anonymous namespace

void ReadEditorSettings(const JsonReader& in, EditorSettings& out)
{
    // schema bump 时在 head 检查 schemaVersion 字段；v1.0 暂不校验。
    ReadFloat(in, "gizmo/lineWidth/translateIdle",      out.gizmoLineWidthTranslateIdle);
    ReadFloat(in, "gizmo/lineWidth/translateHighlight", out.gizmoLineWidthTranslateHighlight);
    ReadFloat(in, "gizmo/lineWidth/rotateIdle",         out.gizmoLineWidthRotateIdle);
    ReadFloat(in, "gizmo/lineWidth/rotateHighlight",    out.gizmoLineWidthRotateHighlight);
    ReadFloat(in, "gizmo/lineWidth/scaleIdle",          out.gizmoLineWidthScaleIdle);
    ReadFloat(in, "gizmo/lineWidth/scaleHighlight",     out.gizmoLineWidthScaleHighlight);

    ReadFloat(in, "gizmo/handleScreenLengthPx",         out.gizmoHandleScreenLengthPx);
    ReadFloat(in, "gizmo/hitThresholdPx",               out.gizmoHitThresholdPx);

    ReadVec4(in, "gizmo/color/xIdle",      out.gizmoColorXIdle);
    ReadVec4(in, "gizmo/color/xHighlight", out.gizmoColorXHighlight);
    ReadVec4(in, "gizmo/color/yIdle",      out.gizmoColorYIdle);
    ReadVec4(in, "gizmo/color/yHighlight", out.gizmoColorYHighlight);
    ReadVec4(in, "gizmo/color/zIdle",      out.gizmoColorZIdle);
    ReadVec4(in, "gizmo/color/zHighlight", out.gizmoColorZHighlight);

    // schema minor 1：视口显示开关。缺字段（老文件 / minor 0）时 ReadBool 不
    // 改写、保留 struct 默认值。
    in.ReadBool("viewport/grid",      out.viewportGridEnabled);
    in.ReadBool("viewport/sky",       out.viewportSkyEnabled);
    in.ReadBool("viewport/debugDraw", out.viewportDebugDrawEnabled);
    in.ReadBool("viewport/colliders", out.viewportCollidersEnabled);
}

void WriteEditorSettings(JsonWriter& out, const EditorSettings& s)
{
    out.WriteString("schemaVersion/namespace", "editor/settings");
    out.WriteInt("schemaVersion/major", 1);
    out.WriteInt("schemaVersion/minor", 1);   // minor 1：+视口显示开关

    out.WriteFloat("gizmo/lineWidth/translateIdle",      s.gizmoLineWidthTranslateIdle);
    out.WriteFloat("gizmo/lineWidth/translateHighlight", s.gizmoLineWidthTranslateHighlight);
    out.WriteFloat("gizmo/lineWidth/rotateIdle",         s.gizmoLineWidthRotateIdle);
    out.WriteFloat("gizmo/lineWidth/rotateHighlight",    s.gizmoLineWidthRotateHighlight);
    out.WriteFloat("gizmo/lineWidth/scaleIdle",          s.gizmoLineWidthScaleIdle);
    out.WriteFloat("gizmo/lineWidth/scaleHighlight",     s.gizmoLineWidthScaleHighlight);

    out.WriteFloat("gizmo/handleScreenLengthPx", s.gizmoHandleScreenLengthPx);
    out.WriteFloat("gizmo/hitThresholdPx",       s.gizmoHitThresholdPx);

    WriteVec4(out, "gizmo/color/xIdle",      s.gizmoColorXIdle);
    WriteVec4(out, "gizmo/color/xHighlight", s.gizmoColorXHighlight);
    WriteVec4(out, "gizmo/color/yIdle",      s.gizmoColorYIdle);
    WriteVec4(out, "gizmo/color/yHighlight", s.gizmoColorYHighlight);
    WriteVec4(out, "gizmo/color/zIdle",      s.gizmoColorZIdle);
    WriteVec4(out, "gizmo/color/zHighlight", s.gizmoColorZHighlight);

    out.WriteBool("viewport/grid",      s.viewportGridEnabled);
    out.WriteBool("viewport/sky",       s.viewportSkyEnabled);
    out.WriteBool("viewport/debugDraw", s.viewportDebugDrawEnabled);
    out.WriteBool("viewport/colliders", s.viewportCollidersEnabled);
}
