using System.Reflection;
using System.Runtime.Loader;

namespace Orange;

/// <summary>
/// 游戏程序集的可卸载（collectible）加载上下文（PIE roadmap M8 热重载）。
///
/// 每个实例持有一份 <see cref="isCollectible"/> ALC，把游戏脚本程序集（+ 私有依赖）
/// 与 Default ALC 隔离；卸载它即可释放旧一代游戏代码，令 Play 中重编 + 重载生效。
///
/// **两条正确性前提**（M8 探查结论，务必保持）：
///   1. <see cref="OrangeScriptSDK"/> 及所有框架程序集在 <see cref="Load"/> 返回
///      null → 回退 Default **单副本**。否则会：
///        * 把 OrangeScriptSDK 加载出第二份，令 <c>typeof(OrangeScript)</c> 跨 ALC
///          类型身份不一致（<c>IsAssignableFrom(gameType)</c> 失败）；
///        * 让 C++ 缓存的托管函数指针（全指向 Default 里那份 OrangeScriptSDK）错位。
///   2. 游戏程序集本体用 <c>LoadFromStream</c>（读全字节到内存）加载，**不锁文件**
///      —— 托管等价 M7 的 shadow-copy，令 Play 中重编游戏 dll 可行。
///
/// 卸载协议见 <see cref="ScriptRuntime.UnloadGameAssemblies"/>：调用方须先 Free 所有
/// 脚本 GCHandle，本上下文才可能被 GC 真正回收。
/// </summary>
internal sealed class GameLoadContext : AssemblyLoadContext
{
    // 已加载的游戏程序集（key = 规范化全路径），避免同一 dll 被 LoadFromStream 出多份。
    private readonly Dictionary<string, Assembly> _loaded =
        new(StringComparer.OrdinalIgnoreCase);

    // 私有依赖探测器：用首个游戏程序集的 .deps.json 解析其私有依赖路径。
    private AssemblyDependencyResolver? _resolver;

    public GameLoadContext()
        : base(name: "OrangeGameLoadContext", isCollectible: true)
    {
    }

    /// <summary>
    /// 加载（或复用已加载的）一个游戏程序集。首次加载建 resolver。程序集本体走
    /// <c>LoadFromStream</c>（内存字节，不锁文件）—— 令 Play 中原 dll 可被重编覆盖。
    /// </summary>
    public Assembly GetOrLoadGameAssembly(string fullPath)
    {
        if (_loaded.TryGetValue(fullPath, out Assembly? existing))
        {
            return existing;
        }
        _resolver ??= new AssemblyDependencyResolver(fullPath);
        Assembly asm = LoadFromBytes(fullPath);
        _loaded[fullPath] = asm;
        return asm;
    }

    /// <summary>
    /// 依赖解析。三类分别处理：
    ///   * OrangeScriptSDK glue —— **显式返回已加载的 glue 程序集本体**
    ///     （<c>typeof(OrangeScript).Assembly</c>）。不能返回 null 靠 Default 兜底：
    ///     glue 是经 hostfxr <c>load_assembly_and_get_function_pointer</c> 加载的，
    ///     不在 Default ALC 的可探测范围，且本 ALC 的游戏程序集走 LoadFromStream 无
    ///     磁盘目录上下文供 Default 探测 → 靠兜底会 FileNotFound。显式返回同一实例
    ///     既保证类型身份一致（<c>typeof(OrangeScript).IsAssignableFrom(gameType)</c>），
    ///     又保证 C++ 缓存的托管函数指针不错位（指向的正是这份 glue）。
    ///   * 框架程序集（System.* / Microsoft.* / netstandard / mscorlib / WindowsBase）
    ///     —— null → Default 兜底（它们在 shared framework TPA 里，Default 恒能解析）。
    ///   * 游戏私有依赖 —— 经 resolver 解析后进本 collectible ALC（字节流加载不锁文件）。
    /// </summary>
    protected override Assembly? Load(AssemblyName name)
    {
        if (name.Name == "OrangeScriptSDK")
        {
            return typeof(OrangeScript).Assembly;
        }
        if (IsFrameworkAssembly(name))
        {
            return null;
        }
        string? path = _resolver?.ResolveAssemblyToPath(name);
        if (path != null)
        {
            return LoadFromBytes(path); // 游戏私有依赖：进本 ALC，仍不锁文件
        }
        return null; // 解析不到：交给 Default 兜底
    }

    // 从磁盘读全字节 + 可选 .pdb，经 MemoryStream 加载 —— 不持有文件句柄。
    private Assembly LoadFromBytes(string dllPath)
    {
        byte[]           dllBytes = File.ReadAllBytes(dllPath);
        using var        dllStream = new MemoryStream(dllBytes);
        string           pdbPath   = Path.ChangeExtension(dllPath, ".pdb");
        if (File.Exists(pdbPath))
        {
            using var pdbStream = new MemoryStream(File.ReadAllBytes(pdbPath));
            return LoadFromStream(dllStream, pdbStream);
        }
        return LoadFromStream(dllStream);
    }

    // 是否框架程序集（回退 Default 单副本，它们在 shared framework TPA 里恒可解析）。
    // 保守按名前缀判定 —— M8 场景下游戏无私有 NuGet 依赖，宁可多回退也不误把框架
    // 程序集加载出第二份。
    private static bool IsFrameworkAssembly(AssemblyName name)
    {
        string? n = name.Name;
        if (string.IsNullOrEmpty(n))
        {
            return true;
        }
        return n.StartsWith("System", StringComparison.Ordinal)
            || n.StartsWith("Microsoft", StringComparison.Ordinal)
            || n == "netstandard"
            || n == "mscorlib"
            || n == "WindowsBase";
    }
}
