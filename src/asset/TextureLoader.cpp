#include "orange/engine/asset/TextureLoader.h"

#include "orange/engine/core/Log.h"
#include "orange/engine/core/Serialization.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// stb_image 单头实现：仅在本 TU 内 expand。v1.1 起启用 .hdr / .png /
// .jpg / .tga 四种解码器，覆盖 DCC 工作流主流贴图格式（PolyHaven HDRI
// + Blender / Substance / Photoshop 默认导出）。BMP / PSD / GIF / PIC /
// PNM 仍关闭——这些在 DCC 流水线里几乎用不到，关掉省 ~80 KB 静态库体积。
//
// HDR 是 stb_image 自家"我编的"路径之一，被禁用的 NO_HDR 反向是"启用
// HDR 解码"，所以这里**不**写 STBI_NO_HDR。STBI_NO_LINEAR 也**不**写——
// 该宏关掉 stbi_loadf 系列 API（不是 LDR→linear 转换的语义）。
//
// MSVC 把警告当 error，stb_image.h 内部有 C4100（unreferenced parameter）/
// C4244（narrowing conversion）/ C4996（CRT deprecation）等噪音，include
// 周围 push/disable/pop 关掉。与 Pipeline.cpp 内 stb_image_write 同款做法。
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4100)  // unreferenced formal parameter
#  pragma warning(disable: 4244)  // narrowing conversion
#  pragma warning(disable: 4505)  // unreferenced local function (PNG/JPG 禁用副作用)
#  pragma warning(disable: 4996)  // CRT deprecation (sprintf etc.)
#endif
#include "stb_image.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

