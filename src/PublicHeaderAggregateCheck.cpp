// 便利聚合头（prelude + 各子系统 <orange/engine/<name>.h>）的自包含性检查。
//
// 把 prelude 与全部子系统聚合头在这一个 TU 里 include 一次，编进
// orange_engine。任一聚合头写错路径、漏掉某个头、或某头组合到一起时
// 冲突（宏撞名等），构建会在这里失败，而不是延后到 OrangeGames 消费者
// 那一刻。个头的自包含性另由各模块 src/<mod>/<Mod>HeaderCheck.cpp 保证；
// 本 TU 专门守聚合头这一层。
//
// 新增一个子系统聚合头时，在此追加一行。聚合头内部是否漏登记子头，则由
// scripts/check_invariants.py 的 aggregator-completeness 规则机器强制。

#include "orange/engine/prelude.h"

#include "orange/engine/animation.h"
#include "orange/engine/app.h"
#include "orange/engine/asset.h"
#include "orange/engine/audio.h"
#include "orange/engine/camerarig.h"
#include "orange/engine/core.h"
#include "orange/engine/input.h"
#include "orange/engine/nav.h"
#include "orange/engine/noise.h"
#include "orange/engine/particle.h"
#include "orange/engine/physics.h"
#include "orange/engine/platform.h"
#include "orange/engine/render.h"
#include "orange/engine/save.h"
#include "orange/engine/scene.h"
#include "orange/engine/script.h"
#include "orange/engine/tilemap.h"
#include "orange/engine/tween.h"

namespace Orange::Engine
{
    namespace
    {

        [[maybe_unused]] inline constexpr int sPublicHeaderAggregateCheckSentinel = 0;

    } // namespace
} // namespace Orange::Engine
