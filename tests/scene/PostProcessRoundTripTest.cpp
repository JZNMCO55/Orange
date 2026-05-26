// PostProcessRoundTripTest —— PostProcessComponent 的 Save / Load 往返字段无丢失
// （schema v1.7 新增 optional component；v1.8 加相机运动模糊字段）。设非默认值 →
// Save → Load → 逐字段比对。

#include <orange/engine/render/PostProcessComponent.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/World.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>

using Orange::Engine::Entity;
using Orange::Engine::World;
namespace Ser = Orange::Engine::Scene;
namespace R   = Orange::Engine::Render;

namespace
{
bool Eq(float a, float b, float eps = 1e-5f) noexcept { return std::fabs(a - b) <= eps; }
}

int main()
{
    const auto path = (std::filesystem::temp_directory_path()
                       / "orange_pp_roundtrip.scene.json").string();
    std::error_code ec;
    std::filesystem::remove(path, ec);

    // —— 构造非默认配置 ——
    R::PostProcessComponent src;
    src.mode          = R::PostProcessComponent::Mode::Local;
    src.localExtent   = {3.0f, 4.0f, 5.0f};
    src.priority      = 2.0f;
    src.blendDistance = 1.5f;
    src.ssaoEnabled = true;  src.ssaoUseGtao = false;
    src.ssaoRadius = 0.42f;  src.ssaoStrength = 0.8f;  src.ssaoPower = 2.5f;
    src.ssrEnabled = false;  src.ssrMaxDistance = 9.0f; src.ssrThickness = 0.4f; src.ssrStrength = 0.3f;
    src.contactEnabled = true; src.contactLength = 0.2f; src.contactThickness = 0.25f; src.contactStrength = 0.7f;
    src.dofEnabled = true; src.dofFocusDistance = 7.5f; src.dofFocusRange = 6.0f; src.dofMaxCoCRadius = 0.02f;
    src.taaEnabled = true; src.taaFeedback = 0.85f;
    src.gradeEnabled = true; src.gradeExposure = 0.3f; src.gradeContrast = 1.2f;
    src.gradeSaturation = 0.9f; src.gradeTemperature = -0.4f; src.gradeTint = 0.1f;
    src.motionBlurEnabled = true; src.motionBlurIntensity = 0.7f;
    src.motionBlurMaxRadius = 0.06f; src.motionBlurSampleCount = 12;
    src.pcssLightSize = 8.0f; src.shadowMapResolution = 4096;

    {
        World w;
        Entity e = w.CreateEntity();
        w.AddComponent(e, src);
        if (Ser::Save(w, path).IsErr())
        {
            std::fprintf(stderr, "PostProcessRoundTripTest: Save 失败\n");
            return 1;
        }
    }

    World loaded;
    if (Ser::Load(path, loaded).IsErr())
    {
        std::fprintf(stderr, "PostProcessRoundTripTest: Load 失败\n");
        return 1;
    }

    // 找到带 PostProcessComponent 的 entity（新 World 的 Entity 数值可能不同）。
    auto& reg = loaded.Registry();
    auto  view = reg.view<R::PostProcessComponent>();
    Entity le = Entity::Invalid();
    int hits = 0;
    for (auto ent : view) { ++hits; le = World::FromEntt(ent); }
    if (hits != 1)
    {
        std::fprintf(stderr, "PostProcessRoundTripTest: 期望 1 个 PostProcess，实际 %d\n", hits);
        return 1;
    }
    const auto* l = loaded.GetComponent<R::PostProcessComponent>(le);
    assert(l != nullptr);

    bool ok = true;
    auto chk = [&](bool cond, const char* what) { if (!cond) { std::fprintf(stderr, "  字段不匹配: %s\n", what); ok = false; } };

    chk(l->mode == R::PostProcessComponent::Mode::Local, "mode");
    chk(Eq(l->localExtent.x,3) && Eq(l->localExtent.y,4) && Eq(l->localExtent.z,5), "localExtent");
    chk(Eq(l->priority,2.0f), "priority");
    chk(Eq(l->blendDistance,1.5f), "blendDistance");
    chk(l->ssaoEnabled && !l->ssaoUseGtao, "ssao flags");
    chk(Eq(l->ssaoRadius,0.42f) && Eq(l->ssaoStrength,0.8f) && Eq(l->ssaoPower,2.5f), "ssao params");
    chk(!l->ssrEnabled, "ssrEnabled");
    chk(Eq(l->ssrMaxDistance,9.0f) && Eq(l->ssrThickness,0.4f) && Eq(l->ssrStrength,0.3f), "ssr params");
    chk(l->contactEnabled, "contactEnabled");
    chk(Eq(l->contactLength,0.2f) && Eq(l->contactThickness,0.25f) && Eq(l->contactStrength,0.7f), "contact params");
    chk(l->dofEnabled, "dofEnabled");
    chk(Eq(l->dofFocusDistance,7.5f) && Eq(l->dofFocusRange,6.0f) && Eq(l->dofMaxCoCRadius,0.02f), "dof params");
    chk(l->taaEnabled && Eq(l->taaFeedback,0.85f), "taa");
    chk(l->gradeEnabled, "gradeEnabled");
    chk(Eq(l->gradeExposure,0.3f) && Eq(l->gradeContrast,1.2f) && Eq(l->gradeSaturation,0.9f)
        && Eq(l->gradeTemperature,-0.4f) && Eq(l->gradeTint,0.1f), "grade params");
    chk(l->motionBlurEnabled, "motionBlurEnabled");
    chk(Eq(l->motionBlurIntensity,0.7f) && Eq(l->motionBlurMaxRadius,0.06f)
        && l->motionBlurSampleCount == 12, "motion blur params");
    chk(Eq(l->pcssLightSize,8.0f), "pcssLightSize");
    chk(l->shadowMapResolution == 4096u, "shadowMapResolution");

    std::filesystem::remove(path, ec);
    if (!ok) { return 1; }
    std::printf("PostProcessRoundTripTest OK\n");
    return 0;
}
