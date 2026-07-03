#ifndef ORANGE_ENGINE_SCENE_WORLD_H
#define ORANGE_ENGINE_SCENE_WORLD_H

// ---------------------------------------------------------------------------
// World —— 实体 + 组件存储与查询的根入口；EnTT registry 的薄包装。
//
// 公共 API：
//   * 实体生命周期（CreateEntity / DestroyEntity / IsValid / Size /
//     Empty）
//   * 组件 CRUD 模板（AddComponent / GetComponent / HasComponent /
//     RemoveComponent），全部 inline 走 entt::registry 同名接口
//   * `Registry()` 直接暴露底层 entt::registry —— 给需要 view / group
//     / observer 等 EnTT 高级语义的子系统使用（Render 模块收集
//     drawable list、Physics 同步 transform、序列化遍历等）
//
// 设计取舍：为什么把 entt::registry 进公共 API？
//   * 3rdparty.json 早已声明 EnTT 是 PUBLIC 链接——"游戏侧能注册自己
//     的 component 类型"是 ECS 的核心扩展点，要让游戏代码 emplace<T>
//     就必须看见 registry；
//   * 反过来，把 entt 完全包到 PIMPL 里再用 type-erased 接口转发，会
//     把 EnTT 的零开销优势挡在 ABI 之外，等同于自己再造一个 ECS 抽
//     象层；
//   * 后端切换的可能性当前不重要——0.x 阶段不承诺 ABI 稳定，真正要换
//     ECS 后端时整个引擎面都会一起调整，这层抽象省不下成本。
//
// 与 entt::entity 的映射：Entity 内部用 64-bit 数值；entt::entity 是
// 32-bit。我们把 entt::entity 的位模式直接放到 Entity::Value() 的低
// 32 位（高 32 位留 0），不做任何位运算解读——entt 自己已经在 32-bit
// 内拆好了 index + version。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/scene/Entity.h>

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

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
        std::size_t Size() const noexcept; // 当前活跃实体数
        bool        Empty() const noexcept;

        // 组件 CRUD —— 全部 inline 走 entt::registry。组件类型 T 必须可
        // 移动构造，且不要在内部持有指向 World 自身的指针 / 引用（否则
        // 实体迁移 / archetype 重排时会失效）。
        template <typename T>
        T& AddComponent(Entity entity, T component)
        {
            return mRegistry.emplace_or_replace<T>(ToEntt(entity), std::move(component));
        }

        template <typename T>
        void RemoveComponent(Entity entity)
        {
            if (!mRegistry.valid(ToEntt(entity)))
            {
                return;
            }
            mRegistry.remove<T>(ToEntt(entity));
        }

        template <typename T>
        bool HasComponent(Entity entity) const noexcept
        {
            const auto e = ToEntt(entity);
            if (!mRegistry.valid(e))
            {
                return false;
            }
            return mRegistry.all_of<T>(e);
        }

        template <typename T>
        T* GetComponent(Entity entity) noexcept
        {
            const auto e = ToEntt(entity);
            if (!mRegistry.valid(e))
            {
                return nullptr;
            }
            return mRegistry.try_get<T>(e);
        }

        template <typename T>
        const T* GetComponent(Entity entity) const noexcept
        {
            const auto e = ToEntt(entity);
            if (!mRegistry.valid(e))
            {
                return nullptr;
            }
            return mRegistry.try_get<T>(e);
        }

        // 直接拿底层 registry。慎用——这是 EnTT 的逃生舱口，把高级语义
        // (view / group / observer) 暴露给 Render / Physics / 序列化层；
        // 普通游戏代码应优先走 AddComponent / GetComponent / ...
        //
        // 注意：通过 Registry() 直接 create / destroy 实体会绕过 World
        // 自己维护的活实体计数（Size() 用），导致 Size() 失准。这是设
        // 计取舍——给逃生舱口加 wrapper 反而把"等同于 entt::registry"
        // 的承诺削弱了。需要精确实体数的子系统应自己 view 一遍。
        entt::registry&       Registry() noexcept { return mRegistry; }
        const entt::registry& Registry() const noexcept { return mRegistry; }

        // 双向转换辅助。Engine 内部 / 跨模块（Asset、Render、Physics）会
        // 直接拿 entt::entity 用于查 view，所以两侧公开 inline 转换。
        static constexpr entt::entity ToEntt(Entity entity) noexcept
        {
            return entity.IsValid()
                       ? static_cast<entt::entity>(static_cast<std::uint32_t>(entity.Value()))
                       : entt::null;
        }

        static constexpr Entity FromEntt(entt::entity entity) noexcept
        {
            if (entity == entt::null)
            {
                return Entity::Invalid();
            }
            return Entity{static_cast<Entity::ValueType>(static_cast<std::uint32_t>(entity))};
        }

    private:
        entt::registry mRegistry;
        std::size_t    mLiveCount{0};
    };

} // namespace Orange::Engine

#endif // ORANGE_ENGINE_SCENE_WORLD_H
