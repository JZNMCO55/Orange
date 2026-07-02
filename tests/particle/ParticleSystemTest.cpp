// ParticleSystem / Particle 的 headless 单元测试：2D CPU 粒子发射器 + 仿真核。
// 纯数据 + 数学 (复用 Noise::Rng 确定性 + Tween::Easing)，无 World / GPU / GUI。
// 裸 main() + <cassert>。确定性，故可逐帧 / 逐粒子精确断言。
//
// 覆盖点：
//   * Particle 查询 —— NormalizedAge / SizeAt (缓动) / ColorAt (alpha 淡出) / lifetime<=0 守卫。
//   * Emit 爆发 —— AliveCount == min(count, capacity)；溢出丢弃、返回实际发射数；池满再发返 0。
//   * 确定性 —— 同 capacity+seed+config + 同 (Emit/Update) 调用序 → 逐粒子状态完全相同 (含大量死亡后)。
//   * 寿命淘汰 —— 均匀寿命全体在越过寿命的那帧清零。
//   * swap-remove —— 混合寿命两批：短命批死、长命批存活且身份 (lifetime/age) 正确 (验证紧凑压缩不串味)。
//   * 重力积分 —— 半隐式欧拉：Update(dt) 后 v == g·dt、pos == v·dt。
//   * 阻尼 —— 指数衰减 v *= exp(-damping·dt)，速度真变小且吻合闭式。
//   * 连续发射 —— emitRate 按 dt 累积整数发射 + 保留小数余量 (帧率无关：0.5+0.5 才凑一个)。
//   * 形状包含 —— Disk 在半径内 / Box 在半尺寸内 / Point 精确在原点 (含 SetOrigin 偏移)。
//   * 方向速度 —— spread=0 时速度严格沿 directionAngle。
//   * dt<=0 不动 —— 不发射、不积分、不老化。
//   * Clear —— 存活清零。
// 仅 orange_engine + glm + 标准库，headless 完全可验。

#include "orange/engine/particle/Particle.h"
#include "orange/engine/particle/ParticleSystem.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace P = ::Orange::Engine::Particle;
using P::EmitShape;
using P::EmitterConfig;
using P::Particle;
using P::ParticleSystem;
using Ease = ::Orange::Engine::Tween::EaseType;

