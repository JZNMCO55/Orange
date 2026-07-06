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
#include <orange/engine/scene/WorldPartition.h>

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
    // v0.6 c6：多文件 + manifest 路径。SaveSplitAs 弹 manifest 文件
    // dialog，按 partition.GetLayers() 写 per-layer .scene.json + manifest；
    // OpenSplit 反向。当前 Save 菜单仍走单文件 SaveAs / Save；SplitAs
    // 是显式新入口（File>Save Split As / Open Split），不替换原路径。
    SaveSplitAs,
    OpenSplit,
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
    EnterPlay, // Edit  → Play （建 snapshot + 启动 simulation）
    Pause,     // Play  → Paused
    Resume,    // Paused → Play
    Stop,      // Play / 已 Paused → Edit （销毁 simulation + 还原 snapshot）
};

// v0.6 c2：未保存改动确认对话框的"待执行动作"。
// 触发：用户在 dirty=true 时按 Esc / 点窗口 × / 点 File>New / File>Open。
// 触发路径**不**直接执行（不丢未保存），而是把目标动作记到本字段 +
// 弹模态 popup；popup 用户选 Save / Discard / Cancel 后决定怎么走。
//
// 三个用户操作的语义：
//   * Save    —— 执行 Save 流程（同 SceneOp::Save）成功后再执行 pending action
//   * Discard —— 忽略未保存改动，直接执行 pending action
//   * Cancel  —— 取消 pending action，编辑器回到原状态
//
// "Save 流程"包括 currentScenePath 为空时弹文件对话框（同 SceneOp::Save 内
// 的 SaveAs fallback）；Save 失败时**不**继续 pending action（保持 popup
// 让用户重试或 Cancel）。
enum class PendingCloseAction : std::uint8_t
{
    None = 0,
    Exit,      // Esc / 窗口 × → RequestExit
    NewScene,  // File>New → pendingSceneOp = New
    OpenScene, // File>Open → pendingSceneOp = Open
};

struct EditorSceneContext
{
    // 编辑器持有 World 所有权 —— 场景 Open / New 需要在 OnUpdate 内整体
    // swap world，必须放在 context 里让 layer 能直接 reset / replace。
    std::unique_ptr<Orange::Engine::World> pWorld;

    // v0.6 c4：layer manifest 持有者，与 pWorld 同生命周期 —— New / Open
    // 时一并 reset / 重建。值成员（非 unique_ptr）：默认构造即注册 "default"
    // layer，无空状态需要保护；不需要跨 host 共享，少一层间接。
    //
    // 设计取舍：放在 EditorSceneContext（scene 子域）而非 EditorHost 顶层，
    // 因为 partition 是 "本次打开的 scene 的元数据"，scene swap 时必须随之
    // 重建——否则旧 manifest 里的 visible 状态会泄露到新 scene 上。
    //
    // Pipeline.SetWorldPartition + Physics::ApplyLayerVisibility 每帧消费它；
    // 编辑器侧 Layer Manager panel / Hierarchy 右键 "Move to layer" 都把
    // mutate 走 CommandStack 反映到这里。
    Orange::Engine::Scene::WorldPartition partition;

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
    PlayState   playState     = PlayState::Edit;
    PlayOp      pendingPlayOp = PlayOp::None;
    std::string playSnapshotPath;

    // M9.2 帧步进：Paused 态下的单步请求。ToolbarPanel 的 Step 按钮 / MCP step
    // tool 置 true，帧末 EditorRenderLayer 消费——推进一个固定步长的 sim 后清零。
    // 与 pendingPlayOp 同款"帧末 layer 消费"节奏。放 context 而非 layer 私有：
    // MCP handler 只拿得到 EditorHost&，够不到 layer 成员。
    bool pendingStep = false;

    // M9.2 时间缩放：Play/Paused 期 sim tick 的 dt 乘子（0.1/0.5/1.0 三档 slow-mo）。
    // 只缩放游戏 simulation（module / physics / vfx / anim / audio），**不**缩放
    // 编辑器 shader 预览时间（mEditorTime 保持未缩放）。layer 消费时 clamp 到
    // [0.05, 4.0]。同样放 context 供 Toolbar 按钮 + MCP tool 共写。
    float playTimeScale = 1.0f;

    // v0.6 c2：未保存确认 popup 状态。pendingCloseAction != None 时下一
    // 帧 EditorRenderLayer 弹 modal popup；用户选 Save/Discard/Cancel 后
    // 决定怎么走 pendingCloseAction（执行 / 重置）。
    PendingCloseAction pendingCloseAction = PendingCloseAction::None;

    // Asset 浏览器双击 .scene.json → 请求打开该场景（跳过文件对话框）。
    // DrawAssetFileList（自由函数）只能写 host 级字段，无法直接碰
    // EditorRenderLayer 的 mPendingOpenScenePath；故经此桥接：EditorRenderLayer
    // OnUpdate 帧首消费——非空则路由到与 Open Recent 完全相同的打开流程
    // （注入 mPendingOpenScenePath + dirty 时走未保存确认）后清空。
    std::string requestedOpenScenePath;
};

#endif // ORANGE_EDITOR_CONTEXT_EDITOR_SCENE_CONTEXT_H
