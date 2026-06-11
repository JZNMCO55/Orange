using Orange;

namespace OrangeFixtures;

/// <summary>
/// script_system_test 的 tweakable 测试脚本（ADR-017 B1.3）：每帧把所属实体
/// 沿 +X 平移 (Speed,0,0)。证明 ScriptComponent.fieldOverrides 经托管反射在
/// OnStart 前真的写进了脚本对象的 public 字段。
///
/// 约定：
///   * <see cref="Speed"/> 默认 1.0 —— 无 override 时每帧 +1（4 帧后 x==4）；
///   * override Speed="2.5" 时每帧 +2.5（4 帧后 x==10）。
/// 两者对照即可区分"override 生效"与"用了默认值"。
/// </summary>
public class Tweakable : OrangeScript
{
    // public 实例字段 → 可被 SetInstanceField 反射写入。默认 1.0。
    public float Speed = 1.0f;

    public override void OnUpdate(float dt)
    {
        // 回调进 C++：Entity.Position 的 get/set 转发 EngineInterop 函数指针。
        // Entity 是 readonly struct（值类型），先 copy 到 local 再写，避免
        // CS1612（改不了 getter 返回的临时值）。
        Entity e = Entity;
        e.Position += new Vec3(Speed, 0.0f, 0.0f);
    }
}
