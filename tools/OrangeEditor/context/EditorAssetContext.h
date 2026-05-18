#ifndef ORANGE_EDITOR_CONTEXT_EDITOR_ASSET_CONTEXT_H
#define ORANGE_EDITOR_CONTEXT_EDITOR_ASSET_CONTEXT_H

// EditorAssetContext —— 编辑器自管 AssetRegistry + MaterialSystem + 内置
// mesh handle + 内置 / demo 用的 MaterialInstance 集合。
//
// v0.2.5 整骨：从原 god struct EditorState 拆出 asset 子域。
//
// 生命周期约束：AssetRegistry 与 MaterialSystem 必须长于任一引用其中
// mesh handle / material instance 的 World——因此它们由编辑器顶层 context
// 持有，World swap（New / Open）时 context 不动。
//
// 场景 Save / Load 当前不串联 AssetRegistry；存盘的 scene JSON 里的 mesh /
// material 引用对应的是 *本次启动* 创建的内置 handle，跨进程加载语义还
// 需要后续把 AssetRegistry 也参与序列化（登记到 docs/engine-known-gaps.md
// 或对应 milestone）。

#include <orange/engine/animation/AnimatorRegistry.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>

#include <memory>
#include <string>

struct EditorAssetContext
{
    std::unique_ptr<Orange::Engine::Asset::AssetRegistry>     pAssets;
    std::unique_ptr<Orange::Engine::Render::MaterialSystem>   pMaterials;
    std::unique_ptr<Orange::Engine::Animation::AnimatorRegistry> pAnimators;

    // 内置 mesh handle —— SeedDemoWorld 给 Floor 用 plane / Wall 用 cube。
    Orange::Engine::Asset::AssetHandle<Orange::Engine::Asset::MeshAsset>
        cubeMeshHandle  {};
    Orange::Engine::Asset::AssetHandle<Orange::Engine::Asset::MeshAsset>
        planeMeshHandle {};

    // demo 场景用的各材质实例 —— 地址要稳定供
    // RenderableComponent::materialInstance 持有，生命周期跟着 context 走。
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pFloorMaterial;
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pWallMaterial;     // 保留备用（textured）
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pToonMaterial;     // 二阶 cel-shading
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pRimLightMaterial; // fresnel rim glow
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pDissolveMaterial; // noise 溶解 + 发光边沿
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pPbrMaterial;      // Cook-Torrance PBR baseline

    // "Create Light Object" / "Add Renderable Component" 等编辑器创建路径
    // 共用的默认 material instance：
    //   * pDefaultRenderableMaterial = textured —— "+ Add Component → Renderable"
    //     时默认绑这个，让新挂的 Renderable 立刻能看到（而非 mesh=Invalid /
    //     material=nullptr 的空挂）；
    //   * pLightObjectMaterial = emissive —— "Create Light Object" 把灯做
    //     成发光的可见物体（cube + emissive material）。
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pDefaultRenderableMaterial;
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pLightObjectMaterial;

    // v0.5 c3：Asset 浏览器选中状态。
    // browserCurrentDir 是浏览器当前查看的目录（相对仓库根，前缀 "assets/"），
    // 启动期初始化为 "assets/" 根目录，用户点目录树切换；selectedAssetPath
    // 是浏览器当前选中的 asset 文件路径（同款相对路径），空字符串表示未选中。
    // v0.5 c4 DnD 写入 / c5 Material 子模式入口都消费 selectedAssetPath。
    std::string browserCurrentDir   = "assets";
    std::string selectedAssetPath   = {};

    // v0.5 c5 修 B2：Material 子模式编辑缓存。Combo 切换 template 需要 UI
    // 状态跨帧持久（否则每帧 DrawMaterialSubMode 都重读盘把 newTemplateIdx
    // 覆盖回盘上值，用户切换立即被冲掉）。当 editingMaterialPath 与当前
    // selectedAssetPath 不一致时（用户切到另一个 .material）重置缓存重新
    // 从盘读；一致则 editingTemplateName 持续保留用户选择，Save 时落盘。
    std::string editingMaterialPath   = {};
    std::string editingTemplateName   = {};
};

#endif  // ORANGE_EDITOR_CONTEXT_EDITOR_ASSET_CONTEXT_H
