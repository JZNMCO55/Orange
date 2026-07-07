using Orange;

namespace OrangeFixtures;

/// <summary>
/// script_reload_test 的行为变体 fixture **V2**（PIE M8）：每帧把实体沿 +X 平移 2
/// （V1 = ScriptFixtures 里的同名 <c>OrangeFixtures.Hop</c> 平移 1）。
///
/// 与 V1 同命名空间 + 同类型名，仅每帧位移不同。热重载切到本程序集后帧位移由 1 变 2，
/// 证明可卸载 ALC 真加载了不同的代码（而非沿用旧程序集）。
/// </summary>
public class Hop : OrangeScript
{
    public override void OnUpdate(float dt)
    {
        Entity e = Entity;
        e.Position += new Vec3(2.0f, 0.0f, 0.0f);
    }
}
