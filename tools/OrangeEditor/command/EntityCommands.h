#ifndef ORANGE_EDITOR_COMMAND_ENTITYCOMMANDS_H
#define ORANGE_EDITOR_COMMAND_ENTITYCOMMANDS_H

#include "ICommand.h"

#include <orange/engine/scene/Entity.h>

#include <functional>
#include <string>

namespace Orange::Engine
{
    class World;
}
struct EditorHost;

// ---------------------------------------------------------------------------
// World* 解耦纪律（v0.2.5 commit 14 起）
// ---------------------------------------------------------------------------
//
// 命令一律存 `EditorHost*`（弱引用），不存 `World*`。Execute / Undo 时通过
// `host->scene.pWorld.get()` 间接解 World——切场景时 host.scene.pWorld 换
// 新指针 / 置空，命令自动看到新 World 或走 nullptr 防御分支 no-op。
//
// 配套纪律：scene swap / 破坏性操作（DestroySubtree / RemoveComponent）仍
// 应调用 `CommandStack::Clear()`——旧命令在新 World 上 entity id 大概率
// 无效，能 no-op 但不能正确回放，语义上仍该清栈。c14 改进只是把"漏 Clear
// 必崩"降级为"漏 Clear 安全 no-op"。

// CreateEntityCommand：创建实体（通过调用方提供的 creator lambda）；
// Undo 调 EditorHierarchy::DestroySubtree 销毁所创建的实体。
// Creator 捕获 EditorHost.assets 中所需的材质/网格句柄，命令本身不持有状态。
class CreateEntityCommand : public ICommand
{
public:
    using CreatorFn = std::function<Orange::Engine::Entity(Orange::Engine::World&)>;

    CreateEntityCommand(EditorHost& host, CreatorFn creator);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return "create_entity"; }
    const char* GetLabel() const override { return "Create Entity"; }

    Orange::Engine::Entity CreatedEntity() const { return mCreated; }

private:
    EditorHost*            mpHost;
    CreatorFn              mCreatorFn;
    Orange::Engine::Entity mCreated;
};

// RenameCommand：改 NameComponent.name；支持 coalesce —— 同一实体连续改名
// 时合并为一条撤销步骤。
class RenameCommand : public ICommand
{
public:
    RenameCommand(EditorHost&            host,
                  Orange::Engine::Entity entity,
                  std::string            oldName,
                  std::string            newName);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return "rename_entity"; }
    const char* GetLabel() const override { return "Rename Entity"; }
    bool        Merge(ICommand& newer) override;

private:
    EditorHost*            mpHost;
    Orange::Engine::Entity mEntity;
    std::string            mOldName;
    std::string            mNewName;
};

// SwitchAnimatorBackendCommand：通过 AnimatorRegistry::Create(name) 重建
// AnimatorComponent.animator，把旧 IAnimator 实例销毁。
//
// **有损语义**（v0.7 c1 落地刻意接受）：
//   * Execute / Undo 每次都走 factory.Create()，新 animator 内部状态从 factory
//     fresh 创建（无 elapsed clock / 当前 clip 等），旧实例的 runtime state
//     在切换瞬间丢失
//   * Undo 只恢复 backend 类型，不恢复内部状态——状态保存路径属于 v0.7 c2
//     状态机图编辑器 / v1.x animator runtime state serialization 的范围
//
// **解析 registry / world 走 host 间接**：与其他命令同纪律——切场景 / Animator
// Registry 被替换时，本命令在 Execute/Undo 入口看到 nullptr 自然 no-op，不
// 持有强引用。
class SwitchAnimatorBackendCommand : public ICommand
{
public:
    SwitchAnimatorBackendCommand(EditorHost&            host,
                                 Orange::Engine::Entity entity,
                                 std::string            oldBackendName,
                                 std::string            newBackendName);

    void        Execute() override;
    void        Undo() override;
    const char* GetType() const override { return "switch_animator_backend"; }
    const char* GetLabel() const override { return "Switch Animator Backend"; }

private:
    EditorHost*            mpHost;
    Orange::Engine::Entity mEntity;
    std::string            mOldBackend;
    std::string            mNewBackend;
};

#endif // ORANGE_EDITOR_COMMAND_ENTITYCOMMANDS_H
