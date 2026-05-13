#include "EntityCommands.h"

#include "../EditorHierarchy.h"
#include "../EditorHost.h"

#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/World.h>

// ---------------------------------------------------------------------------
// 防御 helper —— 把 host->scene.pWorld 解出来；任一环为空返回 nullptr。
// 调用方判 nullptr 即可决定是 no-op 还是继续操作。所有命令的 Execute /
// Undo 入口都走这个 helper，统一漏 Clear 时的安全降级行为。
// ---------------------------------------------------------------------------
namespace
{
Orange::Engine::World* ResolveWorld(EditorHost* pHost)
{
    if (pHost == nullptr) { return nullptr; }
    return pHost->scene.pWorld.get();
}
}  // anonymous namespace

// ---------------------------------------------------------------------------
// CreateEntityCommand
// ---------------------------------------------------------------------------

CreateEntityCommand::CreateEntityCommand(EditorHost& host, CreatorFn creator)
    : mpHost(&host)
    , mCreatorFn(std::move(creator))
    , mCreated(Orange::Engine::Entity::Invalid())
{}

void CreateEntityCommand::Execute()
{
    auto* pWorld = ResolveWorld(mpHost);
    if (pWorld == nullptr) { return; }   // 漏 Clear 的安全降级
    mCreated = mCreatorFn(*pWorld);
}

void CreateEntityCommand::Undo()
{
    if (!mCreated.IsValid()) { return; }
    auto* pWorld = ResolveWorld(mpHost);
    if (pWorld == nullptr) { return; }
    // World::IsValid 走 registry.valid()（含 EnTT version 检查），能甄别
    // 虽然句柄非零但已被销毁的死实体 —— Entity::IsValid 只检查哨兵 null，
    // 不足以防止对死实体调 DestroySubtree（double-destroy → EnTT assert）。
    if (!pWorld->IsValid(mCreated)) {
        mCreated = Orange::Engine::Entity::Invalid();
        return;
    }
    EditorHierarchy::DestroySubtree(*pWorld, mCreated);
    mCreated = Orange::Engine::Entity::Invalid();
}

// ---------------------------------------------------------------------------
// RenameCommand
// ---------------------------------------------------------------------------

RenameCommand::RenameCommand(EditorHost&            host,
                             Orange::Engine::Entity entity,
                             std::string            oldName,
                             std::string            newName)
    : mpHost(&host)
    , mEntity(entity)
    , mOldName(std::move(oldName))
    , mNewName(std::move(newName))
{}

void RenameCommand::Execute()
{
    using NC = Orange::Engine::Scene::NameComponent;
    auto* pWorld = ResolveWorld(mpHost);
    if (pWorld == nullptr) { return; }
    if (auto* nc = pWorld->GetComponent<NC>(mEntity))
    {
        nc->name = mNewName;
    }
}

void RenameCommand::Undo()
{
    using NC = Orange::Engine::Scene::NameComponent;
    auto* pWorld = ResolveWorld(mpHost);
    if (pWorld == nullptr) { return; }
    if (auto* nc = pWorld->GetComponent<NC>(mEntity))
    {
        nc->name = mOldName;
    }
}

bool RenameCommand::Merge(ICommand& newer)
{
    auto& n = static_cast<RenameCommand&>(newer);
    if (n.mEntity != mEntity) { return false; }
    mNewName = std::move(n.mNewName);
    return true;
}
