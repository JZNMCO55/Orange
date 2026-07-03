#ifndef ORANGE_EDITOR_PLUGIN_DRAGON_BONES_ASSET_INSPECTOR_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_DRAGON_BONES_ASSET_INSPECTOR_PLUGIN_H

// DragonBonesAssetInspectorPlugin —— v0.7 c3 落地：IEditorAssetInspector
// Plugin 的第三个真实 case（对偶 MaterialAssetInspectorPlugin /
// AnimFsmAssetInspectorPlugin）。
//
// 职责：当 Asset 浏览器选中 DragonBones 骨架资源（`_ske.json` /
// `_ske.dbbin`）时接管整个 Inspector 区域，显示 dragonBonesName + 每个
// armature 的 metadata（bone 数 / animation 数 / animation 名清单）。
//
// 落地范围（c3 scope）：
//   * plugin 链路 + .ske 后缀识别
//   * 加载 SkeletonAsset（AssetRegistry::Load<SkeletonAsset>）+ 缓存
//   * UI：dragonBonesName / Armature TreeNode（含 bone / animation 列表）
//   * **不**含实际单 clip 预览渲染 —— viewport 内渲染 SkeletalAnimator pose
//     需要独立 preview viewport，工程量超 c3 scope；落到 c3 patch / v1.x

#include "IEditorAssetInspectorPlugin.h"

#include <memory>
#include <string>

namespace Orange::Engine::Asset
{
    class SkeletonAsset;
}

namespace Orange::Editor::Plugin
{

    class DragonBonesAssetInspectorPlugin : public IEditorAssetInspectorPlugin
    {
    public:
        DragonBonesAssetInspectorPlugin();
        ~DragonBonesAssetInspectorPlugin() override;

        // 按 path 末尾 "_ske.json" / "_ske.dbbin" 后缀比较匹配。
        bool CanHandle(const std::string& assetPath) const override;

        // 接管 Inspector 整段：显示 metadata + armature TreeNode。
        void Draw(EditorHost& host, const std::string& assetPath) override;

    private:
        // editing 副本：切换 path 时 reload。失败保持 mpAsset = nullptr。
        std::string                                                   mCachedPath;
        std::unique_ptr<const ::Orange::Engine::Asset::SkeletonAsset> mpAsset;
        bool                                                          mLoadAttempted{false};
        bool                                                          mLoadFailed{false};
    };

} // namespace Orange::Editor::Plugin

#endif // ORANGE_EDITOR_PLUGIN_DRAGON_BONES_ASSET_INSPECTOR_PLUGIN_H
