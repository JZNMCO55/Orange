#ifndef ORANGE_EDITOR_EDITOR_HIERARCHY_H
#define ORANGE_EDITOR_EDITOR_HIERARCHY_H

// 父子树维护工具 —— 引擎层 HierarchyComponent 是裸数据（parent + 双向
// sibling chain），刻意不提供 reparent / link / unlink helper；那些是"图
// 操作"语义，引擎自己只有序列化等数据流场景，运行时父子关系变动是编辑
// 器才有的需求。本组函数放在编辑器本地（不进引擎 include/），与 Task
// 06-03 描述里"不引入新公共 API"一致。
//
// 同时该组也是后续 DnD reparent 的底座；当前提供 LinkAsLastChild /
// Detach / IsAncestorOf / ReparentTo / MoveToPosition / MoveBefore /
// MoveAfter（精确位置重排）/ DestroySubtree 完整集合。

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
// 挂为 newParent 的**末子**（位置不精确）。调用方负责防环（IsAncestorOf
// 检查），本函数不再二次校验。
void ReparentTo(Orange::Engine::World& world,
                Orange::Engine::Entity child,
                Orange::Engine::Entity newParent);

// 把 child 移到 `parent` 子链中、紧跟在 `afterSibling` 之后。
//   * parent == Invalid：仅把 child 摘成 root（afterSibling 忽略——根无顺序）。
//   * afterSibling == Invalid：插到 parent 子链最前（成为新 firstChild）。
//   * afterSibling 有效：插到它之后；afterSibling 须当前就是 parent 的子，
//     否则退化为挂尾（防御，不破链）。
// 内部先 Detach(child)，原位置自动让出；child 自身子树随之整体移动。调用方
// 负责防环。这是 reorder 与"精确 undo 复位"共用的底层原语。
void MoveToPosition(Orange::Engine::World& world,
                    Orange::Engine::Entity child,
                    Orange::Engine::Entity parent,
                    Orange::Engine::Entity afterSibling);

// 把 child 移成 target 的前一个兄弟（同父）。target 为 root（无父）时退化为
// 把 child 摘到 root。child 已紧邻 target 之前则 no-op。调用方负责防环。
void MoveBefore(Orange::Engine::World& world,
                Orange::Engine::Entity child,
                Orange::Engine::Entity target);

// 把 child 移成 target 的后一个兄弟（同父）。target 为 root 时退化为把 child
// 摘到 root。child 已紧邻 target 之后则 no-op。调用方负责防环。
void MoveAfter(Orange::Engine::World& world,
               Orange::Engine::Entity child,
               Orange::Engine::Entity target);

// 递归销毁 e 及其整个子树。先收集 child 列表（不能边遍历兄弟链边
// destroy，destroy 会把组件抽走 sibling 字段失效），再依次递归销毁，最
// 后把 e 自己从父链摘下并销毁。
void DestroySubtree(Orange::Engine::World& world, Orange::Engine::Entity e);

// 把 root 在根序（HierarchyComponent.sortIndex，ADR-014）里移动 delta 位
// （delta<0=更靠前/上移，>0=更靠后/下移）。仅作用于根节点（parent==Invalid）；
// 非根 / 单根 / 已在边界 → 不动并返回 false。会把所有根的 sortIndex 规整为
// 0..n-1（无 HC 的根一并补 HC；确保反复 reorder 不漂移、Move 可逆）。返回是否
// 真的发生移动——编辑器据此决定是否记 Undo 命令。
//
// dryRun=true：只做同样的"能否移动"判断（非根 / 单根 / 边界），**不改任何
// 状态、不补 HC、不写 sortIndex**。给调用方做预检——避免"判断时执行一次 + 命令
// 栈 Execute 再执行一次"导致移两位（reorder 直接跳顶/底 bug）。
bool MoveRootRelative(Orange::Engine::World& world,
                      Orange::Engine::Entity root,
                      int delta,
                      bool dryRun = false);

}  // namespace EditorHierarchy

#endif  // ORANGE_EDITOR_EDITOR_HIERARCHY_H
