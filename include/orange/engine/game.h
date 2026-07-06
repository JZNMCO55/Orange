#ifndef ORANGE_ENGINE_GAME_H
#define ORANGE_ENGINE_GAME_H

// game 子系统便利聚合头 —— 一次性引入 <orange/engine/game/*> 全部公共头。
// PIE 玩法宿主接口（ADR-021）：IGameModule 生命周期接口 + GameModuleHost
// 扇出驱动。完整性由 check_invariants.py 的 aggregator-completeness 机器强制。

#include <orange/engine/game/GameModuleHost.h>
#include <orange/engine/game/IGameModule.h>
#include <orange/engine/game/ScriptGameModule.h>

#endif // ORANGE_ENGINE_GAME_H
