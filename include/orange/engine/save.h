#ifndef ORANGE_ENGINE_SAVE_H
#define ORANGE_ENGINE_SAVE_H

// Save 子系统便利聚合头：一次性引入 <orange/engine/save/*> 全部公共头。
// 维护：save/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/save/AutosaveScheduler.h>
#include <orange/engine/save/SaveableComponent.h>
#include <orange/engine/save/SaveGameRegistry.h>
#include <orange/engine/save/SaveGameSystem.h>
#include <orange/engine/save/SavePath.h>
#include <orange/engine/save/SlotManager.h>

#endif // ORANGE_ENGINE_SAVE_H
