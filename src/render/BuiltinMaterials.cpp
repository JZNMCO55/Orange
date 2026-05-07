// BuiltinMaterials 实现：把 .exe 同目录下的 `shaders/orange_engine/<name>.spv`
// 注册到 AssetRegistry，再装配出 Material 描述符。
//
// 路径解析复用 Pipeline.cpp 里 GetExecutableDir 的同一思路（GetModuleFileNameW
// 拿 .exe 路径 → parent_path），让 .spv 不论 CWD 在哪都能稳定从 .exe
// 同目录的 shaders/orange_engine/ 找到。这层 helper 当前在两处复制
// （Pipeline.cpp + BuiltinMaterials.cpp）；等到 3+ 处复用再提到 Platform
// 模块统一。
//
// uniform 列表的字段顺序与对应 GLSL push_constant block 的字段顺序严
// 格一一对应——Phase 3 / Task 04 起 Pipeline 按这个顺序 + std430 对齐
// 把 MaterialInstance 覆盖打包成 push-constant bytes。改这里时记得同步
// 改 .vert.glsl / .frag.glsl 里的 push_constant block 字段顺序。

#include "orange/engine/render/BuiltinMaterials.h"

#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/asset/ShaderAsset.h"
#include "orange/engine/core/Log.h"

#include <filesystem>
#include <string>

#if defined(_WIN32)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace Orange::Engine::Render::BuiltinMaterials
{
namespace
{

// 解析当前可执行体所在目录。CWD 可能与 .exe 目录不一致（尤其是从 repo
// 根用 build/bin/Debug/...exe 跑时），所以把 SPIR-V 路径锚定到 .exe 自
// 身所在目录更稳。Win32 用 GetModuleFileName；其它平台暂时回退到
// fs::current_path（Phase 2 工程只发 Windows）。
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

// 把 "shaders/orange_engine/<file>.spv" 解析成 .exe-相对的绝对路径，
// 作为 AssetRegistry 的 dedup key。返回的 string 直接喂 registry.Load。
std::string ResolveBuiltinShaderPath(const char* relative)
{
    return (GetExecutableDir() / relative).string();
}

// 通用的内置 Material 装配步骤：解析两个 SPIR-V 路径 → registry.Load →
// 把 handle 填进调用方传入的 Material 描述符。失败路径下保留 desc 的
// uniform / texture 槽布局，仅留无效 handle——让调用方能 graceful 处理。
Material BuildMaterial(Asset::AssetRegistry& registry,
                       Material              desc,
                       const char*           vertSpvRelative,
                       const char*           fragSpvRelative)
{
    const auto vertPath = ResolveBuiltinShaderPath(vertSpvRelative);
    const auto fragPath = ResolveBuiltinShaderPath(fragSpvRelative);

    auto vsResult = registry.Load<Asset::ShaderAsset>(vertPath);
    if (vsResult.IsErr())
    {
        ORANGE_LOG_ERROR("BuiltinMaterials: 加载内置 vertex SPIR-V 失败 (path={}, code={})",
                         vertPath, static_cast<unsigned>(vsResult.Error()));
    }
    else
    {
        desc.vertexShader = vsResult.Value();
    }

    auto fsResult = registry.Load<Asset::ShaderAsset>(fragPath);
    if (fsResult.IsErr())
    {
        ORANGE_LOG_ERROR("BuiltinMaterials: 加载内置 fragment SPIR-V 失败 (path={}, code={})",
                         fragPath, static_cast<unsigned>(fsResult.Error()));
    }
    else
    {
        desc.fragmentShader = fsResult.Value();
    }

    return desc;
}

}  // namespace

Material LoadTextured(Asset::AssetRegistry& registry)
{
    Material desc;
    desc.name = "textured";

    // textured_mesh shader 的 push-constant block 仅 uMVP(mat4) = 64 字节。
    // 与 Pipeline 已有的 hardcoded textured pipeline 对齐——当前 06.01 子任
    // 务保持视觉等价，靠的是 Pipeline 仍走 hardcoded 路径；这里 schema 提
    // 前对齐，06.02 切到 per-template Pipeline 缓存时无 schema churn。
    desc.uniforms = {
        {"uMVP", MaterialUniformType::Mat4},
    };
    // binding 0 是程序式 fragment shader 当前未采样的"占位 sampler"槽——
    // 把"贴图存在"以 MaterialInstance::SetTexture 喂进来，等到 fragment
    // shader 真切到 `texture(sampler2D(uTexture), vUV)` 时无 schema 改动。
    desc.textureSlots = {
        {0, "uTexture"},
    };

    return BuildMaterial(registry, std::move(desc),
                         "shaders/orange_engine/textured_mesh.vert.spv",
                         "shaders/orange_engine/textured_mesh.frag.spv");
}

Material LoadToon(Asset::AssetRegistry& registry)
{
    Material desc;
    desc.name = "toon";

    // uniform 顺序与 src/render/builtin_shaders/toon.{vert,frag}.glsl 中
    // push_constant block 的字段顺序一一对应。
    desc.uniforms = {
        {"uMVP",             MaterialUniformType::Mat4 },
        {"uColorWarm",       MaterialUniformType::Vec3 },
        {"uColorCool",       MaterialUniformType::Vec3 },
        {"uLightDir",        MaterialUniformType::Vec3 },
        {"uShadowThreshold", MaterialUniformType::Float},
    };
    // toon 当前不采样贴图——纯程序式 cel banding。后续 Phase 3 task 把
    // base color texture 接上时再追加 textureSlot。
    desc.textureSlots = {};

    return BuildMaterial(registry, std::move(desc),
                         "shaders/orange_engine/toon.vert.spv",
                         "shaders/orange_engine/toon.frag.spv");
}

Material LoadRimLight(Asset::AssetRegistry& registry)
{
    Material desc;
    desc.name = "rim_light";

    // uniform 顺序与 src/render/builtin_shaders/rim_light.{vert,frag}.glsl
    // 中 push_constant block 的字段顺序一一对应。
    desc.uniforms = {
        {"uMVP",          MaterialUniformType::Mat4 },
        {"uViewPos",      MaterialUniformType::Vec3 },
        {"uRimColor",     MaterialUniformType::Vec3 },
        {"uRimPower",     MaterialUniformType::Float},
        {"uRimIntensity", MaterialUniformType::Float},
    };
    desc.textureSlots = {};

    return BuildMaterial(registry, std::move(desc),
                         "shaders/orange_engine/rim_light.vert.spv",
                         "shaders/orange_engine/rim_light.frag.spv");
}

}  // namespace Orange::Engine::Render::BuiltinMaterials
