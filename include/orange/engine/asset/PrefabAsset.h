#ifndef ORANGE_ENGINE_ASSET_PREFAB_ASSET_H
#define ORANGE_ENGINE_ASSET_PREFAB_ASSET_H

// ---------------------------------------------------------------------------
// PrefabAsset —— prefab 资源的 CPU 数据容器。
//
// prefab = 一棵实体子树的可复用模板。设计核心：prefab 不引入新的子树表示，
// 直接复用 Scene::SaveSubtreeToString 产出的 scene/world JSON 文本作为"模板
// blob"。实例化时把这段 blob 喂给 Scene::LoadFromString 即得到一份内部引用
// 已重映射的克隆。PrefabAsset 因此只需持有这段原文字符串（形态 B：template
// 存为 blob 字符串），不做二次解析。
//
// 磁盘格式由 PrefabLoader 负责（独立 schema namespace "prefab/asset" 1.0）：
//   {
//     "schemaVersion": { "namespace": "prefab/asset", "major": 1, "minor": 0 },
//     "prefabName": "...",
//     "template": "<SaveSubtreeToString 产出的 JSON 原文，作为一个字符串字段>"
//   }
// mTemplateBlob 即 template 字段原文，字节保真——Save→Load 往返不丢字节。
//
// MVP 不承载：override 表 / 嵌套引用 / 缩略图等元数据。这些是后续 milestone
// 的事；prefab/asset 1.0 schema 一次冻结，扩字段走 minor bump。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <string>
#include <utility>

namespace Orange::Engine::Asset
{

class ORANGE_ENGINE_API PrefabAsset
{
public:
    PrefabAsset() = default;

    PrefabAsset(std::string prefabName, std::string templateBlob)
        : mPrefabName(std::move(prefabName)), mTemplateBlob(std::move(templateBlob))
    {
    }

    // 人类可读的 prefab 名（资产浏览器显示用，非身份键——身份键是资源路径）。
    const std::string& PrefabName() const noexcept { return mPrefabName; }

    // 模板 blob：SaveSubtreeToString 产出的 scene/world JSON 原文。实例化时
    // 直接喂给 Scene::LoadFromString，零二次解析。
    const std::string& TemplateBlob() const noexcept { return mTemplateBlob; }

private:
    std::string mPrefabName;
    std::string mTemplateBlob;
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_PREFAB_ASSET_H
