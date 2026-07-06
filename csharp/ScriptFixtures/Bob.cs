using Orange;

namespace OrangeFixtures;

/// <summary>
/// 编辑器 PIE dogfood 的演示脚本（ADR-021 M5，C# 第二宿主）：让所属实体绕初始
/// 位置做平滑的正弦上下浮动 + 缓慢侧移——**dt 缩放**，故在编辑器 60fps 下是肉眼
/// 可见的温和运动（不像 Mover/Tweakable 每帧固定加、瞬间飞出屏幕）。
///
/// 用途：在 OrangeEditor 里给一个可渲染实体挂 ScriptComponent 指向本类，Play 时
/// 看它浮动、Stop 时由 Play 快照还原回初始位置——证明 C# 脚本经 IGameModule 第二
/// 宿主在编辑器 Play 生命周期里真实驱动引擎实体。
///
/// tweakable：Amplitude / Speed 是 public 字段，可经 Inspector 的 fieldOverrides
/// authored（B1.3），Play 前注入。
/// </summary>
public class Bob : OrangeScript
{
    // 浮动幅度（世界单位）。public → 可被 fieldOverrides 反射注入。
    public float Amplitude = 1.0f;

    // 角速度（弧度/秒）。
    public float Speed = 2.0f;

    // 初始位置（OnStart 记下，每帧相对它偏移，避免累积漂移）。
    private Vec3 _origin;
    private float _elapsed;

    public override void OnStart()
    {
        _origin = Entity.Position;
        _elapsed = 0.0f;
    }

    public override void OnUpdate(float dt)
    {
        _elapsed += dt;
        // 相对初始位置的正弦偏移：Y 上下浮动，X 缓慢侧向漂（幅度减半）。
        float sy = (float)System.Math.Sin(_elapsed * Speed) * Amplitude;
        float sx = (float)System.Math.Sin(_elapsed * Speed * 0.5f) * (Amplitude * 0.5f);
        Entity e = Entity;
        e.Position = _origin + new Vec3(sx, sy, 0.0f);
    }
}
