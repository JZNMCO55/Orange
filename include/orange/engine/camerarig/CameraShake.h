#ifndef ORANGE_ENGINE_CAMERARIG_CAMERA_SHAKE_H
#define ORANGE_ENGINE_CAMERARIG_CAMERA_SHAKE_H

// ---------------------------------------------------------------------------
// CameraShake2D —— 基于 trauma 的 2D 相机抖动 (juice 原语)。
//
// 经典 "trauma" 模型 (Squirrel Eiserloh)：外部事件 AddTrauma(amount) 往 [0,1] 的 trauma
// 池灌值，trauma 随时间线性衰减；每帧的抖动强度 = trauma^exponent (感知曲线，让抖动尾巴
// 快速收敛而非线性拖沓)。抖动偏移 = 强度 × maxTranslation × 平滑值噪声 (per-axis 不同相位，
// 故不是死板正弦)。旋转 roll 供 2.5D 用 (纯 2D 置 0)。
//
// 纯数据 + 数学：不依赖 Render::Camera / World / GPU，自包含 (内部 hash 值噪声，不依赖 noise
// 模块)。确定性 (同 seed + 同累计时间 → 同偏移)，故 headless 完全可测。消费者把返回的
// translation / roll **叠加**到相机跟随算出的位置上 (与 CameraFollow2D 解耦、可组合)。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <glm/vec2.hpp>

#include <cstdint>

namespace Orange::Engine::CameraRig
{

// 相机抖动参数。默认值给一个"中等爆炸"档 (满 trauma 时抖 0.5 世界单位、1 秒衰减完)。
struct ShakeParams
{
    // 满强度 (shake=1) 时的最大平移偏移 (世界单位，per-axis)。
    glm::vec2 maxTranslation{0.5f, 0.5f};
    // 满强度时的最大 roll (弧度)；2.5D 相机可用，纯 2D 置 0。
    float         maxRoll = 0.0f;
    // trauma 每秒线性衰减量 (1.0 = 满到 0 需 1 秒)。<=0 → 不衰减 (需手动清)。
    float         traumaDecay = 1.0f;
    // 抖动频率 (Hz)，决定值噪声推进速度。越大越"高频神经质"。
    float         frequency = 25.0f;
    // 强度曲线指数：shake = trauma^exponent。2~3 常用 (越大尾巴收得越快)。
    float         traumaExponent = 2.0f;
    // 值噪声相位种子 (不同实例给不同 seed → 抖动不同步)。
    std::uint32_t seed = 1u;
};

// 一帧的抖动输出：平移偏移 + roll，叠加到相机位置 / 朝向上。
struct ShakeOffset
{
    glm::vec2 translation{0.0f, 0.0f};
    float     roll = 0.0f;
};

// 基于 trauma 的相机抖动控制器。
class ORANGE_ENGINE_API CameraShake2D
{
public:
    CameraShake2D() = default;
    explicit CameraShake2D(const ShakeParams& params) : mParams(params) {}

    void               SetParams(const ShakeParams& params) { mParams = params; }
    const ShakeParams& GetParams() const noexcept { return mParams; }

    // 灌 trauma (受伤 / 爆炸 / 落地冲击等触发)；累加后钳到 [0,1]。负值当 0 (不减 trauma)。
    void AddTrauma(float amount) noexcept;
    // 直接置 trauma (钳 [0,1])；SetTrauma(0) = 立即停抖。
    void SetTrauma(float trauma) noexcept;
    float GetTrauma() const noexcept { return mTrauma; }

    // 推进一帧 (dt 秒)：累计时间前进、trauma 衰减，返回当前抖动偏移。
    // trauma=0 时返回零偏移 (无抖动)。dt<=0 时不推进时间 / 不衰减，仍按当前时间返回偏移。
    ShakeOffset Update(float dt) noexcept;

private:
    ShakeParams mParams{};
    float       mTrauma = 0.0f;
    float       mTime   = 0.0f;  // 累计时间 (驱动值噪声相位)
};

} // namespace Orange::Engine::CameraRig

#endif // ORANGE_ENGINE_CAMERARIG_CAMERA_SHAKE_H
