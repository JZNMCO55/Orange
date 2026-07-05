#ifndef ORANGE_ENGINE_PLATFORM_H
#define ORANGE_ENGINE_PLATFORM_H

// Platform 子系统便利聚合头：一次性引入 <orange/engine/platform/*> 全部公共头。
// 维护：platform/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/platform/Window.h>
#include <orange/engine/platform/WindowEvent.h>

#endif // ORANGE_ENGINE_PLATFORM_H
