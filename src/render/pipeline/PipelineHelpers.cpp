#include "PipelineHelpers.h"

#include "orange/engine/asset/SpirvDiskLoader.h"
#include "orange/engine/core/Log.h"

#include <cstring>

namespace Orange::Engine::Render::PipelineDetail
{

std::uint32_t PushConstantBytesFor(MaterialUniformType type) noexcept
{
    switch (type)
    {
        case MaterialUniformType::Float: return 4;
        case MaterialUniformType::Int:   return 4;
        case MaterialUniformType::Vec2:  return 8;
        case MaterialUniformType::Vec3:  return 16;
        case MaterialUniformType::Vec4:  return 16;
        case MaterialUniformType::Mat4:  return 64;
    }
    return 0;
}

std::uint32_t ComputePushConstantSize(const Material& mat) noexcept
{
    std::uint32_t total = 0;
    for (const auto& u : mat.uniforms)
    {
        total += PushConstantBytesFor(u.type);
    }
    return total;
}

void FillVertexInputLayout(Orange::Rhi::GraphicsPipelineDesc& desc, bool withTangent)
{
    Orange::Rhi::VertexBindingDesc binding{};
    binding.mBinding   = 0;
    binding.mStride    = sizeof(InterleavedVertex);
    binding.mInputRate = Orange::Rhi::VertexInputRate::Vertex;
    desc.mVertexInput.mBindings.push_back(binding);

    Orange::Rhi::VertexAttributeDesc attrPos{};
    attrPos.mLocation = 0;
    attrPos.mBinding  = 0;
    attrPos.mOffset   = offsetof(InterleavedVertex, position);
    attrPos.mFormat   = Orange::Rhi::VertexFormat::Float32x3;
    desc.mVertexInput.mAttributes.push_back(attrPos);

    Orange::Rhi::VertexAttributeDesc attrUV{};
    attrUV.mLocation = 1;
    attrUV.mBinding  = 0;
    attrUV.mOffset   = offsetof(InterleavedVertex, uv);
    attrUV.mFormat   = Orange::Rhi::VertexFormat::Float32x2;
    desc.mVertexInput.mAttributes.push_back(attrUV);

    Orange::Rhi::VertexAttributeDesc attrNormal{};
    attrNormal.mLocation = 2;
    attrNormal.mBinding  = 0;
    attrNormal.mOffset   = offsetof(InterleavedVertex, normal);
    attrNormal.mFormat   = Orange::Rhi::VertexFormat::Float32x3;
    desc.mVertexInput.mAttributes.push_back(attrNormal);

    // location 3 = tangent（vec4）。仅 PBR 模板声明（pbr.vert 消费它）；其余
    // 模板 withTangent=false 不声明，避免 validation "location 3 not consumed"
    // 警告。binding stride 不变（48B），缺声明只是该 attribute 不被该 pipeline
    // 读取。GAP-2026-05-25 A2 / 法线贴图基础设施。
    if (withTangent)
    {
        Orange::Rhi::VertexAttributeDesc attrTangent{};
        attrTangent.mLocation = 3;
        attrTangent.mBinding  = 0;
        attrTangent.mOffset   = offsetof(InterleavedVertex, tangent);
        attrTangent.mFormat   = Orange::Rhi::VertexFormat::Float32x4;
        desc.mVertexInput.mAttributes.push_back(attrTangent);
    }
}

// 给 GraphicsPipelineDesc 加单条 Vertex stage 的 push range。
//
// 历史决策：曾尝试同 offset / 同 size 同时声明 Vertex + Fragment 双
// range（让 toon / rim_light 的 fragment 端 push_constant 引用合法），
// 但 Vulkan 校验要求 vkCmdPushConstants 的 stageFlags 必须涵盖所有重叠
// range 的 stage——OrangeRender 当前的 `PushConstantRange::mStage` 与
// `SetPushConstants` 都只支持单 stage，没法一次发到两段 stage，重叠双
// range 会跑出 VK_ERROR-级 validation。
//
// 折中：只声明 Vertex range。textured 只 vertex 用 push_constant、不
// 受影响；toon / rim_light 的 fragment 端读取 push_constant 会触发
// "shader 在 X 阶段用 push constant 但 layout 没声明 X" 的 validation
// 提示，pipeline 仍能创建但 fragment 端读到 undefined 内容——视觉正确
// 性等到 OrangeRender 把 PushConstantRange 升级为多 stage（或 SetPushConstants
// 支持多 stage flag）后跟进。
void FillPushConstantRanges(Orange::Rhi::GraphicsPipelineDesc& desc, std::uint32_t size)
{
    if (size == 0)
    {
        return;
    }
    Orange::Rhi::PushConstantRange vs{};
    vs.mStage  = Orange::Rhi::ShaderStage::Vertex;
    vs.mOffset = 0;
    vs.mSize   = size;
    desc.mPushConstantRanges.push_back(vs);
}

std::vector<InterleavedVertex> InterleaveMesh(const Asset::MeshAsset& mesh)
{
    const auto& positions = mesh.Positions();
    const auto& uvs       = mesh.UVs();
    const auto& normals   = mesh.Normals();
    const auto& tangents  = mesh.Tangents();
    std::vector<InterleavedVertex> out(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        out[i].position[0] = positions[i].x;
        out[i].position[1] = positions[i].y;
        out[i].position[2] = positions[i].z;
        if (i < uvs.size())
        {
            out[i].uv[0] = uvs[i].u;
            out[i].uv[1] = uvs[i].v;
        }
        else
        {
            out[i].uv[0] = 0.0f;
            out[i].uv[1] = 0.0f;
        }
        if (i < normals.size())
        {
            out[i].normal[0] = normals[i].x;
            out[i].normal[1] = normals[i].y;
            out[i].normal[2] = normals[i].z;
        }
        else
        {
            // MeshLoader / 程序化构造路径都保证 Normals 非空；这里兜底
            // +Y，避免极端构造路径（手工 Insert(MeshAsset) 不带 normal）
            // 把 NaN/0 法线塞进 vertex buffer。
            out[i].normal[0] = 0.0f;
            out[i].normal[1] = 1.0f;
            out[i].normal[2] = 0.0f;
        }
        if (i < tangents.size())
        {
            out[i].tangent[0] = tangents[i].x;
            out[i].tangent[1] = tangents[i].y;
            out[i].tangent[2] = tangents[i].z;
            out[i].tangent[3] = tangents[i].w;
        }
        else
        {
            // 缺 tangent（无 UV mesh / 手工构造）→ 默认 +X 切线 + 正手性。
            // pbr.frag 配合 default flat-normal 贴图 → TBN 退化为不扰动法线，
            // 视觉等价于"无切线空间"，安全。
            out[i].tangent[0] = 1.0f;
            out[i].tangent[1] = 0.0f;
            out[i].tangent[2] = 0.0f;
            out[i].tangent[3] = 1.0f;
        }
    }
    return out;
}

std::uint16_t FloatToHalf(float f) noexcept
{
    // IEEE 754 binary32 → binary16 简版：覆盖正常数 + 0 + 极简饱和到 ±max；
    // 不处理 NaN（按指数饱和路径返 inf，调用方场景 ambient/clear 不会有 NaN），
    // 不处理极小 denormal（指数 < -14 直接丢精度回 0，对 ambient 量级无视觉损失）。
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
    const std::int32_t  exp  = static_cast<std::int32_t>((bits >> 23) & 0xffu) - 127 + 15;
    const std::uint16_t mant = static_cast<std::uint16_t>((bits >> 13) & 0x3ffu);
    if (exp <= 0)
    {
        return sign;  // 极小数 / 0 →（带符号）0
    }
    if (exp >= 31)
    {
        return static_cast<std::uint16_t>(sign | 0x7c00u);  // ±Inf（含 NaN 简化）
    }
    return static_cast<std::uint16_t>(sign
                                      | (static_cast<std::uint16_t>(exp) << 10)
                                      | mant);
}

std::vector<std::uint32_t> LoadSpirv(const char* relativePath)
{
    // 委托到公共面 Asset::LoadSpirvFromExecutableDir（GAP-2026-05-24：把 .exe 相对
    // .spv 加载 helper 提为公共 API 供 editor aux-pass provider / sample / 游戏 fork
    // 复用，引擎内部不再各持一份）。
    return Orange::Engine::Asset::LoadSpirvFromExecutableDir(relativePath);
}

void OrangeRenderLogAdapter(::Orange::LogCategory category,
                            ::Orange::LogLevel    level,
                            const char*           pMessage,
                            void* /*pUserData*/)
{
    if (pMessage == nullptr)
    {
        return;
    }
    const char* catStr   = ::Orange::ToString(category);
    const char* levelStr = ::Orange::ToString(level);
    switch (level)
    {
        case ::Orange::LogLevel::Info:
            ORANGE_LOG_INFO("[OrangeRender][{}][{}] {}", catStr, levelStr, pMessage);
            break;
        case ::Orange::LogLevel::Warn:
            ORANGE_LOG_WARN("[OrangeRender][{}][{}] {}", catStr, levelStr, pMessage);
            break;
        case ::Orange::LogLevel::Error:
            ORANGE_LOG_ERROR("[OrangeRender][{}][{}] {}", catStr, levelStr, pMessage);
            break;
    }
}

}  // namespace Orange::Engine::Render::PipelineDetail
