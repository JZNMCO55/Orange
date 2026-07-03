// ScriptRuntime 的实现 —— 建在 ScriptHost（裸 CoreCLR 嵌入）之上的高层门面。
//
// **本文件与 ScriptBindings.cpp / ScriptHost.cpp 同属 src/script/dotnet/ 的
// 唯一 CLR 消费区**。但本 TU 本身不直接 include 任何 CLR / hostfxr 头 —— 它
// 经 ScriptHost::GetManagedFunction 取托管 glue 的 [UnmanagedCallersOnly]
// 入口函数指针，按已知 C ABI 签名 reinterpret_cast 后调用。CLR 类型只在
// ScriptHost.cpp 出现。
//
// 流程：
//   Initialize：ScriptHost.Initialize（起 runtime）→ 取 ScriptRuntime.cs 的
//     7 个托管入口（Bootstrap / CreateInstance / InvokeStart / InvokeUpdate /
//     InvokeDestroy / Release / SetInstanceField）→ 调 Bootstrap 把
//     GetScriptBindingTable() 推过去。
//   CreateInstance：EncodeEntityId → 调托管 CreateInstance（加载 game
//     assembly、Activator 构造、注入 Entity、GCHandle 保活）→ 句柄包成
//     ScriptInstanceHandle。
//   InvokeStart/Update/Destroy / Release：转调对应托管入口。
//
// 托管 glue 程序集是 OrangeScriptSDK.dll，类型全名固定
// "Orange.ScriptRuntime, OrangeScriptSDK"。

#include <orange/engine/script/ScriptRuntime.h>
#include <orange/engine/script/ScriptHost.h>

#include "ScriptBindings.h"

#include <cstdint>

namespace Orange::Engine::Script
{

    namespace
    {

        // 托管 glue（ScriptRuntime.cs）的 assembly-qualified 类型全名。
        const char* const kGlueTypeName = "Orange.ScriptRuntime, OrangeScriptSDK";

        // 托管入口的 native 签名（均 [UnmanagedCallersOnly] + Cdecl）。
        //   Bootstrap(IntPtr bindingTablePtr)
        using BootstrapFn = void (*)(const ScriptBindingTable*);
        //   CreateInstance(IntPtr assemblyPathUtf8, IntPtr typeNameUtf8, ulong entityId) -> IntPtr
        using CreateInstanceFn = void* (*)(const char*, const char*, std::uint64_t);
        //   InvokeStart(IntPtr h)
        using InvokeStartFn = void (*)(void*);
        //   InvokeUpdate(IntPtr h, float dt)
        using InvokeUpdateFn = void (*)(void*, float);
        //   InvokeDestroy(IntPtr h)
        using InvokeDestroyFn = void (*)(void*);
        //   Release(IntPtr h)
        using ReleaseFn = void (*)(void*);
        //   SetInstanceField(IntPtr h, IntPtr fieldNameUtf8, int fieldType,
        //                    IntPtr valueUtf8) -> int（1 成功 / 0 失败）
        using SetInstanceFieldFn = int (*)(void*, const char*, int, const char*);

        // 把 ScriptInstanceHandle 的不透明 uint64 与托管 GCHandle 的 IntPtr（void*）
        // 互转。MVP 直接把指针位模式塞进 uint64（64-bit 平台指针 ≤ 64 位）。
        void* HandleToPtr(ScriptInstanceHandle h) noexcept
        {
            return reinterpret_cast<void*>(static_cast<std::uintptr_t>(h.value));
        }

