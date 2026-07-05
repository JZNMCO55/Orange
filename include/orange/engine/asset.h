#ifndef ORANGE_ENGINE_ASSET_H
#define ORANGE_ENGINE_ASSET_H

// Asset 子系统便利聚合头：一次性引入 <orange/engine/asset/*> 全部公共头。
// 维护：asset/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/IAssetLoader.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/asset/PrefabAsset.h>
#include <orange/engine/asset/PrefabLoader.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/asset/SkeletonAsset.h>
#include <orange/engine/asset/SkeletonLoader.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/asset/SoundLoader.h>
#include <orange/engine/asset/SpirvDiskLoader.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/asset/TextureLoader.h>

#endif // ORANGE_ENGINE_ASSET_H
