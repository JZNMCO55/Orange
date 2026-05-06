#ifndef ORANGE_ENGINE_ASSET_SHADER_LOADER_H
#define ORANGE_ENGINE_ASSET_SHADER_LOADER_H

// ---------------------------------------------------------------------------
// ShaderLoader —— SPIR-V 二进制的同步加载器。
//
// 文件直接是 .spv 字节流：
//   * 前 4 字节是 SPIR-V magic 0x07230203 (little-endian)；
//   * 余下按 32-bit word 排列；
//   * 总字节数必须是 4 的整数倍。
//
// `ShaderStage` 推断走文件名后缀映射，规则与业内常见约定一致：
//   *.vert.spv → Vertex
//   *.frag.spv → Fragment
//   *.comp.spv → Compute
//   其它       → Unknown（仍然加载成功，stage 字段留作 Unknown，由
//                调用方在创建 pipeline 时自己声明）
//
// 选这条而不去解析 OpEntryPoint 是 Phase 2 的范围决策：解析 SPIR-V
// 模块是 Render 模块该做的事（pipeline 创建时本来就要走一遍反射），
// Asset 层只负责把字节读进来。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/IAssetLoader.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/core/Result.h>

#include <cstdint>
#include <memory>
#include <string_view>

namespace Orange::Engine::Asset
{

class ORANGE_ENGINE_API ShaderLoader final : public IAssetLoader<ShaderAsset>
{
public:
    static constexpr std::uint32_t kSpirVMagic = 0x07230203U;

    ShaderLoader() = default;
    ~ShaderLoader() override = default;

    Result<std::unique_ptr<ShaderAsset>, ResultCode> Load(std::string_view path) override;

    // 暴露给单元测试的纯函数；不依赖磁盘 IO，方便覆盖各种后缀。
    static ShaderStage StageFromPath(std::string_view path) noexcept;
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_SHADER_LOADER_H
