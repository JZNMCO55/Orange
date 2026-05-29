#ifndef ORANGE_EDITOR_COMMAND_COMMANDSTACK_H
#define ORANGE_EDITOR_COMMAND_COMMANDSTACK_H

#include "ICommand.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// CommandStack：编辑器撤销/重做栈。
//
// 生命周期纪律：
//   * 切换场景（New / Open）时必须调 Clear()，因为命令内的 lambda 捕获了
//     当前 World 指针，新 World 上这些引用失效。
//   * 破坏性操作（DestroySubtree / RemoveComponent）执行后也调 Clear()，
//     v0.2 阶段暂不支持这两类操作的撤销；v0.3+ 完整序列化后解锁。
//
// Coalesce 规则（v0.2 起）：Push 时若新命令 GetType() 与栈顶相同，则调
// Merge()；Merge 成功则重新 Execute 栈顶（不插入新条目）。
//
// 命令组（v0.2.5 commit 13 起）：BeginGroup/EndGroup 之间的 Push 收集到
// 组内不直接上栈；EndGroup 把整组打包成单条 CommandGroup（ICommand 子
// 类）入栈。组内仍走 intra-group coalesce（同 GetType + Merge）；EndGroup
// 入栈时按 MergeMode 决定是否与栈中同名 group 合并。

// ---------------------------------------------------------------------------
// MergeMode —— 命令组 EndGroup 入栈时的合并策略（仅作用于 BeginGroup 路径）
// ---------------------------------------------------------------------------
//
// 设计参考：vendor/godot/core/object/undo_redo.h 的 MergeMode（DISABLE /
// ENDS / ALL）。语义在 OrangeEditor 上的具体化：
//
//   * Disable —— 永不与已有条目合并。EndGroup 总产生新栈条目。
//                典型用例：明显独立的操作（"Create Entity" 等）。
//
//   * Ends    —— 仅与栈顶同名 group 合并（典型 coalesce 语义；默认值）。
//                适用：连续触发的同类型编辑（gizmo 拖动期间每帧 EndGroup）。
//
//   * All     —— 与栈中最近的同名 group 合并，允许跨过 unrelated 条目。
//                **使用注意**：跨条目合并隐含命令时序重排（合并后 group
//                的 mNewValue 反映最新拖动结果，但栈中 unrelated 条目的
//                时间序仍保留在原位）。当原 group 与 unrelated 条目之间
//                无字段重叠时安全；有重叠时可能出现"Undo 回到不一致状
//                态"。调用方负责保证安全。本期不为重叠检测撒网。
//
// 调用方代码风格：`stack.BeginGroup("Transform Drag", MergeMode::Ends);`。
// 未指定时默认 `Ends`（与原 Push coalesce 语义一致，最小惊讶）。
enum class MergeMode : std::uint8_t
{
    Disable = 0,
    Ends    = 1,  // 默认
    All     = 2,
};

class CommandStack
{
public:
    static constexpr int kMaxSize = 200;

    CommandStack();
    ~CommandStack();

    CommandStack(const CommandStack&)            = delete;
    CommandStack& operator=(const CommandStack&) = delete;
    CommandStack(CommandStack&&)                 = delete;
    CommandStack& operator=(CommandStack&&)      = delete;

    // 执行命令并入栈（自动 coalesce + 清除 redo 历史）。
    // 若当前处于 BeginGroup..EndGroup 之间，命令进入 pending group 缓冲，
    // 不直接上 mStack；EndGroup 时整组打包入栈。
    void Push(std::unique_ptr<ICommand> cmd);

