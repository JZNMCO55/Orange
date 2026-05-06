// World —— PIMPL 骨架 + 非模板生命周期方法占位。
//
// 当前阶段（Phase 2 / Task 03）只把 World 的对象生命周期、移动语义、
// 和"实体创建 / 销毁 / 校验"的非模板表面落到 .cpp。真正的存储 (EnTT
// registry 或自研 sparse-set) 与组件 CRUD 模板的 body 由 Phase 2 /
// Task 04 落地。
//
// 实体生命周期目前给一份**最小骨架实现**（自研 sparse-set 的简化
// 版），让 World 至少能创建 / 销毁 / 校验实体：
//   * mNextIndex 单调递增分配 32-bit index；
//   * mGenerations[index] 记录世代号；DestroyEntity 增 1；
//   * Entity::Value() 把 (index, generation) 打包为 64-bit。
// 这套东西在 Task 04 引入 EnTT 时会被替换；之所以现在就写它，是为了
// IsValid / CreateEntity 这条路径能被简单 sample 与未来的小测试触
// 碰。

#include "orange/engine/scene/World.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Orange::Engine
{
namespace
{

constexpr std::uint64_t kIndexShift = 32;
constexpr std::uint64_t kIndexMask  = 0xFFFFFFFFULL;

constexpr std::uint64_t MakeEntityValue(std::uint32_t index, std::uint32_t generation) noexcept
{
    return (static_cast<std::uint64_t>(generation) << kIndexShift)
        | static_cast<std::uint64_t>(index);
}

constexpr std::uint32_t EntityIndex(std::uint64_t value) noexcept
{
    return static_cast<std::uint32_t>(value & kIndexMask);
}

constexpr std::uint32_t EntityGeneration(std::uint64_t value) noexcept
{
    return static_cast<std::uint32_t>((value >> kIndexShift) & kIndexMask);
}

}  // namespace

struct World::Impl
{
    // 每个 index 一个槽：generation 号 + alive 标志。
    std::vector<std::uint32_t> generations;
    std::vector<bool>          alive;
    std::vector<std::uint32_t> freeIndices;
    std::size_t                liveCount{0};
};

World::World() : mpImpl(std::make_unique<Impl>())
{
}

World::~World() = default;

World::World(World&&) noexcept            = default;
World& World::operator=(World&&) noexcept = default;

Entity World::CreateEntity()
{
    auto& impl = *mpImpl;
    std::uint32_t index = 0;
    if (!impl.freeIndices.empty())
    {
        index = impl.freeIndices.back();
        impl.freeIndices.pop_back();
        impl.alive[index] = true;
    }
    else
    {
        index = static_cast<std::uint32_t>(impl.generations.size());
        impl.generations.push_back(0u);
        impl.alive.push_back(true);
    }
    ++impl.liveCount;
    return Entity{MakeEntityValue(index, impl.generations[index])};
}

void World::DestroyEntity(Entity entity)
{
    if (!IsValid(entity))
    {
        return;
    }
    auto& impl = *mpImpl;
    const std::uint32_t index = EntityIndex(entity.Value());
    impl.alive[index] = false;
    ++impl.generations[index];  // 让旧 handle 失效
    impl.freeIndices.push_back(index);
    if (impl.liveCount > 0)
    {
        --impl.liveCount;
    }
}

bool World::IsValid(Entity entity) const noexcept
{
    if (!entity.IsValid())
    {
        return false;
    }
    const auto& impl = *mpImpl;
    const std::uint32_t index = EntityIndex(entity.Value());
    if (index >= impl.generations.size())
    {
        return false;
    }
    if (!impl.alive[index])
    {
        return false;
    }
    return impl.generations[index] == EntityGeneration(entity.Value());
}

std::size_t World::Size() const noexcept
{
    return mpImpl ? mpImpl->liveCount : 0;
}

bool World::Empty() const noexcept
{
    return Size() == 0;
}

}  // namespace Orange::Engine
