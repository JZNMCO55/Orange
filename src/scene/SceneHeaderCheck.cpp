// Scene 公共头自包含性检查。每个公共头都被 include 一次、互相隔
// 离；任一头若不慎引入了多余依赖、长出隐式 include、或自身不再可独
// 立编译，构建会在这里失败。新增 `include/orange/engine/scene/`
// 下的头文件时，记得在这里追加一行 include。

#include "orange/engine/scene/Entity.h"
#include "orange/engine/scene/HierarchyComponent.h"
#include "orange/engine/scene/ISystem.h"
#include "orange/engine/scene/TransformComponent.h"
#include "orange/engine/scene/World.h"

namespace Orange::Engine::Scene
{
namespace
{

[[maybe_unused]] inline constexpr int sSceneHeaderCheckSentinel = 0;

}  // namespace
}  // namespace Orange::Engine::Scene
