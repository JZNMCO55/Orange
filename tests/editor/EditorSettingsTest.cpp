// EditorSettings JSON 序列化往返单测（gap 报告 §5 "Settings JSON 往返可单测"）。
//
// EditorSettings 序列化此前零单测，而本 session 给它加了 autosave 段
// （schema minor 2）；补一个 round-trip + forward-compat 测试锁住契约：
//   1. 全字段非默认值 Write → Read 往返等价（gizmo / 视口 / autosave）；
//   2. 空 JSON Read → 全字段保留 struct 默认（"老文件缺字段走默认"前向兼容）。
//
// 同 material_file_io_test / editor_hierarchy_test 模式：EditorSettings.cpp 不属
// 引擎 lib，直接编进本测试 exe + 加 tools/OrangeEditor include path。仅依赖
// Core::Serialization + glm，零 ImGui / GLFW / Vulkan。

#include "context/EditorSettings.h"

#include <orange/engine/core/Serialization.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using Orange::Engine::JsonReader;
using Orange::Engine::JsonWriter;

namespace
{

bool FloatEq(float a, float b)
{
    return std::fabs(a - b) < 1e-6f;
}

bool Vec4Eq(const glm::vec4& a, const glm::vec4& b)
{
    return FloatEq(a.x, b.x) && FloatEq(a.y, b.y)
        && FloatEq(a.z, b.z) && FloatEq(a.w, b.w);
}

bool Vec3Eq(const glm::vec3& a, const glm::vec3& b)
{
    return FloatEq(a.x, b.x) && FloatEq(a.y, b.y) && FloatEq(a.z, b.z);
}

// 全字段往返：设非默认值（用 2 的幂 / 半数等可精确表示的 float，避免
// float→double→float 精度抖动），Write → Read 后逐字段比对。
void TestRoundTrip()
{
    EditorSettings s;
    s.gizmoLineWidthTranslateIdle      = 1.5f;
    s.gizmoLineWidthTranslateHighlight = 2.5f;
    s.gizmoLineWidthRotateIdle         = 3.5f;
    s.gizmoLineWidthRotateHighlight    = 4.5f;
    s.gizmoLineWidthScaleIdle          = 5.5f;
    s.gizmoLineWidthScaleHighlight     = 6.5f;
    s.gizmoHandleScreenLengthPx        = 120.0f;
    s.gizmoHitThresholdPx              = 10.0f;
    s.gizmoColorXIdle      = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);
    s.gizmoColorXHighlight = glm::vec4(0.5f, 0.25f, 0.125f, 0.875f);
    s.gizmoColorYIdle      = glm::vec4(0.125f, 0.25f, 0.5f, 1.0f);
    s.gizmoColorYHighlight = glm::vec4(0.75f, 0.5f, 0.25f, 0.5f);
    s.gizmoColorZIdle      = glm::vec4(0.0f, 0.5f, 1.0f, 1.0f);
    s.gizmoColorZHighlight = glm::vec4(1.0f, 0.0f, 0.5f, 0.25f);
    s.viewportGridEnabled      = false;  // 默认 true → 翻
    s.viewportSkyEnabled       = false;  // 默认 true → 翻
    s.viewportDebugDrawEnabled = true;   // 默认 false → 翻
    s.viewportCollidersEnabled = false;  // 默认 true → 翻
    s.autosaveEnabled            = false;  // 默认 true → 翻
    s.autosaveIntervalSeconds    = 240.0f;
    s.autosaveMinIntervalSeconds = 45.0f;
    // 相机书签：slot 0 全字段非默认 + valid；slot 2 仅 valid + radius；slot 1/3
    // 留默认（valid=false）以验证 per-slot 独立 + 未设槽不被误置 valid。
    auto& bm0       = s.cameraBookmarks[0];
    bm0.pivot       = glm::vec3(1.0f, 2.0f, 3.0f);
    bm0.azimuth     = 0.5f;
    bm0.elevation   = 0.25f;
    bm0.radius      = 12.0f;
    bm0.fovYDegrees = 60.0f;
    bm0.zNear       = 0.5f;
    bm0.zFar        = 200.0f;
    bm0.valid       = true;
    s.cameraBookmarks[2].valid  = true;
    s.cameraBookmarks[2].radius = 4.0f;

    JsonWriter writer;
    WriteEditorSettings(writer, s);

    auto readerResult = JsonReader::FromString(writer.Dump());
    assert(readerResult.IsOk());
    EditorSettings out;  // 起始全默认
    ReadEditorSettings(readerResult.Value(), out);

