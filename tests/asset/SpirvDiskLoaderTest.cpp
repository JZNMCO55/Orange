// SpirvDiskLoaderTest —— 验证公共面 Asset::LoadSpirvFromExecutableDir
// （GAP-2026-05-24-loadspirv-helper-not-publicly-exposed）：
//   * 加载真实存在的内置 .spv（fullscreen.vert.spv，与测试 exe 同目录的
//     shaders/orange_engine/ 下）→ 返回非空 + 大小为 4 字节倍数 + 首 word 是
//     SPIR-V magic（0x07230203）。
//   * 不存在的路径 → 返回空向量（不崩，失败软退）。

#include "orange/engine/asset/SpirvDiskLoader.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

int main()
{
    using Orange::Engine::Asset::LoadSpirvFromExecutableDir;

    // 1) 真实内置 spv（任何 sample / 引擎都会编出 fullscreen.vert.spv）。
    const std::vector<std::uint32_t> words =
        LoadSpirvFromExecutableDir("shaders/orange_engine/fullscreen.vert.spv");
    if (words.empty())
    {
        std::fprintf(stderr, "SpirvDiskLoaderTest: fullscreen.vert.spv 加载为空\n");
        return 1;
    }
    // SPIR-V magic number（小端首 word）。
    constexpr std::uint32_t kSpirvMagic = 0x07230203u;
    if (words.front() != kSpirvMagic)
    {
        std::fprintf(stderr, "SpirvDiskLoaderTest: 首 word 0x%08X != SPIR-V magic\n",
                     words.front());
        return 1;
    }

    // 2) 不存在的路径 → 空向量，不崩。
    const std::vector<std::uint32_t> missing =
        LoadSpirvFromExecutableDir("shaders/orange_engine/__does_not_exist__.spv");
    if (!missing.empty())
    {
        std::fprintf(stderr, "SpirvDiskLoaderTest: 不存在路径应返回空向量\n");
        return 1;
    }

    std::printf("SpirvDiskLoaderTest OK (%zu words)\n", words.size());
    return 0;
}
