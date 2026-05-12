#ifndef ORANGE_EDITOR_COMMAND_ENTITYCOMMANDS_H
#define ORANGE_EDITOR_COMMAND_ENTITYCOMMANDS_H

#include "ICommand.h"

#include <orange/engine/scene/Entity.h>

#include <functional>
#include <string>

namespace Orange::Engine { class World; }

// CreateEntityCommand：创建实体（通过调用方提供的 creator lambda）；
// Undo 调 EditorHierarchy::DestroySubtree 销毁所创建的实体。
// Creator 捕获 EditorState 中所需的材质/网格句柄，命令本身不持有状态。
class CreateEntityCommand : public ICommand
{
public:
    using CreatorFn = std::function<Orange::Engine::Entity(Orange::Engine::World&)>;

    CreateEntityCommand(Orange::Engine::World& world, CreatorFn creator);

    void Execute() override;
    void Undo()    override;
    const char* GetType() const override { return "create_entity"; }

    Orange::Engine::Entity CreatedEntity() const { return mCreated; }

private:
    Orange::Engine::World* mpWorld;
    CreatorFn              mCreatorFn;
    Orange::Engine::Entity mCreated;
};

// RenameCommand：改 NameComponent.name；支持 coalesce —— 同一实体连续改名
// 时合并为一条撤销步骤。
class RenameCommand : public ICommand
{
public:
    RenameCommand(Orange::Engine::World& world,
                  Orange::Engine::Entity  entity,
                  std::string            oldName,
                  std::string            newName);

    void Execute() override;
    void Undo()    override;
    const char* GetType() const override { return "rename_entity"; }
    bool Merge(ICommand& newer) override;

private:
    Orange::Engine::World* mpWorld;
    Orange::Engine::Entity mEntity;
    std::string            mOldName;
    std::string            mNewName;
};

#endif  // ORANGE_EDITOR_COMMAND_ENTITYCOMMANDS_H