namespace
{

bool Near(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

int gChecks = 0;
void Check(bool cond, const char* what)
{
    ++gChecks;
    if (!cond)
    {
        std::fprintf(stderr, "[ParticleSystemTest] FAILED: %s\n", what);
        assert(cond);
    }
}

float Len(const glm::vec2& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

bool SameParticle(const Particle& a, const Particle& b)
{
    return Near(a.position.x, b.position.x) && Near(a.position.y, b.position.y) &&
           Near(a.velocity.x, b.velocity.x) && Near(a.velocity.y, b.velocity.y) &&
           Near(a.age, b.age) && Near(a.lifetime, b.lifetime) &&
           Near(a.rotation, b.rotation) && Near(a.angularVelocity, b.angularVelocity);
}

} // namespace

int main()
{
    // —— Particle 查询：NormalizedAge / SizeAt / ColorAt / lifetime<=0 守卫 ——
    {
        Particle p{};
        p.lifetime = 2.0f;
        p.sizeBegin = 4.0f;
        p.sizeEnd = 0.0f;
        p.colorBegin = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
        p.colorEnd = glm::vec4{1.0f, 1.0f, 1.0f, 0.0f};

        p.age = 0.0f;
        Check(Near(p.NormalizedAge(), 0.0f), "NormalizedAge: age0 → 0");
        Check(Near(p.SizeAt(), 4.0f), "SizeAt: t0 → sizeBegin");
        Check(Near(p.ColorAt().w, 1.0f), "ColorAt: t0 alpha → 1");
        Check(!p.IsExpired(), "IsExpired: age0 未死");

        p.age = 1.0f;
        Check(Near(p.NormalizedAge(), 0.5f), "NormalizedAge: 中点 → 0.5");
        Check(Near(p.SizeAt(), 2.0f), "SizeAt: 线性中点 → 2");
        Check(Near(p.ColorAt().w, 0.5f), "ColorAt: 中点 alpha → 0.5");

        p.age = 2.0f;
        Check(Near(p.NormalizedAge(), 1.0f), "NormalizedAge: 满 → 1");
        Check(Near(p.SizeAt(), 0.0f), "SizeAt: t1 → sizeEnd");
        Check(p.IsExpired(), "IsExpired: age==lifetime 死");

        p.age = 5.0f;
        Check(Near(p.NormalizedAge(), 1.0f), "NormalizedAge: 越界 clamp 1");

        Particle q{};
        q.lifetime = 0.0f; // 守卫：不除零
        Check(Near(q.NormalizedAge(), 1.0f), "NormalizedAge: lifetime<=0 → 1");
    }

    // —— Emit 爆发 + 容量 + 溢出 ——
    {
        ParticleSystem sys(10, 1);
        Check(sys.Capacity() == 10, "Capacity == 10");
        Check(sys.AliveCount() == 0, "初始存活 0");

        const std::size_t got = sys.Emit(25); // 请求超容量
        Check(got == 10, "Emit 溢出：实际发射 == 容量");
        Check(sys.AliveCount() == 10, "存活 == 容量");

        const std::size_t got2 = sys.Emit(5); // 池满
        Check(got2 == 0, "池满再 Emit 返 0");
        Check(sys.AliveCount() == 10, "池满存活不变");

        sys.Clear();
        Check(sys.AliveCount() == 0, "Clear 后存活 0");
    }

    // —— Point 形状精确在原点 (含 SetOrigin) ——
    {
        ParticleSystem sys(16, 7);
        EmitterConfig cfg{}; // 默认 Point
        sys.SetConfig(cfg);
        sys.SetOrigin(glm::vec2{7.0f, -2.0f});
        sys.Emit(8);
        for (std::size_t i = 0; i < sys.AliveCount(); ++i)
        {
            Check(Near(sys.GetParticle(i).position.x, 7.0f) &&
                      Near(sys.GetParticle(i).position.y, -2.0f),
                  "Point：粒子精确在 origin");
        }
    }

    // —— 方向速度：spread=0 → 严格沿 directionAngle (+X) ——
    {
        ParticleSystem sys(32, 3);
        EmitterConfig cfg{};
        cfg.directionAngle = 0.0f; // +X
        cfg.spreadAngle = 0.0f;
        cfg.speedMin = cfg.speedMax = 5.0f;
        sys.SetConfig(cfg);
        sys.Emit(20);
        for (std::size_t i = 0; i < sys.AliveCount(); ++i)
        {
            const glm::vec2 v = sys.GetParticle(i).velocity;
            Check(Near(v.x, 5.0f) && Near(v.y, 0.0f), "spread0：速度严格沿 +X 且大小 5");
        }
    }

    // —— Disk 形状：全在半径内 ——
    {
        ParticleSystem sys(300, 42);
        EmitterConfig cfg{};
        cfg.shape = EmitShape::Disk;
        cfg.shapeRadius = 5.0f;
        sys.SetConfig(cfg);
        sys.Emit(300);
        bool allInside = true;
        for (std::size_t i = 0; i < sys.AliveCount(); ++i)
        {
            if (Len(sys.GetParticle(i).position) > 5.0f + 1e-3f)
            {
                allInside = false;
                break;
            }
        }
        Check(allInside, "Disk：所有粒子在半径内");
    }

    // —— Box 形状：全在半尺寸内 ——
    {
        ParticleSystem sys(300, 99);
        EmitterConfig cfg{};
        cfg.shape = EmitShape::Box;
        cfg.shapeHalfExtent = glm::vec2{3.0f, 2.0f};
        sys.SetConfig(cfg);
        sys.Emit(300);
        bool allInside = true;
        for (std::size_t i = 0; i < sys.AliveCount(); ++i)
        {
            const glm::vec2 pos = sys.GetParticle(i).position;
            if (std::fabs(pos.x) > 3.0f + 1e-3f || std::fabs(pos.y) > 2.0f + 1e-3f)
            {
                allInside = false;
                break;
            }
        }
        Check(allInside, "Box：所有粒子在半尺寸内");
    }

    // —— 重力积分：半隐式欧拉 v==g·dt、pos==v·dt ——
    {
        ParticleSystem sys(4, 5);
        EmitterConfig cfg{};
        cfg.speedMin = cfg.speedMax = 0.0f; // 零初速
        cfg.gravity = glm::vec2{0.0f, -10.0f};
        cfg.lifetimeMin = cfg.lifetimeMax = 100.0f;
        sys.SetConfig(cfg);
        sys.Emit(1);
        sys.Update(0.1f);
        const Particle& p = sys.GetParticle(0);
        Check(Near(p.velocity.y, -1.0f), "重力：v.y == g·dt == -1");
        Check(Near(p.velocity.x, 0.0f), "重力：v.x 恒 0");
        Check(Near(p.position.y, -0.1f), "重力：pos.y == v·dt == -0.1 (半隐式)");
        Check(Near(p.position.x, 0.0f), "重力：pos.x 恒 0");
        Check(Near(p.age, 0.1f), "重力：age 累积 dt");
    }

    // —— 阻尼：v *= exp(-damping·dt) ——
    {
        ParticleSystem sys(4, 6);
        EmitterConfig cfg{};
        cfg.directionAngle = 0.0f;
        cfg.spreadAngle = 0.0f;
        cfg.speedMin = cfg.speedMax = 10.0f;
        cfg.damping = 5.0f;
        cfg.lifetimeMin = cfg.lifetimeMax = 100.0f;
        sys.SetConfig(cfg);
        sys.Emit(1);
        sys.Update(0.1f);
        const float expected = 10.0f * std::exp(-5.0f * 0.1f);
        Check(sys.GetParticle(0).velocity.x < 10.0f, "阻尼：速度确实变小");
        Check(Near(sys.GetParticle(0).velocity.x, expected, 1e-3f), "阻尼：吻合 exp 闭式");
    }

    // —— 寿命淘汰：均匀寿命全体在越过寿命那帧清零 ——
    {
        ParticleSystem sys(100, 8);
        EmitterConfig cfg{};
        cfg.lifetimeMin = cfg.lifetimeMax = 1.0f;
        cfg.speedMin = cfg.speedMax = 0.0f;
        sys.SetConfig(cfg);
        sys.Emit(10);
        sys.Update(0.5f);
        Check(sys.AliveCount() == 10, "寿命：0.5s 全存活");
        sys.Update(0.4f);
        Check(sys.AliveCount() == 10, "寿命：0.9s 全存活");
        sys.Update(0.2f); // 累积 1.1 >= 1.0
        Check(sys.AliveCount() == 0, "寿命：越过寿命全清零");
    }

    // —— swap-remove：混合寿命两批，短命死、长命存活且身份正确 ——
    {
        ParticleSystem sys(100, 11);
        EmitterConfig shortLived{};
        shortLived.lifetimeMin = shortLived.lifetimeMax = 1.0f;
        shortLived.speedMin = shortLived.speedMax = 0.0f;
        sys.SetConfig(shortLived);
        sys.Emit(5); // 5 个寿命 1

        EmitterConfig longLived{};
        longLived.lifetimeMin = longLived.lifetimeMax = 10.0f;
        longLived.speedMin = longLived.speedMax = 0.0f;
        sys.SetConfig(longLived);
        sys.Emit(5); // 5 个寿命 10

        Check(sys.AliveCount() == 10, "混合批：共 10 存活");
        sys.Update(1.5f); // 短命 (1) 死，长命 (10) 存活
        Check(sys.AliveCount() == 5, "swap-remove：短命死后剩 5");
        bool allLong = true;
        for (std::size_t i = 0; i < sys.AliveCount(); ++i)
        {
            const Particle& p = sys.GetParticle(i);
            if (!Near(p.lifetime, 10.0f) || !Near(p.age, 1.5f))
            {
                allLong = false;
                break;
            }
        }
        Check(allLong, "swap-remove：存活者身份正确 (lifetime10 / age1.5，压缩不串味)");
    }

    // —— 连续发射：emitRate 按 dt 累积整数 + 保留小数余量 ——
    {
        ParticleSystem sys(1000, 13);
        EmitterConfig cfg{};
        cfg.emitRate = 100.0f;
        cfg.lifetimeMin = cfg.lifetimeMax = 1000.0f; // 测试期不死
        cfg.speedMin = cfg.speedMax = 0.0f;
        sys.SetConfig(cfg);

        sys.Update(1.0f); // 累积 100 → 发 100
        Check(sys.AliveCount() == 100, "连续：1s@100rate → 100");
        sys.Update(0.005f); // +0.5 → 不足 1
        Check(sys.AliveCount() == 100, "连续：0.5 余量不发射");
        sys.Update(0.005f); // 再 +0.5 → 1.0 → 发 1
        Check(sys.AliveCount() == 101, "连续：小数余量累够才发 (帧率无关)");
    }

    // —— dt<=0 不动：不发射、不积分、不老化 ——
    {
        ParticleSystem sys(16, 21);
        EmitterConfig cfg{};
        cfg.emitRate = 1000.0f;
        cfg.gravity = glm::vec2{0.0f, -50.0f};
        sys.SetConfig(cfg);
        sys.Emit(5);
        sys.Update(0.0f);
        Check(sys.AliveCount() == 5, "dt0：不发射");
        Check(Near(sys.GetParticle(0).age, 0.0f), "dt0：不老化");
        sys.Update(-1.0f);
        Check(sys.AliveCount() == 5, "dt<0：不发射");
        Check(Near(sys.GetParticle(0).age, 0.0f), "dt<0：不老化");
    }

    // —— 确定性：同 seed+config+调用序 → 逐粒子完全相同 (穿越大量死亡) ——
    {
        EmitterConfig cfg{};
        cfg.shape = EmitShape::Disk;
        cfg.shapeRadius = 4.0f;
        cfg.spreadAngle = 3.14159265f; // 全向
        cfg.speedMin = 2.0f;
        cfg.speedMax = 8.0f;
        cfg.lifetimeMin = 0.1f;
        cfg.lifetimeMax = 0.3f; // 短命 → 更新中大量死亡
        cfg.gravity = glm::vec2{0.0f, -9.8f};
        cfg.damping = 1.5f;
        cfg.sizeJitter = 0.3f;
        cfg.rotationMin = 0.0f;
        cfg.rotationMax = 6.28f;
        cfg.angularVelocityMin = -1.0f;
        cfg.angularVelocityMax = 1.0f;

        ParticleSystem a(200, 12345);
        ParticleSystem b(200, 12345);
        a.SetConfig(cfg);
        b.SetConfig(cfg);
        a.Emit(150);
        b.Emit(150);
        for (int f = 0; f < 30; ++f)
        {
            a.Update(0.016f);
            b.Update(0.016f);
        }
        Check(a.AliveCount() == b.AliveCount(), "确定性：存活数相同");
        bool allSame = true;
        for (std::size_t i = 0; i < a.AliveCount(); ++i)
        {
            if (!SameParticle(a.GetParticle(i), b.GetParticle(i)))
            {
                allSame = false;
                break;
            }
        }
        Check(allSame, "确定性：逐粒子状态完全相同 (含死亡后压缩序)");
        Check(a.AliveCount() < 150, "确定性场景确有粒子死亡 (前提有效)");
    }

    std::printf("[ParticleSystemTest] all %d checks passed\n", gChecks);
    return 0;
}
