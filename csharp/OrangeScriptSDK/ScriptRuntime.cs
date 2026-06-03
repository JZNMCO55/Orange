using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Orange;

/// <summary>
/// 托管运行时 glue（internal）—— C++ ScriptRuntime 经 hostfxr 的
/// <c>load_assembly_and_get_function_pointer</c> 取本类的
/// <c>[UnmanagedCallersOnly]</c> 静态方法函数指针并调用。游戏脚本**不**直接用本类。
///
/// 职责：
///   * <see cref="Bootstrap"/>：接收 C++ 绑定函数指针表，交给 <see cref="EngineInterop"/>；
///   * <see cref="CreateInstance"/>：加载 game assembly + 反射构造 OrangeScript 子类
///     + 注入 Entity + GCHandle 保活；
///   * <see cref="InvokeStart"/> / <see cref="InvokeUpdate"/> / <see cref="InvokeDestroy"/>：
///     按生命周期回调脚本；
///   * <see cref="Release"/>：Free GCHandle。
///
/// **异常纪律（ADR-017 陷阱）**：脚本回调里抛出的异常必须在本边界 try/catch、
/// 标 stderr，**绝不**让异常穿回 C++（会崩编辑器 / 测试进程）。
/// </summary>
internal static class ScriptRuntime
{
    /// <summary>接收 C++ 绑定函数指针表，转交 EngineInterop。</summary>
    [UnmanagedCallersOnly]
    public static void Bootstrap(IntPtr bindingTablePtr)
    {
        try
        {
            EngineInterop.Bootstrap(bindingTablePtr);
        }
        catch (Exception ex)
        {
            LogBoundaryException(nameof(Bootstrap), ex);
        }
    }

    /// <summary>
    /// 加载 game assembly → 按类型全名 Activator 构造 OrangeScript 子类 → 注入
    /// Entity → GCHandle(Normal) 保活 → 返回 GCHandle.ToIntPtr。失败返回 IntPtr.Zero。
    /// </summary>
    /// <param name="assemblyPathUtf8">game assembly 的 UTF-8 路径（native 指针）。</param>
    /// <param name="typeNameUtf8">assembly-qualified 类型全名的 UTF-8 指针，
    /// 如 "OrangeFixtures.Mover, ScriptFixtures"。</param>
    /// <param name="entityId">C++ EncodeEntityId 编码后的实体句柄。</param>
    [UnmanagedCallersOnly]
    public static IntPtr CreateInstance(IntPtr assemblyPathUtf8, IntPtr typeNameUtf8, ulong entityId)
    {
        try
        {
            string? assemblyPath = Marshal.PtrToStringUTF8(assemblyPathUtf8);
            string? typeName = Marshal.PtrToStringUTF8(typeNameUtf8);
            if (string.IsNullOrEmpty(assemblyPath) || string.IsNullOrEmpty(typeName))
            {
                return IntPtr.Zero;
            }

            // 加载 game assembly 到 Default ALC（OrangeScriptSDK 在同目录，
            // 依赖自动解析）。MVP 不做可卸载 collectible ALC —— 热重载留后续。
            Assembly gameAssembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(assemblyPath);

            // typeName 是 assembly-qualified（"Ns.Type, AssemblyName"）。
            // Type.GetType 先按程序集限定名解析；解析不到再退回已加载的 game
            // assembly 里按裸类型名找（容错 assembly 名拼写差异）。
            Type? type = Type.GetType(typeName, throwOnError: false);
            if (type == null)
            {
                string bareName = typeName.Split(',')[0].Trim();
                type = gameAssembly.GetType(bareName, throwOnError: false);
            }
            if (type == null || !typeof(OrangeScript).IsAssignableFrom(type))
            {
                Console.Error.WriteLine(
                    $"[OrangeScript] CreateInstance: 类型 '{typeName}' 不存在或非 OrangeScript 子类。");
                return IntPtr.Zero;
            }

            object? obj = Activator.CreateInstance(type);
            if (obj is not OrangeScript script)
            {
                return IntPtr.Zero;
            }

            // 注入所属实体（Entity.Entity 是 internal set）。
            script.Entity = new Entity(entityId);

            // Normal GCHandle 保活：C++ 侧持有 IntPtr，期间 GC 不回收实例。
            GCHandle handle = GCHandle.Alloc(script, GCHandleType.Normal);
            return GCHandle.ToIntPtr(handle);
        }
        catch (Exception ex)
        {
            LogBoundaryException(nameof(CreateInstance), ex);
            return IntPtr.Zero;
        }
    }

    [UnmanagedCallersOnly]
    public static void InvokeStart(IntPtr handlePtr)
    {
        OrangeScript? script = Resolve(handlePtr);
        if (script == null)
        {
            return;
        }
        try
        {
            script.OnStart();
        }
        catch (Exception ex)
        {
            LogBoundaryException(nameof(InvokeStart), ex);
        }
    }

    [UnmanagedCallersOnly]
    public static void InvokeUpdate(IntPtr handlePtr, float dt)
    {
        OrangeScript? script = Resolve(handlePtr);
        if (script == null)
        {
            return;
        }
        try
        {
            script.OnUpdate(dt);
        }
        catch (Exception ex)
        {
            LogBoundaryException(nameof(InvokeUpdate), ex);
        }
    }

    [UnmanagedCallersOnly]
    public static void InvokeDestroy(IntPtr handlePtr)
    {
        OrangeScript? script = Resolve(handlePtr);
        if (script == null)
        {
            return;
        }
        try
        {
            script.OnDestroy();
        }
        catch (Exception ex)
        {
            LogBoundaryException(nameof(InvokeDestroy), ex);
        }
    }

    /// <summary>Free GCHandle —— 解保活，允许 GC 回收脚本实例。</summary>
    [UnmanagedCallersOnly]
    public static void Release(IntPtr handlePtr)
    {
        try
        {
            if (handlePtr == IntPtr.Zero)
            {
                return;
            }
            GCHandle handle = GCHandle.FromIntPtr(handlePtr);
            if (handle.IsAllocated)
            {
                handle.Free();
            }
        }
        catch (Exception ex)
        {
            LogBoundaryException(nameof(Release), ex);
        }
    }

    // 从 GCHandle IntPtr 取脚本实例。无效 / 已释放返回 null。
    private static OrangeScript? Resolve(IntPtr handlePtr)
    {
        if (handlePtr == IntPtr.Zero)
        {
            return null;
        }
        try
        {
            GCHandle handle = GCHandle.FromIntPtr(handlePtr);
            if (!handle.IsAllocated)
            {
                return null;
            }
            return handle.Target as OrangeScript;
        }
        catch (Exception ex)
        {
            LogBoundaryException(nameof(Resolve), ex);
            return null;
        }
    }

    // 边界异常统一标 stderr —— 不传播进 C++。
    private static void LogBoundaryException(string where, Exception ex)
    {
        Console.Error.WriteLine($"[OrangeScript] {where} 抛出异常：{ex}");
    }
}
