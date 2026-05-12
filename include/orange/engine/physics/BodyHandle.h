#ifndef ORANGE_ENGINE_PHYSICS_BODY_HANDLE_H
#define ORANGE_ENGINE_PHYSICS_BODY_HANDLE_H

// ---------------------------------------------------------------------------
// BodyHandle —— PhysicsWorld 中 body 的不透明强类型句柄。
//
// 用 Core::TypedHandle 携带 phantom tag，让 BodyHandle 与其它 typed handle
// （AssetHandle / 未来的 ConstraintHandle）在编译期不可互换。
//
// PhysicsWorld 内部如何分配 / 解读这个 64 位 value 是后端细节（Box2D
// 后端拆 32 位 index + 32 位 generation 走 b2BodyId
// 等价模式），消费者按 IsValid() / 等值比较使用即可。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Handle.h>

namespace Orange::Engine::Physics
{

// phantom tag —— 仅作类型区分，不需要任何成员
struct PhysicsBodyTag
{};

using BodyHandle = ::Orange::Engine::TypedHandle<PhysicsBodyTag>;

}  // namespace Orange::Engine::Physics

#endif  // ORANGE_ENGINE_PHYSICS_BODY_HANDLE_H
