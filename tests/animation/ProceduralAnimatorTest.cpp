// ProceduralAnimatorTest —— Phase 4 / Task 04 验收：
// ProceduralAnimator 把"时间 → uniform"曲线挂到 MaterialInstance 上，每
// Tick 写一次。本期 Material UBO 还没上线，验证只看 MaterialInstance 内
// override 表里的值——Pipeline 把表 push 到 GPU 是 Phase 6 的事。
//
// 覆盖：
//   1. AddChannel<float> + Tick(1.0) → GetUniformFloat 返回 fn(1.0)；
//   2. 多 channel（float / vec3）共存互不干扰；
//   3. nullptr target 下 Tick 仍合法（elapsed 推进，但不写 uniform）；
//   4. SetTarget 切换目标后继续工作；
//   5. ChannelCount / ElapsedSeconds / ResetElapsed 行为正确。

#include "orange/engine/animation/AnimationClip.h"
#include "orange/engine/animation/ProceduralAnimator.h"
#include "orange/engine/render/Material.h"
#include "orange/engine/render/MaterialInstance.h"
#include "orange/engine/render/MaterialTypes.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>

namespace Ani = Orange::Engine::Animation;
namespace Rd  = Orange::Engine::Render;

namespace
{

// 构造一个有 "noise_amp"(float) + "tint"(vec3) 两个 uniform 的 Material。
// shader handle 不填——本测不渲染，Material 只用作 MaterialInstance 的
// uniform 描述源（SetUniform 按 name 在 uniforms[] 里查）。
Rd::Material MakeTwoSlotMaterial()
{
    Rd::Material m;
    m.name = "test/proc";
    m.uniforms = {
        {"noise_amp", Rd::MaterialUniformType::Float},
        {"tint",      Rd::MaterialUniformType::Vec3},
    };
    return m;
}

void TestFloatChannelDrivesUniform()
{
    auto material = MakeTwoSlotMaterial();
    Rd::MaterialInstance mi(&material);

    Ani::ProceduralAnimator anim(&mi);
    anim.AddChannel<float>("noise_amp",
                           [](float t) { return std::sin(t * 2.0f); });
    assert(anim.ChannelCount() == 1);

    // Tick(0)：fn(0) = sin(0) = 0
    anim.Tick(0.0f);
    auto v0 = mi.GetUniformFloat("noise_amp");
    assert(v0.has_value());
    assert(std::fabs(*v0) < 1e-5f);

    // Tick(1.0)：累计到 1.0；fn(1.0) = sin(2.0) ≈ 0.9093
    anim.Tick(1.0f);
    auto v1 = mi.GetUniformFloat("noise_amp");
    assert(v1.has_value());
    const float expected1 = std::sin(2.0f);
    if (std::fabs(*v1 - expected1) > 1e-4f)
    {
        std::fprintf(stderr, "[ProceduralAnimatorTest] noise_amp@t=1: got %.5f expected %.5f\n",
                     *v1, expected1);
    }
    assert(std::fabs(*v1 - expected1) <= 1e-4f);

    // 再 Tick(0.5)：累计到 1.5；fn(1.5) = sin(3.0) ≈ 0.1411
    anim.Tick(0.5f);
    auto v2 = mi.GetUniformFloat("noise_amp");
    const float expected2 = std::sin(3.0f);
    assert(std::fabs(*v2 - expected2) <= 1e-4f);

    assert(std::fabs(anim.ElapsedSeconds() - 1.5f) < 1e-5f);
}

void TestMultipleChannelsIndependent()
{
    auto material = MakeTwoSlotMaterial();
    Rd::MaterialInstance mi(&material);

    Ani::ProceduralAnimator anim(&mi);
    anim.AddChannel<float>("noise_amp",
                           [](float t) { return t * 2.0f; });
    anim.AddChannel<glm::vec3>("tint",
                               [](float t) {
                                   return glm::vec3{t, -t, 0.5f};
                               });
    assert(anim.ChannelCount() == 2);

    anim.Tick(0.25f);

    auto fv = mi.GetUniformFloat("noise_amp");
    auto v3 = mi.GetUniformVec3("tint");
    assert(fv.has_value());
    assert(v3.has_value());

    assert(std::fabs(*fv - 0.5f) <= 1e-5f);          // 0.25 * 2
    assert(std::fabs(v3->x - 0.25f) <= 1e-5f);
    assert(std::fabs(v3->y + 0.25f) <= 1e-5f);
    assert(std::fabs(v3->z - 0.5f)  <= 1e-5f);
}

void TestNullTargetSafe()
{
    Ani::ProceduralAnimator anim(nullptr);
    anim.AddChannel<float>("noise_amp", [](float t) { return t; });

    // 不崩，elapsed 正常推进。
    anim.Tick(0.5f);
    anim.Tick(0.25f);
    assert(std::fabs(anim.ElapsedSeconds() - 0.75f) < 1e-5f);

    // SetTarget 接上一个真 mi 后，下一次 Tick 应能写出当前 elapsed。
    auto material = MakeTwoSlotMaterial();
    Rd::MaterialInstance mi(&material);
    anim.SetTarget(&mi);
    anim.Tick(0.0f);  // dt=0：elapsed 不变 = 0.75，fn(0.75) = 0.75
    auto v = mi.GetUniformFloat("noise_amp");
    assert(v.has_value());
    assert(std::fabs(*v - 0.75f) <= 1e-5f);
}

void TestResetAndClear()
{
    auto material = MakeTwoSlotMaterial();
    Rd::MaterialInstance mi(&material);
    Ani::ProceduralAnimator anim(&mi);
    anim.AddChannel<float>("noise_amp", [](float t) { return t; });
    anim.Tick(2.0f);
    assert(std::fabs(anim.ElapsedSeconds() - 2.0f) < 1e-5f);

    anim.ResetElapsed();
    assert(anim.ElapsedSeconds() == 0.0f);
    anim.Tick(0.1f);
    auto v = mi.GetUniformFloat("noise_amp");
    assert(v.has_value());
    assert(std::fabs(*v - 0.1f) <= 1e-5f);

    anim.ClearChannels();
    assert(anim.ChannelCount() == 0);
    // 清空之后 Tick 不再写新值（旧覆盖保留）；不崩。
    anim.Tick(1.0f);
    auto v2 = mi.GetUniformFloat("noise_amp");
    assert(v2.has_value());
    assert(std::fabs(*v2 - 0.1f) <= 1e-5f);  // 没被覆盖
}

void TestIAnimatorContract()
{
    auto material = MakeTwoSlotMaterial();
    Rd::MaterialInstance mi(&material);
    Ani::ProceduralAnimator anim(&mi);

    // procedural 永远 not finished
    assert(!anim.IsFinished());
    anim.Tick(1000.0f);
    assert(!anim.IsFinished());
    assert(anim.BackendName() == "procedural");

    // 负 dt → no-op（不前进）
    const float before = anim.ElapsedSeconds();
    anim.Tick(-1.0f);
    assert(anim.ElapsedSeconds() == before);
}

// B2.1：AddDataChannel 把 AnimationTrack 的关键帧采样曲线驱动 uniform
//（数据 channel 与 lambda channel 并存、同一 Tick）。
void TestDataChannelFromTrack()
{
    auto material = MakeTwoSlotMaterial();
    Rd::MaterialInstance mi(&material);
    Ani::ProceduralAnimator anim(&mi);

    // Float track：t=0→0, t=2→100（线性）。
    Ani::AnimationTrack ftrack;
    ftrack.valueType = Ani::TrackValueType::Float;
    Ani::Keyframe fk0; fk0.time = 0.0f; fk0.value = glm::vec4(0.0f);
    fk0.interp = Ani::InterpMode::Linear;
    Ani::Keyframe fk1; fk1.time = 2.0f; fk1.value = glm::vec4(100.0f, 0, 0, 0);
    fk1.interp = Ani::InterpMode::Linear;
    ftrack.keys = {fk0, fk1};

    // Vec3 track：t=0→(0,0,0), t=2→(2,4,6)（线性）。
    Ani::AnimationTrack vtrack;
    vtrack.valueType = Ani::TrackValueType::Vec3;
    Ani::Keyframe vk0; vk0.time = 0.0f; vk0.value = glm::vec4(0.0f);
    vk0.interp = Ani::InterpMode::Linear;
    Ani::Keyframe vk1; vk1.time = 2.0f; vk1.value = glm::vec4(2, 4, 6, 0);
    vk1.interp = Ani::InterpMode::Linear;
    vtrack.keys = {vk0, vk1};

    anim.AddDataChannel("noise_amp", ftrack);
    anim.AddDataChannel("tint", vtrack);
    assert(anim.ChannelCount() == 2);

    // Tick 到 elapsed=1.0（两 track 区间中点）→ noise_amp=50, tint=(1,2,3)。
    anim.Tick(1.0f);
    auto fv = mi.GetUniformFloat("noise_amp");
    auto vv = mi.GetUniformVec3("tint");
    assert(fv.has_value() && std::fabs(*fv - 50.0f) <= 1e-3f &&
           "Float 数据 channel t=1 → 线性中点 50");
    assert(vv.has_value() &&
           std::fabs(vv->x - 1.0f) <= 1e-3f &&
           std::fabs(vv->y - 2.0f) <= 1e-3f &&
           std::fabs(vv->z - 3.0f) <= 1e-3f &&
           "Vec3 数据 channel t=1 → (1,2,3)");

    // clamp：Tick 过末 key（elapsed=3.0 > duration 2.0）→ 末 key 值。
    anim.Tick(2.0f);
    auto fv2 = mi.GetUniformFloat("noise_amp");
    assert(fv2.has_value() && std::fabs(*fv2 - 100.0f) <= 1e-3f &&
           "数据 channel 超末 key → clamp 到末值 100");
}

}  // namespace

int main()
{
    TestFloatChannelDrivesUniform();
    TestMultipleChannelsIndependent();
    TestNullTargetSafe();
    TestResetAndClear();
    TestIAnimatorContract();
    TestDataChannelFromTrack();
    return 0;
}
