#ifndef ORANGE_ENGINE_SCENE_H
#define ORANGE_ENGINE_SCENE_H

// Scene 子系统便利聚合头：一次性引入 <orange/engine/scene/*> 全部公共头。
// 维护：scene/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/scene/ComponentSerializerEntry.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/EntityGuid.h>
#include <orange/engine/scene/GuidComponent.h>
#include <orange/engine/scene/HierarchyComponent.h>
#include <orange/engine/scene/ISystem.h>
#include <orange/engine/scene/LayerComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/PrefabInstanceComponent.h>
#include <orange/engine/scene/PrefabInstantiation.h>
#include <orange/engine/scene/PrefabOverride.h>
#include <orange/engine/scene/SceneSerialization.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/TransformMath.h>
#include <orange/engine/scene/TransformSystem.h>
#include <orange/engine/scene/World.h>
#include <orange/engine/scene/WorldPartition.h>
#include <orange/engine/scene/WorldTransformComponent.h>

#endif // ORANGE_ENGINE_SCENE_H