namespace Orange::Engine::Asset
{
namespace
{

// 取小写后缀（'.' 之后）。返回空 string_view 表示无后缀；不分配。
std::string_view LowerSuffix(std::string_view path) noexcept
{
    auto dotPos = path.rfind('.');
    if (dotPos == std::string_view::npos || dotPos + 1 >= path.size())
    {
        return {};
    }
    return path.substr(dotPos + 1);
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') { ca = static_cast<char>(ca + 32); }
        if (cb >= 'A' && cb <= 'Z') { cb = static_cast<char>(cb + 32); }
        if (ca != cb)
        {
            return false;
        }
    }
    return true;
}

// HDR 路径：用 stbi_loadf 从磁盘读 Radiance RGBE，输出 4-channel float RGBA。
// stb 返回的 float* 由 stbi_image_free 释放；这里**不**走 BinaryReader 因为
// stb 需要 file 路径 / FILE* 接口（自己内部 fopen），从 buffer 加载也行
// （stbi_loadf_from_memory）但要求把整个文件 mmap 进内存，相比直接走文件
// 路径没好处——HDR 资产典型 5-20 MB，stb 自己 fopen + 流式解码内存峰值更低。
Result<std::unique_ptr<TextureAsset>, ResultCode> LoadHdrEquirect(std::string_view path)
{
    // stb 接口要求 0-terminated cstring；string_view 本身可能不带 \0。
    std::string cpath(path);

    int width      = 0;
    int height     = 0;
    int channels   = 0;
    // 第 4 参数 4 = 强制 4 通道（RGBA），stb 自动 expand alpha=1.0f；
    // RGB16Float / RGBA16Float storage image 在 Vulkan 端 4 通道支持最稳，
    // bake 端 source 也按 RGBA 对齐避免 stride 怪味。
    float* pixels = stbi_loadf(cpath.c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr)
    {
        ORANGE_LOG_ERROR("TextureLoader: stbi_loadf failed for '{}': {}",
                         cpath, stbi_failure_reason() ? stbi_failure_reason() : "(no reason)");
        return ResultCode::InvalidArgument;
    }

    if (width <= 0 || height <= 0)
    {
        stbi_image_free(pixels);
        return ResultCode::InvalidArgument;
    }
    // 软上限避免恶意输入诱发巨型分配。HDR equirect 典型 2K (2048×1024)，
    // 极端工作流到 8K (8192×4096)；16K 是合理上限。
    constexpr int kMaxDim = 16 * 1024;
    if (width > kMaxDim || height > kMaxDim)
    {
        stbi_image_free(pixels);
        return ResultCode::OutOfRange;
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t byteCount  = pixelCount * BytesPerPixel(TextureFormat::R32G32B32A32_Float);

    // float* → byte buffer 的字节级转储；stb 已经给的是 RGBA float32，直接
    // memcpy 即可。下游消费方按 R32G32B32A32_Float 重解释。
    std::vector<std::uint8_t> bytes;
    bytes.resize(byteCount);
    std::memcpy(bytes.data(), pixels, byteCount);
    stbi_image_free(pixels);

    return std::make_unique<TextureAsset>(static_cast<std::uint32_t>(width),
                                          static_cast<std::uint32_t>(height),
                                          TextureFormat::R32G32B32A32_Float,
                                          std::move(bytes));
}

// LDR 路径：用 stbi_load 从磁盘读 8-bit PNG / JPG / TGA，输出 4-channel
// RGBA，TextureFormat::R8G8B8A8_UNorm。颜色空间假设：sRGB-encoded（典型
// albedo / basecolor），由消费方在 shader 端 sRGB→linear；本路径不做
// gamma 转换。alpha 缺失时 stb 自动补 1.0（uint8 = 0xFF）。
Result<std::unique_ptr<TextureAsset>, ResultCode> LoadStbImageLdr(std::string_view path)
{
    std::string cpath(path);

    int width    = 0;
    int height   = 0;
    int channels = 0;
    // 第 4 参数 4 = 强制 4 通道（RGBA）；stb 内部 expand 缺失通道：
    //   1ch → RGBA = (gray, gray, gray, 1)
    //   3ch → RGBA = (R, G, B, 1)
    //   4ch → 原样
    stbi_uc* pixels = stbi_load(cpath.c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr)
    {
        ORANGE_LOG_ERROR("TextureLoader: stbi_load failed for '{}': {}",
                         cpath, stbi_failure_reason() ? stbi_failure_reason() : "(no reason)");
        return ResultCode::InvalidArgument;
    }

    if (width <= 0 || height <= 0)
    {
        stbi_image_free(pixels);
        return ResultCode::InvalidArgument;
    }
    // 软上限：LDR 贴图典型 2K-4K，8K 已属罕见；16K 是合理上限。与 HDR 路径一致。
    constexpr int kMaxDim = 16 * 1024;
    if (width > kMaxDim || height > kMaxDim)
    {
        stbi_image_free(pixels);
        return ResultCode::OutOfRange;
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t byteCount  = pixelCount * BytesPerPixel(TextureFormat::R8G8B8A8_UNorm);

    std::vector<std::uint8_t> bytes;
    bytes.resize(byteCount);
    std::memcpy(bytes.data(), pixels, byteCount);
    stbi_image_free(pixels);

    return std::make_unique<TextureAsset>(static_cast<std::uint32_t>(width),
                                          static_cast<std::uint32_t>(height),
                                          TextureFormat::R8G8B8A8_UNorm,
                                          std::move(bytes));
}

// 引擎自有 .texture 二进制容器路径。format 字段允许的取值：
//   1 = R8G8B8A8_UNorm（LDR 主流）
//   2 = R32G32B32A32_Float（HDR 离线 cook 产物，c7 起合法）
Result<std::unique_ptr<TextureAsset>, ResultCode> LoadOrtxContainer(std::string_view path)
{
    auto bytesResult = BinaryReader::LoadFile(path);
    if (bytesResult.IsErr())
    {
        return bytesResult.Error();
    }
    const auto& bytes = bytesResult.Value();
    BinaryReader reader{bytes.data(), bytes.size()};

    std::uint32_t magic = 0;
    if (!reader.Read(magic))
    {
        return ResultCode::InvalidArgument;
    }
    if (magic != TextureLoader::kMagic)
    {
        return ResultCode::SchemaMismatch;
    }

    std::uint32_t version = 0;
    if (!reader.Read(version))
    {
        return ResultCode::InvalidArgument;
    }
    if (version != TextureLoader::kSupportedVersion)
    {
        return ResultCode::SchemaMismatch;
    }

    std::uint32_t width  = 0;
    std::uint32_t height = 0;
    std::uint32_t format = 0;
    if (!reader.Read(width) || !reader.Read(height) || !reader.Read(format))
    {
        return ResultCode::InvalidArgument;
    }

    if (width == 0 || height == 0)
    {
        return ResultCode::InvalidArgument;
    }
    // 软上限避免恶意输入诱发巨型分配；当前场景远小于这个值。
    constexpr std::uint32_t kMaxDim = 16u * 1024u;
    if (width > kMaxDim || height > kMaxDim)
    {
        return ResultCode::OutOfRange;
    }

    TextureFormat fmt = TextureFormat::Unknown;
    switch (format)
    {
        case static_cast<std::uint32_t>(TextureFormat::R8G8B8A8_UNorm):
            fmt = TextureFormat::R8G8B8A8_UNorm;
            break;
        case static_cast<std::uint32_t>(TextureFormat::R32G32B32A32_Float):
            fmt = TextureFormat::R32G32B32A32_Float;
            break;
        default:
            return ResultCode::Unsupported;
    }

    const std::size_t bytesPerPixel = BytesPerPixel(fmt);
    const std::size_t pixelCount    = static_cast<std::size_t>(width) * height;
    const std::size_t pixelBytes    = pixelCount * bytesPerPixel;

    std::vector<std::uint8_t> pixels;
    pixels.resize(pixelBytes);
    if (pixelBytes > 0 && !reader.ReadBytes(pixels.data(), pixelBytes))
    {
        return ResultCode::InvalidArgument;
    }

    return std::make_unique<TextureAsset>(width, height, fmt, std::move(pixels));
}

}  // namespace

Result<std::unique_ptr<TextureAsset>, ResultCode> TextureLoader::Load(std::string_view path)
{
    // 后缀 dispatch：.hdr 走 stbi_loadf（HDR float），.png / .jpg / .jpeg /
    // .tga 走 stbi_load（LDR uint8 RGBA），其它走引擎自有 ORTX 容器。后缀
    // 大小写不敏感（PolyHaven 偶有 .HDR 大写命名；Windows 资源管理器拖入
    // 文件后缀大小写也不稳定）。
    const auto suffix = LowerSuffix(path);
    if (EqualsIgnoreCase(suffix, "hdr"))
    {
        return LoadHdrEquirect(path);
    }
    if (EqualsIgnoreCase(suffix, "png") ||
        EqualsIgnoreCase(suffix, "jpg") ||
        EqualsIgnoreCase(suffix, "jpeg") ||
        EqualsIgnoreCase(suffix, "tga"))
    {
        return LoadStbImageLdr(path);
    }
    return LoadOrtxContainer(path);
}

}  // namespace Orange::Engine::Asset
