// Pipeline 的 capture 路径实现：把 HDR offscreen 主帧 RGBA16Float 内容拷
// 回 CPU + tonemap 后存 PNG。
//
//   1. Pipeline::RequestCapture(path) 仅缓存路径；
//   2. Render() Stage A 末尾若 pendingCapturePath 有值，
//      EnsureCaptureBuffer 按 hdr 尺寸 grow buffer，RecordCaptureCopy
//      在已 Begin 的 offscreenCmd 上追加 transition + CopyTextureToBuffer
//      + 转回 ShaderReadOnly；
//   3. Render() WaitIdle 之后 FinalizeCapture：Map → 按 chain 活动
//      TonemapPass.op + exposure 选算子（ACES_Narkowicz / AgX / Reinhard /
//      Linear）→ stbi_write_png。
//
// 与 stage B tonemap.frag.glsl 的 4 算子对应关系按 enum class TonemapOperator
// 同款（PostProcessPasses.h）。早期实现 hardcode 跑 ACES_Narkowicz 不接
// chain.TonemapPass，导致 `--tonemap=...` CLI 切换在 sample 14 capture 上
// 看不出差异（BUG-2026-05-28-pipeline-capture-tonemap-hardcoded-aces）。
// 不接 bloom：FinalizeCapture 拿到的 hdrColor 是 stage A 主帧 + bloom 前
// 的 HDR，与 shader 路径的 `hdr + bloom * intensity` 合成有 visual 偏差；
// 该偏差在 4 算子 PR-review 对照里同向、可接受。需要 bloom 合成结果时
// 等 stage B capture 接口拉动（GAP 留待后续）。
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
#pragma warning(push)
#pragma warning(disable : 4996) // 'sprintf' deprecated
#pragma warning(disable : 4244) // narrowing conversion
#endif
#include "stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <glm/glm.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <cmath>
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
        renderDevice->WaitIdle(); // 旧 buffer 可能被 in-flight 命令引用 — 等空再释放
        captureBuffer.reset();
        Orange::Rhi::BufferDesc desc{};
        desc.mSize        = needed;
        desc.mUsage       = Orange::Rhi::BufferUsage::Transfer;
        desc.mMemoryUsage = Orange::Rhi::MemoryUsage::GpuToCpu;
        captureBuffer     = renderDevice->GetRhiDevice().CreateBuffer(desc);
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
            std::uint32_t       f = 0;
            if (e == 0)
            {
                if (m == 0)
                {
                    f = s << 31; // ±0
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
                f = (s << 31) | 0x7F800000u | (m << 13); // inf / nan
            }
            else
            {
                f = (s << 31) | ((e + 127u - 15u) << 23) | (m << 13);
            }
            float r;
            std::memcpy(&r, &f, sizeof(float));
            return r;
        }

        // ACES Narkowicz fit —— 与内置 tonemap.frag ACESNarkowicz 同曲线。
        glm::vec3 AcesNarkowicz(const glm::vec3& x) noexcept
        {
            constexpr float a      = 2.51f;
            constexpr float b      = 0.03f;
            constexpr float c      = 2.43f;
            constexpr float d      = 0.59f;
            constexpr float e      = 0.14f;
            const glm::vec3 result = (x * (a * x + b)) / (x * (c * x + d) + e);
            return glm::clamp(result, glm::vec3(0.0f), glm::vec3(1.0f));
        }

        // AgX (Troy Sobotka minimal fit) —— 与 tonemap.frag AgX 同矩阵 + 多项式。
        glm::vec3 AgxApprox(const glm::vec3& x) noexcept
        {
            const glm::vec3 x2 = x * x;
            const glm::vec3 x4 = x2 * x2;
            return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;
        }

        glm::vec3 AgX(const glm::vec3& hdr) noexcept
        {
            // 与 tonemap.frag glm::mat3 同款 sRGB → AgX wide gamut 矩阵。glm::mat3
            // 列优先，注意与 GLSL 行优先看上去转置；这里按列填入与 GLSL 一致的
            // 物理映射。
            const glm::mat3 agxMat(
                0.842479062253094f, 0.0784335999999992f, 0.0792237451477643f,
                0.0423282422610123f, 0.878468636469772f, 0.0791661274605434f,
                0.0423756549057051f, 0.0784336f, 0.879142973793104f);
            const glm::mat3 agxMatInv(
                1.19687900512017f, -0.0980208811401368f, -0.0990297440797205f,
                -0.0528968517574562f, 1.15190312990417f, -0.0989611768448433f,
                -0.0529716355144438f, -0.0980434501171241f, 1.15107367264116f);

            constexpr float minEv = -12.47393f;
            constexpr float maxEv = 4.026069f;

            glm::vec3 v = agxMat * hdr;
            // log2 域 clamp 前先把 0/负值抬到极小，避免 log2(0) = -inf 串到后面。
            v = glm::vec3(std::log2(std::max(v.x, 1e-10f)),
                          std::log2(std::max(v.y, 1e-10f)),
                          std::log2(std::max(v.z, 1e-10f)));
            v = glm::clamp(v, glm::vec3(minEv), glm::vec3(maxEv));
            v = (v - minEv) / (maxEv - minEv);
            v = AgxApprox(v);
            v = agxMatInv * v;
            return glm::clamp(v, glm::vec3(0.0f), glm::vec3(1.0f));
        }

        // Reinhard per-channel `x / (1 + x)` —— 与 tonemap.frag Reinhard 同曲线。
        glm::vec3 Reinhard(const glm::vec3& x) noexcept
        {
            return glm::clamp(x / (glm::vec3(1.0f) + x), glm::vec3(0.0f), glm::vec3(1.0f));
        }

        // Linear clamp —— 不做曲线压缩，硬 clamp 到 [0, 1]。
        glm::vec3 LinearClamp(glm::vec3 x) noexcept
        {
            return glm::clamp(x, glm::vec3(0.0f), glm::vec3(1.0f));
        }

        // 与 tonemap.frag ApplyTonemap 同款 switch dispatch。
        glm::vec3 ApplyTonemap(const glm::vec3& hdr, TonemapOperator op) noexcept
        {
            switch (op)
            {
                case TonemapOperator::ACES_Narkowicz:
                    return AcesNarkowicz(hdr);
                case TonemapOperator::AgX:
                    return AgX(hdr);
                case TonemapOperator::Reinhard:
                    return Reinhard(hdr);
                case TonemapOperator::Linear:
                    return LinearClamp(hdr);
            }
            return AcesNarkowicz(hdr); // unknown enum → ACES 兜底（与 shader 一致）
        }

    } // namespace

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

        // 读 chain 当前 TonemapPass.op + exposure，与 stage B shader 路径
        // 同款语义；chain 没挂 TonemapPass（罕见——典型 sample 都走
        // BuiltinPostProcessChain::CreateDefault）→ 退回 ACES + exposure=1
        // 的历史兼容行为。
        const TonemapPass*    tm  = FindActiveTonemapPass();
        const TonemapOperator op  = (tm != nullptr) ? tm->op : TonemapOperator::ACES_Narkowicz;
        const float           exp = (tm != nullptr) ? tm->exposure : 1.0f;

        const std::uint32_t       pixelCount = hdrWidth * hdrHeight;
        std::vector<std::uint8_t> ldr(static_cast<std::size_t>(pixelCount) * 4);
        const std::uint16_t*      hdrPx = static_cast<const std::uint16_t*>(mapped);

        for (std::uint32_t i = 0; i < pixelCount; ++i)
        {
            const std::uint16_t* p = hdrPx + i * 4;
            const glm::vec3      hdr(HalfToFloat(p[0]), HalfToFloat(p[1]), HalfToFloat(p[2]));
            const glm::vec3      sdr = ApplyTonemap(hdr * exp, op);
            ldr[i * 4 + 0]           = static_cast<std::uint8_t>(sdr.r * 255.0f + 0.5f);
            ldr[i * 4 + 1]           = static_cast<std::uint8_t>(sdr.g * 255.0f + 0.5f);
            ldr[i * 4 + 2]           = static_cast<std::uint8_t>(sdr.b * 255.0f + 0.5f);
            ldr[i * 4 + 3]           = 255; // alpha 一律不透明，HDR alpha 不参与 tonemap
        }

        captureBuffer->Unmap();

        const std::string outStr = outPath.string();
        const int         ok     = stbi_write_png(outStr.c_str(),
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

} // namespace Orange::Engine::Render
