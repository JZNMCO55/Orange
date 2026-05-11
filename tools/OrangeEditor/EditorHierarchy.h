#ifndef ORANGE_EDITOR_EDITOR_HIERARCHY_H
#define ORANGE_EDITOR_EDITOR_HIERARCHY_H

// 父子树维护工具 —— 引擎层 HierarchyComponent 是裸数据（parent + 双向
// sibling chain），刻意不提供 reparent / link / unlink helper；那些是"图
// 操作"语义，引擎自己只有序列化等数据流场景，运行时父子关系变动是编辑
// 器才有的需求。本组函数放在编辑器本地（不进引擎 include/），与 Task
// 06-03 描述里"不引入新公共 API"一致。
//
// 同时该组也是后续 DnD reparent 的底座；当前提供 LinkAsLastChild /
// Detach / IsAncestorOf / ReparentTo / DestroySubtree 完整集合。

#include <orange/engine/scene/Entity.h>  // Entity 按值传，需要完整定义

namespace Orange::Engine
{
class World;
}

namespace EditorHierarchy
{

// 把 child 挂到 parent 的子链末尾。child 进入时假定为 detached（parent
// 为 Invalid）。种子构造期顺序调用即可保证；运行时调用前先 Detach。
void LinkAsLastChild(Orange::Engine::World& world,
                     Orange::Engine::Entity parent,
                     Orange::Engine::Entity child);

// 把实体从其父亲的子链上摘下来，使其变为 root。不销毁实体本身、不动
// firstChild —— 整个子树仍然挂在该实体下，只是它从父链脱离。
void Detach(Orange::Engine::World& world, Orange::Engine::Entity e);

// `ancestor` 是不是 `descendant` 的祖先（含本身）。DnD 防环用。
bool IsAncestorOf(Orange::Engine::World& world,
                  Orange::Engine::Entity ancestor,
                  Orange::Engine::Entity descendant);

// child 改挂到 newParent 下；newParent == Invalid 时把 child 提到 root。
// 调用方负责防环（IsAncestorOf 检查），本函数不再二次校验。
void ReparentTo(Orange::Engine::World& world,
                Orange::Engine::Entity child,
                Orange::Engine::Entity newParent);

// 递归销毁 e 及其整个子树。先收集 child 列表（不能边遍历兄弟链边
// destroy，destroy 会把组件抽走 sibling 字段失效），再依次递归销毁，最
// 后把 e 自己从父链摘下并销毁。
void DestroySubtree(Orange::Engine::World& world, Orange::Engine::Entity e);

}  // namespace EditorHierarchy

#endif  // ORANGE_EDITOR_EDITOR_HIERARCHY_H
