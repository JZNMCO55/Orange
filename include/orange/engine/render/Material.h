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

    // 顶点是否声明 tangent 属性（location 3）。仅"消费 tangent 的 shader"
    // （如 pbr.vert 做切线空间法线贴图）置 true——Pipeline 据此声明 vertex
    // attribute location 3。与"是否用 descriptor set 1 贴图"是两个正交概念：
    // 用贴图 ≠ 用 tangent（textured 用贴图但不读 tangent，不应声明 location 3，
    // 否则触发 validation "location 3 not consumed" 警告）。
    bool usesTangentVertex{false};

    // 渲染状态覆盖。默认 false = 标准不透明（depth test/write on + 无 blend）。
    // 特殊 material（典型 debug view）按需打开：
    //   additiveBlend     —— color/alpha blend 改加性（src ONE + dst ONE，叠加
    //                        累积），overdraw 热图 / 粒子用；
    //   disableDepthTest  —— 关 depth test + depth write，让重叠 fragment 全部
    //                        画出（overdraw 计数需要：否则前面的 fragment 写了
    //                        depth，后面重叠的被剔除就无从累加）。
    bool additiveBlend{false};
    bool disableDepthTest{false};
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_MATERIAL_H
