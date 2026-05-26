#include "orange/engine/asset/SpirvDiskLoader.h"

#include "orange/engine/core/Log.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

#if defined(_WIN32)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace Orange::Engine::Asset
{
namespace
{

// 解析当前可执行体所在目录。CWD 与 .exe 目录可能不一致；把 .spv 锚定到
// .exe 同目录更稳（与引擎内置 shader / BuiltinMaterials 的路径解析风格一致）。
std::filesystem::path GetExecutableDir()
{
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD len = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
    {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(std::wstring(buffer, len)).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

}  // namespace

std::vector<std::uint32_t> LoadSpirvFromExecutableDir(std::string_view relativePath) noexcept
{
    const auto fullPath = (GetExecutableDir() / std::string(relativePath)).string();
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file)
    {
        ORANGE_LOG_ERROR("Asset: 无法打开 SPIR-V {}", fullPath);
        return {};
    }
    const std::streamsize size = file.tellg();
    if (size <= 0 || (size % 4) != 0)
    {
        ORANGE_LOG_ERROR("Asset: SPIR-V 大小非法 ({}) for {}",
                         static_cast<long long>(size), fullPath);
        return {};
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), size);
    return words;
}

}  // namespace Orange::Engine::Asset
