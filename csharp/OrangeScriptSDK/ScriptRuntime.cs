using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

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
    // 当前游戏程序集的可卸载加载上下文（M8 热重载）。游戏脚本进这里，
    // OrangeScriptSDK / 框架程序集回退 Default 单副本（见 GameLoadContext）。
    // 惰性建；UnloadGameAssemblies 卸载后置 null（断唯一强引用，令 GC 可回收）。
    private static GameLoadContext? s_gameAlc;

    // reload 期的运行时状态快照：entityId → 该实例 public 字段（编码为字符串）。
    // 只存字符串 / 值类型，不 pin collectible ALC（否则挡卸载）。
    private static readonly Dictionary<ulong, List<FieldSnap>> s_snapshots = new();

    // 卸载轮询上限：Unload 后最多跑这么多轮 GC 等 ALC 被回收。
    private const int kMaxUnloadGc = 10;

    // 一条字段快照。只含 string / int（值语义），刻意不引用任何 collectible ALC
    // 里的对象 —— 否则会 pin 住 ALC 挡卸载。Type 用与 SetInstanceField 同款 int
    // 编码（0=Float / 1=Int / 2=Bool / 3=String）。
    private readonly record struct FieldSnap(string Name, int Type, string Value);

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
    // NoInlining：把 gameAssembly / type / 局部实例引用严格限制在本帧，杜绝被内联进
    // 调用者帧后其生命周期跨到卸载期的 GC 轮询 —— 那正是 collectible ALC 卸不掉的
    // 经典 JIT local-lifetime pin。
    [MethodImpl(MethodImplOptions.NoInlining)]
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

            // 经可卸载 collectible ALC 加载 game assembly（M8 热重载）。惰性建 ALC，
            // 程序集本体走 LoadFromStream 不锁文件；OrangeScriptSDK / 框架程序集回退
            // Default 单副本。
            Assembly gameAssembly = EnsureGameAssembly(assemblyPath);

            // typeName 是 assembly-qualified（"Ns.Type, AssemblyName"）。collectible
            // ALC 里的游戏类型 Type.GetType(assemblyQualified) 搜不到（它只搜 Default
            // + 已 pin 的 ALC），故直接在该 game assembly 上按裸类型名解析（主路径）。
            string bareName = typeName.Split(',')[0].Trim();
            Type?  type     = gameAssembly.GetType(bareName, throwOnError: false);
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

    /// <summary>
    /// 按 authored 值写脚本对象的一个 public 实例字段（B1.3 tweakable）。
    /// 实例化后、OnStart 前由 C++ ScriptSystem 逐条调用。
    ///
    /// <paramref name="fieldType"/> 是 C++ ScriptFieldType 的 int 值
    /// （0=Float / 1=Int / 2=Bool / 3=String）；<paramref name="valueUtf8"/> 是
    /// 字符串形态的值，按 fieldType 用 InvariantCulture 解析后再
    /// Convert.ChangeType 适配字段真实类型（兼容字段是 int 而 authored 是 long
    /// 等）。成功返回 1；任何失败（句柄解不出 / 字段不存在 / 解析或转换抛异常）
    /// 标 stderr 后返回 0，**绝不**让异常穿回 C++。
    /// </summary>
    [UnmanagedCallersOnly]
    public static int SetInstanceField(IntPtr handlePtr, IntPtr fieldNameUtf8, int fieldType, IntPtr valueUtf8)
    {
        try
        {
            OrangeScript? script = Resolve(handlePtr);
            if (script == null)
            {
                return 0;
            }

            string? fieldName = Marshal.PtrToStringUTF8(fieldNameUtf8);
            if (string.IsNullOrEmpty(fieldName))
            {
                return 0;
            }
            // value 允许为空串（如 String 类型 authored ""）；用 "" 兜底 null。
            string value = Marshal.PtrToStringUTF8(valueUtf8) ?? string.Empty;

            return ApplyFieldValue(script, fieldName, fieldType, value) ? 1 : 0;
        }
        catch (Exception ex)
        {
            LogBoundaryException(nameof(SetInstanceField), ex);
            return 0;
        }
    }

    // -----------------------------------------------------------------------
    // M8 热重载：状态快照 / 回灌 / 卸载
    // -----------------------------------------------------------------------

    /// <summary>
    /// 反射读脚本实例的 public 字段，编码为字符串快照存进 <see cref="s_snapshots"/>
    /// （按 entityId）。reload 后经 <see cref="RestoreInstanceState"/> 回灌，令
    /// 运行时改过的可调字段跨热重载保留。成功返回 1，失败返回 0。
    /// </summary>
    [UnmanagedCallersOnly]
    public static int SnapshotInstanceState(ulong entityId, IntPtr handlePtr)
    {
        try
        {
            OrangeScript? script = Resolve(handlePtr);
            if (script == null)
            {
                return 0;
            }
            var snaps = new List<FieldSnap>();
            foreach (FieldInfo field in script.GetType().GetFields(
                         BindingFlags.Public | BindingFlags.Instance))
            {
                if (TryEncodeField(field, script, out int type, out string value))
                {
                    snaps.Add(new FieldSnap(field.Name, type, value));
                }
            }
            s_snapshots[entityId] = snaps;
            return 1;
        }
        catch (Exception ex)
        {
            LogBoundaryException(nameof(SnapshotInstanceState), ex);
            return 0;
        }
    }

    /// <summary>
    /// 把先前 <see cref="SnapshotInstanceState"/> 存的字段值经反射写回新实例（同
    /// <see cref="SetInstanceField"/> 解析 + Convert.ChangeType 逻辑）。无对应快照
    /// 或任一字段写回失败返回 0，全成功返回 1。
    /// </summary>
    [UnmanagedCallersOnly]
    public static int RestoreInstanceState(ulong entityId, IntPtr handlePtr)
    {
        try
        {
            OrangeScript? script = Resolve(handlePtr);
            if (script == null)
            {
                return 0;
            }
            if (!s_snapshots.TryGetValue(entityId, out List<FieldSnap>? snaps))
            {
                return 0;
            }
            bool allOk = true;
            foreach (FieldSnap snap in snaps)
            {
                if (!ApplyFieldValue(script, snap.Name, snap.Type, snap.Value))
                {
                    allOk = false; // 单字段失败不中断其余（如脚本删了某字段）
                }
            }
            return allOk ? 1 : 0;
        }
        catch (Exception ex)
        {
            LogBoundaryException(nameof(RestoreInstanceState), ex);
            return 0;
        }
    }

    /// <summary>
    /// 卸载当前游戏程序集的 collectible ALC（M8 热重载）：断 s_gameAlc 唯一强引用 →
    /// Unload → 轮询 GC。**调用方须在此之前 Free 所有脚本 GCHandle**。返回 0 = 弱引用
    /// 已在 <see cref="kMaxUnloadGc"/> 轮 GC 内消亡；非 0 = 未消亡。
    ///
    /// **功能语义**：无论返回 0 / 非 0，本调用后 s_gameAlc 已置 null，下一次
    /// <see cref="CreateInstance"/> 会重建新 ALC 加载（可能已重编的）新代码 —— 功能性
    /// 热重载（卸旧引用 + 载新代码 + 回灌状态）不依赖同步回收。
    ///
    /// **已知宿主限制**：本宿主由 C++ 经 reverse-P/Invoke（[UnmanagedCallersOnly]）驱动。
    /// 实测：加载 collectible 程序集时只要进程内存在 reverse-P/Invoke 栈帧，CoreCLR 会
    /// 持久 GC-root 该 ALC，故本方法的同步弱引用消亡通常达不到（返回非 0），旧 ALC 的
    /// 内存要到进程后续无 reverse-P/Invoke 帧的时机才可能回收。纯托管线程加载则可正常
    /// 卸载（M8 探查以最小 repro 定位）。严格按 MS unloadability 样板：触发段
    /// <see cref="MethodImplOptions.NoInlining"/>、不把 ALC / Assembly / Type 局部留到
    /// 收集期，故 glue 侧不引入额外 pin。
    /// </summary>
    [UnmanagedCallersOnly]
    public static int UnloadGameAssemblies()
    {
        try
        {
            WeakReference? weak = BeginUnload();
            if (weak == null)
            {
                return 0; // 没有已加载的游戏 ALC —— 视为已卸载
            }
            for (int i = 0; weak.IsAlive && i < kMaxUnloadGc; i++)
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
            }
            if (weak.IsAlive)
            {
                // 弱引用未消亡。**已知宿主限制**：C++ 经 reverse-P/Invoke 驱动 glue，
                // 加载 collectible 程序集时进程内存在 reverse-P/Invoke 栈帧 → CoreCLR
                // 持久 GC-root 该 ALC，同步回收不可达（详见 M8 探查）。功能性热重载
                // （卸旧引用 + 载新代码 + 回灌状态）不受影响；此处仅诊断，返回非 0。
                Console.Error.WriteLine(
                    "[OrangeScript] UnloadGameAssemblies: 弱引用未在 GC 预算内消亡"
                    + "（reverse-P/Invoke 持久 root，已知宿主限制；功能性热重载不受影响）。");
                return 1;
            }
            return 0;
        }
        catch (Exception ex)
        {
            LogBoundaryException(nameof(UnloadGameAssemblies), ex);
            return 1;
        }
    }

    /// <summary>清空状态快照字典（reload 收尾）。</summary>
    [UnmanagedCallersOnly]
    public static void ClearSnapshots()
    {
        try
        {
            s_snapshots.Clear();
        }
        catch (Exception ex)
        {
            LogBoundaryException(nameof(ClearSnapshots), ex);
        }
    }

    // 触发卸载：Unload() → 建弱引用 → 断 s_gameAlc 唯一强引用 → 返回弱引用。
    // 标 NoInlining 且不把 ALC 局部留到 return 后 —— 令收集期无任何强根指向 ALC。
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference? BeginUnload()
    {
        if (s_gameAlc == null)
        {
            return null;
        }
        s_gameAlc.Unload();
        var weak = new WeakReference(s_gameAlc, trackResurrection: true);
        s_gameAlc = null; // 关键：断唯一强引用，否则 ALC 永不被回收
        return weak;
    }

    // 惰性建 collectible ALC 并加载（复用）游戏程序集。多个游戏程序集共用同一 ALC，
    // 一次 UnloadGameAssemblies 整批卸载。
    private static Assembly EnsureGameAssembly(string assemblyPath)
    {
        s_gameAlc ??= new GameLoadContext();
        return s_gameAlc.GetOrLoadGameAssembly(Path.GetFullPath(assemblyPath));
    }

    // 反射写一个 public 实例字段（SetInstanceField / RestoreInstanceState 共用）。
    // fieldType：0=Float / 1=Int / 2=Bool / 3=String。value 按 InvariantCulture
    // 解析后 Convert.ChangeType 适配字段真实类型。字段不存在 / 解析或转换抛异常
    // 返回 false（调用方统一在边界 catch，绝不让异常穿回 C++）。
    private static bool ApplyFieldValue(object script, string fieldName, int fieldType, string value)
    {
        FieldInfo? field = script.GetType().GetField(
            fieldName, BindingFlags.Public | BindingFlags.Instance);
        if (field == null)
        {
            Console.Error.WriteLine(
                $"[OrangeScript] ApplyFieldValue: 类型 '{script.GetType().FullName}' 上找不到 public 字段 '{fieldName}'。");
            return false;
        }

        object parsed = fieldType switch
        {
            0 => float.Parse(value, CultureInfo.InvariantCulture),  // Float
            1 => long.Parse(value, CultureInfo.InvariantCulture),   // Int（再 ChangeType 适配 int/long）
            2 => value == "true" || value == "1",                   // Bool
            _ => value,                                             // String（含 3 及其它）
        };
        object converted = Convert.ChangeType(parsed, field.FieldType, CultureInfo.InvariantCulture);
        field.SetValue(script, converted);
        return true;
    }

    // 把一个 public 字段编码成 (type, value 字符串) 快照。仅支持可经字符串 ABI
    // round-trip 的 tweakable 类型（float/double、整型、bool、string）；其它类型
    // 跳过（不进快照，reload 后走该字段默认值 / OnStart 重建）。
    private static bool TryEncodeField(FieldInfo field, object instance, out int type, out string value)
    {
        type  = 0;
        value = string.Empty;
        Type    ft  = field.FieldType;
        object? raw = field.GetValue(instance);

        if (ft == typeof(float))
        {
            type  = 0;
            value = ((float)(raw ?? 0.0f)).ToString("R", CultureInfo.InvariantCulture);
            return true;
        }
        if (ft == typeof(double))
        {
            // 与 authored 语义一致：Float tag（回灌时按 float 解析）。double 字段
            // 会降到 float 精度 —— tweakable 体系本就以 float 为主，可接受。
            type  = 0;
            value = ((double)(raw ?? 0.0)).ToString("R", CultureInfo.InvariantCulture);
            return true;
        }
        if (ft == typeof(sbyte) || ft == typeof(byte) || ft == typeof(short) || ft == typeof(ushort)
            || ft == typeof(int) || ft == typeof(uint) || ft == typeof(long) || ft == typeof(ulong))
        {
            type  = 1;
            value = Convert.ToInt64(raw ?? 0L, CultureInfo.InvariantCulture)
                        .ToString(CultureInfo.InvariantCulture);
            return true;
        }
        if (ft == typeof(bool))
        {
            type  = 2;
            value = ((bool)(raw ?? false)) ? "true" : "false";
            return true;
        }
        if (ft == typeof(string))
        {
            type  = 3;
            value = (string?)raw ?? string.Empty;
            return true;
        }
        return false;
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
