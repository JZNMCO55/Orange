#include "CommandStack.h"

#include <cassert>
#include <string_view>
#include <utility>

namespace
{

// CommandGroup —— 原子化打包多条 ICommand 的 ICommand 子类。
//
// 由 CommandStack::EndGroup 构造：
//   * Execute() 顺序执行子命令（构造时的 push_back 顺序）；
//   * Undo() 反序撤销（先 push 进来的最后 undo）；
//   * GetType() 返回组名——CommandStack 的 EndGroup 入栈 merge 路径靠它
//     做同名判别；
//   * Merge(other) 把 other 的子命令并入 self——按 sub-command GetType 配
//     对，找到匹配的就调 sub-command Merge；不匹配的 append 到 self 末
//     尾。语义：两组同名 EndGroup 连续触发时，self 内的字段编辑都被 other
//     的最新值覆盖（mNewValue 滚到最新），栈上仍是单条 group 条目。
//
// 命名约定：组名（mName）字符串字面量静态生命周期，由 CommandStack 持有
// 不复制。CommandGroup 仅引用——若调用方传入临时 std::string::c_str() 则
// caller 自己负责。
class CommandGroup final : public ICommand
{
public:
    CommandGroup(const char*                            name,
                 std::vector<std::unique_ptr<ICommand>> children)
        : mName(name != nullptr ? name : "?")
        , mChildren(std::move(children))
    {}

    void Execute() override
    {
        for (auto& cmd : mChildren)
        {
            if (cmd != nullptr) { cmd->Execute(); }
        }
    }

    void Undo() override
    {
        // 反序 undo：保证字段在多 sub-command 之间的依赖关系正确（如 add
        // entity 命令必须先于在该 entity 上的 SetField 命令，undo 时反过
        // 来）。本期所有 sub-command 都是 SetFieldValueCommand 类型，子
        // 命令之间无相互依赖；预留反序逻辑给后续可能引入的复合命令。
        for (auto it = mChildren.rbegin(); it != mChildren.rend(); ++it)
        {
            auto& cmd = *it;
            if (cmd != nullptr) { cmd->Undo(); }
        }
    }

    const char* GetType() const override { return mName; }

