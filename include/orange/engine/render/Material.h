#ifndef ORANGE_ENGINE_RENDER_MATERIAL_H
#define ORANGE_ENGINE_RENDER_MATERIAL_H

// ---------------------------------------------------------------------------
// Material —— "材质模板" 描述记录。
//
// Material 是纯数据结构（无 PIMPL）：把一组 shader + uniform 布局 +
// texture 槽布局打包成一个可命名的模板，供 MaterialInstance 引用。
// MaterialInstance 在每实例上覆盖具体 uniform 值与 texture 绑定。
//
// 当前阶段 Material 是 ownership-agnostic 的——
// 后续决定把它放进 Asset 路径还是 MaterialSystem 自己的表里；
// 引入 `MaterialSystem::RegisterTemplate` 真正落地存储后，
// MaterialInstance 会改持 system-managed 引用。在那之前，调用方负责
// 让 Material 活到所有引用它的 MaterialInstance 都析构完。
//
// 字段层面有意贴近内置 template 与
// ShaderSourceDesc：直接持 `AssetHandle<ShaderAsset>`，shader 走
// AssetRegistry 加载（不裸 fopen）；uniform 与 texture 槽是显式声明、
// 不靠运行时反射。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/render/MaterialTypes.h>

#include <string>
#include <vector>

namespace Orange::Engine::Render
{

struct Material
{
    std::string name;

    // 顶点 / 片段 shader handle —— 通过 AssetRegistry 加载，引用稳定。
    Asset::AssetHandle<Asset::ShaderAsset> vertexShader;
    Asset::AssetHandle<Asset::ShaderAsset> fragmentShader;

    // uniform / texture 槽布局。MaterialInstance::SetUniform / SetTexture
    // 在 SetXxx 时按 name / binding 在这两个列表里查找；找不到 / 类型
    // 不匹配则 no-op。
    std::vector<MaterialUniformDesc>     uniforms;
    std::vector<MaterialTextureSlotDesc> textureSlots;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_MATERIAL_H
