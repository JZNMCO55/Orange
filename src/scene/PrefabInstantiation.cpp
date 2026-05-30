// PrefabInstantiation 实现 —— PrefabAsset → World 实例化。
//
// 复用既有基建：SaveSubtreeToString/LoadFromString（子树 clone + remap，已测）
// + EntityGuid（ADR-013）。本文件不引入新的子树表示或图操作。

#include "orange/engine/scene/PrefabInstantiation.h"

#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/scene/EntityGuid.h"
#include "orange/engine/scene/HierarchyComponent.h"
#include "orange/engine/scene/PrefabInstanceComponent.h"
#include "orange/engine/scene/SceneSerialization.h"
#include "orange/engine/scene/World.h"

#include <string>
#include <vector>

namespace Orange::Engine::Scene
{

Result<Entity, ResultCode> InstantiatePrefab(World& world,
                                             Asset::AssetRegistry& registry,
                                             Asset::AssetHandle<Asset::PrefabAsset> prefab,
                                             const InstantiateOptions& options)
{
    // MVP 只支持把实例当作新根；挂任意 parent 留编辑器消费 session 用
    // EditorHierarchy::ReparentTo（引擎层不重写兄弟链插入，见头文件注释）。
    if (options.parent.IsValid())
    {
        ORANGE_LOG_ERROR("InstantiatePrefab: MVP 不支持 parent 非 Invalid；"
                         "实例只能作为新根，挂载请走编辑器 EditorHierarchy::ReparentTo");
        return ResultCode::InvalidArgument;
    }

    const Asset::PrefabAsset* asset = registry.Get(prefab);
    if (asset == nullptr)
    {
        ORANGE_LOG_ERROR("InstantiatePrefab: prefab handle 无效或资源已卸载");
        return ResultCode::InvalidArgument;
    }

    // sourcePrefabPath 在 LoadFromString 改动 world 之前先取——PathOf 返回的
    // string_view 生存期只到下一次 registry 改动，立刻拷成 std::string 持有。
    const std::string sourcePrefabPath(registry.PathOf(prefab));

    // 1) 模板 blob → world 追加。LoadFromString 不清空 world、回填 created。
    std::vector<Entity> created;
    const LoadOptions emptyOptions{};
    const LoadOptions& loadOptions = (options.loadOptions != nullptr)
                                         ? *options.loadOptions
                                         : emptyOptions;
    auto loadResult =
        Scene::LoadFromString(asset->TemplateBlob(), world, loadOptions, &created);
    if (loadResult.IsErr())
    {
        ORANGE_LOG_ERROR("InstantiatePrefab: 模板 blob 反序列化失败 (code={})",
                         ToString(loadResult.Error()));
        return loadResult.Error();
    }

    if (created.empty())
    {
        // 空模板没有可作为实例根的实体。LoadFromString 已经没新建任何东西，
        // world 维持原状，直接返回错误。
        ORANGE_LOG_ERROR("InstantiatePrefab: prefab 模板为空（0 实体），无可实例化的根");
        return ResultCode::InvalidArgument;
    }

    // 2) 换新身份：模板 blob 保留了 GuidComponent，不换的话克隆体与模板
    //    共享 GUID。对全部新建实体强制 Reassign。
    Scene::ReassignEntityGuids(world, created);

    // 3) 定位实例根：克隆出来后子树根的 parent 被 remap 成 Invalid（脱钩成
    //    新根，见 LoadFromString 语义）。MVP 单根 prefab——取第一个 parent
    //    为 Invalid 的实体作为根。
    Entity instanceRoot = Entity::Invalid();
    for (const Entity e : created)
    {
        const auto* h = world.GetComponent<HierarchyComponent>(e);
        // 无 HierarchyComponent 的实体也视为根候选（parent 概念上 Invalid）。
        if (h == nullptr || !h->parent.IsValid())
        {
            instanceRoot = e;
            break;
        }
    }
    // created 非空时必然存在至少一个 parent==Invalid 的实体（子树根），故
    // instanceRoot 一定有效；这里防御性断言式兜底，避免后续解引用空 root。
    if (!instanceRoot.IsValid())
    {
        ORANGE_LOG_ERROR("InstantiatePrefab: 内部错误——克隆子树未找到根实体");
        return ResultCode::InternalError;
    }

    // 4) 给每个新建实体挂 PrefabInstanceComponent：同一 sourcePrefabPath +
    //    同一 instanceId（标识"本次实例化"），仅根 isInstanceRoot=true。
    const Core::Guid instanceId = Core::Guid::Generate();
    for (const Entity e : created)
    {
        PrefabInstanceComponent link;
        link.sourcePrefabPath = sourcePrefabPath;
        link.instanceId       = instanceId;
        link.isInstanceRoot   = (e == instanceRoot);
        world.AddComponent(e, std::move(link));
    }

    return instanceRoot;
}

}  // namespace Orange::Engine::Scene
