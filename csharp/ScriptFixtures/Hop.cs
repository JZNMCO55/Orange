using Orange;

namespace OrangeFixtures;

/// <summary>
/// script_reload_test 的行为变体 fixture **V1**（PIE M8）：每帧把实体沿 +X 平移 1。
/// V2（ScriptFixturesV2 里的同名 <c>OrangeFixtures.Hop</c>）每帧平移 2。
///
/// 热重载把某实体的脚本从 ScriptFixtures.dll 换到 ScriptFixturesV2.dll（同类型全名，
/// 经裸类型名解析），reload 后帧位移由 1 变 2 —— 证明**新代码**真被加载、旧代码被卸载。
/// 无 public 字段：行为差异纯来自被加载的程序集不同，与状态保持正交。
/// </summary>
public class Hop : OrangeScript
{
    public override void OnUpdate(float dt)
    {
        Entity e = Entity;
        e.Position += new Vec3(1.0f, 0.0f, 0.0f);
    }
}
