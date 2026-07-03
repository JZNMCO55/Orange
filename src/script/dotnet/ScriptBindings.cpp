// ScriptBindings 的实现 —— C# 脚本调引擎的 native 绑定（Pattern A 函数指针表）。
//
// 绑定函数 decode scriptId → entity → 在 g_currentScriptWorld 上取 / 写
// TransformComponent。当前 World 由 ScriptRuntime 在调脚本回调前 SetCurrentWorld
// 设好。无 World / 无效实体 / 无 TransformComponent 一律走安全 fallback
// （读返回零向量、写空操作、IsValid 返回 0），绝不解引用空指针。
//
// 本文件不消费任何 CLR / hostfxr 头 —— 纯引擎公共面 + C ABI。

#include "ScriptBindings.h"

#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

namespace Orange::Engine::Script
{

    namespace
    {

        // 当前脚本 World。脚本在 Play tick 单线程跑，MVP 普通 file-scope static 即可
        // （非线程安全是有意为之，多线程脚本调度不在 MVP 范围）。
        World* sCurrentScriptWorld = nullptr;

        // Input_GetAxis 的测试可设值。真实输入源接入留后续；MVP 默认 0。
        float sInputAxisTestValue = 0.0f;

    } // namespace

    void SetCurrentScriptWorld(World* world) noexcept
    {
        sCurrentScriptWorld = world;
    }

    World* GetCurrentScriptWorld() noexcept
    {
        return sCurrentScriptWorld;
    }

    // Entity id codec：scriptId = entity.Value() + 1（id 0 = none / invalid）。
    // 无效 entity 编码成 0；scriptId==0 解码成 Entity::Invalid()。
    std::uint64_t EncodeEntityId(Entity entity) noexcept
    {
        if (!entity.IsValid())
        {
            return 0;
        }
        return entity.Value() + 1;
    }

    Entity DecodeEntityId(std::uint64_t scriptId) noexcept
    {
        if (scriptId == 0)
        {
            return Entity::Invalid();
        }
        return Entity{static_cast<Entity::ValueType>(scriptId - 1)};
    }

    void SetInputAxisForTest(float value) noexcept
    {
        sInputAxisTestValue = value;
    }

    // --- 绑定函数 -------------------------------------------------------------

    extern "C" ScriptVec3 Orange_Entity_GetPosition(std::uint64_t scriptId)
    {
        ScriptVec3 result{0.0f, 0.0f, 0.0f};
        World*     world = sCurrentScriptWorld;
        if (world == nullptr)
        {
            return result;
        }
        const Entity entity    = DecodeEntityId(scriptId);
        const auto*  transform = world->GetComponent<Scene::TransformComponent>(entity);
        if (transform == nullptr)
        {
            return result;
        }
        result.x = transform->position.x;
        result.y = transform->position.y;
        result.z = transform->position.z;
        return result;
    }

    extern "C" void Orange_Entity_SetPosition(std::uint64_t scriptId, ScriptVec3 v)
    {
        World* world = sCurrentScriptWorld;
        if (world == nullptr)
        {
            return;
        }
        const Entity entity    = DecodeEntityId(scriptId);
        auto*        transform = world->GetComponent<Scene::TransformComponent>(entity);
        if (transform == nullptr)
        {
            return;
        }
        transform->position.x = v.x;
        transform->position.y = v.y;
        transform->position.z = v.z;
    }

    extern "C" int Orange_Entity_IsValid(std::uint64_t scriptId)
    {
        World* world = sCurrentScriptWorld;
        if (world == nullptr)
        {
            return 0;
        }
        const Entity entity = DecodeEntityId(scriptId);
        if (!entity.IsValid())
        {
            return 0;
        }
        return world->IsValid(entity) ? 1 : 0;
    }

    extern "C" float Orange_Input_GetAxis(const char* /*utf8Name*/)
    {
        // MVP：忽略 axis 名，返回测试可设值（默认 0）。真实输入映射留后续。
        return sInputAxisTestValue;
    }

    // --- 绑定表 ---------------------------------------------------------------

    const ScriptBindingTable* GetScriptBindingTable() noexcept
    {
        // 静态存储期，地址全程稳定。初始化即引用四个绑定函数 —— linker 因此
        // keep 它们（不会被当成无引用的 extern "C" 死代码裁掉）。
        static const ScriptBindingTable sTable{
            &Orange_Entity_GetPosition,
            &Orange_Entity_SetPosition,
            &Orange_Entity_IsValid,
            &Orange_Input_GetAxis,
        };
        return &sTable;
    }

} // namespace Orange::Engine::Script
