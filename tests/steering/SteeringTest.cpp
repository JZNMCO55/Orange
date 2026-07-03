// Steering 转向行为库的 headless 单元测试：Reynolds seek/flee/arrive/pursue/evade/wander。
// 纯 glm + 数学 (Wander 随机复用 Noise::Rng 确定性)，无 World / GPU / GUI。裸 main() + <cassert>。
//
// 覆盖点：
//   * Truncate —— 超长钳制 (保方向) / 未超原样 / 零向量 / maxLen<=0 → 零。
//   * Seek —— 力朝 target；闭环收敛：Agent 反复 Seek 能逼近 target (最近距离趋零)。
//   * Flee —— 力背离 target。
//   * Arrive —— ★到点收敛静止 (末速 ≈0；关键差异是 vs Seek 绕点不停振荡)；减速带内力 < Seek 力。
//   * Pursue —— 预测拦截：追 +Y 移动目标时力有 +Y 分量 (瞄向前方) 而非只朝当前位置。
//   * Wander —— 同 seed+state 确定性 (力相同 / angle 同步演进) + 力有界 (<=maxForce) + angle 被扰动。
//   * Agent::Integrate —— maxSpeed 钳速 (巨力 → 速度恰为 maxSpeed) + dt<=0 不动。
// 仅 orange_engine + glm + 标准库，headless 完全可验。

#include "orange/engine/steering/Steering.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace S = ::Orange::Engine::Steering;
using S::Agent;
using S::SteeringParams;
using S::WanderState;

namespace
{

    bool Near(float a, float b, float eps = 1e-4f)
    {
        return std::fabs(a - b) <= eps;
    }

    float Len(const glm::vec2& v)
    {
        return std::sqrt(v.x * v.x + v.y * v.y);
    }

    int  gChecks = 0;
    void Check(bool cond, const char* what)
    {
        ++gChecks;
        if (!cond)
        {
            std::fprintf(stderr, "[SteeringTest] FAILED: %s\n", what);
            assert(cond);
        }
    }

} // namespace

