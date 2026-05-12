#ifndef ORANGE_ENGINE_RENDER_BUILTIN_SHADOW_SHADERS_H
#define ORANGE_ENGINE_RENDER_BUILTIN_SHADOW_SHADERS_H

// ---------------------------------------------------------------------------
// BuiltinShadowShaders —— 内置 shadow pass shader 的工厂。
//
// 与 BuiltinMaterials 平行：不强行套进 Material（shadow_caster 没有
// uniform 描述符 / 没有 texture 槽，是 Pipeline-managed shader pair，
// 不该走 MaterialInstance 路径）。Pipeline 接通 shadow pass
// 时直接拿 ShaderPair 的两个 handle 编 RHI Pipeline。
//
// `LoadShadowCaster` 负责：
//   1. 把 shadow_caster.vert.spv / shadow_caster.frag.spv 注册到
//      AssetRegistry（按 .exe-相对路径作 dedup key——重复调用幂等，
//      handle 沿用）；
//   2. 返回 `ShaderPair` 含两个 ShaderAsset handle。
//
// **调用前置**：调用方需要先 `RegisterLoader<Asset::ShaderAsset>(loader)`，
// 与 BuiltinMaterials 同契约。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/ShaderAsset.h>

namespace Orange::Engine::Asset
{
class AssetRegistry;
}  // namespace Orange::Engine::Asset

namespace Orange::Engine::Render::BuiltinShadowShaders
{

// 顶点 + 片段 SPIR-V handle 对。Pipeline 在编 RHI Pipeline 时各取一个。
struct ShaderPair
{
    Asset::AssetHandle<Asset::ShaderAsset> vertex;
    Asset::AssetHandle<Asset::ShaderAsset> fragment;
};

// 加载内置 shadow_caster 模板。失败语义同 BuiltinMaterials::LoadToon——
// SPIR-V 不存在 / 解析失败时 ShaderPair 仍返回，但对应字段是无效 handle。
// 调用方按 `pair.vertex.IsValid()` 判定。
ORANGE_ENGINE_API ShaderPair LoadShadowCaster(Asset::AssetRegistry& registry);

}  // namespace Orange::Engine::Render::BuiltinShadowShaders

#endif  // ORANGE_ENGINE_RENDER_BUILTIN_SHADOW_SHADERS_H
