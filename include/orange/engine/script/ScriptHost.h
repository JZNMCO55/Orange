#ifndef ORANGE_ENGINE_SCRIPT_SCRIPTHOST_H
#define ORANGE_ENGINE_SCRIPT_SCRIPTHOST_H

// ---------------------------------------------------------------------------
// ScriptHost —— Play-in-Editor C# 脚本运行时的公共门面（ADR-017 方案 A）。
//
// 把 CoreCLR 嵌入引擎进程：经官方 nethost / hostfxr 加载 .NET 运行时，
// 用 runtimeconfig 初始化一个 runtime 上下文，再按 (assembly, type, method)
// 取一个 [UnmanagedCallersOnly] 托管方法的原生函数指针交给调用方。
//
// **header isolation**：本头**绝不**暴露任何 CLR / hostfxr 类型——hostfxr.h /
// nethost.h / coreclr_delegates.h 仅出现在 src/script/dotnet/**（唯一消费区，
// 类比 src/physics/box2d/**）。hostfxr handle 等实现细节藏在 PIMPL 之后。
//
// 可失败路径统一走引擎自己的 Core::Result（不抛异常、不和 CLR 错误码互通）。
// 坏 runtimeconfig / 找不到 assembly / 解析方法失败都返回 Err 而非崩溃。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>

#include <memory>
#include <string>

namespace Orange::Engine::Script
{

// 托管方法原生入口的不透明指针类型。调用方按已知签名 reinterpret_cast 回
// 具体函数指针类型（例如 `int (*)(int)`）后再调用。host 侧不感知签名——
// 这与 hostfxr 的 load_assembly_and_get_function_pointer 语义一致：托管方法
// 必须标 [UnmanagedCallersOnly]，过界只传 blittable 值类型。
using ManagedFunctionPtr = void*;

// CLR 嵌入是单进程单运行时的重资源，藏在 PIMPL 之后。一个 ScriptHost 实例
// 对应一次 runtime 初始化（hostfxr_initialize_for_runtime_config）。
class ORANGE_ENGINE_API ScriptHost
{
public:
    ScriptHost();
    ~ScriptHost();

    // 不可拷贝（持有底层 hostfxr handle，复制语义无意义）；可移动。
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;
    ScriptHost(ScriptHost&&) noexcept;
    ScriptHost& operator=(ScriptHost&&) noexcept;

    // 用 runtimeconfig.json 初始化 .NET 运行时。runtimeConfigPath 是 UTF-8
    // 路径（边界处转 wide char 喂给 hostfxr）。重复 Initialize 视为
    // AlreadyInitialized。坏路径 / 坏 runtimeconfig 返回 Err（IoError /
    // InternalError），不崩。
    Result<void> Initialize(const std::string& runtimeConfigPath);

    // 取一个 [UnmanagedCallersOnly] 托管方法的原生函数指针。
    //   * assemblyPath —— 托管 .dll 的 UTF-8 路径；
    //   * typeName     —— assembly-qualified 类型全名，如
    //                     "OrangeProbe.Probe, HostProbe"；
    //   * methodName   —— 静态方法名，如 "Run"。
    // 成功返回非空 ManagedFunctionPtr；任何失败（未初始化 / 找不到 assembly /
    // 找不到类型或方法 / 方法未标 UnmanagedCallersOnly）返回 Err。
    Result<ManagedFunctionPtr> GetManagedFunction(const std::string& assemblyPath,
                                                  const std::string& typeName,
                                                  const std::string& methodName);

    // 关闭运行时上下文（hostfxr_close）。析构会自动调用；显式 Shutdown 允许
    // 提前释放。Shutdown 后可再次 Initialize。
    void Shutdown() noexcept;

    // 是否已成功 Initialize。
    bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Script

#endif  // ORANGE_ENGINE_SCRIPT_SCRIPTHOST_H
