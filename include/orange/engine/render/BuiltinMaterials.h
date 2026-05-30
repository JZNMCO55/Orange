#ifndef ORANGE_ENGINE_RENDER_BUILTIN_MATERIALS_H
#define ORANGE_ENGINE_RENDER_BUILTIN_MATERIALS_H

// ---------------------------------------------------------------------------
// BuiltinMaterials —— 引擎内置 Material 模板的工厂。
//
// 引擎内置三个 Material 模板：
//   * `textured`  —— 复用 Pipeline 既有的 textured_mesh shader 对，作为
//                    "把 RenderableComponent 接入 MaterialInstance schema"
//                    的最小过渡模板（程序式 checker，不真采样贴图）；
//   * `toon`      —— 二阶 cel-shading（warm / cool 双色 + 法线驱动 banding）；
//   * `rim_light` —— fresnel 风格 rim glow（边沿发光）。
//
// LoadXxx 负责：
//   1. 把对应 SPIR-V 注册到 AssetRegistry（按 .exe-相对路径作 dedup
//      key——重复调用幂等，handle 沿用）；
//   2. 返回 Material 描述符——name、两个 ShaderAsset handle、uniform 布
//      局、texture 槽布局都填好，开箱即用。
//
// **调用前置**：调用方需要先 `RegisterLoader<Asset::ShaderAsset>(loader)`，
// 与 "AssetRegistry 不在构造时自动注册任何 loader" 的契约对齐。重复调
// 用 LoadXxx 不会再注册 loader（Asset 层 dedup 保证 .spv 文件只读一次）。
//
// **当前阶段限制**：Pipeline 的真实 Material 路由（按 MaterialInstance
// 覆盖打 push-constant / 描述符）随后续接通；当前
// 仅交付 "Material 数据可被消费"——descriptor 已稳定、在不破
// 公共面的前提下把渲染管线接上。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/render/Material.h>

namespace Orange::Engine::Asset
{
class AssetRegistry;
}  // namespace Orange::Engine::Asset

namespace Orange::Engine::Render::BuiltinMaterials
{

// 加载内置 textured 模板。复用 Pipeline 既有的 textured_mesh shader 对：
// 顶点 layout = pos+uv，push-constant block 仅 uMVP(mat4)，textureSlots
// 声明 binding 0 = "uTexture"——当前 fragment shader 是程序式 checker，
// 不实际采样这张贴图，但 schema 已对齐，等到 OrangeRender 暴露真 sampler
// 路径时直接接入即可。语义同 LoadToon。
ORANGE_ENGINE_API Material LoadTextured(Asset::AssetRegistry& registry);

// 加载内置 toon 模板，返回完整可用的 Material（含已注册的 ShaderAsset
// handle）。同 registry 上重复调用幂等，handle 沿用。
//
// 失败语义：SPIR-V 不存在 / 解析失败时返回的 Material 仍保留 uniform
// + textureSlot 描述符，但 vertexShader / fragmentShader 字段为无效
// handle。调用方按 `material.vertexShader.IsValid()` 判定。
ORANGE_ENGINE_API Material LoadToon(Asset::AssetRegistry& registry);

// 加载内置 rim-light 模板。语义同 LoadToon。
ORANGE_ENGINE_API Material LoadRimLight(Asset::AssetRegistry& registry);

// 加载内置 dissolve 模板：noise(uv) + 时间驱动阈值 alpha discard，阈值
// 附近 ±kEdgeWidth 区间输出 HDR > 1 的发光边沿色。dissolve_t 在 frag
// 内部由 light UBO 的 uFrameInfo.x 自驱（pingpong 0..1..0），调用方无
// 须手动驱动；per-instance 调速 / 调色等到 Material UBO 接通后再补。
ORANGE_ENGINE_API Material LoadDissolve(Asset::AssetRegistry& registry);

// 加载内置 emissive 模板：直接输出 HDR > 1 的常量色（带轻微 vignette），
// 不计算光照——表面"自发光"，颜色超出 LDR 阈值由既有 bloom pass 自动
// 拾取产出光晕。颜色 / 强度参数 hardcode 同 toon / rim_light。
ORANGE_ENGINE_API Material LoadEmissive(Asset::AssetRegistry& registry);

// 加载内置 halo 模板：PointLight halo 可见光晕路径（GAP-2026-05-11 G3）。
// 与 emissive 同款自发光出 HDR 路径，但 color × intensity 经 push constant
// uHaloColorIntensity (.rgb=color, .a=intensity) 由 Pipeline halo loop
// per-light 喂入，让每个 PointLight halo 与 light 自身 color / intensity 同步。
// 由 Pipeline 内部持有 + GetOrCompilePipeline 复用主 forward layout，**不**
// 经 MaterialSystem::RegisterBuiltins 暴露给用户（halo 是 PointLight 内嵌
// 视觉表现，挂 RenderableComponent 路径走 emissive 即可）。
ORANGE_ENGINE_API Material LoadHalo(Asset::AssetRegistry& registry);

// 加载内置 PBR 模板：monolithic Cook-Torrance + GGX + Smith correlated +
// Schlick + Lambert + IBL split-sum 三槽位。push-constant 与现有 toon /
// rim_light 同款 {uMVP, uModel} = 128 B。当前五通道材质参数（baseColor /
// metallic / roughness / normal / AO）在 shader 内 hardcoded；后续把它们
// 抬进 MaterialInstance + Inspector schema 时 textureSlots 同步扩。当前
// textureSlots 留空，避免 schema 与 shader 默认值漂移。
//
// Pipeline 在 Initialize 期把"drawable.materialInstance == nullptr"的
// fallback 切到本模板（替代历史 textured 棋盘），所有未显式挂材质的
// Renderable 自动按 PBR 渲染——这就是 "棋盘塑料 → PBR 真实感" 视觉跃迁
// 的来源。textured 不删，仍可被 sample 显式 LoadTextured 用作 dev-checker
// fallback。
ORANGE_ENGINE_API Material LoadPbr(Asset::AssetRegistry& registry);

// 加载内置 debug-view normals 模板（DebugViewMode::Normals）：把 world-space
// normal 映射到 RGB 作可视化，无光照 / 无贴图。push constant {uMVP, uModel}
// = 128 B（同 toon/pbr）；Pipeline 在 Normals mode 时用它替换 drawable
// material 渲染所有 drawable。不经 RegisterBuiltins 暴露给用户（纯诊断）。
ORANGE_ENGINE_API Material LoadDebugNormals(Asset::AssetRegistry& registry);

// 加载内置 debug-view unlit 模板（DebugViewMode::Unlit）：直出 material base
// color、无光照。push constant 复用 PBR {uMVP, uModel, uBaseColor, uMRA}
// = 160 B（drawable loop 喂 drawable material instance 的 uBaseColor）；
// Pipeline 在 Unlit mode 时用它替换 drawable material。纯诊断，不经 RegisterBuiltins。
ORANGE_ENGINE_API Material LoadDebugUnlit(Asset::AssetRegistry& registry);

}  // namespace Orange::Engine::Render::BuiltinMaterials

#endif  // ORANGE_ENGINE_RENDER_BUILTIN_MATERIALS_H
