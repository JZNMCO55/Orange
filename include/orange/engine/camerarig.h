#ifndef ORANGE_ENGINE_CAMERARIG_H
#define ORANGE_ENGINE_CAMERARIG_H

// CameraRig 子系统便利聚合头：一次性引入 <orange/engine/camerarig/*> 全部公共头。
// 维护：camerarig/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/camerarig/CameraFollow.h>
#include <orange/engine/camerarig/CameraRig.h>
#include <orange/engine/camerarig/CameraShake.h>
#include <orange/engine/camerarig/CameraZoom.h>

#endif // ORANGE_ENGINE_CAMERARIG_H
