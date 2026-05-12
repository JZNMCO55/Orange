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
// 格一一对应——Pipeline 按这个顺序 + std430 对齐
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
// fs::current_path（当前工程只发 Windows）。
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

    // textured 与 toon / rim_light 同模式——push-constant
    // {uMVP, uModel} = 128 B；fragment 端用 vWorldPos + light UBO + shadow
    // map 接通"接收阴影"路径，让 plane 这类用 textured 的 entity 也能
    // 看到 cube / sphere 投下来的 shadow。
    desc.uniforms = {
        {"uMVP",   MaterialUniformType::Mat4},
        {"uModel", MaterialUniformType::Mat4},
    };
    // binding 0 是 sampler 占位槽（未来真接 sampler 时替换 fragment 内
    // checker 程序合成）。
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

    // push-constant 收缩为 {uMVP, uModel} = 128 B（Pipeline
    // 上限 + 极简）。颜色 / threshold / light 方向都迁出 push constant：
    //   * uLightDir / uLightColor / uLightIntensity / shadow params →
    //     主 pass 的 light UBO（descriptor set 0 binding 1，per-frame）；
    //   * uColorWarm / uColorCool / uShadowThreshold → 暂时 hardcode 进
    //     toon.frag.glsl，per-instance 自定义留待 Material UBO。
    //
    // Material.uniforms 仅保留 Pipeline 实际 push 的两个字段——这是
    // Pipeline 路由时计算 push-constant size 的依据。
    desc.uniforms = {
        {"uMVP",   MaterialUniformType::Mat4},
        {"uModel", MaterialUniformType::Mat4},
    };
    desc.textureSlots = {};

    return BuildMaterial(registry, std::move(desc),
                         "shaders/orange_engine/toon.vert.spv",
                         "shaders/orange_engine/toon.frag.spv");
}

Material LoadRimLight(Asset::AssetRegistry& registry)
{
    Material desc;
    desc.name = "rim_light";

    // 与 LoadToon 同模式——push-constant 收缩为 {uMVP, uModel}，
    // 其余 rim 参数（uRimColor / uRimPower / uRimIntensity / uViewPos）
    // hardcode 进 rim_light.frag.glsl。
    desc.uniforms = {
        {"uMVP",   MaterialUniformType::Mat4},
        {"uModel", MaterialUniformType::Mat4},
    };
    desc.textureSlots = {};

    return BuildMaterial(registry, std::move(desc),
                         "shaders/orange_engine/rim_light.vert.spv",
                         "shaders/orange_engine/rim_light.frag.spv");
}

Material LoadDissolve(Asset::AssetRegistry& registry)
{
    Material desc;
    desc.name = "dissolve";

    // 与 toon / rim_light 同模式：push constant 仅 {uMVP, uModel}；其余
    // dissolve 参数（noise scale / edge width / edge color）hardcode 进
    // dissolve.frag.glsl，dissolve_t 由 frag 端读 light UBO 的
    // uFrameInfo.x 自驱（pingpong 0..1..0）。per-instance 调参等
    // Material UBO 路径接通后再补。
    desc.uniforms = {
        {"uMVP",   MaterialUniformType::Mat4},
        {"uModel", MaterialUniformType::Mat4},
    };
    desc.textureSlots = {};

    return BuildMaterial(registry, std::move(desc),
                         "shaders/orange_engine/dissolve.vert.spv",
                         "shaders/orange_engine/dissolve.frag.spv");
}

Material LoadEmissive(Asset::AssetRegistry& registry)
{
    Material desc;
    desc.name = "emissive";

    // 与 dissolve / toon 同模式：push constant {uMVP, uModel}；emissive
    // color / intensity hardcode 进 emissive.frag.glsl，HDR > 1 触发既
    // 有 bloom pass。
    desc.uniforms = {
        {"uMVP",   MaterialUniformType::Mat4},
        {"uModel", MaterialUniformType::Mat4},
    };
    desc.textureSlots = {};

    return BuildMaterial(registry, std::move(desc),
                         "shaders/orange_engine/emissive.vert.spv",
                         "shaders/orange_engine/emissive.frag.spv");
}

}  // namespace Orange::Engine::Render::BuiltinMaterials
