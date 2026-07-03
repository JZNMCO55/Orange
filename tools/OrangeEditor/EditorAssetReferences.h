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

namespace Orange::Editor::Schema
{
    struct ComponentSchema;
    struct PropertyDescriptor;
} // namespace Orange::Editor::Schema

namespace Orange::Editor
{

    struct AssetReference
    {
        Orange::Engine::Entity entity;
        const char*            componentType; // schema.typeName（进程期静态字面量）
        const char*            fieldName;     // prop.name（同上）
    };

    // 找出 World 内所有引用 assetPath 的字段。assetPath 空 / 无 World → 空 vector。
    std::vector<AssetReference> FindAssetReferences(EditorHost& host, std::string_view assetPath);

    // 把所有引用 fromPath 的组件字段改指向 toPath（prop.assetRefSet），返回改了
    // 几个字段。资产 rename 时用：先 fs::rename 文件，再 Remap(old→new) 让组件
    // 引用跟上——handle 类（mesh/texture/sound）的 assetRefSet 内部 Load(toPath)
    // 拿新 handle，故**要求 toPath 文件已存在**（rename 必须先于 Remap）。可逆：
    // undo 时 fs::rename 回 + Remap(new→old)。仅作用 assetRefGet+assetRefSet 双非空
    // 的字段（material 的 ptr 语义不在此可靠 remap，rename 入口已排除 material 文件）。
    std::size_t RemapAssetReferences(EditorHost& host, std::string_view fromPath, std::string_view toPath);

    // 被清空的一条资产引用快照——记下 (entity, schema, prop) 以便 undo 时按原 path
    // 恢复。schema/prop 是 registry 内的稳定指针（进程期不失效）。
    struct ClearedAssetRef
    {
        Orange::Engine::Entity                            entity;
        const Orange::Editor::Schema::ComponentSchema*    schema;
        const Orange::Editor::Schema::PropertyDescriptor* prop;
    };

    // 把所有引用 assetPath 的组件字段**清空**（assetRefSet 空串 → 字段置 none），
    // 同时返回被清字段的快照列表，供后续 RestoreAssetReferences 还原。资产 delete
    // 用：软删除文件后清掉悬空引用；undo 时还原文件 + 调 RestoreAssetReferences。
    std::vector<ClearedAssetRef> ClearAssetReferences(EditorHost& host, std::string_view assetPath);

    // 把 ClearAssetReferences 返回的快照列表里每条字段重新指向 assetPath
    // （assetRefSet）。entity 已失效则跳过。
    void RestoreAssetReferences(EditorHost&                         host,
                                const std::vector<ClearedAssetRef>& cleared,
                                std::string_view                    assetPath);

} // namespace Orange::Editor

#endif // ORANGE_EDITOR_EDITOR_ASSET_REFERENCES_H
