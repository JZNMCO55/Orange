// World —— EnTT registry 的薄包装实现。
//
// 公共 API 的模板组件 CRUD 已在 World.h inline 完成；本 TU 只承担：
//   * 三个非模板生命周期方法（Create / Destroy / IsValid）；
//   * 元信息（Size / Empty）；
//   * 由 ORANGE_ENGINE_API 标记导出但需要在 .cpp 落地的特殊成员
//     （ctor / dtor / move）。
//
// entt::registry 本身是 move-only 友好的，move ctor / move assign 不需
// 要任何额外动作。

#include "orange/engine/scene/World.h"

#include <utility>

namespace Orange::Engine
{

    World::World() = default;

    World::~World() = default;

    World::World(World&&) noexcept            = default;
    World& World::operator=(World&&) noexcept = default;

    Entity World::CreateEntity()
    {
        auto handle = FromEntt(mRegistry.create());
        ++mLiveCount;
        return handle;
    }

    void World::DestroyEntity(Entity entity)
    {
        const auto e = ToEntt(entity);
        if (mRegistry.valid(e))
        {
            // entt::registry::destroy 会一并卸载所有挂在该实体上的组件、
            // 并把 entity 句柄回收到 free list；旧 handle 因 generation
            // 自增而自动失效。
            mRegistry.destroy(e);
            if (mLiveCount > 0)
            {
                --mLiveCount;
            }
        }
    }

    bool World::IsValid(Entity entity) const noexcept
    {
        if (!entity.IsValid())
        {
            return false;
        }
        return mRegistry.valid(ToEntt(entity));
    }

    std::size_t World::Size() const noexcept
    {
        // 手动维护的活实体数：在 Create / Destroy 这两条 World 自有路径
        // 上同步更新。绕过 World 直接走 Registry() 操作实体的代码路径
        // 不会反映到这里——见 World.h 中 Registry() 的注释。
        return mLiveCount;
    }

    bool World::Empty() const noexcept
    {
        return Size() == 0;
    }

} // namespace Orange::Engine
