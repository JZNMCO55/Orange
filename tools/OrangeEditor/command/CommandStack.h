#ifndef ORANGE_EDITOR_COMMAND_COMMANDSTACK_H
#define ORANGE_EDITOR_COMMAND_COMMANDSTACK_H

#include "ICommand.h"

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
// Coalesce 规则：Push 时若新命令 GetType() 与栈顶相同，则调 Merge()；
// Merge 成功则重新 Execute 栈顶（不插入新条目）。

class CommandStack
{
public:
    static constexpr int kMaxSize = 200;

    // 执行命令并入栈（自动 coalesce + 清除 redo 历史）。
    void Push(std::unique_ptr<ICommand> cmd);

    void Undo();
    void Redo();
    void Clear();

    bool CanUndo() const;
    bool CanRedo() const;

private:
    std::vector<std::unique_ptr<ICommand>> mStack;
    int mIndex = -1;  // 最后已执行命令的下标；-1 = 栈空 / 全部已撤销
};

#endif  // ORANGE_EDITOR_COMMAND_COMMANDSTACK_H
