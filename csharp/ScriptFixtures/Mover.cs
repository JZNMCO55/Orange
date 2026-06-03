using Orange;

namespace OrangeFixtures;

/// <summary>
/// script_runtime_test 的测试脚本（ADR-017 B1.1）：每帧把所属实体沿 +X 平移
/// (1,0,0)*1。证明 C# 脚本经函数指针表回调进 C++ 真的改了引擎侧
/// TransformComponent.position。
///
/// 生命周期可观测性：OnStart / OnUpdate / OnDestroy 都通过 Entity.Position 写
/// 不同分量（y / x / z），让 C++ 侧能仅凭引擎侧 TransformComponent 断言每个回调
/// 各被调一次（无需跨界读 C# 静态字段）。约定：
///   * OnStart：position.y += 100 —— 调一次 y==100；
///   * OnUpdate：position.x += 1  —— 调 N 次 x==N；
///   * OnDestroy：position.z += 7 —— 调一次 z==7。
/// </summary>
public class Mover : OrangeScript
{
    public override void OnStart()
    {
        Entity e = Entity;
        Vec3 p = e.Position;
        p.Y += 100.0f;
        e.Position = p;
    }

    public override void OnUpdate(float dt)
    {
        // 回调进 C++：Entity.Position 的 get/set 转发 EngineInterop 函数指针，
        // 改的是引擎侧该实体的 TransformComponent.position。Entity 是 readonly
        // struct（值类型），直接 `Entity.Position += ...` 触发 CS1612（改不了
        // getter 返回的临时值），先 copy 到 local 再写。
        Entity e = Entity;
        e.Position += new Vec3(1.0f, 0.0f, 0.0f);
    }

    public override void OnDestroy()
    {
        Entity e = Entity;
        Vec3 p = e.Position;
        p.Z += 7.0f;
        e.Position = p;
    }
}
