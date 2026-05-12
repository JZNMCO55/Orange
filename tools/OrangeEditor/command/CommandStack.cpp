#include "CommandStack.h"

#include <string_view>

void CommandStack::Push(std::unique_ptr<ICommand> cmd)
{
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
}

void CommandStack::Undo()
{
    if (!CanUndo()) { return; }
    mStack[mIndex]->Undo();
    --mIndex;
}

void CommandStack::Redo()
{
    if (!CanRedo()) { return; }
    ++mIndex;
    mStack[mIndex]->Execute();
}

void CommandStack::Clear()
{
    mStack.clear();
    mIndex = -1;
}

bool CommandStack::CanUndo() const
{
    return mIndex >= 0;
}

bool CommandStack::CanRedo() const
{
    return mIndex + 1 < static_cast<int>(mStack.size());
}
