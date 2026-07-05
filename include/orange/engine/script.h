#ifndef ORANGE_ENGINE_SCRIPT_H
#define ORANGE_ENGINE_SCRIPT_H

// Script 子系统便利聚合头：一次性引入 <orange/engine/script/*> 全部公共头。
// 这些头是 PIMPL façade（CLR host 细节全藏在 src/script/dotnet/），不因
// ORANGE_ENGINE_WITH_DOTNET 是否开启而暴露任何 hostfxr / CLR 类型。
// 维护：script/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/script/ScriptComponent.h>
#include <orange/engine/script/ScriptHost.h>
#include <orange/engine/script/ScriptRuntime.h>
#include <orange/engine/script/ScriptSystem.h>

#endif // ORANGE_ENGINE_SCRIPT_H
