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

Material LoadHalo(Asset::AssetRegistry& registry)
{
    Material desc;
    desc.name = "halo";

    // GAP-2026-05-11 G3 PointLight halo 路径。与 emissive 同款 mesh vertex
    // input（pos+uv+normal，stride 48B），push constant 多一个 Vec4 槽位
    // uHaloColorIntensity（.rgb = light.color，.a = light.intensity *
    // light.haloIntensity）—— Pipeline halo loop 按 per-light 喂入，让 halo
    // 视觉强度与 light 自身字段同步。emissive surface 本质（无光照计算 + 不
    // 读 shadow map），靠 BloomPass 自然散光产生 glow。
    //
    // Total push constant size = 64B (mat4) + 64B (mat4) + 16B (vec4) = 144B
    // （与 Vulkan minSpec 128B 上限相比超出 16B；本生态下游 spec 是 RTX 系列
    // 普遍 256B+，运行无忧——若移植到 minSpec 设备需把 color/intensity 改成
    // descriptor set 1 UBO 路径）。
    desc.uniforms = {
        {"uMVP",                MaterialUniformType::Mat4},
        {"uModel",              MaterialUniformType::Mat4},
        {"uHaloColorIntensity", MaterialUniformType::Vec4},
    };
    desc.textureSlots = {};

    return BuildMaterial(registry, std::move(desc),
                         "shaders/orange_engine/halo.vert.spv",
                         "shaders/orange_engine/halo.frag.spv");
}

Material LoadPbr(Asset::AssetRegistry& registry)
{
    Material desc;
    desc.name = "pbr";

    // push constant 在 toon / rim_light 的 {uMVP, uModel} 128 B 基础上扩两
    // 条 vec4：uBaseColor + uMRA = 32 B，总 160 B，仍在所有桌面级 GPU 的
    // maxPushConstantsSize（普遍 256 B）以内。fragment 端材质参数（baseColor /
    // metallic / roughness / ao）由 vert 读 push constant 后透传 varying；
    // 待 OrangeRender PushConstantRange 支持 multi-stage 或 per-instance
    // material UBO 上线后，本 transport 路径可改成 frag stage 直接读 push
    // constant 或 set 1 UBO。
    //
    // uMRA 字段约定：.x = metallic，.y = roughness，.z = ao，.w 预留（normal
    // map scale 等延后通道）。emissive 自发光（glTF emissiveFactor×strength）走
    // 独立的 uEmissive vec4（push constant 160→176 B；emissive 是 vec3 色，塞不
    // 进 uMRA.w 单标量），.rgb 用、.a 预留；frag 端 × set 1 binding 4 emissive
    // 贴图叠加到最终色，> 1 的 HDR 值经 bloom 发光。
    //
    // 默认值（MaterialInstance 不覆盖时 Pipeline pack 路径喂入）：
    //   * uBaseColor = (0.8, 0.8, 0.8, 1.0)  —— 中性灰塑料
    //   * uMRA       = (0.0, 0.5, 1.0, 0.0)  —— 非金属、中等粗糙、AO 满
    //   * uEmissive  = (0.0, 0.0, 0.0, 0.0)  —— 无自发光（零回归默认）
    //
    // set 0 binding 2/3/4 的 IBL 三纹理由 Pipeline 全局注入（per-frame，dummy
    // 或真实），不走 MaterialInstance 路径——它们与 EnvironmentComponent 生命
    // 周期同步。下面 textureSlots 是 set 1 per-instance material 贴图
    // （GAP-2026-05-25 A2/G1），binding 与 pbr.frag set=1 声明严格对齐：
    //   0 = baseColor / 1 = normal / 2 = metalRough(G=rough,B=metal) / 3 = ao /
    //   4 = emissive。MaterialInstance 不绑某槽时 Pipeline 喂 default 贴图（白 /
    //   flat-normal；emissive 默认白 → 仅 emissiveFactor 生效），采样 ×scalar =
    //   scalar、法线不扰动、emissive=factor → 退化为纯 scalar PBR（零回归）。
    desc.uniforms = {
        {"uMVP",       MaterialUniformType::Mat4},
        {"uModel",     MaterialUniformType::Mat4},
        {"uBaseColor", MaterialUniformType::Vec4},
        {"uMRA",       MaterialUniformType::Vec4},
        {"uEmissive",  MaterialUniformType::Vec4},
    };
    desc.textureSlots = {
        {0, "uBaseColorTex"},
        {1, "uNormalTex"},
        {2, "uMetalRoughTex"},
        {3, "uAoTex"},
        {4, "uEmissiveTex"},
    };
    // pbr.vert 消费 tangent（location 3）做切线空间法线贴图——唯一需要声明
    // tangent vertex 属性的内置模板（其余模板 usesTangentVertex 保持默认 false）。
    desc.usesTangentVertex = true;

    return BuildMaterial(registry, std::move(desc),
                         "shaders/orange_engine/pbr.vert.spv",
                         "shaders/orange_engine/pbr.frag.spv");
}

