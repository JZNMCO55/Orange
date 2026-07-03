// Asset 公共头自包含性检查。每个公共头都被 include 一次、互相隔
// 离；任一头若不慎引入了多余依赖、长出隐式 include、或自身不再可独
// 立编译，构建会在这里失败。新增 `include/orange/engine/asset/`
// 下的头文件时，记得在这里追加一行 include。

#include "orange/engine/asset/AssetHandle.h"
#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/asset/IAssetLoader.h"
#include "orange/engine/asset/MeshAsset.h"
#include "orange/engine/asset/MeshLoader.h"
#include "orange/engine/asset/PrefabAsset.h"
#include "orange/engine/asset/PrefabLoader.h"
#include "orange/engine/asset/ShaderAsset.h"
#include "orange/engine/asset/ShaderLoader.h"
#include "orange/engine/asset/TextureAsset.h"
#include "orange/engine/asset/TextureLoader.h"

namespace Orange::Engine::Asset
{
    namespace
    {

        [[maybe_unused]] inline constexpr int sAssetHeaderCheckSentinel = 0;

    } // namespace
} // namespace Orange::Engine::Asset
