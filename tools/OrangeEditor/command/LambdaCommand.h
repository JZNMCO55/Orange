#ifndef ORANGE_EDITOR_COMMAND_LAMBDACOMMAND_H
#define ORANGE_EDITOR_COMMAND_LAMBDACOMMAND_H

#include "ICommand.h"

#include <functional>

// LambdaCommand：通用的 execute/undo lambda 封装，用于不需要 coalesce 的
// 一次性命令（CreateEntity / Reparent / AddComponent 等）。
//
// mType 为字面量字符串；CommandStack 用它判断是否同类（LambdaCommand
// 不支持 Merge，因此即使同类也不会 coalesce，直接 push 新条目）。

class LambdaCommand : public ICommand
{
public:
    LambdaCommand(const char*           type,
                  std::function<void()> executeFn,
                  std::function<void()> undoFn)
        : mType(type)
        , mExecuteFn(std::move(executeFn))
        , mUndoFn(std::move(undoFn))
    {}

    void Execute() override { mExecuteFn(); }
    void Undo()    override { mUndoFn(); }
    const char* GetType() const override { return mType; }

private:
    const char*           mType;
    std::function<void()> mExecuteFn;
    std::function<void()> mUndoFn;
};

#endif  // ORANGE_EDITOR_COMMAND_LAMBDACOMMAND_H
