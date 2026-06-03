// ScriptHost 的 CoreCLR 实现（ADR-017 方案 A）。
//
// **这是 OrangeEngine 里唯一允许 #include CLR hosting 头（nethost.h /
// hostfxr.h / coreclr_delegates.h）的目录**（CLAUDE.md "Header isolation"
// 不变量，类比 src/physics/box2d/**）。公共门面 ScriptHost.h 不漏任何 CLR /
// hostfxr 类型；底层 hostfxr handle + 函数指针都藏在本 TU 的 Impl 里。
//
// 标准 nativehosting 流程：
//   1. get_hostfxr_path(nethost)  —— 定位本机 hostfxr.dll；
//   2. LoadLibraryW(hostfxr)      —— 载入并取三个函数指针：
//        hostfxr_initialize_for_runtime_config /
//        hostfxr_get_runtime_delegate / hostfxr_close；
//   3. initialize_for_runtime_config(runtimeconfig) —— 起一个 runtime 上下文；
//   4. get_runtime_delegate(hdt_load_assembly_and_get_function_pointer)
//        —— 拿 load_assembly_and_get_function_pointer 委托；
//   5. 用该委托按 (assembly, type, method) + UNMANAGEDCALLERSONLY_METHOD
//        delegate 类型名取 [UnmanagedCallersOnly] 托管方法的原生指针。
//
// 路径全程 wide char：hostfxr 的 char_t 在 Windows 是 wchar_t；门面收 UTF-8
// std::string，在本边界转 std::wstring。

#include <orange/engine/script/ScriptHost.h>

// CLR hosting 头（唯一消费区） ----------------------------------------------
// 默认链 import 库 nethost.lib + 运行期 nethost.dll：get_hostfxr_path 经
// __declspec(dllimport) 解析（nethost.h 默认形态），DLL 不带自己的静态 CRT，
// 故任意 /MDd 或 /MT 配置都不会撞 CRT 不匹配。若改链静态 libnethost.lib，需在
// 此前 `#define NETHOST_USE_AS_STATIC` 且全工程统一 /MT —— 见 CMake 注释。
#include <nethost.h>
#include <coreclr_delegates.h>
#include <hostfxr.h>

#include <windows.h>

#include <string>
#include <vector>

namespace Orange::Engine::Script
{

namespace
{

// UTF-8 → UTF-16（wide）。空串安全返回空 wstring。失败（非法序列）返回空，
// 由调用方按"路径解析不出来"处理。hostfxr 的 char_t 在 Windows 即 wchar_t。
std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
    {
        return std::wstring{};
    }
    const int needed = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0)
    {
        return std::wstring{};
    }
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), needed);
    return wide;
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl —— 全部 CLR / hostfxr 状态的载体。门面 PIMPL 持有它，对外零暴露。
// ---------------------------------------------------------------------------
struct ScriptHost::Impl
{
    // hostfxr.dll 模块 + 三个入口函数指针（步骤 2）。
    HMODULE                                     hostfxrLib = nullptr;
    hostfxr_initialize_for_runtime_config_fn    initFn = nullptr;
    hostfxr_get_runtime_delegate_fn             getDelegateFn = nullptr;
    hostfxr_close_fn                            closeFn = nullptr;

    // 当前 runtime 上下文（步骤 3）+ 取托管函数指针的委托（步骤 4）。
    hostfxr_handle                              context = nullptr;
    load_assembly_and_get_function_pointer_fn   loadAssemblyFn = nullptr;

    bool initialized = false;

    // 释放 runtime 上下文 + 卸载 hostfxr.dll。幂等。
    void Reset() noexcept
    {
        if (context != nullptr && closeFn != nullptr)
        {
            closeFn(context);
        }
        context = nullptr;
        loadAssemblyFn = nullptr;

        if (hostfxrLib != nullptr)
        {
            ::FreeLibrary(hostfxrLib);
        }
        hostfxrLib = nullptr;
        initFn = nullptr;
        getDelegateFn = nullptr;
        closeFn = nullptr;
        initialized = false;
    }

    // RAII：Impl 析构即 Reset。保证 move-assign 覆盖旧 Impl 时也释放 runtime
    // 上下文 + hostfxr 模块——unique_ptr move-assign 会销毁被覆盖的旧 Impl，
    // 经此析构走 Reset，避免"已初始化 host 被移动赋值覆盖时 hostfxr_close /
    // FreeLibrary 不执行"的泄漏（Rule-of-Five）。Reset 幂等，二次调用安全。
    ~Impl() noexcept
    {
        Reset();
    }
};

// ---------------------------------------------------------------------------
// 构造 / 析构 / 移动
// ---------------------------------------------------------------------------
ScriptHost::ScriptHost() : mpImpl(std::make_unique<Impl>())
{
}

// 析构走 default：unique_ptr 销毁 Impl → Impl 析构调 Reset（RAII）释放
// runtime 上下文 + hostfxr 模块。move ctor / move-assign 也都 default，旧 Impl
// 的释放统一由 ~Impl 兜底，不会漏。
ScriptHost::~ScriptHost() = default;

ScriptHost::ScriptHost(ScriptHost&&) noexcept = default;
ScriptHost& ScriptHost::operator=(ScriptHost&&) noexcept = default;

