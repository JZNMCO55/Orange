#ifndef ORANGE_ENGINE_PRELUDE_H
#define ORANGE_ENGINE_PRELUDE_H

// ---------------------------------------------------------------------------
// OrangeEngine prelude —— 精选便利头（对标 UE `CoreMinimal.h` / Bevy prelude）。
//
// 只引入"几乎每个游戏 TU 都要用"的少量核心类型：ECS（World / Entity /
// Transform / Name）、每帧上下文 + Layer、日志 + 时间、glm 核心向量矩阵。
// 消费者写 `#include <orange/engine/prelude.h>` 起步，专门的子系统再按需
// `#include <orange/engine/<subsystem>.h>` 补上。
//
// **刻意最小**：这不是全引擎 umbrella。全引擎 139 个公共头一次性 parse 会
// 拖垮编译时间（UE 早年 monolithic header 的教训），故只聚合子系统粒度 +
// 这个精选核心。需要某个子系统的完整能力时用它的聚合头，别往这里堆。
//
// glm 核心随引擎 PUBLIC 传播；扩展（如 gtc/matrix_transform）仍由消费者
// 按需显式 include，与 glm 自己 `<glm/glm.hpp>` + gtc/gtx 的取向一致。
// ---------------------------------------------------------------------------

#include <orange/engine/core/Log.h>
#include <orange/engine/core/Time.h>

#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>

#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/glm.hpp>

#endif // ORANGE_ENGINE_PRELUDE_H
