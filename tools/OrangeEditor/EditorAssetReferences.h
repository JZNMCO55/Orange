#ifndef ORANGE_EDITOR_EDITOR_ASSET_REFERENCES_H
#define ORANGE_EDITOR_EDITOR_ASSET_REFERENCES_H

// EditorAssetReferences —— "谁引用了这个资产" 的只读依赖扫描。
//
// 遍历 World 全实体 × ComponentSchemaRegistry 中带 AssetRef 字段的 schema，
// 用 schema.get 拿组件 + prop.assetRefGet 解析该字段引用的资产路径，与目标
// path 逐一比对，收集命中的 (entity, component, field)。
//
// 用途：资产浏览器"Referenced by N"显示——删/改资产前先看牵连，避免断引用。
// 也是资产删除/重命名功能（gap 报告 §2.2 P1）的依赖图基础设施第一步：rename
// 时按本扫描结果批量改引用。**本文件纯只读、不动文件系统、不改任何引用**；
// 写侧（rename 文件 + 批量改引用 + undo）留 focused session（filesystem mutation
// 不可逆，不宜与只读扫描混在一起）。
//
// 复杂度 O(entities × assetRef-schemas × assetRef-props)，由 user 触发（选中
// 资产时），editor 规模可忽略。路径比较为精确字符串相等——调用方需保证传入
// path 与 assetRefGet 返回的路径同格式（均为 assets/ 相对正斜杠路径）。

#include <orange/engine/scene/Entity.h>

#include <string>
#include <string_view>
#include <vector>

struct EditorHost;

namespace Orange::Editor
{

struct AssetReference
{
    Orange::Engine::Entity entity;
    const char*            componentType;  // schema.typeName（进程期静态字面量）
    const char*            fieldName;       // prop.name（同上）
};

// 找出 World 内所有引用 assetPath 的字段。assetPath 空 / 无 World → 空 vector。
std::vector<AssetReference> FindAssetReferences(EditorHost& host, std::string_view assetPath);

}  // namespace Orange::Editor

#endif  // ORANGE_EDITOR_EDITOR_ASSET_REFERENCES_H
