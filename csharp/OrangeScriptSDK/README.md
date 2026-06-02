# OrangeScriptSDK — OrangeEngine C# 脚本 SDK（B1 骨架）

游戏侧玩法逻辑用 **C#** 写（脚本语言 = C#，用户 2026-06-02 拍板），继承
`Orange.OrangeScript`，引擎在 Play 模式按生命周期回调。对标 Unity MonoBehaviour。

## 状态：B1 骨架（脚本 API 契约已定义，未接 CLR host）

本程序集定义**脚本侧 API 契约**——基类 `OrangeScript`、托管句柄 `Entity` + `Vec3`、
P/Invoke 绑定声明 `EngineInterop`。C++ 侧的 CLR host（CoreCLR 嵌入 + `extern "C"` 导出）
是 B1.1+ 的事，设计见 [`../../docs/pie-csharp-scripting-design.md`](../../docs/pie-csharp-scripting-design.md)。

> ⚠️ 本机**无 .NET SDK**（仅有 dotnet host，无 SDK），未做 `dotnet build` 编译验证。
> 这些 `.cs` 是脚本 API 面定义；落地 B1.1 host spike 时随之一起编译验证。

## 示例：游戏侧脚本写法

```csharp
using Orange;

// 挂到实体的 ScriptComponent；引擎 Play 时实例化并每帧 OnUpdate。
public class Mover : OrangeScript
{
    public float Speed = 2.0f;                  // public field → Inspector tweakable（B1.3）

    public override void OnStart()
    {
        // 初始化逻辑（进 Play 时跑一次）。
    }

    public override void OnUpdate(float dt)
    {
        // 写 local position，引擎 TransformSystem 累积世界变换、子节点跟随（ADR-016）。
        Entity.Position += new Vec3(Speed * dt, 0.0f, 0.0f);
    }
}
```

## 文件
- `OrangeScript.cs` — 脚本基类（`OnStart` / `OnUpdate(dt)` / `OnDestroy` + `Entity`）
- `Entity.cs` — 托管实体句柄（uint64，不持裸指针）+ `Vec3`（blittable，与 glm::vec3 同布局）
- `EngineInterop.cs` — P/Invoke 绑定声明（C++↔C# 边界，句柄过界 + 值类型直传 + 薄绑定面）
- `OrangeScriptSDK.csproj` — net8.0 class library（与推荐的 CoreCLR host 对齐）

## 后续（见设计文档实施顺序）
B1.0 CLR host spike → B1.1 binding 最小集导出（让 `EngineInterop` 的符号在 C++ 侧落地）→
B1.2 ScriptComponent + 生命周期接进 EnterPlay S4 → B1.3 Inspector tweakable → B1.4 热重载。
