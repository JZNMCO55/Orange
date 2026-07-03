#ifndef ORANGE_ENGINE_SRC_SCRIPT_DOTNET_SCRIPTBINDINGS_H
#define ORANGE_ENGINE_SRC_SCRIPT_DOTNET_SCRIPTBINDINGS_H

// ---------------------------------------------------------------------------
// ScriptBindings —— C# 脚本调引擎的 native 绑定面（Pattern A 函数指针表）。
//
// 一组 `extern "C"` 普通函数（**不需 dllexport**）操作"当前脚本 World"
// （g_currentScriptWorld）。这些函数的地址被装进 ScriptBindingTable，由
// ScriptRuntime 在 Bootstrap 时整张表推给托管 glue（C# 侧 EngineInterop
// 存为函数指针，call through）。不走 DllImport-against-exe —— 进程导出符号
// 在静态库 / 各链接配置下不可靠，函数指针表是更稳的封送方式（借 Mono
// mono_add_internal_call 的"集中登记"思路，单点登记便于审计绑定面）。
//
// 约定（与 C# 侧 Vec3 [Sequential] / EngineInterop 对齐）：
//   * 实体句柄按 uint64 scriptId 过界（EncodeEntityId / DecodeEntityId 单点
//     编解码，host 注入与绑定 decode 共用）；
//   * Vec3 是 POD struct { float x,y,z; }，与 C# Vec3 + glm::vec3 同布局，
//     blittable 直传零封送；
//   * bool 用 int 0/1 过界，避免 bool ABI 歧义。
//
// **本文件不 include 任何 CLR / hostfxr 头**——纯 C ABI 声明。CLR 消费在
// ScriptRuntime.cpp / ScriptHost.cpp。
// ---------------------------------------------------------------------------

#include <cstdint>

namespace Orange::Engine
{
    class World;
    class Entity;
} // namespace Orange::Engine

namespace Orange::Engine::Script
{

    // 与 C# Orange.Vec3（[StructLayout(Sequential)]）+ glm::vec3 同内存布局。
    // 纯 POD，三个 float 紧排 —— 跨 C++/C# 边界 blittable 直传零封送。
    struct ScriptVec3
    {
        float x;
        float y;
        float z;
    };

    // 当前脚本 World 上下文。脚本在 Play tick 单线程跑，MVP 普通 static 即可。
    // 绑定函数 decode scriptId → entity 后在此 World 上取 / 写组件。
    void   SetCurrentScriptWorld(World* world) noexcept;
    World* GetCurrentScriptWorld() noexcept;

    // Entity id codec（单点）：scriptId = entity.Value() + 1（id 0 = none）。
    // entt 首个实体 Value()==0，而 C# 拿 Id!=0 当 cheap null —— 偏移避冲突。
    std::uint64_t EncodeEntityId(Entity entity) noexcept;
    Entity        DecodeEntityId(std::uint64_t scriptId) noexcept;

    // Input 绑定的测试 hook：让 headless 测试可设一个 axis 值（MVP 无真实输入源）。
    void SetInputAxisForTest(float value) noexcept;

    // --- 绑定函数（C# 经函数指针表调用） --------------------------------------
    // 形参 scriptId 是 EncodeEntityId 编码后的句柄。
    extern "C" ScriptVec3 Orange_Entity_GetPosition(std::uint64_t scriptId);
    extern "C" void       Orange_Entity_SetPosition(std::uint64_t scriptId, ScriptVec3 v);
    extern "C" int        Orange_Entity_IsValid(std::uint64_t scriptId);
    extern "C" float      Orange_Input_GetAxis(const char* utf8Name);

    // 绑定函数指针表。字段顺序 = C# 侧 BindingTable 读取顺序，二者必须一致。
    // 各成员均为 C 调用约定（extern "C" 函数）的指针，与 C# 侧
    // delegate* unmanaged[Cdecl] 对齐。
    struct ScriptBindingTable
    {
        ScriptVec3 (*entityGetPosition)(std::uint64_t scriptId);
        void (*entitySetPosition)(std::uint64_t scriptId, ScriptVec3 v);
        int (*entityIsValid)(std::uint64_t scriptId);
        float (*inputGetAxis)(const char* utf8Name);
    };

    // 取填好的绑定表（静态存储期，天然引用上述绑定函数 —— linker 会 keep）。
    const ScriptBindingTable* GetScriptBindingTable() noexcept;

} // namespace Orange::Engine::Script

#endif // ORANGE_ENGINE_SRC_SCRIPT_DOTNET_SCRIPTBINDINGS_H