    bool Merge(ICommand& newer) override
    {
        // 仅同类型（其它 CommandGroup）才尝试 merge。本类的 GetType ==
        // mName 唯一性已由 CommandStack 在入栈前判别（同名 group 才进 Merge
        // 路径），这里再加一层 dynamic_cast 防呆：跨类型误调直接拒绝。
        auto* g = dynamic_cast<CommandGroup*>(&newer);
        if (g == nullptr) { return false; }

        // 把 g 的子命令按 GetType 在 self 内找配对项。匹配则 sub-Merge；不
        // 匹配则 append 到 self 末尾。注意：sub-Merge 失败（子命令自身拒绝
        // 合并，如 SetFieldValueCommand 的 entity / fieldKey 不匹配）也走
        // append 路径——保留 g 的该子命令到 self 末尾，避免丢效果。
        //
        // 复杂度 O(n*m)：n = self 子命令数，m = g 子命令数。group 内典型
        // 子命令 ≤ 一双数（多字段 gizmo 拖动），不构成 hot path。
        for (auto& newerCmd : g->mChildren)
        {
            if (newerCmd == nullptr) { continue; }
            bool merged = false;
            for (auto& existing : mChildren)
            {
                if (existing == nullptr) { continue; }
                if (std::string_view(existing->GetType())
                    == std::string_view(newerCmd->GetType())
                    && existing->Merge(*newerCmd))
                {
                    merged = true;
                    break;
                }
            }
            if (!merged)
            {
                mChildren.push_back(std::move(newerCmd));
            }
        }
        // 不论 sub-Merge 是否全成，整 group Merge 返回 true——表示"newer
        // 已被 self 吸收"。CommandStack 入栈路径需要这个返回值决定不再
        // push newer，避免栈重复。
        return true;
    }

private:
    const char*                            mName;
    std::vector<std::unique_ptr<ICommand>> mChildren;
};

// 工具：与栈顶（或更早条目）同名 group 的 in-place merge。
// 调用方保证 stack[idx] 是 CommandGroup 且 name 与 incoming group 相同。
// merge 成功后重新 Execute stack[idx]，让 mNewValue 滚到最新值。
bool MergeIntoExistingGroup(std::vector<std::unique_ptr<ICommand>>& stack,
                            int                                     idx,
                            std::unique_ptr<ICommand>&              incoming)
{
    if (idx < 0 || idx >= static_cast<int>(stack.size())) { return false; }
    if (stack[idx] == nullptr || incoming == nullptr)    { return false; }
    if (!stack[idx]->Merge(*incoming))                    { return false; }
    stack[idx]->Execute();
    return true;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// CommandStack
// ---------------------------------------------------------------------------

CommandStack::CommandStack()  = default;
CommandStack::~CommandStack() = default;

void CommandStack::Push(std::unique_ptr<ICommand> cmd)
{
    if (cmd == nullptr) { return; }

    // ---- 组内分支 -------------------------------------------------------
    //
    // BeginGroup..EndGroup 之间的 Push 不直接上 mStack：
    //   1. 立刻 Execute（caller 期待 live preview）；
    //   2. 尝试在 pending group 末尾做 intra-group coalesce——同 GetType
    //      + Merge 返回 true 即合并不重复入队，避免 60 帧拖动产生 60 条；
    //   3. 合并失败 / pending 为空时 append 到 pending 末尾。
    //
    // EndGroup 时把整段 pending 打包成单条 CommandGroup 入 mStack。
    if (mInGroup)
    {
        cmd->Execute();
        if (!mPendingGroup.empty()
            && mPendingGroup.back() != nullptr
            && std::string_view(mPendingGroup.back()->GetType()) == cmd->GetType()
            && mPendingGroup.back()->Merge(*cmd))
        {
            if (mOnChanged) { mOnChanged(); }
            return;
        }
        mPendingGroup.push_back(std::move(cmd));
        if (mOnChanged) { mOnChanged(); }
        return;
    }

    // ---- 默认（无组）路径 ----------------------------------------------
    //
    // 清除 redo 历史（插入点之后的命令不再可达）。
    // 注意：必须先算整数加法再传给 resize，不能写成
    //   mStack.erase(mStack.begin() + mIndex + 1, mStack.end())
    // ——C++ 左结合律会先算 begin() + mIndex（当 mIndex=-1 时越过头部），
    // MSVC _ITERATOR_DEBUG_LEVEL=2 对这个非法中间 iterator 立即 abort。
    if (mIndex + 1 < static_cast<int>(mStack.size()))
    {
        mStack.resize(static_cast<std::size_t>(mIndex + 1));
    }

    // 尝试与栈顶同类命令 coalesce：
    //   1. GetType() 相同 —— 进一步调 Merge
    //   2. Merge 返回 true   —— 合并成功，重新执行栈顶命令，不 push 新条目
    if (mIndex >= 0
        && std::string_view(mStack[mIndex]->GetType()) == cmd->GetType()
        && mStack[mIndex]->Merge(*cmd))
    {
        mStack[mIndex]->Execute();
        if (mOnChanged) { mOnChanged(); }
        return;
    }

    // 正常 push：先执行，再入栈
    cmd->Execute();
    mStack.push_back(std::move(cmd));
    ++mIndex;

    // 超出容量上限时淘汰最旧的命令
    while (static_cast<int>(mStack.size()) > kMaxSize)
    {
        mStack.erase(mStack.begin());
        --mIndex;
    }

    if (mOnChanged) { mOnChanged(); }
}

void CommandStack::BeginGroup(const char* name, MergeMode mode)
{
    // 嵌套 BeginGroup —— release 期 forgiving：把已有 pending 命令一同并
    // 入新组，避免 caller 漏调 EndGroup 卡死状态。debug 期断言提醒。
    assert(!mInGroup && "Nested BeginGroup is not supported");
    (void)0;  // assert 上方已显式断言；release 走 forgiving 行为下文

    mInGroup   = true;
    mGroupName = (name != nullptr) ? name : "Group";
    mGroupMode = mode;
    // mPendingGroup 不在此处清空——若是嵌套调用（assert 已警告），保留
    // 已有 pending 让下一次 EndGroup 一次性打包是 forgiving fallback。
    // 正常无嵌套路径下 mPendingGroup 在上一次 EndGroup 末尾已清空。
}

void CommandStack::EndGroup()
{
    assert(mInGroup && "EndGroup without matching BeginGroup");
    if (!mInGroup) { return; }
    mInGroup = false;

    // 空组直接丢弃（typical：caller BeginGroup 后没产生任何编辑，比如用
    // 户点了 gizmo 但没拖动）。mGroupName / mGroupMode 重置由本函数末尾统
    // 一处理。
    if (mPendingGroup.empty())
    {
        mGroupName = nullptr;
        return;
    }

    // 打包成 CommandGroup ICommand 实例。注意 mGroupName 在 mInGroup ==
    // true 期间由 BeginGroup 设置；mPendingGroup move 出去后清空。
    std::unique_ptr<ICommand> grouped =
        std::make_unique<CommandGroup>(mGroupName, std::move(mPendingGroup));
    mPendingGroup.clear();  // move 后 source 可能保持已分配 capacity，显式 clear

    // ---- MergeMode 三档分派 --------------------------------------------
    //
    // Disable: 跳过 merge，直接 push。
    // Ends:    仅尝试与栈顶（mIndex）同名 group merge。
    // All:     从栈顶往下找最近的同名 group merge；找到即停。
    bool merged = false;
    if (mGroupMode != MergeMode::Disable && mIndex >= 0)
    {
        const std::string_view groupedName = grouped->GetType();
        const int searchEnd = (mGroupMode == MergeMode::All) ? 0 : mIndex;
        for (int i = mIndex; i >= searchEnd; --i)
        {
            if (mStack[i] == nullptr) { continue; }
            if (std::string_view(mStack[i]->GetType()) != groupedName)
            {
                if (mGroupMode == MergeMode::Ends) { break; }  // 仅栈顶检查
                continue;
            }
            if (MergeIntoExistingGroup(mStack, i, grouped))
            {
                merged = true;
                break;
            }
            if (mGroupMode == MergeMode::Ends) { break; }
        }
    }

    if (!merged)
    {
        // 清除 redo 历史 + 正常 push。grouped 内部已经 Execute 过子命令（Push
        // 进 pending 时已执行），这里不再调 Execute——避免对字段值二次应用。
        if (mIndex + 1 < static_cast<int>(mStack.size()))
        {
            mStack.resize(static_cast<std::size_t>(mIndex + 1));
        }
        mStack.push_back(std::move(grouped));
        ++mIndex;
        while (static_cast<int>(mStack.size()) > kMaxSize)
        {
            mStack.erase(mStack.begin());
            --mIndex;
        }
    }

    mGroupName = nullptr;

    // 非空 EndGroup（无论 merged 还是新 push）都视为一次有效变更。空组上方
    // 已 early return，不会到达这里。
    if (mOnChanged) { mOnChanged(); }
}

bool CommandStack::InGroup() const
{
    return mInGroup;
}

void CommandStack::Undo()
{
    if (!CanUndo()) { return; }
    mStack[mIndex]->Undo();
    --mIndex;
    if (mOnChanged) { mOnChanged(); }
}

void CommandStack::Redo()
{
    if (!CanRedo()) { return; }
    ++mIndex;
    mStack[mIndex]->Execute();
    if (mOnChanged) { mOnChanged(); }
}

void CommandStack::Clear()
{
    mStack.clear();
    mIndex = -1;
    // 清理 group 状态——切场景 / 破坏性操作时调 Clear，pending group 也
    // 跟着一起清掉（否则下次 EndGroup 会把跨场景的命令打包，闯祸）。
    mPendingGroup.clear();
    mGroupName = nullptr;
    mGroupMode = MergeMode::Ends;
    mInGroup   = false;
}

void CommandStack::SetOnChanged(std::function<void()> hook)
{
    mOnChanged = std::move(hook);
}

bool CommandStack::CanUndo() const
{
    return mIndex >= 0;
}

bool CommandStack::CanRedo() const
{
    return mIndex + 1 < static_cast<int>(mStack.size());
}
