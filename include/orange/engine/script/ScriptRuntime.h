#ifndef ORANGE_ENGINE_SCRIPT_SCRIPTRUNTIME_H
#define ORANGE_ENGINE_SCRIPT_SCRIPTRUNTIME_H

// ---------------------------------------------------------------------------
// ScriptRuntime —— Play-in-Editor C# 脚本运行时的高层门面（ADR-017 方案 A）。
//
// 建在 ScriptHost（裸 CoreCLR 嵌入）之上，多了三件事：
//   1. 把 C++ 侧的绑定函数指针表（GetScriptBindingTable）经托管 glue 的
//      Bootstrap 推给 C# 运行时——脚本调引擎走的就是这张表（Pattern A 函数
//      指针表，不是 DllImport-against-exe）；
//   2. 维护"当前脚本 World"上下文：绑定函数 decode scriptId → entity 后在
//      该 World 上取组件。SetCurrentWorld 由调用方在调脚本回调前设好；
//   3. 实例化托管 OrangeScript 子类、按生命周期（OnStart / OnUpdate /
//      OnDestroy）回调、释放 GCHandle。
//
// **header isolation**：本头**绝不**暴露任何 CLR / hostfxr 类型——全部 CLR
// 状态藏在 PIMPL 之后，CLR hosting 头只在 src/script/dotnet/** 消费。脚本句柄
// 用不透明 uint64 包装托管 GCHandle 的 IntPtr，调用方不解读。
//
// 可失败路径统一走 Core::Result，不抛异常、不和 CLR 错误码互通。脚本自身
// 抛出的异常在托管 glue 的边界 try/catch（标 stderr），绝不穿回 C++。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/scene/Entity.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Orange::Engine
{
    class World;
}

namespace Orange::Engine::Script
{

    // 托管脚本实例的不透明句柄。内部包装托管 GCHandle 的 IntPtr（指针宽度的
    // 整数）；0 表示无效 / 实例化失败。调用方不解读其位模式。
    struct ScriptInstanceHandle
    {
        std::uint64_t value = 0;

        bool IsValid() const noexcept { return value != 0; }
    };

    // 一个 ScriptRuntime 实例对应一次 CoreCLR 初始化 + 托管 glue 引导。重资源，
    // 不可拷贝（持有 ScriptHost）；可移动。
    class ORANGE_ENGINE_API ScriptRuntime
    {
    public:
        ScriptRuntime();
        ~ScriptRuntime();

        ScriptRuntime(const ScriptRuntime&)            = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;
        ScriptRuntime(ScriptRuntime&&) noexcept;
        ScriptRuntime& operator=(ScriptRuntime&&) noexcept;

        // 起 CoreCLR + 取托管 glue（ScriptRuntime.cs）的 5 个
        // [UnmanagedCallersOnly] 入口 + 调 Bootstrap 把 C++ 绑定函数指针表推给
        // 托管侧。
        //   * runtimeConfigPath —— 托管 glue 程序集旁的 *.runtimeconfig.json，
        //     用于 hostfxr 定位运行时框架（UTF-8 路径）；
        //   * sdkAssemblyPath   —— OrangeScriptSDK.dll 的路径（托管 glue 所在
        //     程序集；CreateInstance 时 game assembly 同目录自动解析）。
        // 重复 Initialize 返回 AlreadyInitialized。任何失败返回 Err 不崩。
        Result<void> Initialize(const std::string& runtimeConfigPath,
                                const std::string& sdkAssemblyPath);

        // 设当前脚本 World：绑定函数 decode scriptId 后在该 World 上取组件。
        // 脚本在 Play tick 单线程跑，MVP 用普通 static 持有，调脚本回调前设好。
        // 传 nullptr 清空上下文。
        void SetCurrentWorld(World* world) noexcept;

