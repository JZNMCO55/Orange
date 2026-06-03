// ScriptHost CoreCLR 嵌入通路的端到端 spike 测试（ADR-017 B1.0）。
//
// 不引入测试框架；用 <cassert> + 独立 main()。证明：C++ host 初始化 .NET
// 运行时 → 加载托管 probe assembly → 取一个 [UnmanagedCallersOnly] 静态方法
// 的函数指针 → reinterpret_cast 成已知签名调用 → 返回值正确（fn(41) == 42）。
// 外加错误路径：坏 runtimeconfig / 坏 assembly / 未初始化都返回 Err 而非崩。
//
// **进程内单次运行时启动的约束**：CoreCLR 每进程只能加载一次 runtime（且不卸
// 载）。`hostfxr_close` 只释放 context handle，runtime 仍驻留——故第二次成功的
// `initialize_for_runtime_config` 会撞 hostfxr 的"不支持二次启动"路径。因此本
// 测试只做**一次**成功的运行时启动（happy path），其余错误路径要么不启动运行时
// （坏 config 在读文件阶段就失败 / 未初始化直接返回），要么复用同一个已初始化
// 的 host（坏 assembly 走 GetManagedFunction 错误，不再 init）。
//
// probe DLL + runtimeconfig 的磁盘路径由 CMake 经 target_compile_definitions
// 注入（ORANGE_PROBE_ASSEMBLY_PATH / ORANGE_PROBE_RUNTIMECONFIG_PATH）。

#include <orange/engine/script/ScriptHost.h>

#include <cassert>
#include <cstdio>
#include <string>

using Orange::Engine::ResultCode;
using Orange::Engine::Script::ManagedFunctionPtr;
using Orange::Engine::Script::ScriptHost;

#ifndef ORANGE_PROBE_ASSEMBLY_PATH
#error "ORANGE_PROBE_ASSEMBLY_PATH 未由 CMake 注入"
#endif
#ifndef ORANGE_PROBE_RUNTIMECONFIG_PATH
#error "ORANGE_PROBE_RUNTIMECONFIG_PATH 未由 CMake 注入"
#endif

namespace
{

// 托管 OrangeProbe.Probe.Run 的原生签名：[UnmanagedCallersOnly] int Run(int)。
using ProbeRunFn = int (*)(int);

const std::string kAssemblyPath = ORANGE_PROBE_ASSEMBLY_PATH;
const std::string kRuntimeConfigPath = ORANGE_PROBE_RUNTIMECONFIG_PATH;

// 错误路径 1：未 Initialize 直接取函数 → NotInitialized（不启动运行时）。
void TestNotInitialized()
{
    ScriptHost host;
    auto r = host.GetManagedFunction(
        kAssemblyPath, "OrangeProbe.Probe, HostProbe", "Run");
    assert(r.IsErr());
    assert(r.Error() == ResultCode::NotInitialized);
    std::fprintf(stdout, "  [PASS] get function before init -> NotInitialized\n");
}

// 错误路径 2：坏 runtimeconfig 路径 → Err 而非崩。在任何成功启动前调用——
// hostfxr 在读 config 文件阶段即失败，不会真正加载 runtime。
void TestBadRuntimeConfig()
{
    ScriptHost host;
    auto r = host.Initialize("Z:/definitely/missing/Bad.runtimeconfig.json");
    assert(r.IsErr());
    assert(!host.IsInitialized());
    std::fprintf(stdout, "  [PASS] bad runtimeconfig -> Err (%s)\n",
                 Orange::Engine::ToString(r.Error()));
}

// 正常路径 + 复用同一 host 的错误路径：
//   * init → 取函数指针 → 调用 → 断言 fn(41) == 42；
//   * 同一已初始化 host 上重复 Initialize → AlreadyInitialized（早返回，不碰 hostfxr）；
//   * 同一已初始化 host 上取坏 assembly → Err 而非崩。
// 这是进程内唯一一次真正的 runtime 启动。
void TestHappyPathAndReuse()
{
    ScriptHost host;
    assert(!host.IsInitialized());

    auto initResult = host.Initialize(kRuntimeConfigPath);
    assert(initResult.IsOk());
    assert(host.IsInitialized());

    auto fnResult = host.GetManagedFunction(
        kAssemblyPath, "OrangeProbe.Probe, HostProbe", "Run");
    assert(fnResult.IsOk());

    ManagedFunctionPtr raw = fnResult.Value();
    assert(raw != nullptr);

    auto run = reinterpret_cast<ProbeRunFn>(raw);
    const int out = run(41);
    assert(out == 42);
    std::fprintf(stdout, "  [PASS] happy path: Run(41) == %d\n", out);

    // 重复 Initialize：门面早返回 AlreadyInitialized，不重启运行时。
    auto second = host.Initialize(kRuntimeConfigPath);
    assert(second.IsErr());
    assert(second.Error() == ResultCode::AlreadyInitialized);
    std::fprintf(stdout, "  [PASS] double initialize -> AlreadyInitialized\n");

    // 坏 assembly：runtime 已起，GetManagedFunction 解析失败 → Err 而非崩。
    auto bad = host.GetManagedFunction(
        "Z:/missing/NoSuch.dll", "No.Such.Type, NoSuch", "Run");
    assert(bad.IsErr());
    std::fprintf(stdout, "  [PASS] bad assembly -> Err (%s)\n",
                 Orange::Engine::ToString(bad.Error()));
}

}  // namespace

int main()
{
    std::fprintf(stdout, "[dotnet_host_test] running\n");
    std::fprintf(stdout, "  assembly      = %s\n", kAssemblyPath.c_str());
    std::fprintf(stdout, "  runtimeconfig = %s\n", kRuntimeConfigPath.c_str());

    // 顺序要点：所有"不启动运行时"的检查在前，唯一一次成功启动放最后。
    TestNotInitialized();
    TestBadRuntimeConfig();
    TestHappyPathAndReuse();

    std::fprintf(stdout, "[dotnet_host_test] all tests passed.\n");
    return 0;
}
