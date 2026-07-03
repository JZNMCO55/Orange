#ifndef ORANGE_ENGINE_ASSET_SHADER_ASSET_H
#define ORANGE_ENGINE_ASSET_SHADER_ASSET_H

// ---------------------------------------------------------------------------
// ShaderAsset —— shader 资源的 CPU 数据容器。
//
// 引擎一律消费 SPIR-V（offline 由 glslangValidator 编译）。Asset 层只
// 把整段 SPIR-V word 流读进来；解析 / 反射 / pipeline 化由 Render 模
// 块在后续 task 完成。
//
// SPIR-V 是 32-bit word 序列、按 host endian 存储。我们的 host 在编
// 译期通过 Core::Serialization 的静态断言确认是 little-endian，所以
// 这里直接把磁盘字节按 4 字节一组复制到 vector<uint32_t> 即可——不
// 用做字节序翻转。
//
// `ShaderStage` 标识本 shader 的执行模型；从 SPIR-V 的 OpEntryPoint
// 解析 ExecutionModel 是合法但繁琐，当前"按文件名后缀判
// 定"足够（.vert.spv / .frag.spv / .comp.spv），ShaderLoader 会负责
// 这层映射。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Orange::Engine::Asset
{

    enum class ShaderStage : std::uint32_t
    {
        Unknown = 0,
        Vertex,
        Fragment,
        Compute,
    };

    class ORANGE_ENGINE_API ShaderAsset
    {
    public:
        ShaderAsset() = default;

        ShaderAsset(ShaderStage stage, std::vector<std::uint32_t> spirv)
            : mStage(stage), mSpirV(std::move(spirv))
        {
        }

        ShaderStage                       Stage() const noexcept { return mStage; }
        const std::vector<std::uint32_t>& SpirV() const noexcept { return mSpirV; }

        std::size_t WordCount() const noexcept { return mSpirV.size(); }
        std::size_t ByteSize() const noexcept { return mSpirV.size() * sizeof(std::uint32_t); }

        bool Empty() const noexcept { return mSpirV.empty(); }

    private:
        ShaderStage                mStage{ShaderStage::Unknown};
        std::vector<std::uint32_t> mSpirV;
    };

} // namespace Orange::Engine::Asset

#endif // ORANGE_ENGINE_ASSET_SHADER_ASSET_H