Material LoadDebugNormals(Asset::AssetRegistry& registry)
{
    Material desc;
    desc.name = "debug_normals";
    // push constant {uMVP, uModel} = 128 B（drawable loop 的 pcSize>=128 分支
    // 喂 mvp+model）。无 textureSlots / 不用 descriptor set 1；usesTangentVertex
    // 默认 false（debug normals 用 vertex normal，不用 tangent）。
    desc.uniforms = {
        {"uMVP",   MaterialUniformType::Mat4},
        {"uModel", MaterialUniformType::Mat4},
    };
    return BuildMaterial(registry, std::move(desc),
                         "shaders/orange_engine/debug_normals.vert.spv",
                         "shaders/orange_engine/debug_normals.frag.spv");
}

Material LoadDebugUnlit(Asset::AssetRegistry& registry)
{
    Material desc;
    desc.name = "debug_unlit";
    // push constant 复用 PBR 160 B {uMVP, uModel, uBaseColor, uMRA}——drawable
    // loop 的 pcSize>=160 分支会喂 drawable material instance 的 uBaseColor
    // override（即 drawable 的 albedo）。unlit shader 只读 uBaseColor 直出，
    // 忽略 uMRA。无 textureSlots / usesTangentVertex 默认 false。
    desc.uniforms = {
        {"uMVP",       MaterialUniformType::Mat4},
        {"uModel",     MaterialUniformType::Mat4},
        {"uBaseColor", MaterialUniformType::Vec4},
        {"uMRA",       MaterialUniformType::Vec4},
    };
    return BuildMaterial(registry, std::move(desc),
                         "shaders/orange_engine/debug_unlit.vert.spv",
                         "shaders/orange_engine/debug_unlit.frag.spv");
}

Material LoadDebugOverdraw(Asset::AssetRegistry& registry)
{
    Material desc;
    desc.name = "debug_overdraw";
    // push constant {uMVP, uModel} = 128 B（同 debug_normals，drawable loop 的
    // pcSize>=128 分支喂 mvp+model）。overdraw 靠渲染状态而非 shader 数据：开
    // additiveBlend（每次覆盖把片段色叠加）+ disableDepthTest（重叠 fragment
    // 不被 depth 剔除，全部累加）——二者合起来形成 overdraw 热图。
    desc.uniforms = {
        {"uMVP",   MaterialUniformType::Mat4},
        {"uModel", MaterialUniformType::Mat4},
    };
    desc.additiveBlend    = true;
    desc.disableDepthTest = true;
    return BuildMaterial(registry, std::move(desc),
                         "shaders/orange_engine/debug_overdraw.vert.spv",
                         "shaders/orange_engine/debug_overdraw.frag.spv");
}

Material LoadDebugWireframe(Asset::AssetRegistry& registry)
{
    Material desc;
    desc.name = "debug_wireframe";
    // push constant {uMVP, uModel} = 128 B（同 debug_normals，drawable loop 的
    // pcSize>=128 分支喂 mvp+model）。wireframe 靠渲染状态：wireframe=true 让
    // GetOrCompilePipeline 设 polygonMode=Line（前提 device feature
    // fillModeNonSolid，否则 fallback Fill）。
    desc.uniforms = {
        {"uMVP",   MaterialUniformType::Mat4},
        {"uModel", MaterialUniformType::Mat4},
    };
    desc.wireframe = true;
    return BuildMaterial(registry, std::move(desc),
                         "shaders/orange_engine/debug_wireframe.vert.spv",
                         "shaders/orange_engine/debug_wireframe.frag.spv");
}

}  // namespace Orange::Engine::Render::BuiltinMaterials
