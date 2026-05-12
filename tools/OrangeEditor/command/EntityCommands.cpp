#include "EntityCommands.h"

#include "../EditorHierarchy.h"

#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/World.h>

// ---------------------------------------------------------------------------
// CreateEntityCommand
// ---------------------------------------------------------------------------

CreateEntityCommand::CreateEntityCommand(Orange::Engine::World& world,
                                         CreatorFn              creator)
    : mpWorld(&world)
    , mCreatorFn(std::move(creator))
    , mCreated(Orange::Engine::Entity::Invalid())
{}

void CreateEntityCommand::Execute()
{
    mCreated = mCreatorFn(*mpWorld);
}

void CreateEntityCommand::Undo()
{
    if (!mCreated.IsValid()) { return; }
    // World::IsValid 走 registry.valid()（含 EnTT version 检查），能甄别
    // 虽然句柄非零但已被销毁的死实体 —— Entity::IsValid 只检查哨兵 null，
    // 不足以防止对死实体调 DestroySubtree（double-destroy → EnTT assert）。
    if (!mpWorld->IsValid(mCreated)) {
        mCreated = Orange::Engine::Entity::Invalid();
        return;
    }
    EditorHierarchy::DestroySubtree(*mpWorld, mCreated);
    mCreated = Orange::Engine::Entity::Invalid();
}

// ---------------------------------------------------------------------------
// RenameCommand
// ---------------------------------------------------------------------------

RenameCommand::RenameCommand(Orange::Engine::World& world,
                             Orange::Engine::Entity  entity,
                             std::string            oldName,
                             std::string            newName)
    : mpWorld(&world)
    , mEntity(entity)
    , mOldName(std::move(oldName))
    , mNewName(std::move(newName))
{}

void RenameCommand::Execute()
{
    using NC = Orange::Engine::Scene::NameComponent;
    if (auto* nc = mpWorld->GetComponent<NC>(mEntity))
    {
        nc->name = mNewName;
    }
}

void RenameCommand::Undo()
{
    using NC = Orange::Engine::Scene::NameComponent;
    if (auto* nc = mpWorld->GetComponent<NC>(mEntity))
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
