#ifndef ORANGE_EDITOR_COMMAND_PREFABCOMMANDS_H
#define ORANGE_EDITOR_COMMAND_PREFABCOMMANDS_H

#include "ICommand.h"

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/PrefabAsset.h>
#include <orange/engine/scene/Entity.h>

#include <memory>

struct EditorHost;

// ---------------------------------------------------------------------------
// InstantiatePrefabCommand —— 把一个 prefab 实例化为新根（命令栈可 undo/redo）。
//
// 与 EntityCommands 同纪律：命令持 `EditorHost*`（弱引用），不持 `World*`——
// 切场景时 host.scene.pWorld 换新指针 / 置空，命令 Execute/Undo 入口经
// host 间接解 World，自动看到新 World 或走 nullptr 防御分支 no-op。
//
// **shared_ptr<Entity> 追踪实例根**：每次 Execute（含 redo 重放）都重新调
// Scene::InstantiatePrefab，得到的实例根是**全新的 EnTT id**——不能把第一次
// Execute 的 id 缓存进值成员，否则 redo 后 Undo 删的是已失效的旧 id（delete-
// undo 踩过的坑）。用 shared_ptr<Entity> 在 Execute 时回写最新根 id，Undo 据
// 此销毁整棵实例子树，二者共享同一块追踪存储。
//
// MVP 边界：实例只作新根（InstantiateOptions.parent = Invalid）；不支持挂到
// 任意 parent / override / 嵌套。
class InstantiatePrefabCommand : public ICommand
{
public:
    InstantiatePrefabCommand(
        EditorHost& host,
        Orange::Engine::Asset::AssetHandle<Orange::Engine::Asset::PrefabAsset> handle);

    void Execute() override;
    void Undo()    override;
    const char* GetType()  const override { return "instantiate_prefab"; }
    const char* GetLabel() const override { return "Instantiate Prefab"; }

    // 当前追踪的实例根（Execute 后有效；Undo 后回 Invalid）。
    Orange::Engine::Entity InstanceRoot() const { return *mRootPtr; }

private:
    EditorHost*                                                            mpHost;
    Orange::Engine::Asset::AssetHandle<Orange::Engine::Asset::PrefabAsset> mHandle;
    // 实例根追踪——Execute / redo 重放时回写新 id，Undo 据此删整棵子树。
    std::shared_ptr<Orange::Engine::Entity> mRootPtr;
};

#endif  // ORANGE_EDITOR_COMMAND_PREFABCOMMANDS_H
