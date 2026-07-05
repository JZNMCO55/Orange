#ifndef ORANGE_ENGINE_AUDIO_H
#define ORANGE_ENGINE_AUDIO_H

// Audio 子系统便利聚合头：一次性引入 <orange/engine/audio/*> 全部公共头。
// 维护：audio/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/audio/AudioEngine.h>
#include <orange/engine/audio/AudioSourceComponent.h>
#include <orange/engine/audio/Sound.h>
#include <orange/engine/audio/SoundInstance.h>

#endif // ORANGE_ENGINE_AUDIO_H