        ScriptInstanceHandle PtrToHandle(void* p) noexcept
        {
            ScriptInstanceHandle h;
            h.value = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p));
            return h;
        }

    } // namespace

    // ---------------------------------------------------------------------------
    // Impl —— 持有 ScriptHost + 7 个托管入口函数指针。
    // ---------------------------------------------------------------------------
    struct ScriptRuntime::Impl
    {
        ScriptHost host;

        BootstrapFn        bootstrapFn        = nullptr;
        CreateInstanceFn   createInstanceFn   = nullptr;
        InvokeStartFn      invokeStartFn      = nullptr;
        InvokeUpdateFn     invokeUpdateFn     = nullptr;
        InvokeDestroyFn    invokeDestroyFn    = nullptr;
        ReleaseFn          releaseFn          = nullptr;
        SetInstanceFieldFn setInstanceFieldFn = nullptr;

        bool initialized = false;
    };

    // ---------------------------------------------------------------------------
    // 构造 / 析构 / 移动
    // ---------------------------------------------------------------------------
    ScriptRuntime::ScriptRuntime() : mpImpl(std::make_unique<Impl>())
    {
    }

    ScriptRuntime::~ScriptRuntime()                                   = default;
    ScriptRuntime::ScriptRuntime(ScriptRuntime&&) noexcept            = default;
    ScriptRuntime& ScriptRuntime::operator=(ScriptRuntime&&) noexcept = default;

    // ---------------------------------------------------------------------------
    // Initialize —— 起 runtime → 取 7 个托管入口 → Bootstrap 推绑定表。
    // ---------------------------------------------------------------------------
    Result<void> ScriptRuntime::Initialize(const std::string& runtimeConfigPath,
                                           const std::string& sdkAssemblyPath)
    {
        if (mpImpl->initialized)
        {
            return ResultCode::AlreadyInitialized;
        }

        // 起 CoreCLR runtime（ScriptHost 负责 nethost / hostfxr / runtimeconfig）。
        auto initResult = mpImpl->host.Initialize(runtimeConfigPath);
        if (initResult.IsErr())
        {
            return initResult.Error();
        }

        // 取托管 glue 的 7 个 [UnmanagedCallersOnly] 入口函数指针。任一缺失即视为
        // glue 与 host 不匹配，返回 Err。
        auto getFn = [&](const char* method, void** out) -> bool
        {
            auto r = mpImpl->host.GetManagedFunction(sdkAssemblyPath, kGlueTypeName, method);
            if (r.IsErr())
            {
                return false;
            }
            *out = r.Value();
            return true;
        };

        void* bootstrap        = nullptr;
        void* createInstance   = nullptr;
        void* invokeStart      = nullptr;
        void* invokeUpdate     = nullptr;
        void* invokeDestroy    = nullptr;
        void* release          = nullptr;
        void* setInstanceField = nullptr;
        if (!getFn("Bootstrap", &bootstrap) ||
            !getFn("CreateInstance", &createInstance) ||
            !getFn("InvokeStart", &invokeStart) ||
            !getFn("InvokeUpdate", &invokeUpdate) ||
            !getFn("InvokeDestroy", &invokeDestroy) ||
            !getFn("Release", &release) ||
            !getFn("SetInstanceField", &setInstanceField))
        {
            return ResultCode::InternalError;
        }

        mpImpl->bootstrapFn        = reinterpret_cast<BootstrapFn>(bootstrap);
        mpImpl->createInstanceFn   = reinterpret_cast<CreateInstanceFn>(createInstance);
        mpImpl->invokeStartFn      = reinterpret_cast<InvokeStartFn>(invokeStart);
        mpImpl->invokeUpdateFn     = reinterpret_cast<InvokeUpdateFn>(invokeUpdate);
        mpImpl->invokeDestroyFn    = reinterpret_cast<InvokeDestroyFn>(invokeDestroy);
        mpImpl->releaseFn          = reinterpret_cast<ReleaseFn>(release);
        mpImpl->setInstanceFieldFn = reinterpret_cast<SetInstanceFieldFn>(setInstanceField);

        // 把 C++ 绑定函数指针表推给托管侧（C# EngineInterop 存为函数指针）。
        mpImpl->bootstrapFn(GetScriptBindingTable());

        mpImpl->initialized = true;
        return Result<void>{};
    }

    // ---------------------------------------------------------------------------
    // SetCurrentWorld —— 转发给绑定层的 file-scope static。
    // ---------------------------------------------------------------------------
    void ScriptRuntime::SetCurrentWorld(World* world) noexcept
    {
        SetCurrentScriptWorld(world);
    }

    // ---------------------------------------------------------------------------
    // CreateInstance —— EncodeEntityId → 调托管 CreateInstance。
    // ---------------------------------------------------------------------------
    Result<ScriptInstanceHandle> ScriptRuntime::CreateInstance(const std::string& assemblyPath,
                                                               const std::string& typeName,
                                                               Entity             entity)
    {
        if (!mpImpl->initialized || mpImpl->createInstanceFn == nullptr)
        {
            return ResultCode::NotInitialized;
        }

        const std::uint64_t scriptId = EncodeEntityId(entity);
        void*               managed  = mpImpl->createInstanceFn(
            assemblyPath.c_str(), typeName.c_str(), scriptId);
        if (managed == nullptr)
        {
            // 加载不到 assembly / 找不到类型 / 构造抛异常（托管侧已 catch + 标
            // stderr，返回 IntPtr.Zero）。
            return ResultCode::NotFound;
        }
        return PtrToHandle(managed);
    }

    // ---------------------------------------------------------------------------
    // 生命周期回调 / Release —— 无效句柄静默忽略。
    // ---------------------------------------------------------------------------
    void ScriptRuntime::InvokeStart(ScriptInstanceHandle handle)
    {
        if (!mpImpl->initialized || !handle.IsValid() || mpImpl->invokeStartFn == nullptr)
        {
            return;
        }
        mpImpl->invokeStartFn(HandleToPtr(handle));
    }

    void ScriptRuntime::InvokeUpdate(ScriptInstanceHandle handle, float dt)
    {
        if (!mpImpl->initialized || !handle.IsValid() || mpImpl->invokeUpdateFn == nullptr)
        {
            return;
        }
        mpImpl->invokeUpdateFn(HandleToPtr(handle), dt);
    }

    void ScriptRuntime::InvokeDestroy(ScriptInstanceHandle handle)
    {
        if (!mpImpl->initialized || !handle.IsValid() || mpImpl->invokeDestroyFn == nullptr)
        {
            return;
        }
        mpImpl->invokeDestroyFn(HandleToPtr(handle));
    }

    void ScriptRuntime::Release(ScriptInstanceHandle handle)
    {
        if (!mpImpl->initialized || !handle.IsValid() || mpImpl->releaseFn == nullptr)
        {
            return;
        }
        mpImpl->releaseFn(HandleToPtr(handle));
    }

    // ---------------------------------------------------------------------------
    // SetInstanceField —— 转调托管 SetInstanceField（反射设 public 字段）。
    // 失败（0 返回）映射成 Err；语义最贴的现有 ResultCode 是 NotFound（字段不
    // 存在 / 解析失败 / 句柄解不出实例都归"目标拿不到"）。
    // ---------------------------------------------------------------------------
    Result<void> ScriptRuntime::SetInstanceField(ScriptInstanceHandle handle,
                                                 const std::string&   fieldName,
                                                 int                  fieldType,
                                                 const std::string&   valueUtf8)
    {
        if (!mpImpl->initialized || mpImpl->setInstanceFieldFn == nullptr)
        {
            return ResultCode::NotInitialized;
        }
        if (!handle.IsValid())
        {
            return ResultCode::InvalidArgument;
        }

        const int ok = mpImpl->setInstanceFieldFn(
            HandleToPtr(handle), fieldName.c_str(), fieldType, valueUtf8.c_str());
        if (ok != 1)
        {
            // 托管侧已 catch 异常 + 标 stderr（字段不存在 / 解析失败 / 转换抛异常）。
            return ResultCode::NotFound;
        }
        return Result<void>{};
    }

    bool ScriptRuntime::IsInitialized() const noexcept
    {
        return mpImpl && mpImpl->initialized;
    }

} // namespace Orange::Engine::Script