        // 实例化一个托管 OrangeScript 子类并绑定到 entity：
        //   * assemblyPath —— game assembly（含脚本类型）的 UTF-8 路径；
        //   * typeName     —— assembly-qualified 类型全名，如
        //                     "OrangeFixtures.Mover, ScriptFixtures"；
        //   * entity       —— 脚本所属实体（内部 EncodeEntityId 注入托管侧）。
        // 成功返回有效 ScriptInstanceHandle（托管侧 GCHandle 保活）；失败
        // （未初始化 / 加载不到 assembly / 找不到类型 / 构造抛异常）返回 Err。
        Result<ScriptInstanceHandle> CreateInstance(const std::string& assemblyPath,
                                                    const std::string& typeName,
                                                    Entity             entity);

        // 生命周期回调。脚本异常在托管边界已 catch，这些调用恒不抛。无效句柄
        // 静默忽略（不崩）。InvokeUpdate 的 dt 单位秒。
        void InvokeStart(ScriptInstanceHandle handle);
        void InvokeUpdate(ScriptInstanceHandle handle, float dt);
        void InvokeDestroy(ScriptInstanceHandle handle);

        // 释放托管 GCHandle（解保活，允许 GC 回收实例）。Release 后句柄失效。
        void Release(ScriptInstanceHandle handle);

        // 按 authored 值写脚本对象的一个 public 实例字段（B1.3 tweakable）。
        //   * handle    —— 目标脚本实例句柄；
        //   * fieldName —— public 字段名；
        //   * fieldType —— ScriptFieldType 的 int 值（0=Float / 1=Int / 2=Bool /
        //     3=String）。本头**不**得 include ScriptComponent.h（跨模块），故这里
        //     用 int 而非枚举；调用方传 static_cast<int>(ScriptFieldType)；
        //   * valueUtf8 —— 字符串形态的值；托管侧按 fieldType 解析后用
        //     System.Reflection 设字段（再 Convert.ChangeType 适配字段真实类型）。
        // 未初始化 / 无效句柄 / 字段不存在 / 解析失败均返回 Err，绝不崩。
        Result<void> SetInstanceField(ScriptInstanceHandle handle,
                                      const std::string&   fieldName,
                                      int                  fieldType,
                                      const std::string&   valueUtf8);

        // --- M8 热重载：状态快照 / 回灌 / 卸载 -----------------------------------
        // 见 ScriptSystem::ReloadWorld —— 这四件由它编排。

        // 反射读脚本实例的 public 字段存进托管侧快照字典（按 entity）。reload 后经
        // RestoreState 回灌，令运行时改过的 tweakable 字段跨热重载保留。成功返回 true。
        // 无效句柄 / 未初始化返回 false（不崩）。
        bool SnapshotState(Entity entity, ScriptInstanceHandle handle);

        // 把先前 SnapshotState 存的字段值回灌进新实例（覆盖 authored 默认）。无对应
        // 快照 / 写回失败返回 false。
        bool RestoreState(Entity entity, ScriptInstanceHandle handle);

        // 卸载当前游戏程序集的可卸载 collectible ALC（.NET 端热重载核心）：断托管侧
        // 唯一强引用 → Unload → 轮询 GC。**调用方须在此之前 Release 所有脚本句柄**。
        // 弱引用在若干轮 GC 内消亡返回 Ok；未消亡返回 Err（InternalError）。未加载任何
        // 游戏 ALC 时返回 Ok。
        //
        // **功能不依赖同步回收**：无论 Ok/Err，调用后托管侧已断引用，下次 CreateInstance
        // 会建新 ALC 载新代码 —— ReloadWorld 收到 Err 只 warn 不中断。**已知宿主限制**：
        // 本宿主 C++ 经 reverse-P/Invoke 驱动 glue，实测加载 collectible 程序集时进程内
        // 有 reverse-P/Invoke 栈帧会令 CoreCLR 持久 GC-root 该 ALC，故同步弱引用消亡通常
        // 达不到（返回 Err），旧 ALC 内存延后回收。功能性热重载（卸旧引用/载新代码/回灌
        // 状态）不受影响。详见 M8 探查记录。
        Result<void> UnloadGameAssemblies();

        // 清空托管侧状态快照字典（reload 收尾）。未初始化时 no-op。
        void ClearSnapshots();

        // 是否已成功 Initialize。
        bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> mpImpl;
    };

} // namespace Orange::Engine::Script

#endif // ORANGE_ENGINE_SCRIPT_SCRIPTRUNTIME_H
