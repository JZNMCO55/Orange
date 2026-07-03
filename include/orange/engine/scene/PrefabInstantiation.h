#ifndef ORANGE_ENGINE_SCENE_PREFAB_INSTANTIATION_H
#define ORANGE_ENGINE_SCENE_PREFAB_INSTANTIATION_H

// ---------------------------------------------------------------------------
// PrefabInstantiation —— 把一个 PrefabAsset 实例化到 World。
//
// 实例化 = 模板子树 clone + 稳定身份 + 链接组件：
//   1. 取 PrefabAsset 的模板 blob（SaveSubtreeToString 产出的 scene/world
//      JSON 原文）→ Scene::LoadFromString 追加到 world，得到一份内部引用
//      已重映射的克隆。
//   2. 对新建实体调 Scene::ReassignEntityGuids —— 模板 blob 里保留了
//      GuidComponent（SaveSubtreeToString 天然写出），不换新的话克隆体会与
//      模板（以及彼此）共享身份 GUID，破坏唯一性。
//   3. 给每个新建实体挂 PrefabInstanceComponent（同一 sourcePrefabPath +
//      同一 instanceId；仅实例根 isInstanceRoot=true），把它们绑回源 prefab。
//
// MVP 边界（见上层 gap 报告）：
//   * 整体实例化，无 override、无嵌套 prefab。
//   * 不做编辑器消费（资产浏览器 / 拖入 / 蓝条），那是后续 session 的事。
//   * parent 只支持 Invalid（实例作为新根）—— 见 InstantiateOptions::parent。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/PrefabAsset.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/scene/Entity.h>

namespace Orange::Engine
{
    class World;
}

namespace Orange::Engine::Asset
{
    class AssetRegistry;
}

namespace Orange::Engine::Scene
{

    struct LoadOptions;

    struct InstantiateOptions
    {
        // 实例根挂到哪个父实体下。MVP **只支持 Invalid**（实例作为新根）：
        // 非 Invalid 时 InstantiatePrefab 返回 InvalidArgument。
        //
        // 为什么不在引擎层支持任意 parent：把实例根插进某父的兄弟链需要改写
        // HierarchyComponent 的 parent/firstChild/sibling 链——这是 hierarchy 图
        // 操作，引擎刻意不做（见 HierarchyComponent.h 注释 + EditorHierarchy）。
        // 编辑器消费 session 会在拿到实例根后用 EditorHierarchy::ReparentTo 完成
        // 挂载，那一层才持有图操作逻辑。
        Engine::Entity parent{Engine::Entity::Invalid()};

        // 透传给 Scene::LoadFromString 的 LoadOptions（assetRegistry /
        // materialResolver 等，让模板里的 Renderable 等组件能正确认领资源）。
        // 空 → LoadFromString 用默认 LoadOptions。
        const Scene::LoadOptions* loadOptions{nullptr};
    };

    // 把 prefab 实例化到 world。返回实例根实体。
    //
    // 失败语义：
    //   * prefab handle 无效 / Get<PrefabAsset> 取不到 → InvalidArgument
    //   * options.parent 非 Invalid（MVP 不支持）→ InvalidArgument
    //   * LoadFromString 失败（模板 blob 坏）→ 透传其 ResultCode
    //   * 模板为空（0 实体）→ InvalidArgument（没有可作为根的实体）
    ORANGE_ENGINE_API Result<Entity, ResultCode> InstantiatePrefab(
        World&                                 world,
        Asset::AssetRegistry&                  registry,
        Asset::AssetHandle<Asset::PrefabAsset> prefab,
        const InstantiateOptions&              options = {});

} // namespace Orange::Engine::Scene

#endif // ORANGE_ENGINE_SCENE_PREFAB_INSTANTIATION_H
