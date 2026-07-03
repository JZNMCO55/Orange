#ifndef ORANGE_ENGINE_ASSET_SPIRV_DISK_LOADER_H
#define ORANGE_ENGINE_ASSET_SPIRV_DISK_LOADER_H

// ---------------------------------------------------------------------------
// SpirvDiskLoader —— 从可执行体所在目录的相对路径加载裸 SPIR-V 字节。
//
// 供外部消费者**直接喂 RHI** `ShaderModuleDesc`（绕过 AssetRegistry /
// ShaderAsset handle 路径）。典型场景：editor aux-pass provider（自管 grid /
// outline / wireframe pipeline）、sample、游戏 fork 的启动期一次性 .spv 加载。
//
// 路径锚定到 .exe 同目录（不是 CWD）—— 与引擎内置 shader 落点
// `<exe-dir>/shaders/orange_engine/*.spv` 一致；CWD 与 .exe 目录可能不同。
//
// 返回 word（uint32）向量，可直接 `reinterpret` 进 `ShaderModuleDesc.mpCode` +
// `mCodeSize = size() * 4`。失败（打不开 / 大小非 4 字节倍数）返回**空向量** +
// 记一条 `ORANGE_LOG_ERROR`。
//
// 本头是 header-isolation 安全的公共面：不依赖任何 `<orange/rhi/...>`，只用标准库。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace Orange::Engine::Asset
{

    // 见文件头说明。失败返回空向量。
    ORANGE_ENGINE_API std::vector<std::uint32_t>
                      LoadSpirvFromExecutableDir(std::string_view relativePath) noexcept;

} // namespace Orange::Engine::Asset

#endif // ORANGE_ENGINE_ASSET_SPIRV_DISK_LOADER_H
