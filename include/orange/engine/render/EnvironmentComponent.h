#ifndef ORANGE_ENGINE_RENDER_ENVIRONMENT_COMPONENT_H
#define ORANGE_ENGINE_RENDER_ENVIRONMENT_COMPONENT_H

// ---------------------------------------------------------------------------
// EnvironmentComponent —— World 全局 IBL 环境的 ECS component（POD）。
//
// 一份 World 一般只挂一个 EnvironmentComponent，承载"这整个场景被什么样
// 的环境光照"的全局信息：HDR 环境贴图资产引用 + 强度 + 色调。Pipeline
// 渲染主 pass 时按 first-found 取——同一 World 出现多个时取迭代器第一
// 个，剩余被忽略（与 `DirectionalLight` 一致；多 environment 之间的优先
// 级 / blending / 体积探针留给未来 reflection probe milestone）。
//
// 数学语义（参 `vendor/Orange-Wiki/wiki/concepts/rendering/environment-lighting.md`
// §IBL split-sum 与 `vendor/Orange-Wiki/wiki/techniques/rendering/image-based-lighting.md`）：
//
//   irradiance_used    = irradiance_baked    * (tint * intensity)
//   prefiltered_used   = prefiltered_baked   * (tint * intensity)
//
// 这两个乘子在 Pipeline 端打包成 `vec4 uIblFactor`（rgb = tint * intensity，
// a 保留）写到 LightUbo，shader 端的 `iblDiffuse / iblSpec` 各乘一次。
// 这样 intensity / tint 可以运行时改无需重新烘焙——重烘 irradiance +
// prefiltered（O(秒) 的 GGX importance sampling）一遍只为调一下亮度并不划算。
//
// 设计选择对位（**Lumix `render_module.h:267 EnvProbeInfo` audit 2026-05-18**）：
//
// | 字段 | Lumix EnvProbeInfo | OrangeEngine EnvironmentComponent |
// |------|--------------------|----------------------------------|
// | 反射贴图 | `gpu::TextureHandle reflection` | `cubemap`（HDR equirect 资产，启动期 IBL 烘焙的源） |
// | 漫反射 | `Vec3 sh_coefs[9]` 或 `gpu::TextureHandle radiance` | 同一 `cubemap` 烘出 irradiance cube，不存 SH（baseline 不引 spherical harmonics） |
// | 位置 / 体积 | `DVec3 position` + `Vec3 half_extents` | 不存——baseline 单一 World 全局环境，无体积 blending |
// | 强度 / 色调 | Lumix 由 material 端混 | `intensity` + `tint`，直通 Pipeline UBO，运行时可调 |
//
// Lumix 的 `position` / `half_extents` 是 reflection probe 体积 blending
// 的必需字段——本 PBR + IBL baseline 明确 out-of-scope，对应留给后续独
// 立 reflection probe milestone（见 `docs/pbr-ibl-milestone.md`
// §Out-of-scope）。
//
// **资产管线**：`cubemap` 当前持 `AssetHandle<TextureAsset>` —— c6 阶段
// （EnvironmentComponent 公共 API + serializer + Pipeline 接入）只声明
// 引用关系，**真实 HDR 资产加载留给 c7**（PolyHaven CC0 HDRI 入库 + HDR
// equirect loader）。c6 落地时 component 持有的 handle 通常是 invalid 或
// 指向尚未支持 HDR 的 8-bit `TextureAsset`，Pipeline 端会保留 dummy IBL
// fallback；intensity / tint 已经接通 UBO，c7 把 cubemap 真正接到
// `IblBaker → Pipeline::SetIblTextures` 上后立刻视觉生效。
//
// **POD + trivially-copyable**：与 `DirectionalLight` / `RigidBodyComponent`
// 同节奏；不持有运行时缓存（烘焙产物由 Pipeline 侧 LRU 或单实例 cache
// 管理，不挂回 component 上——避免 component 变 "数据 + 状态" 混合体）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/AssetHandle.h>

#include <glm/vec3.hpp>

namespace Orange::Engine::Asset
{
    class TextureAsset;
}

namespace Orange::Engine::Render
{

    // World 全局 IBL 环境（一个 World 一个，Pipeline first-found 取用）。
    struct EnvironmentComponent
    {
        // HDR 环境贴图资产（典型为 equirect / 已是 cubemap 视情况）。Pipeline
        // 在场景加载完毕后通过 `IblBaker` 把它烘焙成 irradiance + prefiltered
        // specular cubemap，再喂给 `Pipeline::SetIblTextures`。c6 阶段该 handle
        // 通常为 invalid——HDR loader 在 c7 落地；invalid 时 Pipeline 退化到
        // dummy IBL（与未挂 component 视觉等价）。
        Asset::AssetHandle<Asset::TextureAsset> cubemap{};

        // 线性 RGB 色调乘子。1,1,1 = 无修正；0,0,0 = 关掉 IBL 贡献。Pipeline
        // 端打包成 `tint * intensity` 写入 LightUbo，shader 端各 IBL 项乘一次。
        glm::vec3 tint{1.0f, 1.0f, 1.0f};

        // 标量强度乘子。1 = 烘焙原始亮度；> 1 = 加亮（适用 LDR HDRI 想拉夸张
        // 场景）；< 1 = 减亮（夜景 / 阴天）。可负但无物理意义。
        float intensity{1.0f};
    };

} // namespace Orange::Engine::Render

#endif // ORANGE_ENGINE_RENDER_ENVIRONMENT_COMPONENT_H
