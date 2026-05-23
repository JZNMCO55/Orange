// Pipeline 内部使用的小型 helper：interleaved 顶点格式 / vertex input 填
// 装 / push-constant 字节计算 / SPIR-V 文件读取 / OrangeRender 日志桥 /
// 一组 Pipeline 共用的 constexpr。原本散落在 Pipeline.cpp 顶部的匿名
// namespace；为让"按 pass 拆 .cpp"后多个子 .cpp 都能共享同一份实现，
// 抽到 internal header（src/render/pipeline/ 不导出公共面）。
//
// 命名空间 `PipelineDetail` 沿用 anonymous ns "纯内部"语义；具体定义
// 放 PipelineHelpers.cpp，header 只暴露 declaration + 必要的 POD 结构。

#ifndef ORANGE_ENGINE_SRC_RENDER_PIPELINE_PIPELINEHELPERS_H
#define ORANGE_ENGINE_SRC_RENDER_PIPELINE_PIPELINEHELPERS_H

#include "orange/engine/asset/MeshAsset.h"
#include "orange/engine/render/Material.h"
#include "orange/engine/render/MaterialTypes.h"

#include "orange/core/Log.h"
#include "orange/rhi/RHI.h"

#include <cstdint>
#include <vector>

namespace Orange::Engine::Render::PipelineDetail
{

// 顶点 layout：interleaved 8 floats = pos(3) + uv(2) + normal(3)。所有
// 内置模板共用本布局；缺 normal 的 mesh 由 MeshLoader / 程序化构造路径
// 调 MeshAsset::ComputeSmoothNormalsFromTriangles 在 CPU 端补算。
struct InterleavedVertex
{
    float position[3];
    float uv[2];
    float normal[3];
};

// 后续 pipeline 子模块都会用到的格式常量。
constexpr Orange::Rhi::TextureFormat kHdrColorFormat       = Orange::Rhi::TextureFormat::RGBA16Float;
constexpr Orange::Rhi::TextureFormat kSwapchainColorFormat = Orange::Rhi::TextureFormat::BGRA8Unorm;

// Bloom mip-chain：6 张 RGBA16F，从 HDR/2 一路下采到 HDR/64。下采 6 次
// 喂出 6 张 mip；上采 5 次按 mip[N+1] tent → additive blend 累加进 mip[N]；
// 最终 mip[0] 作为"bloom 末态"喂给 stage B 的 passthrough_combine。
constexpr std::size_t kBloomMipCount = 6;

// std430 push-constant 字节占用——vec3 padded 到 16，与内置 toon /
// rim_light 的 push_constant block 注释一致。
std::uint32_t PushConstantBytesFor(MaterialUniformType type) noexcept;

std::uint32_t ComputePushConstantSize(const Material& mat) noexcept;

void FillVertexInputLayout(Orange::Rhi::GraphicsPipelineDesc& desc);

// 给 GraphicsPipelineDesc 加单条 Vertex stage 的 push range。size==0 → no-op。
void FillPushConstantRanges(Orange::Rhi::GraphicsPipelineDesc& desc, std::uint32_t size);

std::vector<InterleavedVertex> InterleaveMesh(const Asset::MeshAsset& mesh);

// 把 .exe 同目录下的 SPIR-V 直接读成 word 流——不走 AssetRegistry。
std::vector<std::uint32_t> LoadSpirv(const char* relativePath);

// OrangeRender 日志桥：把 Orange::Log* 的输出（含 Validation 类别）转入
// OrangeEngine 的 ORANGE_LOG_*。Pipeline::Initialize 在 RenderDevice::Create
// 之前 SetLogSink，Shutdown 末段 ClearLogSink。
void OrangeRenderLogAdapter(::Orange::LogCategory category,
                            ::Orange::LogLevel    level,
                            const char*           pMessage,
                            void*                 pUserData);

}  // namespace Orange::Engine::Render::PipelineDetail

#endif  // ORANGE_ENGINE_SRC_RENDER_PIPELINE_PIPELINEHELPERS_H
