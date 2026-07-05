#ifndef ORANGE_ENGINE_APP_H
#define ORANGE_ENGINE_APP_H

// App 子系统便利聚合头：一次性引入 <orange/engine/app/*> 全部公共头。
// 维护：app/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/app/LayerStack.h>

#endif // ORANGE_ENGINE_APP_H
