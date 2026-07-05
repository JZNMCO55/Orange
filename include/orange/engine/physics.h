#ifndef ORANGE_ENGINE_PHYSICS_H
#define ORANGE_ENGINE_PHYSICS_H

// Physics 子系统便利聚合头：一次性引入 <orange/engine/physics/*> 全部公共头。
// 维护：physics/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/physics/BodyHandle.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/physics/LayerVisibilitySync.h>
#include <orange/engine/physics/PhysicsQuery.h>
#include <orange/engine/physics/PhysicsWorld.h>
#include <orange/engine/physics/RigidBodyComponent.h>

#endif // ORANGE_ENGINE_PHYSICS_H
