#include "PrefabCommands.h"

#include "../EditorHierarchy.h"
#include "../EditorHost.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/scene/PrefabInstantiation.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/World.h>

// ---------------------------------------------------------------------------
// 防御 helper —— 把 host->scene.pWorld 解出来；任一环为空返回 nullptr。
// 与 EntityCommands 同纪律：所有命令的 Execute / Undo 入口经此间接解 World，
// 统一漏 Clear 时的安全降级行为（切场景后旧命令安全 no-op）。
// ---------------------------------------------------------------------------
namespace
{
    Orange::Engine::World* ResolveWorld(EditorHost* pHost)
    {
        if (pHost == nullptr)
        {
            return nullptr;
        }
        return pHost->scene.pWorld.get();
    }
} // anonymous namespace

InstantiatePrefabCommand::InstantiatePrefabCommand(
    EditorHost&                                                            host,
    Orange::Engine::Asset::AssetHandle<Orange::Engine::Asset::PrefabAsset> handle)
    : mpHost(&host), mHandle(handle), mRootPtr(std::make_shared<Orange::Engine::Entity>(
                                          Orange::Engine::Entity::Invalid()))
{
}

void InstantiatePrefabCommand::Execute()
{
    auto* pWorld = ResolveWorld(mpHost);
    if (pWorld == nullptr)
    {
        return;
    } // 漏 Clear 的安全降级
    auto* pReg = mpHost->assets.pAssets.get();
    if (pReg == nullptr)
    {
        return;
    }

    // LoadOptions 填全 4 字段（抄 delete-undo 的 undo lambda 填法）——让模板
    // blob 里的 Renderable / Animator 等组件能正确认领资源；漏填会让实例化出来
    // 的实体丢材质 / animator。
    Orange::Engine::Scene::LoadOptions lo;
    lo.assetRegistry          = pReg;
    lo.animatorRegistry       = mpHost->assets.pAnimators.get();
    lo.namedMaterialInstances = &mpHost->assets.namedMaterialInstances;
    lo.extraSerializers       = mpHost->extraSerializers;

    // MVP 仅 parent = Invalid（实例作为新根）。
    Orange::Engine::Scene::InstantiateOptions opt{};
    opt.parent      = Orange::Engine::Entity::Invalid();
    opt.loadOptions = &lo;

    auto r = Orange::Engine::Scene::InstantiatePrefab(*pWorld, *pReg, mHandle, opt);
    if (r.IsOk())
    {
        *mRootPtr = r.Value(); // 回写最新实例根 id（redo 是新 EnTT id）
    }
}

void InstantiatePrefabCommand::Undo()
{
    if (!mRootPtr->IsValid())
    {
        return;
    }
    auto* pWorld = ResolveWorld(mpHost);
    if (pWorld == nullptr)
    {
        return;
    }
    // World::IsValid 走 registry.valid()（含 EnTT version 检查），甄别"句柄非
    // 零但已被销毁"的死实体，避免对死实体调 DestroySubtree（double-destroy →
    // EnTT assert）。
    if (pWorld->IsValid(*mRootPtr))
    {
        EditorHierarchy::DestroySubtree(*pWorld, *mRootPtr);
    }
    *mRootPtr = Orange::Engine::Entity::Invalid();
}
