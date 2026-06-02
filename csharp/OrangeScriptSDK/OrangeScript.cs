namespace Orange;

/// <summary>
/// 所有游戏脚本的基类——游戏侧 C# 玩法逻辑继承它，引擎在 Play 模式按生命周期回调。
/// 对标 Unity MonoBehaviour。挂载方式：实体的 ScriptComponent 引用脚本类全名，
/// 引擎进入 Play（EnterPlay 的 S4，见 docs/pie-csharp-scripting-design.md）时实例化
/// 并注入所属 Entity。
///
/// B1 骨架：本类定义脚本侧 API 契约；C++ 侧 CLR host 调用这些回调的实现是 B1.1+ 的事。
/// </summary>
public abstract class OrangeScript
{
    /// <summary>脚本所属实体——引擎实例化脚本时注入（internal set，脚本不可改）。</summary>
    public Entity Entity { get; internal set; }

    /// <summary>进入 Play / 脚本实例化后调用一次。覆写以做初始化。</summary>
    public virtual void OnStart() { }

    /// <summary>每帧调用一次，dt = 上一帧到本帧的间隔（秒）。覆写以写每帧逻辑。</summary>
    public virtual void OnUpdate(float dt) { }

    /// <summary>Stop / 脚本销毁前调用一次。覆写以做清理。</summary>
    public virtual void OnDestroy() { }
}
