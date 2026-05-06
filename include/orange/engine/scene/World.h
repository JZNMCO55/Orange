#ifndef ORANGE_ENGINE_SCENE_WORLD_H
#define ORANGE_ENGINE_SCENE_WORLD_H

// ---------------------------------------------------------------------------
// World —— 实体 + 组件存储与查询的根入口。
//
// 当前 task 仅交付公共面：
//   * 实体生命周期（Create / Destroy / IsValid）
//   * 组件 CRUD 的模板声明（AddComponent / GetComponent / HasComponent
//     / RemoveComponent）
//   * 元信息（Size / Empty）
//
// 真正的存储后端（EnTT registry 包装）由 Phase 2 / Task 04 在 .cpp 内
// 部以 PIMPL 形式落地。template 方法在 header 内只声明、不内联——
// Task 04 接通时把 body 与一组 erased 入口一起加入。在那之前对模板
// 方法的调用会是链接器 unresolved symbol，刻意作为"还没接通"的硬信
// 号。
//
// 公共表面**不**包含 view / iteration —— EnTT 的 view 类型族非常具
// 体（archetype-bound、template heavy），把它泄漏到公共 API 会把整个
// 引擎绑死在 EnTT 上。Render 模块下一步如何收集 drawable list（Task
// 06）会决定 query API 该长什么样，再回头补到 World 上。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/scene/Entity.h>

#include <cstddef>
#include <memory>

namespace Orange::Engine
{

class ORANGE_ENGINE_API World
{
public:
    World();
    ~World();

    World(const World&)            = delete;
    World& operator=(const World&) = delete;

    World(World&&) noexcept;
    World& operator=(World&&) noexcept;

    // 实体生命周期
    Entity CreateEntity();
    void   DestroyEntity(Entity entity);
    bool   IsValid(Entity entity) const noexcept;

    // 元信息
    std::size_t Size() const noexcept;   // 当前活跃实体数
    bool        Empty() const noexcept;

    // 组件 CRUD —— 仅声明；body 在 Phase 2 / Task 04 接 EnTT 后补。
    // 组件类型 T 必须是 trivially-relocatable / movable 的"普通数据
    // 类"，不要在里面持有任何裸指针或引用 World 自身。
    template <typename T>
    T& AddComponent(Entity entity, T component);

    template <typename T>
    void RemoveComponent(Entity entity);

    template <typename T>
    bool HasComponent(Entity entity) const noexcept;

    template <typename T>
    T* GetComponent(Entity entity) noexcept;

    template <typename T>
    const T* GetComponent(Entity entity) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_SCENE_WORLD_H
