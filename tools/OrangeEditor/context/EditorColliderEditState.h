#ifndef ORANGE_EDITOR_CONTEXT_EDITOR_COLLIDER_EDIT_STATE_H
#define ORANGE_EDITOR_CONTEXT_EDITOR_COLLIDER_EDIT_STATE_H

// EditorColliderEditState —— "Polygon / EdgeChain 顶点在 viewport 内直接编辑"
// 这一交互子模式的全局状态。
//
// 背景（engine-known-gaps GAP-2026-05-21-collider-polygon-edgechain-
// interactive-edit）：Polygon / EdgeChain 顶点过去只能在 Inspector 内
// DragFloat2 输数字，用户得心算坐标 ↔ viewport 位置反向映射。本子模式让
// 用户在视口里直接点击加点 / 拖拽现有点 / 右键或双击删点。
//
// 设计要点（与其它 sub-context 同款，挂在 EditorHost 上而非往 EditorState
// 堆字段，符合 "OrangeEditor 架构纪律" 的 "新功能找对应子 context" 规约）：
//   * 与内置 Translate / Rotate / Scale gizmo 互斥：active 时 ScenePanel
//     跳过 gizmo 处理 + viewport picking，鼠标交互全部交给 collider 顶点编辑
//   * editing entity 进入时锁定（active 期间换选其它实体不改 entity，避免
//     拖拽中途目标漂移）；Esc 或 entity / collider 失效时 Reset
//   * 命令栈 coalesce：拖拽一个顶点的连续帧共享同一 opId（拼进命令 fieldKey）
//     → 合并成一条可一次 Undo 的命令；加点 / 删点 / 新一次拖拽各自递增 opId
//     → 互不 coalesce

#include <orange/engine/scene/Entity.h>

struct EditorColliderEditState
{
    // 是否处于 collider 顶点编辑子模式。
    bool active{false};

    // 正在编辑的 entity —— 进入子模式时锁定，active 期间不随 selection 改变。
    Orange::Engine::Entity entity{Orange::Engine::Entity::Invalid()};

    // 当前被拖拽 / 选中的顶点下标；-1 = 无。
    int selectedVertex{-1};

    // 鼠标 hover 命中的顶点下标（仅用于高亮反馈）；-1 = 无。
    int hoverVertex{-1};

    // 是否正在拖拽顶点（LMB 按住 + 命中顶点起拖）。
    bool dragging{false};

    // 单调递增的操作序号。每个离散操作（开始一次拖拽 / 加点 / 删点）自增一次，
    // 拼进 SetFieldValueCommand 的 fieldKey，使同一拖拽内连续命令 coalesce、
    // 跨操作不 coalesce。不在 Reset 时清零（单调即可，避免跨 entity 复用同 key）。
    int opSeq{0};

    // 当前拖拽对应的 opId（拖拽期间固定，drag 帧共享同一 fieldKey）；-1 = 无活跃拖拽。
    int dragOpId{-1};

    // 退出子模式 / entity 失效时复位（opSeq 保留单调递增）。
    void Reset()
    {
        active         = false;
        entity         = Orange::Engine::Entity::Invalid();
        selectedVertex = -1;
        hoverVertex    = -1;
        dragging       = false;
        dragOpId       = -1;
    }
};

#endif  // ORANGE_EDITOR_CONTEXT_EDITOR_COLLIDER_EDIT_STATE_H
