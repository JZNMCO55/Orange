#ifndef ORANGE_EDITOR_EDITOR_STATE_H
#define ORANGE_EDITOR_EDITOR_STATE_H

// EditorState —— v0.2.5 整骨后退化为薄壳，仅聚合 4 个子 context +
// CommandStack。
//
// 历史背景：v0.1 ~ v0.2 期 EditorState 是 19 字段 god struct（World 所有
// 权 / selection / 文件路径 / Play Mode / Inspector 缓存 / asset / 内置
// material instance / 轨道相机 / CommandStack 全部塞一处）。每个 milestone
// 都往里加字段而没有任何子域切分。架构整骨纠偏见 ADR-001。
//
// 拆分映射：
//   * selection（EditorSelection）—— 选中 entity / 帧末延迟操作 / rename /
//                                      Transform Euler 编辑缓存
//   * scene（EditorSceneContext）—— World 所有权 / scene 文件路径 / SceneOp /
//                                    PlayState / PlayOp
//   * assets（EditorAssetContext）—— AssetRegistry / MaterialSystem / 内置
//                                     mesh handle / demo material instance
//   * camera（EditorCameraState）—— 轨道相机字段（pivot / azimuth / ...）
//   * pCmdStack —— 暂留壳子顶层，下一 commit 引入 EditorHost 后转移过去
//
// 本结构在 commit 2 引入 EditorHost 时会被进一步替换或废弃；这次保留是
// 为了让 EditorRenderLayer 仍持单一 `EditorState&` 引用，把"字段拆分"与
// "调用入口替换"分开两个 commit，每步都能独立编译 + 独立回归。

#include "command/CommandStack.h"
#include "context/EditorAssetContext.h"
#include "context/EditorCameraState.h"
#include "context/EditorSceneContext.h"
#include "context/EditorSelection.h"

#include <memory>

struct EditorState
{
    EditorSelection    selection;
    EditorSceneContext scene;
    EditorAssetContext assets;
    EditorCameraState  camera;

    // CommandStack 暂留壳子顶层；commit 2 转移到 EditorHost。
    std::unique_ptr<CommandStack> pCmdStack;
};

#endif  // ORANGE_EDITOR_EDITOR_STATE_H
