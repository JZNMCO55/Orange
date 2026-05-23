// Pipeline 的 capture 路径实现：把 HDR offscreen 主帧 RGBA16Float 内容拷
// 回 CPU + ACES tonemap 后存 PNG。
//
//   1. Pipeline::RequestCapture(path) 仅缓存路径；
//   2. Render() Stage A 末尾若 pendingCapturePath 有值，
//      EnsureCaptureBuffer 按 hdr 尺寸 grow buffer，RecordCaptureCopy
//      在已 Begin 的 offscreenCmd 上追加 transition + CopyTextureToBuffer
//      + 转回 ShaderReadOnly；
//   3. Render() WaitIdle 之后 FinalizeCapture：Map → ACES → stbi_write_png。
//
// stb_image_write 单头惯例：在唯一一个 TU 里 #define IMPLEMENTATION 把
// 符号定义生进来；本 TU 是消费者，从 Pipeline.cpp 主 TU 搬入。

#include "PipelineImpl.h"

// stb_image_write 仅 capture 路径用 PNG 落盘。include 路径由顶层
// CMakeLists 的 BUILD_INTERFACE vendor/stb 提供，不暴露到公共面。MSVC
// 把 sprintf / strcpy 等 CRT 函数标 deprecated，本工程把警告升成错误，
// 故 push/disable C4996（deprecated）+ C4244（type narrowing）等 stb 内部
// 触发的常见噪音，include 完恢复。
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4996)  // 'sprintf' deprecated
#  pragma warning(disable: 4244)  // narrowing conversion
#endif
#include "stb_image_write.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace Orange::Engine::Render
{

bool Pipeline::Impl::EnsureCaptureBuffer()
{
    if (renderDevice == nullptr || hdrColor == nullptr || hdrWidth == 0 || hdrHeight == 0)
    {
        return false;
    }
    // RGBA16Float = 4 通道 × 2 字节 = 8 字节 / 像素。
    const std::uint64_t needed = static_cast<std::uint64_t>(hdrWidth) * hdrHeight * 8ULL;
    if (captureBuffer && captureBufferCapacity >= needed)
    {
        return true;
    }
    renderDevice->WaitIdle();  // 旧 buffer 可能被 in-flight 命令引用 — 等空再释放
    captureBuffer.reset();
    Orange::Rhi::BufferDesc desc{};
    desc.mSize        = needed;
    desc.mUsage       = Orange::Rhi::BufferUsage::Transfer;
    desc.mMemoryUsage = Orange::Rhi::MemoryUsage::GpuToCpu;
    captureBuffer = renderDevice->GetRhiDevice().CreateBuffer(desc);
    if (!captureBuffer)
    {
        ORANGE_LOG_ERROR("Pipeline: capture buffer create 失败 (size={} bytes)", needed);
        captureBufferCapacity = 0;
        return false;
    }
    captureBufferCapacity = needed;
    return true;
}

bool Pipeline::Impl::RecordCaptureCopy(Orange::Rhi::RHICommandList& cmd)
{
    if (!hdrColor || !captureBuffer)
    {
        return false;
    }
    // 主 pass + bloom 后 hdrColor 处于 ShaderReadOnly；先转 TransferSrc。
    cmd.TransitionTexture(*hdrColor,
                          Orange::Rhi::TextureLayout::ShaderReadOnly,
                          Orange::Rhi::TextureLayout::TransferSrc);

    Orange::Rhi::BufferTextureCopyRegion region{};
    region.mBufferOffset = 0;
    region.mMipLevel     = 0;
    region.mArrayLayer   = 0;
    region.mWidth        = hdrWidth;
    region.mHeight       = hdrHeight;
    region.mDepth        = 1;
    cmd.CopyTextureToBuffer(*hdrColor, *captureBuffer, region);

    // 转回 ShaderReadOnly，让 Stage B 的 tonemap pass 仍可 sample。
    cmd.TransitionTexture(*hdrColor,
                          Orange::Rhi::TextureLayout::TransferSrc,
                          Orange::Rhi::TextureLayout::ShaderReadOnly);
    return true;
}

namespace
{

// IEEE-754 binary16 → binary32。subnormal / inf / nan 全部覆盖。
float HalfToFloat(std::uint16_t h) noexcept
{
    const std::uint32_t s = (h >> 15) & 0x1u;
    const std::uint32_t e = (h >> 10) & 0x1Fu;
    const std::uint32_t m = h & 0x3FFu;
    std::uint32_t f = 0;
    if (e == 0)
    {
        if (m == 0)
        {
            f = s << 31;  // ±0
        }
        else
        {
            // subnormal —— normalize 一下到 binary32 形式
            std::uint32_t mantissa = m;
            std::uint32_t shift    = 0;
            while ((mantissa & 0x400u) == 0)
            {
                mantissa <<= 1;
                ++shift;
            }
            mantissa &= 0x3FFu;
            f = (s << 31) | ((127u - 14u - shift) << 23) | (mantissa << 13);
        }
    }
    else if (e == 31)
    {
        f = (s << 31) | 0x7F800000u | (m << 13);  // inf / nan
    }
    else
    {
        f = (s << 31) | ((e + 127u - 15u) << 23) | (m << 13);
    }
    float r;
    std::memcpy(&r, &f, sizeof(float));
    return r;
}

// ACES Narkowicz fit —— 与内置 tonemap.frag 同曲线。
float AcesNarkowicz(float x) noexcept
{
    constexpr float a = 2.51f;
    constexpr float b = 0.03f;
    constexpr float c = 2.43f;
    constexpr float d = 0.59f;
    constexpr float e = 0.14f;
    const float result = (x * (a * x + b)) / (x * (c * x + d) + e);
    return std::clamp(result, 0.0f, 1.0f);
}

}  // namespace

void Pipeline::Impl::FinalizeCapture()
{
    if (!pendingCapturePath.has_value() || !captureBuffer || hdrWidth == 0 || hdrHeight == 0)
    {
        pendingCapturePath.reset();
        return;
    }
    const std::filesystem::path outPath = std::move(*pendingCapturePath);
    pendingCapturePath.reset();

    void* mapped = captureBuffer->Map();
    if (mapped == nullptr)
    {
        ORANGE_LOG_ERROR("Pipeline: capture buffer Map 失败");
        return;
    }

    const std::uint32_t pixelCount = hdrWidth * hdrHeight;
    std::vector<std::uint8_t> ldr(static_cast<std::size_t>(pixelCount) * 4);
    const std::uint16_t* hdrPx = static_cast<const std::uint16_t*>(mapped);

    for (std::uint32_t i = 0; i < pixelCount; ++i)
    {
        const std::uint16_t* p = hdrPx + i * 4;
        float r = AcesNarkowicz(HalfToFloat(p[0]));
        float g = AcesNarkowicz(HalfToFloat(p[1]));
        float b = AcesNarkowicz(HalfToFloat(p[2]));
        ldr[i * 4 + 0] = static_cast<std::uint8_t>(r * 255.0f + 0.5f);
        ldr[i * 4 + 1] = static_cast<std::uint8_t>(g * 255.0f + 0.5f);
        ldr[i * 4 + 2] = static_cast<std::uint8_t>(b * 255.0f + 0.5f);
        ldr[i * 4 + 3] = 255;  // alpha 一律不透明，HDR alpha 不参与 tonemap
    }

    captureBuffer->Unmap();

    const std::string outStr = outPath.string();
    const int ok = stbi_write_png(outStr.c_str(),
                                  static_cast<int>(hdrWidth),
                                  static_cast<int>(hdrHeight),
                                  4,
                                  ldr.data(),
                                  static_cast<int>(hdrWidth * 4));
    if (ok == 0)
    {
        ORANGE_LOG_ERROR("Pipeline: stbi_write_png 失败 path={}", outStr);
        return;
    }
    ORANGE_LOG_INFO("Pipeline: capture saved to {} ({}x{})", outStr, hdrWidth, hdrHeight);
}

}  // namespace Orange::Engine::Render
