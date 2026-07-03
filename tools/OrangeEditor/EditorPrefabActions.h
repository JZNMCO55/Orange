#ifndef ORANGE_EDITOR_EDITOR_PREFAB_ACTIONS_H
#define ORANGE_EDITOR_EDITOR_PREFAB_ACTIONS_H

// ---------------------------------------------------------------------------
// EditorPrefabActions —— prefab "创建文件" + "拖入实例化" 两件事的逻辑单点
// （schema-first：把 prefab 消费逻辑抽成独立 TU，不堆进 EditorRenderLayer /
// EntityTreePanel mega-class 的函数体）。
//
// 两个自由函数：
//   * CommitNewPrefabFile —— 从一棵选中子树序列化 + PrefabLoader::Save 落盘，
//     产出 .prefab.json。**纯 IO**（同 CommitNewMaterialFile 口径，不进命令栈：
//     创建资产文件是磁盘动作，不是 world mutate）。
//   * InstantiatePrefabFromPath —— 从 .prefab.json 路径 Load handle + push
//     InstantiatePrefabCommand 到命令栈（走 undo/redo）。viewport drop 调它。
//
// 失败语义（宽容口径）：序列化失败 / Save 失败 / Load 失败均 log + 返回，
// 不抛错、不弹 modal（与 CommitNewMaterialFile / ApplyAssetDropToEntity 一致）。
// ---------------------------------------------------------------------------

#include <orange/engine/scene/Entity.h>

#include <string>

struct EditorHost;

namespace Orange::Editor::Prefab
{

    // 从 `sourceRoot` 为根的子树创建一个 .prefab.json 文件，落盘到 `targetPath`。
    // `prefabName` 写入 prefab/asset schema 的 prefabName 字段（人类可读名）。
    // caller 保证 targetPath 的父目录已存在（典型 = browserCurrentDir，免建目录）。
    // 成功返回 true（已落盘）；失败 log + 返回 false。不进命令栈。
    bool CommitNewPrefabFile(EditorHost&            host,
                             Orange::Engine::Entity sourceRoot,
                             const std::string&     targetPath,
                             const std::string&     prefabName);

    // 从 `path`（.prefab.json）Load prefab + push InstantiatePrefabCommand 实例化
    // 为新根。Load 失败 log warn + 返回 false；成功 push 命令并返回 true。
    bool InstantiatePrefabFromPath(EditorHost& host, const std::string& path);

    // ---------------------------------------------------------------------------
    // Create Prefab modal 的跨 TU 请求队列
    // ---------------------------------------------------------------------------
    //
    // "Create Prefab..." 菜单项在 EntityTreePanel.cpp（Hierarchy 节点右键），而
    // 承接它的 modal 在 EditorRenderLayer.cpp（DrawAssetsPanel End() 后）——二者
    // 是不同 TU，文件级 static 无法跨 TU 共享。把这点跨帧请求状态收在本 prefab
    // TU 的 module-level static 里（不塞进 EditorSelection god struct、也不挂
    // EditorRenderLayer 成员），用下面三个自由函数读写。
    //
    // 状态机：右键 MenuItem → RequestCreatePrefab(sourceRoot) 置 pending=true +
    // 记源根；下一帧 DrawAssetsPanel 末尾调 ConsumeCreatePrefabRequest 取出并清
    // 标志 → OpenPopup 绘制 modal。

    // 登记一次"打开 Create Prefab modal"请求，记下源根实体。
    void RequestCreatePrefab(Orange::Engine::Entity sourceRoot);

    // 若有 pending 请求则取出源根写入 *outSourceRoot 并清标志、返回 true；
    // 无请求返回 false（outSourceRoot 不变）。
    bool ConsumeCreatePrefabRequest(Orange::Engine::Entity* outSourceRoot);

} // namespace Orange::Editor::Prefab

#endif // ORANGE_EDITOR_EDITOR_PREFAB_ACTIONS_H
