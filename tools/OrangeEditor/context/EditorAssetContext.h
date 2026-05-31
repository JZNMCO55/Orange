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
#include <unordered_map>
#include <vector>

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
    // PBR showcase scene 用的 sphere mesh（lat/lon UV-sphere，与 sample 13/14
    // 同款）。InitializeEditorAssets 内 lazy bake assets/meshes/sphere.mesh。
    Orange::Engine::Asset::AssetHandle<Orange::Engine::Asset::MeshAsset>
        sphereMeshHandle{};

    // demo 场景用的各材质实例 —— 地址要稳定供
    // RenderableComponent::materialInstance 持有，生命周期跟着 context 走。
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pFloorMaterial;
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pWallMaterial;     // 保留备用（textured）
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pToonMaterial;     // 二阶 cel-shading
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pRimLightMaterial; // fresnel rim glow
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pDissolveMaterial; // noise 溶解 + 发光边沿
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pPbrMaterial;      // Cook-Torrance PBR baseline
    // 程序化动画史莱姆专属材质（pbr 模板 —— 160B push constant 走 uBaseColor
    // 喂入分支，让 ProceduralAnimator 每帧写的 uBaseColor 脉动真到 GPU）。
    // 独占实例，不与 pPbrMaterial/pToonMaterial 共用，避免动画污染其它物体。
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pAnimatedMaterial;

    // "Create Light Object" / "Add Renderable Component" 等编辑器创建路径
    // 共用的默认 material instance：
    //   * pDefaultRenderableMaterial = textured —— "+ Add Component → Renderable"
    //     时默认绑这个，让新挂的 Renderable 立刻能看到（而非 mesh=Invalid /
    //     material=nullptr 的空挂）；
    //   * pLightObjectMaterial = emissive —— "Create Light Object" 把灯做
    //     成发光的可见物体（cube + emissive material）。
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pDefaultRenderableMaterial;
    std::unique_ptr<Orange::Engine::Render::MaterialInstance> pLightObjectMaterial;

    // PBR showcase scene 的 18 个 MaterialInstance（两组 3×3：左 warm 暖橙 /
    // 右 white furnace；每球独立 metallic + roughness 组合）。BuildNamedMaterialInstances
    // 把它们以 "assets/materials/pbr_showcase/<key>.material" 路径加入
    // namedMaterialInstances 映射，让 pbr_showcase.scene.json 的 Renderable
    // materialInstanceId 字段可正确解析。
    std::vector<std::unique_ptr<Orange::Engine::Render::MaterialInstance>>
        pbrShowcaseMaterials;
    std::vector<std::string> pbrShowcaseMaterialPaths;  // 与 pbrShowcaseMaterials 一一对应

    // v0.5 c3：Asset 浏览器选中状态。
    // browserCurrentDir 是浏览器当前查看的目录（相对仓库根，前缀 "assets/"），
    // 启动期初始化为 "assets/" 根目录，用户点目录树切换；selectedAssetPath
    // 是浏览器当前选中的 asset 文件路径（同款相对路径），空字符串表示未选中。
    // v0.5 c4 DnD 写入 / c5 Material 子模式入口都消费 selectedAssetPath。
    std::string browserCurrentDir   = "assets";
    std::string selectedAssetPath   = {};

    // 内置 MaterialInstance 命名表，集中到 context 自身（v0.8 整骨消除 L15）。
    // 由 BuildNamedMaterialInstances 在 main 启动期填好；v0.9.5 c3 起 Schema
    // AssetRef lambda 通过 SchemaInspector 显式传入的 `const EditorAssetContext&`
    // 参数访问本字段，不再走任何 file-scope 静态注入路径。
    std::unordered_map<std::string,
                       Orange::Engine::Render::MaterialInstance*>
        namedMaterialInstances;

    // v0.5 c5 修 B2：Material 子模式编辑缓存。Combo 切换 template 需要 UI
    // 状态跨帧持久（否则每帧 DrawMaterialSubMode 都重读盘把 newTemplateIdx
    // 覆盖回盘上值，用户切换立即被冲掉）。当 editingMaterialPath 与当前
    // selectedAssetPath 不一致时（用户切到另一个 .material）重置缓存重新
    // 从盘读；一致则 editingTemplateName 持续保留用户选择，Save 时落盘。
    std::string editingMaterialPath   = {};
    std::string editingTemplateName   = {};

    // GAP-2026-05-29-editor-material-asset-dirty-tracking：当前编辑的 .material
    // 是否有**未写盘**改动（持久追踪，跨帧 / 跨切到实体后仍成立）。
    //   facet 2：ImGui 控件仅"值当帧被改"时返回 changed，松手即归 false——
    //   若 Save 按钮直接吃 transient 信号，uniform-only 编辑松手后存不下。故用
    //   持久 flag 累积"自进入本会话 / 上次 Save 以来是否改过（uniform 或 template）"。
    //   facet 1：关窗 / New / Open 未保存确认（EditorRenderLayer::HasUnsavedMaterial）
    //   也读它，让"改了材质没存就关"被拦截（不再静默丢失）。
    //   * 置位：任一 uniform widget 当帧 changed / template Combo 切换
    //   * 清零：切到另一个 .material（editingMaterialPath 变）/ Save 成功 / Discard
    bool editingMaterialDirty = false;

    // v1.2.2 patch · 用户新建（v1.1.1 Create Material UI）/ 手动 copy 进
    // assets/ / 老 .material 等"非 8 个内置 hardcode + PBR showcase 18 个"
    // 的 .material 文件，在 MaterialAssetInspectorPlugin 首次访问时按
    // templateName lazy CreateInstance + ApplyDataToInstance(读 .material
    // override) own 到本 map；BuildNamedMaterialInstances 末尾追加遍历，
    // 让 Inspector 调参 / Pick 路径都能命中。生命周期跟 EditorAssetContext
    // 走，编辑器关闭时统一析构。
    //
    // 关键 friction 修复（GAP-2026-05-24-editor-asset-browser-create-material-
    // missing 对偶）：v1.1.1 ship 了"创建 .material"但没 ship"刚创建立即
    // 可调参"；本字段补完闭环。
    std::unordered_map<std::string,
                       std::unique_ptr<Orange::Engine::Render::MaterialInstance>>
        userMaterials;
};

#endif  // ORANGE_EDITOR_CONTEXT_EDITOR_ASSET_CONTEXT_H
