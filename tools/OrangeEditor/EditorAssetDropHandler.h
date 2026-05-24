#ifndef ORANGE_EDITOR_EDITOR_ASSET_DROP_HANDLER_H
#define ORANGE_EDITOR_EDITOR_ASSET_DROP_HANDLER_H

// ---------------------------------------------------------------------------
// EditorAssetDropHandler —— "把 .material / .mesh / .wav 等资源拖到实体上
// 时怎么 apply" 的单点路由（v1.2.3 patch 引入）。
//
// 两个 caller：
//   * EntityTreePanel —— Hierarchy 行 BeginDragDropTarget 接到 ORANGE_ASSET
//     payload（直接拿到 target entity）
//   * ScenePanel —— viewport ImGui::Image 区域 BeginDragDropTarget +
//     PickEntityAt(光标 ndc) 找到命中 entity 后调本 helper
//
// 分派规则（按 path 文件扩展名）：
//   * .material            → Renderable.materialInstance（需 entity 有 RenderableComponent）
//   * .mesh / .obj         → Renderable.mesh（同上前置）
//   * .wav/.ogg/.mp3/.flac → AudioSource.sound（需 AudioSourceComponent）
//   * 其他扩展名           → log warn + 静默忽略
//
// 命令栈集成：每条 apply 都走 `SetFieldValueCommand<std::string>`，支持
// Undo / Redo（与 EditorRenderLayer "Pick to Renderable.material" 同款）。
//
// 失败语义（宽容口径）：entity invalid / 缺所需 component / 不识别扩展名
// 都 silent skip + log warn，不抛错。返回 true 表示已 push 命令；false
// 表示 skip（caller 可按需做视觉反馈）。
// ---------------------------------------------------------------------------

#include <orange/engine/scene/Entity.h>

#include <string>

struct EditorHost;

namespace Orange::Editor
{

bool ApplyAssetDropToEntity(EditorHost&                 host,
                            Orange::Engine::Entity      target,
                            const std::string&          assetPath);

}  // namespace Orange::Editor

#endif  // ORANGE_EDITOR_EDITOR_ASSET_DROP_HANDLER_H