    // 开始一个原子命令组。在 EndGroup 之前的所有 Push 收集到组内（仍立
    // 刻 Execute——caller 期待 live preview），不直接 push 到 mStack。
    // EndGroup 时把整组打包成单条 CommandGroup（ICommand 子类）入栈。
    //
    // 参数：
    //   * name —— 组名 / 动作名。同时作为 EndGroup 合并键（两组同名才考
    //     虑 merge）。**字符串字面量静态生命周期**——CommandStack 不复制；
    //     调用方保证传入的字符串与 stack 同生命周期（典型：编译期字面量）。
    //   * mode —— 入栈时的合并策略（见 MergeMode 顶注释）。
    //
    // 嵌套约束：本期**不支持**嵌套 BeginGroup（debug 期断言；release 期
    // 后调 BeginGroup 覆盖前一个的 name + mode，前一个 pending 命令一同
    // 并入新组——这是 forgiving 行为以防 caller 漏调 EndGroup 卡死）。
    // 真撞上嵌套用例时再扩接口。
    void BeginGroup(const char* name, MergeMode mode = MergeMode::Ends);

    // 结束当前命令组。组内若有命令则按 MergeMode 处理后入栈；空组直接丢
    // 弃（无 visible 效果，不污染栈）。
    //
    // EndGroup 之外调用是 no-op（debug 期断言）——caller 漏调 BeginGroup
    // 也不该让程序崩。
    void EndGroup();

    // 当前是否在 BeginGroup..EndGroup 之间。
    bool InGroup() const;

    void Undo();
    void Redo();
    void Clear();

    bool CanUndo() const;
    bool CanRedo() const;

    // 返回下一次 Undo / Redo 将作用的命令的可读 label（`ICommand::GetLabel`），
    // 供 Edit 菜单显示 "Undo <label>" / "Redo <label>"。无可 Undo / Redo 时
    // 返回 nullptr（调用方退回纯 "Undo" / "Redo" 文案）。
    //
    // 生命周期：返回指针指向命令持有的字符串（字面量或命令内 std::string），
    // 与对应栈条目同生命周期——**只在同帧内即用即弃**，不缓存跨帧（下一次
    // Push / Undo / Clear 可能让指针失效）。BeginGroup..EndGroup 进行中时仅
    // 反映已入栈条目，不含 pending group（菜单不在组操作中途绘制，无影响）。
    const char* PeekUndoLabel() const;
    const char* PeekRedoLabel() const;

    // 注册 "栈发生有效变更" 回调。触发时机：成功 Push（含组内 Push）/ Undo /
    // Redo / 非空组 EndGroup —— 任一调用都意味着 world 状态已被改动一次。
    // **不**在 Clear() 触发：Clear 是破坏性操作的后置清理（如 RemoveComponent
    // 先 Push 再 Clear），实际改动已在 Push 阶段被计数；Clear 本身只是抹掉
    // 撤销历史，不再代表新的脏化。
    //
    // 典型用途：editor 把 EditorSceneContext::dirty 标 true，让 File>Save
    // 菜单 enabled 判定起效。
    //
    // 替换调用——多次 SetOnChanged 仅保留最后一次。传入 nullptr 关闭通知。
    void SetOnChanged(std::function<void()> hook);

private:
    std::vector<std::unique_ptr<ICommand>> mStack;
    int mIndex = -1;  // 最后已执行命令的下标；-1 = 栈空 / 全部已撤销

    // 进行中 group 的 pending 缓冲。mInGroup == false 时本字段保持空。
    // 用裸 vector 而非 unique_ptr<CommandGroup>——避免 header 暴露
    // CommandGroup 完整定义（CommandGroup 是 .cpp 内 anonymous namespace
    // 的实现细节）。
    std::vector<std::unique_ptr<ICommand>> mPendingGroup;
    const char* mGroupName = nullptr;
    MergeMode   mGroupMode = MergeMode::Ends;
    bool        mInGroup   = false;

    // "栈有效变更" 通知钩子；空表示无注册方，调用点直接跳过。详见 SetOnChanged。
    std::function<void()> mOnChanged;
};

#endif  // ORANGE_EDITOR_COMMAND_COMMANDSTACK_H
