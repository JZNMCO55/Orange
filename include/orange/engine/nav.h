#ifndef ORANGE_ENGINE_NAV_H
#define ORANGE_ENGINE_NAV_H

// Nav 子系统便利聚合头：一次性引入 <orange/engine/nav/*> 全部公共头。
// 维护：nav/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/nav/NavGrid.h>
#include <orange/engine/nav/NavGridBake.h>
#include <orange/engine/nav/Pathfinding.h>

#endif // ORANGE_ENGINE_NAV_H
