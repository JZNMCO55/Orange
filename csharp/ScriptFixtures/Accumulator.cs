using Orange;

namespace OrangeFixtures;

/// <summary>
/// script_reload_test 的状态保持 fixture（PIE M8）：每帧把 public 字段
/// <see cref="Total"/> 累加 <see cref="Step"/>，并镜像进实体的 X 位置。
///
/// <see cref="Total"/> 是**运行时累积**的 public 状态（非 authored 默认）—— 热重载时
/// ScriptSystem 快照它、重载后回灌，令累积值跨 reload 保留：
///   * 无状态保持：reload 后 Total 归 0（默认），后续帧从 0 重累加；
///   * 有状态保持：reload 后 Total 恢复到 reload 前值，后续帧接着累加。
/// 两者位置差异即证明运行时状态是否真被保留。
/// </summary>
public class Accumulator : OrangeScript
{
    // 运行时累积的 public 状态。reload 时被快照 / 回灌。
    public float Total = 0.0f;

    // 每帧步进（tweakable，可经 fieldOverrides authored）。
    public float Step = 1.0f;

    public override void OnUpdate(float dt)
    {
        Total += Step;
        Entity e = Entity;
        Vec3   p = e.Position;
        p.X       = Total; // 把累积值镜像到引擎侧 transform，供 C++ 断言
        e.Position = p;
    }
}
