#ifndef ORANGE_EDITOR_PLUGIN_IMPORT_META_ASSET_INSPECTOR_PLUGIN_H
#define ORANGE_EDITOR_PLUGIN_IMPORT_META_ASSET_INSPECTOR_PLUGIN_H

// ---------------------------------------------------------------------------
// ImportMetaAssetInspectorPlugin —— v1.1 T5 落地。
//
// 当 Asset 浏览器选中 importer 产物（同目录存在 .meta sidecar 的资产）时
// 接管 Inspector 整段，readonly 显示 .meta v1 字段：
//   - source path（导入时记录的源文件路径）
//   - source hash（FNV-1a 64-bit hex）
//   - handle id（v1 占位）
//   - import params（v1 空 object）
//
// 入口策略：按"同目录 .meta 文件存在"判定，与具体扩展名解耦——任何 importer
// 产物（.mesh / .png / .jpg / ...）都自动获得 .meta readonly 视图。
//
// v1.2+ 计划在此基础上加：
//   - 可写 import params 段（normalmap green invert / scale / mipmap mode）
//   - Reimport 按钮（与 Asset Browser 右键的等价 UI 路径）
//   - 资产 thumbnail 预览
//
// 与既有 plugin 的优先级：本 plugin push 到 host.assetInspectorPlugins 末
// 尾——前置的 Material / AnimFsm / DragonBones / Audio plugin 走自己路径
// 时不会与本 plugin 冲突（CanHandle 判扩展名互斥）；只有"未被特化处理的
// importer 产物"落到本 plugin。
// ---------------------------------------------------------------------------

#include "IEditorAssetInspectorPlugin.h"

namespace Orange::Editor::Plugin
{

    class ImportMetaAssetInspectorPlugin : public IEditorAssetInspectorPlugin
    {
    public:
        bool CanHandle(const std::string& assetPath) const override;
        void Draw(EditorHost& host, const std::string& assetPath) override;
    };

} // namespace Orange::Editor::Plugin

#endif // ORANGE_EDITOR_PLUGIN_IMPORT_META_ASSET_INSPECTOR_PLUGIN_H