// ---------------------------------------------------------------------------
// Initialize —— 步骤 1~4：定位 hostfxr → 载入取函数指针 → 起 runtime 上下文
// → 取 load_assembly_and_get_function_pointer 委托。
// ---------------------------------------------------------------------------
Result<void> ScriptHost::Initialize(const std::string& runtimeConfigPath)
{
    if (mpImpl->initialized)
    {
        return ResultCode::AlreadyInitialized;
    }

    const std::wstring wideConfig = Utf8ToWide(runtimeConfigPath);
    if (wideConfig.empty())
    {
        return ResultCode::InvalidArgument;
    }

    // 步骤 1：get_hostfxr_path —— 让 nethost 在本机找 hostfxr.dll 全路径。
    wchar_t hostfxrPath[MAX_PATH];
    size_t  hostfxrPathLen = sizeof(hostfxrPath) / sizeof(hostfxrPath[0]);
    if (get_hostfxr_path(hostfxrPath, &hostfxrPathLen, nullptr) != 0)
    {
        return ResultCode::NotFound;
    }

    // 步骤 2：载入 hostfxr.dll + 取三个入口函数指针。
    mpImpl->hostfxrLib = ::LoadLibraryW(hostfxrPath);
    if (mpImpl->hostfxrLib == nullptr)
    {
        return ResultCode::IoError;
    }

    mpImpl->initFn = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
        ::GetProcAddress(mpImpl->hostfxrLib, "hostfxr_initialize_for_runtime_config"));
    mpImpl->getDelegateFn = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
        ::GetProcAddress(mpImpl->hostfxrLib, "hostfxr_get_runtime_delegate"));
    mpImpl->closeFn = reinterpret_cast<hostfxr_close_fn>(
        ::GetProcAddress(mpImpl->hostfxrLib, "hostfxr_close"));
    if (mpImpl->initFn == nullptr || mpImpl->getDelegateFn == nullptr ||
        mpImpl->closeFn == nullptr)
    {
        mpImpl->Reset();
        return ResultCode::InternalError;
    }

    // 步骤 3：用 runtimeconfig 起 runtime 上下文。
    // 注意 hostfxr 约定：初始化成功的返回码可能是 0（Success）或正值
    // Success_HostAlreadyInitialized / Success_DifferentRuntimeProperties，
    // 后两者同样产出可用 context；只有负值才是真失败。
    hostfxr_handle context = nullptr;
    const int initRc = mpImpl->initFn(wideConfig.c_str(), nullptr, &context);
    if (initRc < 0 || context == nullptr)
    {
        if (context != nullptr)
        {
            mpImpl->closeFn(context);
        }
        mpImpl->Reset();
        return ResultCode::IoError;
    }
    mpImpl->context = context;

    // 步骤 4：取 load_assembly_and_get_function_pointer 委托。
    void* delegatePtr = nullptr;
    const int delRc = mpImpl->getDelegateFn(
        context, hdt_load_assembly_and_get_function_pointer, &delegatePtr);
    if (delRc != 0 || delegatePtr == nullptr)
    {
        mpImpl->Reset();
        return ResultCode::InternalError;
    }
    mpImpl->loadAssemblyFn =
        reinterpret_cast<load_assembly_and_get_function_pointer_fn>(delegatePtr);

    mpImpl->initialized = true;
    return Result<void>{};
}

// ---------------------------------------------------------------------------
// GetManagedFunction —— 步骤 5：按 (assembly, type, method) 取
// [UnmanagedCallersOnly] 托管方法的原生函数指针。
//
// delegate 类型名用 UNMANAGEDCALLERSONLY_METHOD 哨兵——告诉 hostfxr 该托管方法
// 标了 [UnmanagedCallersOnly]，签名由调用方 reinterpret_cast 自负，过界只走
// blittable 值类型。
// ---------------------------------------------------------------------------
Result<ManagedFunctionPtr> ScriptHost::GetManagedFunction(const std::string& assemblyPath,
                                                          const std::string& typeName,
                                                          const std::string& methodName)
{
    if (!mpImpl->initialized || mpImpl->loadAssemblyFn == nullptr)
    {
        return ResultCode::NotInitialized;
    }

    const std::wstring wideAssembly = Utf8ToWide(assemblyPath);
    const std::wstring wideType = Utf8ToWide(typeName);
    const std::wstring wideMethod = Utf8ToWide(methodName);
    if (wideAssembly.empty() || wideType.empty() || wideMethod.empty())
    {
        return ResultCode::InvalidArgument;
    }

    void* fnPtr = nullptr;
    const int rc = mpImpl->loadAssemblyFn(
        wideAssembly.c_str(),
        wideType.c_str(),
        wideMethod.c_str(),
        UNMANAGEDCALLERSONLY_METHOD,  // delegate 类型名哨兵
        nullptr,
        &fnPtr);
    if (rc != 0 || fnPtr == nullptr)
    {
        // 找不到 assembly / 类型 / 方法，或方法未标 UnmanagedCallersOnly。
        return ResultCode::NotFound;
    }

    return static_cast<ManagedFunctionPtr>(fnPtr);
}

// ---------------------------------------------------------------------------
// Shutdown / IsInitialized
// ---------------------------------------------------------------------------
void ScriptHost::Shutdown() noexcept
{
    if (mpImpl)
    {
        mpImpl->Reset();
    }
}

bool ScriptHost::IsInitialized() const noexcept
{
    return mpImpl && mpImpl->initialized;
}

}  // namespace Orange::Engine::Script
