using System.Runtime.InteropServices;

namespace Orange;

/// <summary>
/// C++ ↔ C# 绑定层（P/Invoke 声明）。引擎用 <c>extern "C"</c>（CoreCLR 下配
/// <c>UnmanagedCallersOnly</c> 回调）导出函数，本类声明对应托管入口。约定：
///   * 实体句柄按 uint64 过界，不传裸指针；
///   * 值类型（Vec3 等）按 blittable struct 直传，零封送；
///   * 字符串按 UTF-8 marshaling；
///   * 绑定面保持**薄**——热路径批量逻辑留 native（见设计文档 §4 与"过度脚本化"陷阱）。
///
/// B1 骨架：函数签名定义绑定契约；C++ 侧导出实现（src/script/dotnet/ 的 CLR host +
/// 导出符号）是 B1.1 落地。CLR 嵌在引擎进程内，DllImport 库名指向进程自身导出符号。
/// </summary>
internal static class EngineInterop
{
    // CLR host 嵌在引擎进程内 —— 进程自身导出的符号。实际库名/解析在 B1.1
    // host spike 时按导出方式（可执行自身 / 共享库）对齐。
    private const string Lib = "OrangeEngine";

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern Vec3 Entity_GetPosition(ulong entityId);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern void Entity_SetPosition(ulong entityId, Vec3 value);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    public static extern bool Entity_IsValid(ulong entityId);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    public static extern float Input_GetAxis([MarshalAs(UnmanagedType.LPUTF8Str)] string axisName);
}
