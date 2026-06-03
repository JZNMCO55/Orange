using System.Runtime.InteropServices;

namespace Orange;

/// <summary>
/// C++ ↔ C# 绑定层。引擎用 <c>extern "C"</c> 普通函数（**不需 dllexport**）操作
/// "当前脚本 World"，函数地址装进一张 native 函数指针表（<c>ScriptBindingTable</c>）。
/// CLR host 起来后由 <see cref="ScriptRuntime.Bootstrap"/> 把整张表推过来，本类
/// 存为 <c>delegate* unmanaged[Cdecl]</c> 函数指针并 call through。
///
/// 为什么走函数指针表而非 DllImport-against-exe：引擎是静态库链进宿主进程，
/// 不同链接配置下"进程自身导出符号"不可靠；函数指针表是更稳的封送方式
/// （借 Mono mono_add_internal_call 的"集中登记"思路）。
///
/// 约定（与 C++ 侧 ScriptBindings.h 对齐）：
///   * 实体句柄按 uint64 scriptId 过界（C++ EncodeEntityId 编码后的句柄）；
///   * 值类型（Vec3）按 blittable struct 直传，零封送；
///   * bool 用 int 0/1 过界；字符串按 UTF-8 marshaling；
///   * 绑定面保持**薄** —— 热路径批量逻辑留 native。
/// </summary>
internal static unsafe class EngineInterop
{
    /// <summary>
    /// native 绑定函数指针表 —— 字段顺序与布局必须与 C++ 侧
    /// <c>ScriptBindingTable</c> 严格一致（Bootstrap 收到指针后按此结构读取）。
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    private struct BindingTable
    {
        public delegate* unmanaged[Cdecl]<ulong, Vec3> EntityGetPosition;
        public delegate* unmanaged[Cdecl]<ulong, Vec3, void> EntitySetPosition;
        public delegate* unmanaged[Cdecl]<ulong, int> EntityIsValid;
        public delegate* unmanaged[Cdecl]<IntPtr, float> InputGetAxis;
    }

    // Bootstrap 时由 C++ 推来的函数指针表（值拷贝进来，C++ 侧表是静态存储期，
    // 但本侧自留一份避免持有外部指针）。
    private static BindingTable s_table;
    private static bool s_bootstrapped;

    /// <summary>由 <see cref="ScriptRuntime.Bootstrap"/> 调用，接收 C++ 绑定表指针。</summary>
    internal static void Bootstrap(IntPtr bindingTablePtr)
    {
        s_table = *(BindingTable*)bindingTablePtr;
        s_bootstrapped = true;
    }

    public static Vec3 Entity_GetPosition(ulong entityId)
    {
        if (!s_bootstrapped)
        {
            return default;
        }
        return s_table.EntityGetPosition(entityId);
    }

    public static void Entity_SetPosition(ulong entityId, Vec3 value)
    {
        if (!s_bootstrapped)
        {
            return;
        }
        s_table.EntitySetPosition(entityId, value);
    }

    public static bool Entity_IsValid(ulong entityId)
    {
        if (!s_bootstrapped)
        {
            return false;
        }
        return s_table.EntityIsValid(entityId) != 0;
    }

    public static float Input_GetAxis(string axisName)
    {
        if (!s_bootstrapped)
        {
            return 0.0f;
        }
        // axis 名按 UTF-8 封送给 native（MVP native 侧忽略名字，返回测试值）。
        IntPtr utf8 = Marshal.StringToCoTaskMemUTF8(axisName);
        try
        {
            return s_table.InputGetAxis(utf8);
        }
        finally
        {
            Marshal.FreeCoTaskMem(utf8);
        }
    }
}
