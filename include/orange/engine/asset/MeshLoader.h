#ifndef ORANGE_ENGINE_ASSET_MESH_LOADER_H
#define ORANGE_ENGINE_ASSET_MESH_LOADER_H

// ---------------------------------------------------------------------------
// MeshLoader —— 引擎自有 mesh 二进制格式的同步加载器。
//
// 文件格式（little-endian、紧凑、无 padding）：
//   bytes 0..3   magic = 'O' 'R' 'M' 'E'
//   bytes 4..7   version = 1
//   bytes 8..11  vertexCount  (uint32)
//   bytes 12..15 indexCount   (uint32)
//   bytes 16..   positions[vertexCount] : float[3]
//   随后        indices[indexCount]    : uint32
//
// 选择自有格式而不接 OBJ / glTF 是 Phase 2 的 scope 决策：避免在
// Asset 模块上线时同时解决"第三方解析器 vendoring"这个独立问题。后
// 续 task（在 stb / tinygltf 等 vendor 落地后）可以新增对应的
// MeshLoader 子类，与本类并存。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/IAssetLoader.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/core/Result.h>

#include <memory>
#include <string_view>

namespace Orange::Engine::Asset
{

class ORANGE_ENGINE_API MeshLoader final : public IAssetLoader<MeshAsset>
{
public:
    // 'O','R','M','E' 按 little-endian 读出：内存字节序 0x4F,0x52,0x4D,0x45
    // 反推 uint32 = 0x454D524F。
    static constexpr std::uint32_t kMagic = 0x454D524FU;
    static constexpr std::uint32_t kSupportedVersion = 1;

    MeshLoader() = default;
    ~MeshLoader() override = default;

    Result<std::unique_ptr<MeshAsset>, ResultCode> Load(std::string_view path) override;
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_MESH_LOADER_H
