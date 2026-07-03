#ifndef ORANGE_ENGINE_PARTICLE_PARTICLE_SYSTEM_H
#define ORANGE_ENGINE_PARTICLE_PARTICLE_SYSTEM_H

// ---------------------------------------------------------------------------
// ParticleSystem —— 2D CPU 粒子发射器 + 仿真核 (确定性)，header-only。
//
// 一个固定容量的粒子池 + 一份 EmitterConfig：Emit(n) 爆发发射、Update(dt) 连续
// 发射 + 半隐式欧拉积分 (重力 + 指数阻尼) + 寿命淘汰。淘汰用 swap-remove 把存活
// 粒子始终紧凑保持在池前部 [0,AliveCount)，对渲染遍历 cache 友好。
//
// 随机源是复用的 Noise::Rng (splitmix64)：同 seed + 同 (Emit/Update) 调用序 →
// 逐粒子状态逐字节可复现，故 headless 完全可测 (无 GPU/GLFW)。渲染是消费者的事
// (读 Data()/AliveCount() 各自画 quad/sprite)，本模块只管仿真——与 camerarig /
// nav 等一样纯数据+数学、可被任何 2D 游戏复用。
//
// 发射时的 RNG 抽取顺序 (决定确定性对齐)：位置 → 速度角 → 速率 → 寿命 →
// 尺寸抖动 → 初始旋转 → 角速度。退化区间 (max<=min) 返回 min 且不抽取。
// ---------------------------------------------------------------------------

