#ifndef ORANGE_ENGINE_CORE_H
#define ORANGE_ENGINE_CORE_H

// Core 子系统便利聚合头：一次性引入 <orange/engine/core/*> 全部公共头。
// 维护：core/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/core/Config.h>
#include <orange/engine/core/Guid.h>
#include <orange/engine/core/Handle.h>
#include <orange/engine/core/Hash.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/core/Memory.h>
#include <orange/engine/core/Profiler.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/core/SchemaVersion.h>
#include <orange/engine/core/Serialization.h>
#include <orange/engine/core/Time.h>

#endif // ORANGE_ENGINE_CORE_H
