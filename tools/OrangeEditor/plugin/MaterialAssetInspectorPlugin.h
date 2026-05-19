#ifndef ORANGE_EDITOR_PLUGIN_MATERIAL_ASSET_INSPECTOR_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_MATERIAL_ASSET_INSPECTOR_PLUGIN_H

// MaterialAssetInspectorPlugin —— v0.7 c0 落地：第一个真实
// IEditorAssetInspectorPlugin case。
//
// 职责：当 Asset 浏览器选中 .material 文件时接管整个 Inspector 区域，
// 显示 templateName Combo 切换 + PBR 五通道调参 + Save 写回 .material
// 文件。
//
// 设计意图：本 plugin 是 v0.5 c5 在 InspectorPanel.cpp 顶部 hardcode
// `IsMaterialAssetSelected → DrawMaterialSubMode` if 分支的迁出版——
// 落地 v0.7 c0 抽象后，"按选中资源扩展名切 Inspector 内容" 走
// IEditorAssetInspectorPlugin 注册表，不再 hardcode 在 InspectorPanel
// 上。v0.7 c2 Animation 子模式作为第二个 plugin case 验证抽象边界。

#include "IEditorAssetInspectorPlugin.h"

namespace Orange::Editor::Plugin
{

class MaterialAssetInspectorPlugin : public IEditorAssetInspectorPlugin
{
public:
    // 按 path 末尾 ".material" 后缀比较匹配。空 path / 短 path 返回 false。
    bool CanHandle(const std::string& assetPath) const override;

    // 接管 Inspector 整段，调用原 DrawMaterialSubMode 路径。
    void Draw(EditorHost& host, const std::string& assetPath) override;
};

}  // namespace Orange::Editor::Plugin

#endif  // ORANGE_EDITOR_PLUGIN_MATERIAL_ASSET_INSPECTOR_PLUGIN_H
