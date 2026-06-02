using System.Runtime.InteropServices;

namespace Orange;

/// <summary>3 维向量——与 C++ glm::vec3 同内存布局（blittable），跨 C++/C# 边界零封送拷贝。</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vec3
{
    public float X;
    public float Y;
    public float Z;

    public Vec3(float x, float y, float z)
    {
        X = x;
        Y = y;
        Z = z;
    }

    public static Vec3 operator +(Vec3 a, Vec3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static Vec3 operator *(Vec3 a, float s) => new(a.X * s, a.Y * s, a.Z * s);

    public override string ToString() => $"({X}, {Y}, {Z})";
}

/// <summary>
/// 实体的托管句柄——按 uint64（EnTT entity + generation，或 EntityGuid〔ADR-013〕）跨界，
/// **不持裸指针**（GC 移动 + 生命周期风险，见 docs/pie-csharp-scripting-design.md §4）。
/// 属性访问转发到 C++ 引擎（EngineInterop P/Invoke）。
/// </summary>
public readonly struct Entity
{
    public readonly ulong Id;

    public Entity(ulong id)
    {
        Id = id;
    }

    /// <summary>句柄是否有效（id 非 0）。更严的校验转发引擎（实体可能已销毁）。</summary>
    public bool IsValid => Id != 0 && EngineInterop.Entity_IsValid(Id);

    /// <summary>实体位置（写 local，引擎 TransformSystem 累积世界变换、子节点跟随，见 ADR-016）。</summary>
    public Vec3 Position
    {
        get => EngineInterop.Entity_GetPosition(Id);
        set => EngineInterop.Entity_SetPosition(Id, value);
    }
}
