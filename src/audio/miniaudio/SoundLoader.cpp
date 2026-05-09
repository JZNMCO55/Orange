// SoundLoader 实现 —— 文件 → 字节缓冲。decode 由 Audio 层在 CreateInstance
// 时再做。本 .cpp 不消费任何 miniaudio 头（loader 与 audio 层解耦），但按
// 任务约定放在 src/audio/miniaudio/ 旁边便于日后引入 miniaudio 元数据探测
// （声道数 / 采样率 hint）时少跨目录。

#include "orange/engine/asset/SoundLoader.h"

#include <fstream>
#include <ios>
#include <string>
#include <vector>

namespace Orange::Engine::Asset
{

Result<std::unique_ptr<SoundAsset>, ResultCode> SoundLoader::Load(std::string_view path)
{
    std::string p{path};
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f)
    {
        return ResultCode::NotFound;
    }
    const std::streamsize sz = f.tellg();
    if (sz < 0)
    {
        return ResultCode::IoError;
    }
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sz));
    if (sz > 0)
    {
        f.read(reinterpret_cast<char*>(bytes.data()), sz);
        if (!f)
        {
            return ResultCode::IoError;
        }
    }
    return std::make_unique<SoundAsset>(std::move(bytes), std::string{path});
}

}  // namespace Orange::Engine::Asset
