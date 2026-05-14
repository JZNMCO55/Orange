#ifndef ORANGE_EDITOR_CONTEXT_EDITOR_SCENE_CONTEXT_H
#define ORANGE_EDITOR_CONTEXT_EDITOR_SCENE_CONTEXT_H

// EditorSceneContext —— World 所有权 + 当前 scene 文件路径 + 帧末 SceneOp /
// PlayOp 状态机。
//
// v0.2.5 整骨：从原 god struct EditorState 拆出 scene 子域。
//
// SceneOp / PlayState / PlayOp 三个 enum 仍是 file-scope（非嵌套），保留
// 历史 caller 写 `PlayState::Edit` / `SceneOp::None` 的简洁形式，迁移面
// 最小化。从语义上它们都是 scene 域的"行为标识"，所以放在本头文件内。

#include <orange/engine/scene/World.h>

#include <cstdint>
#include <memory>
#include <string>

// 帧末统一执行的场景级操作。把"用户从菜单点了 New / Open / ..."与模态
// 文件对话框 + 真正 swap world 的执行分开，避免在 ImGui frame 中间或
// EnTT view 迭代中触发模态阻塞 / mutate registry。
enum class SceneOp : std::uint8_t
{
    None = 0,
    New,
    Open,
    Save,
    SaveAs,
};

// Play 模式三态：
//   * Edit  —— 默认；纯编辑器状态，没有 simulation tick；所有结构性 /
//              组件级编辑都允许；selectedEntity 等 UI 状态正常工作
//   * Play  —— "运行" 状态；physics / vfx / animator 每帧 tick；编辑
//              入口（CreateEntity / Delete / DnD reparent / Rename /
//              Inspector 字段）全部 disable，防止 mutate 打破 simulation
//              不变量
//   * Paused —— 同 Play 但 tick 暂停；可用于"观察当前帧"。编辑同样禁
enum class PlayState : std::uint8_t
{
    Edit = 0,
    Play,
    Paused,
};

// 帧末统一 apply 的 Play 操作；与 SceneOp 同节奏，避免在 ImGui 帧内 /
// EnTT view 迭代中切状态破坏不变量。
enum class PlayOp : std::uint8_t
{
    None = 0,
    EnterPlay,    // Edit  → Play （建 snapshot + 启动 simulation）
    Pause,        // Play  → Paused
    Resume,       // Paused → Play
    Stop,         // Play / Paused → Edit （销毁 simulation + 还原 snapshot）
};

struct EditorSceneContext
{
    // 编辑器持有 World 所有权 —— 场景 Open / New 需要在 OnUpdate 内整体
    // swap world，必须放在 context 里让 layer 能直接 reset / replace。
    std::unique_ptr<Orange::Engine::World> pWorld;

    // 当前 scene 文件路径（绝对路径，UTF-8）；空 = 尚未保存过 / "Untitled"。
    // Save 走 currentScenePath；空时回退到 SaveAs 流程。
    std::string currentScenePath;

    // 自上次"保存 / 加载 / 新建"以来 world 是否被修改过。任一 cmdStack
    // Push / Undo / Redo / EndGroup 后由 CommandStack::onChanged 钩子置 true；
    // Save / SaveAs / Open / New 成功后由 EditorRenderLayer 清回 false。
    //
    // File>Save 菜单 enabled 判定就看这个字段——避免之前"始终以
    // currentScenePath 非空为条件"导致 fallback 走 SeedDemoWorld 时 Save 永
    // 远置灰的 bug。currentScenePath 为空时点 Save 会自动转 SaveAs 流程
    // （见 EditorRenderLayer::ApplyPendingSceneOp 内的 Save 分支）。
    bool dirty = false;

    SceneOp pendingSceneOp = SceneOp::None;

    // Play Mode 状态机：playState 是当前模式（Edit / Play / Paused），
    // pendingPlayOp 是用户菜单点击的待执行迁移；帧末
    // EditorRenderLayer::ApplyPendingPlayOp 统一处理。playSnapshotPath
    // 保存 Edit→Play 时的 World 序列化文件路径，Stop 时从该路径反序列化
    // 恢复（用 temp dir 下唯一文件名，editor 退出时清理）。
    PlayState   playState        = PlayState::Edit;
    PlayOp      pendingPlayOp    = PlayOp::None;
    std::string playSnapshotPath;
};

#endif  // ORANGE_EDITOR_CONTEXT_EDITOR_SCENE_CONTEXT_H
