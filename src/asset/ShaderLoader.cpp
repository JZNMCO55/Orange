#include "orange/engine/asset/ShaderLoader.h"

#include "orange/engine/core/Serialization.h"

#include <cstdint>
#include <cstring>

namespace Orange::Engine::Asset
{
namespace
{

// 大小写不敏感的子串匹配——文件后缀检测用，不区分 .VERT.SPV /
// .vert.spv。
bool EndsWithIgnoreCase(std::string_view text, std::string_view suffix) noexcept
{
    if (text.size() < suffix.size())
    {
        return false;
    }
    const std::size_t offset = text.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i)
    {
        char a = text[offset + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b)
        {
            return false;
        }
    }
    return true;
}

}  // namespace

ShaderStage ShaderLoader::StageFromPath(std::string_view path) noexcept
{
    if (EndsWithIgnoreCase(path, ".vert.spv")) return ShaderStage::Vertex;
    if (EndsWithIgnoreCase(path, ".frag.spv")) return ShaderStage::Fragment;
    if (EndsWithIgnoreCase(path, ".comp.spv")) return ShaderStage::Compute;
    return ShaderStage::Unknown;
}

Result<std::unique_ptr<ShaderAsset>, ResultCode> ShaderLoader::Load(std::string_view path)
{
    auto bytesResult = BinaryReader::LoadFile(path);
    if (bytesResult.IsErr())
    {
        return bytesResult.Error();
    }
    const auto& bytes = bytesResult.Value();

    // SPIR-V 严格按 32-bit word 排列；非 4 字节倍数视作畸形。
    if ((bytes.size() % sizeof(std::uint32_t)) != 0)
    {
        return ResultCode::InvalidArgument;
    }
    if (bytes.size() < sizeof(std::uint32_t))
    {
        return ResultCode::InvalidArgument;
    }

    // 头 4 字节是 SPIR-V magic。host 是 little-endian（已在 Core 静态
    // 断言），可直接从字节流复制。
    std::uint32_t magic = 0;
    std::memcpy(&magic, bytes.data(), sizeof(magic));
    if (magic != kSpirVMagic)
    {
        return ResultCode::SchemaMismatch;
    }

    std::vector<std::uint32_t> words;
    words.resize(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), bytes.data(), bytes.size());

    return std::make_unique<ShaderAsset>(StageFromPath(path), std::move(words));
}

}  // namespace Orange::Engine::Asset
