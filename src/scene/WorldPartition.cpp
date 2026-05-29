// WorldPartition 实现：layer manifest CRUD + visibility 查询 + entity 层归属解析。
//
// 简单按 vector + unordered_map<id, index> 维护；插入序保持，让编辑器 UI
// 直接按这个序展示，避免每次 Save 都因 hash 序变化产生 diff。
//
// 头隔离：本 .cpp 仅消费 LayerComponent / World 的公共面，不引入任何
// 第三方头。

#include "orange/engine/scene/WorldPartition.h"

#include "orange/engine/scene/LayerComponent.h"
#include "orange/engine/scene/World.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace Orange::Engine::Scene
{
namespace
{

constexpr std::string_view kDefaultLayerId = "default";

}  // namespace

WorldPartition::WorldPartition()
{
    EnsureDefault();
}

WorldPartition::~WorldPartition() = default;

WorldPartition::WorldPartition(WorldPartition&&) noexcept            = default;
WorldPartition& WorldPartition::operator=(WorldPartition&&) noexcept = default;

std::string_view WorldPartition::DefaultLayerId() noexcept
{
    return kDefaultLayerId;
}

bool WorldPartition::AddLayer(LayerInfo info)
{
    if (info.id.empty())
    {
        return false;
    }
    if (mIndex.find(info.id) != mIndex.end())
    {
        return false;
    }
    const std::size_t newIndex = mLayers.size();
    // 先 emplace 进 vector，再用 vector 里那份 string 当 key 写 index——
    // 这样 RebuildIndex 时无需重新拷贝 key。
    mLayers.push_back(std::move(info));
    mIndex.emplace(mLayers.back().id, newIndex);
    return true;
}

bool WorldPartition::RemoveLayer(std::string_view id)
{
    if (id == kDefaultLayerId)
    {
        // default layer 是兜底 anchor，不允许删；调用方需明确意识到这点。
        return false;
    }
    auto it = mIndex.find(std::string{id});
    if (it == mIndex.end())
    {
        return false;
    }
    const std::size_t pos = it->second;
    mLayers.erase(mLayers.begin() + static_cast<std::ptrdiff_t>(pos));
    // erase 让 [pos..end) 的下标全部减一；重建索引最简单且 layer 数量小
    // （工业惯例 < 几十），不值得做局部修补。
    RebuildIndex();
    return true;
}

void WorldPartition::ResetLayers(std::vector<LayerInfo> layers)
{
    mLayers = std::move(layers);
    RebuildIndex();
    EnsureDefault();
}

bool WorldPartition::MoveLayer(std::string_view id, int delta)
{
    if (delta == 0)
    {
        return false;
    }
    auto it = mIndex.find(std::string{id});
    if (it == mIndex.end())
    {
        return false;
    }
    const std::ptrdiff_t cur  = static_cast<std::ptrdiff_t>(it->second);
    const std::ptrdiff_t last = static_cast<std::ptrdiff_t>(mLayers.size()) - 1;
    std::ptrdiff_t       dst  = cur + delta;
    if (dst < 0)
    {
        dst = 0;
    }
    if (dst > last)
    {
        dst = last;
    }
    if (dst == cur)
    {
        // 已在目标边界，没真正移动——不重建索引、不报 dirty。
        return false;
    }
    // 把 cur 处元素挪到 dst：std::rotate 平移中间区段，其余条目相对序不变。
    if (dst > cur)
    {
        std::rotate(mLayers.begin() + cur,
                    mLayers.begin() + cur + 1,
                    mLayers.begin() + dst + 1);
    }
    else
    {
        std::rotate(mLayers.begin() + dst,
                    mLayers.begin() + cur,
                    mLayers.begin() + cur + 1);
    }
    RebuildIndex();
    return true;
}

bool WorldPartition::HasLayer(std::string_view id) const noexcept
{
    return mIndex.find(std::string{id}) != mIndex.end();
}

const LayerInfo* WorldPartition::GetLayer(std::string_view id) const noexcept
{
    auto it = mIndex.find(std::string{id});
    if (it == mIndex.end())
    {
        return nullptr;
    }
    return &mLayers[it->second];
}

LayerInfo* WorldPartition::GetLayer(std::string_view id) noexcept
{
    auto it = mIndex.find(std::string{id});
    if (it == mIndex.end())
    {
        return nullptr;
    }
    return &mLayers[it->second];
}

bool WorldPartition::IsLayerVisible(std::string_view id) const noexcept
{
    auto it = mIndex.find(std::string{id});
    if (it == mIndex.end())
    {
        // 不存在的 layer id 视为 visible=true——避免"manifest 还没加载完
        // 就有 entity 查询"导致整片场景闪烁消失。真正的"未注册 layer"
        // 由 Save / Editor 路径在 attach-time 检查。
        return true;
    }
    return mLayers[it->second].visible;
}

void WorldPartition::SetLayerVisible(std::string_view id, bool visible)
{
    auto it = mIndex.find(std::string{id});
    if (it == mIndex.end())
    {
        return;
    }
    mLayers[it->second].visible = visible;
}

std::string_view WorldPartition::GetLayerOf(const World& world, Entity entity) const
{
    if (!entity.IsValid())
    {
        return kDefaultLayerId;
    }
    const auto* lc = world.GetComponent<LayerComponent>(entity);
    if (lc == nullptr || lc->layerId.empty())
    {
        return kDefaultLayerId;
    }
    return lc->layerId;
}

bool WorldPartition::IsEntityVisible(const World& world, Entity entity) const
{
    // per-entity hidden override 优先：藏了就直接不可见，不再看 layer。
    if (IsEntityHidden(entity))
    {
        return false;
    }
    return IsLayerVisible(GetLayerOf(world, entity));
}

void WorldPartition::SetEntityHidden(Entity entity, bool hidden)
{
    if (!entity.IsValid())
    {
        return;
    }
    for (auto it = mHiddenEntities.begin(); it != mHiddenEntities.end(); ++it)
    {
        if (*it == entity)
        {
            if (!hidden)
            {
                mHiddenEntities.erase(it);
            }
            return;  // 已在集合里：hidden=true 无需重复 push，false 已 erase。
        }
    }
    if (hidden)
    {
        mHiddenEntities.push_back(entity);
    }
}

bool WorldPartition::IsEntityHidden(Entity entity) const noexcept
{
    for (const auto e : mHiddenEntities)
    {
        if (e == entity)
        {
            return true;
        }
    }
    return false;
}

void WorldPartition::SetLayerOf(World& world, Entity entity, std::string_view layerId)
{
    if (!world.IsValid(entity))
    {
        return;
    }
    LayerComponent lc;
    lc.layerId.assign(layerId);
    world.AddComponent(entity, std::move(lc));
}

void WorldPartition::EnsureDefault()
{
    if (mIndex.find(std::string{kDefaultLayerId}) != mIndex.end())
    {
        return;
    }
    LayerInfo def;
    def.id          = std::string{kDefaultLayerId};
    def.displayName = "Default";
    def.visible     = true;
    // 不在 vector 末尾——希望 default 始终是第一条，让编辑器 UI 渲染顺序
    // 稳定。但 ResetLayers 调用后 default 缺失时补到末尾就行——其余条目
    // 已是调用方期望顺序。这里区分构造期 vs ResetLayers 期没有意义，统
    // 一推到末尾，调用方在 ResetLayers 入参里自己控制顺序。
    const std::size_t newIndex = mLayers.size();
    mLayers.push_back(std::move(def));
    mIndex.emplace(mLayers.back().id, newIndex);
}

void WorldPartition::RebuildIndex()
{
    mIndex.clear();
    mIndex.reserve(mLayers.size());
    for (std::size_t i = 0; i < mLayers.size(); ++i)
    {
        mIndex.emplace(mLayers[i].id, i);
    }
}

}  // namespace Orange::Engine::Scene