int main()
{
    const SteeringParams p{}; // maxSpeed 10, maxForce 40

    // —— Truncate ——
    {
        const glm::vec2 longV{30.0f, 40.0f}; // 长 50
        const glm::vec2 t = S::Truncate(longV, 5.0f);
        Check(Near(Len(t), 5.0f), "Truncate：超长钳到 maxLen");
        Check(Near(t.x, 3.0f) && Near(t.y, 4.0f), "Truncate：保方向 (5·(0.6,0.8))");

        const glm::vec2 shortV{1.0f, 0.0f};
        Check(Near(Len(S::Truncate(shortV, 5.0f)), 1.0f), "Truncate：未超原样");
        Check(Near(Len(S::Truncate(glm::vec2{0.0f, 0.0f}, 5.0f)), 0.0f), "Truncate：零向量仍零");
        Check(Near(Len(S::Truncate(longV, 0.0f)), 0.0f), "Truncate：maxLen<=0 → 零");
    }

    // —— Seek 方向 ——
    {
        const glm::vec2 f = S::Seek(glm::vec2{0, 0}, glm::vec2{0, 0}, glm::vec2{10, 0}, p);
        Check(f.x > 0.0f && Near(f.y, 0.0f), "Seek：力朝 target (+X)");
        Check(Near(f.x, 10.0f), "Seek：期望速度 maxSpeed，力 = desired - 0");
    }

    // —— Seek 闭环收敛：Agent 反复 Seek 能逼近 target ——
    {
        Agent           a{};
        const glm::vec2 target{50.0f, 0.0f};
        float           minDist = Len(target - a.position);
        for (int i = 0; i < 300; ++i)
        {
            a.Integrate(S::Seek(a.position, a.velocity, target, p), p, 0.05f);
            const float d = Len(target - a.position);
            if (d < minDist)
            {
                minDist = d;
            }
        }
        Check(minDist < 2.0f, "Seek：闭环最近距离逼近 target");
    }

    // —— Flee 方向 ——
    {
        const glm::vec2 f = S::Flee(glm::vec2{0, 0}, glm::vec2{0, 0}, glm::vec2{10, 0}, p);
        Check(f.x < 0.0f && Near(f.y, 0.0f), "Flee：力背离 target (-X)");
    }

    // —— Arrive：趋近 target 并收敛静止 (末速 ≈0)。与 Seek 的关键差异是"收敛静止" vs
    // "永远绕 target 振荡不停"——故稳健地只断言 Arrive 的收敛 (线性 arrive 首过冲属正常，
    // 不做过冲量断言以免脆弱)。 ——
    {
        Agent           a{};
        const glm::vec2 target{30.0f, 0.0f};
        for (int i = 0; i < 500; ++i)
        {
            a.Integrate(S::Arrive(a.position, a.velocity, target, p, 10.0f), p, 0.05f);
        }
        Check(Len(target - a.position) < 1.5f, "Arrive：最终停在 target 附近");
        Check(Len(a.velocity) < 2.0f, "Arrive：末速 ≈0 (收敛静止，非 Seek 式绕点振荡)");
    }

    // —— Arrive 减速带：带内力小于 Seek ——
    {
        const glm::vec2 pos{0, 0};
        const glm::vec2 vel{0, 0};
        const glm::vec2 target{5.0f, 0.0f}; // dist 5 < slowRadius 10 → 带内
        const glm::vec2 fa = S::Arrive(pos, vel, target, p, 10.0f);
        const glm::vec2 fs = S::Seek(pos, vel, target, p);
        Check(Len(fa) < Len(fs), "Arrive：减速带内转向力 < Seek");
        Check(Near(Len(fa), 5.0f), "Arrive：带内期望速度 = maxSpeed·(dist/slowRadius) = 5");
    }

    // —— Pursue：预测拦截移动目标 (力瞄向前方) ——
    {
        const glm::vec2 pos{0, 0};
        const glm::vec2 vel{0, 0};
        const glm::vec2 targetPos{10.0f, 0.0f};
        const glm::vec2 targetVel{0.0f, 10.0f}; // 目标向 +Y 移动
        const glm::vec2 fPursue  = S::Pursue(pos, vel, targetPos, targetVel, p, 1.0f);
        const glm::vec2 fSeekNow = S::Seek(pos, vel, targetPos, p); // 只朝当前位置
        Check(fPursue.y > 0.1f, "Pursue：力有 +Y 分量 (瞄向目标前方)");
        Check(Near(fSeekNow.y, 0.0f), "对比：Seek 当前位置无 Y 分量");
    }

    // —— Wander：确定性 + 有界 + angle 被扰动 ——
    {
        const glm::vec2              pos{0, 0};
        const glm::vec2              vel{5.0f, 0.0f};
        ::Orange::Engine::Noise::Rng rngA{5};
        ::Orange::Engine::Noise::Rng rngB{5};
        WanderState                  sa{};
        WanderState                  sb{};
        const glm::vec2              fa = S::Wander(pos, vel, sa, rngA, 3.0f, 2.0f, 0.5f, p);
        const glm::vec2              fb = S::Wander(pos, vel, sb, rngB, 3.0f, 2.0f, 0.5f, p);
        Check(Near(fa.x, fb.x) && Near(fa.y, fb.y), "Wander：同 seed+state 力完全相同");
        Check(Near(sa.angle, sb.angle), "Wander：angle 同步演进");
        Check(!Near(sa.angle, 0.0f), "Wander：angle 被 jitter 扰动");
        Check(Len(fa) <= p.maxForce + 1e-3f, "Wander：力有界 (<=maxForce)");
    }

    // —— Agent::Integrate：maxSpeed 钳速 + dt<=0 不动 ——
    {
        Agent a{};
        a.Integrate(glm::vec2{1000.0f, 0.0f}, p, 0.1f); // 巨力
        Check(Near(Len(a.velocity), p.maxSpeed), "Integrate：速度钳到 maxSpeed");
        Check(Near(a.position.x, p.maxSpeed * 0.1f), "Integrate：pos = clampedVel·dt");

        const glm::vec2 velBefore = a.velocity;
        a.Integrate(glm::vec2{1000.0f, 0.0f}, p, 0.0f);
        Check(Near(a.velocity.x, velBefore.x) && Near(a.velocity.y, velBefore.y), "Integrate：dt<=0 不动");
    }

    std::printf("[SteeringTest] all %d checks passed\n", gChecks);
    return 0;
}