    assert(FloatEq(out.gizmoLineWidthTranslateIdle,      s.gizmoLineWidthTranslateIdle));
    assert(FloatEq(out.gizmoLineWidthTranslateHighlight, s.gizmoLineWidthTranslateHighlight));
    assert(FloatEq(out.gizmoLineWidthRotateIdle,         s.gizmoLineWidthRotateIdle));
    assert(FloatEq(out.gizmoLineWidthRotateHighlight,    s.gizmoLineWidthRotateHighlight));
    assert(FloatEq(out.gizmoLineWidthScaleIdle,          s.gizmoLineWidthScaleIdle));
    assert(FloatEq(out.gizmoLineWidthScaleHighlight,     s.gizmoLineWidthScaleHighlight));
    assert(FloatEq(out.gizmoHandleScreenLengthPx,        s.gizmoHandleScreenLengthPx));
    assert(FloatEq(out.gizmoHitThresholdPx,              s.gizmoHitThresholdPx));
    assert(Vec4Eq(out.gizmoColorXIdle,      s.gizmoColorXIdle));
    assert(Vec4Eq(out.gizmoColorXHighlight, s.gizmoColorXHighlight));
    assert(Vec4Eq(out.gizmoColorYIdle,      s.gizmoColorYIdle));
    assert(Vec4Eq(out.gizmoColorYHighlight, s.gizmoColorYHighlight));
    assert(Vec4Eq(out.gizmoColorZIdle,      s.gizmoColorZIdle));
    assert(Vec4Eq(out.gizmoColorZHighlight, s.gizmoColorZHighlight));
    assert(out.viewportGridEnabled      == s.viewportGridEnabled);
    assert(out.viewportSkyEnabled       == s.viewportSkyEnabled);
    assert(out.viewportDebugDrawEnabled == s.viewportDebugDrawEnabled);
    assert(out.viewportCollidersEnabled == s.viewportCollidersEnabled);
    assert(out.autosaveEnabled == s.autosaveEnabled);
    assert(FloatEq(out.autosaveIntervalSeconds,    s.autosaveIntervalSeconds));
    assert(FloatEq(out.autosaveMinIntervalSeconds, s.autosaveMinIntervalSeconds));

    // 相机书签：slot 0 全字段；slot 2 部分 + valid；slot 1/3 未设保持 valid=false。
    const auto& o0 = out.cameraBookmarks[0];
    assert(o0.valid == true);
    assert(Vec3Eq(o0.pivot, s.cameraBookmarks[0].pivot));
    assert(FloatEq(o0.azimuth,     s.cameraBookmarks[0].azimuth));
    assert(FloatEq(o0.elevation,   s.cameraBookmarks[0].elevation));
    assert(FloatEq(o0.radius,      s.cameraBookmarks[0].radius));
    assert(FloatEq(o0.fovYDegrees, s.cameraBookmarks[0].fovYDegrees));
    assert(FloatEq(o0.zNear,       s.cameraBookmarks[0].zNear));
    assert(FloatEq(o0.zFar,        s.cameraBookmarks[0].zFar));
    assert(out.cameraBookmarks[1].valid == false);
    assert(out.cameraBookmarks[2].valid == true);
    assert(FloatEq(out.cameraBookmarks[2].radius, 4.0f));
    assert(out.cameraBookmarks[3].valid == false);

    std::fprintf(stdout, "  [PASS] EditorSettings round-trip\n");
}

// 前向兼容：空 JSON（无任何 settings 字段，模拟老 / 损坏文件）Read 后所有
// 字段保留 struct 默认值——ReadBool/ReadFloat 严格读，缺字段不改写 out。
void TestMissingFieldsKeepDefaults()
{
    auto readerResult = JsonReader::FromString("{}");
    assert(readerResult.IsOk());

    const EditorSettings defaults;  // struct 默认
    EditorSettings out;
    ReadEditorSettings(readerResult.Value(), out);

    // 抽样代表字段（gizmo / 视口 / autosave 各一）确认默认保留。
    assert(FloatEq(out.gizmoLineWidthTranslateIdle, defaults.gizmoLineWidthTranslateIdle));
    assert(out.viewportGridEnabled      == defaults.viewportGridEnabled);       // true
    assert(out.viewportDebugDrawEnabled == defaults.viewportDebugDrawEnabled);  // false
    assert(out.autosaveEnabled          == defaults.autosaveEnabled);           // true
    assert(FloatEq(out.autosaveIntervalSeconds, defaults.autosaveIntervalSeconds));

    std::fprintf(stdout, "  [PASS] EditorSettings missing-fields keep defaults\n");
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[EditorSettingsTest] running\n");
    TestRoundTrip();
    TestMissingFieldsKeepDefaults();
    std::fprintf(stdout, "[EditorSettingsTest] all tests passed.\n");
    return 0;
}