#include <orange/engine/noise/Rng.h>
#include <orange/engine/particle/Particle.h>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Orange::Engine::Particle
{

    namespace Detail
    {
        constexpr float kPi    = 3.14159265358979323846f;
        constexpr float kTwoPi = 2.0f * kPi;
    } // namespace Detail

    // 发射器初始位置形状。
    enum class EmitShape : std::uint8_t
    {
        Point, // 全部从原点发射
        Disk,  // 半径 shapeRadius 的填充圆盘内 (面积) 均匀
        Box,   // 半尺寸 shapeHalfExtent 的矩形内均匀
    };

    // 发射器配置。所有字段有中性默认值：Point 形状 + 零速 + 1s 寿命 + 无力 + 无连续发射。
    struct EmitterConfig
    {
        // —— 初始位置 ——
        EmitShape shape       = EmitShape::Point;
        float     shapeRadius = 0.0f;          // Disk 用
        glm::vec2 shapeHalfExtent{0.0f, 0.0f}; // Box 用

        // —— 初始速度 (极坐标：基准方向 ± 半扩散角，速率区间) ——
        float directionAngle = 0.0f; // 基准发射方向弧度 (0 = +X)
        float spreadAngle    = 0.0f; // 半扩散角弧度 (取 abs)；π = 全向
        float speedMin       = 0.0f;
        float speedMax       = 0.0f;

        // —— 寿命区间 (秒) ——
        float lifetimeMin = 1.0f;
        float lifetimeMax = 1.0f;

        // —— 受力 ——
        glm::vec2 gravity{0.0f, 0.0f}; // 世界单位/s²
        float     damping = 0.0f;      // 线性阻尼 (1/s)；velocity *= exp(-damping*dt)，帧率无关

        // —— 尺寸随寿命 ——
        float sizeBegin  = 1.0f;
        float sizeEnd    = 0.0f;
        float sizeJitter = 0.0f; // [0,1] 相对抖动：begin/end 同乘 (1 ± jitter)

        // —— 颜色随寿命 (RGBA) ——
        glm::vec4 colorBegin{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};

        // —— 初始旋转 / 自转 ——
        float rotationMin        = 0.0f;
        float rotationMax        = 0.0f;
        float angularVelocityMin = 0.0f;
        float angularVelocityMax = 0.0f;

        // —— 连续发射 ——
        float emitRate = 0.0f; // 每秒发射数；Update 内按 dt 累积整数发射。0 = 仅手动 Emit
    };

    // 2D 粒子系统：持固定容量池 + 配置 + 确定性 RNG + 发射器世界原点。
    class ParticleSystem
    {
    public:
        ParticleSystem() = default;

        explicit ParticleSystem(std::size_t capacity, std::uint64_t seed = 0)
            : mParticles(capacity), mRng(seed)
        {
        }

        // 重设池容量 (预分配)。缩容到小于当前存活数时把存活数一并钳回。
        void Reserve(std::size_t capacity)
        {
            mParticles.resize(capacity);
            if (mAliveCount > capacity)
            {
                mAliveCount = capacity;
            }
        }

        void                 SetConfig(const EmitterConfig& cfg) { mConfig = cfg; }
        const EmitterConfig& GetConfig() const noexcept { return mConfig; }

        // 重置随机源到指定 seed (同 seed → 同发射序列)。
        void SetSeed(std::uint64_t seed) noexcept { mRng = Noise::Rng{seed}; }

        void             SetOrigin(const glm::vec2& origin) noexcept { mOrigin = origin; }
        const glm::vec2& GetOrigin() const noexcept { return mOrigin; }

        std::size_t     Capacity() const noexcept { return mParticles.size(); }
        std::size_t     AliveCount() const noexcept { return mAliveCount; }
        const Particle& GetParticle(std::size_t i) const { return mParticles[i]; }
        const Particle* Data() const noexcept { return mParticles.data(); }

        // 清空所有存活粒子 (保留容量 / 配置 / 种子 / 发射累积清零)。
        void Clear() noexcept
        {
            mAliveCount      = 0;
            mEmitAccumulator = 0.0f;
        }

        // 手动爆发发射 count 个 (受容量上限约束，溢出丢弃)。返回实际发射数。
        std::size_t Emit(std::size_t count)
        {
            std::size_t emitted = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
                if (mAliveCount >= mParticles.size())
                {
                    break; // 池满
                }
                mParticles[mAliveCount] = SpawnOne();
                ++mAliveCount;
                ++emitted;
            }
            return emitted;
        }

        // 推进一帧：连续发射 → 半隐式欧拉积分 (重力 + 阻尼) → 寿命淘汰。
        // dt<=0 直接返回 (暂停帧 / 防除零，不发射不积分)。
        void Update(float dt)
        {
            if (dt <= 0.0f)
            {
                return;
            }

            // —— 连续发射：按速率累积，发射整数个，保留小数余量 (帧率无关) ——
            if (mConfig.emitRate > 0.0f && !mParticles.empty())
            {
                mEmitAccumulator += mConfig.emitRate * dt;
                while (mEmitAccumulator >= 1.0f)
                {
                    if (mAliveCount >= mParticles.size())
                    {
                        // 池满：丢弃剩余累积，避免它无界增长后在腾出空位时暴发补偿。
                        mEmitAccumulator = 0.0f;
                        break;
                    }
                    mEmitAccumulator -= 1.0f;
                    mParticles[mAliveCount] = SpawnOne();
                    ++mAliveCount;
                }
            }

            // —— 积分 + 淘汰 —— 阻尼系数按 dt 指数衰减 (damping<=0 → 1 不衰减)。
            const float dampFactor = mConfig.damping > 0.0f ? std::exp(-mConfig.damping * dt) : 1.0f;

            std::size_t i = 0;
            while (i < mAliveCount)
            {
                Particle& p = mParticles[i];
                p.age += dt;
                if (p.age >= p.lifetime)
                {
                    // swap-remove：把尚未处理的池尾存活粒子换入 i，收缩存活数，重扫 i
                    // (换入者此前在 i 之后、本帧尚未积分，故不 ++i 以对它积分一次)。
                    --mAliveCount;
                    if (i != mAliveCount)
                    {
                        mParticles[i] = mParticles[mAliveCount];
                    }
                    continue;
                }
                p.velocity += mConfig.gravity * dt;
                p.velocity *= dampFactor;
                p.position += p.velocity * dt;
                p.rotation += p.angularVelocity * dt;
                ++i;
            }
        }

    private:
        // 采样 [lo,hi]：退化 (hi<=lo) 返回 lo 且不消耗 RNG (保持确定性对齐的显式约定)。
        float SampleRange(float lo, float hi)
        {
            if (hi <= lo)
            {
                return lo;
            }
            return mRng.NextFloatRange(lo, hi);
        }

        // 按当前配置 + RNG + 原点造一个新粒子。RNG 抽取顺序见文件头注释。
        Particle SpawnOne()
        {
            Particle p{};

            // —— 位置 ——
            switch (mConfig.shape)
            {
                case EmitShape::Point:
                    p.position = mOrigin;
                    break;
                case EmitShape::Disk:
                {
                    // 均匀圆盘：r = R·sqrt(u)，sqrt 保证按面积均匀 (否则圆心过密)。
                    const float r = mConfig.shapeRadius * std::sqrt(mRng.NextFloat());
                    const float a = mRng.NextFloatRange(0.0f, Detail::kTwoPi);
                    p.position    = mOrigin + glm::vec2{r * std::cos(a), r * std::sin(a)};
                    break;
                }
                case EmitShape::Box:
                {
                    const float x = mRng.NextFloatRange(-mConfig.shapeHalfExtent.x, mConfig.shapeHalfExtent.x);
                    const float y = mRng.NextFloatRange(-mConfig.shapeHalfExtent.y, mConfig.shapeHalfExtent.y);
                    p.position    = mOrigin + glm::vec2{x, y};
                    break;
                }
            }

            // —— 速度 (极坐标) ——
            const float spread = std::fabs(mConfig.spreadAngle);
            const float angle  = mConfig.directionAngle + mRng.NextFloatRange(-spread, spread);
            const float speed  = SampleRange(mConfig.speedMin, mConfig.speedMax);
            p.velocity         = glm::vec2{std::cos(angle), std::sin(angle)} * speed;

            // —— 寿命 ——
            p.age      = 0.0f;
            p.lifetime = SampleRange(mConfig.lifetimeMin, mConfig.lifetimeMax);

            // —— 尺寸 (+ 相对抖动) ——
            float sizeScale = 1.0f;
            if (mConfig.sizeJitter > 0.0f)
            {
                const float j = mConfig.sizeJitter;
                sizeScale     = 1.0f + mRng.NextFloatRange(-j, j);
            }
            p.sizeBegin = mConfig.sizeBegin * sizeScale;
            p.sizeEnd   = mConfig.sizeEnd * sizeScale;

            // —— 颜色 (端点直传，随寿命插值在 Particle::ColorAt) ——
            p.colorBegin = mConfig.colorBegin;
            p.colorEnd   = mConfig.colorEnd;

            // —— 旋转 / 自转 ——
            p.rotation        = SampleRange(mConfig.rotationMin, mConfig.rotationMax);
            p.angularVelocity = SampleRange(mConfig.angularVelocityMin, mConfig.angularVelocityMax);

            return p;
        }

        std::vector<Particle> mParticles; // 固定容量池；[0,mAliveCount) 为存活
        std::size_t           mAliveCount = 0;
        EmitterConfig         mConfig{};
        Noise::Rng            mRng{0};
        glm::vec2             mOrigin{0.0f, 0.0f};     // 发射器世界原点
        float                 mEmitAccumulator = 0.0f; // 连续发射的小数余量
    };

} // namespace Orange::Engine::Particle

#endif // ORANGE_ENGINE_PARTICLE_PARTICLE_SYSTEM_H
