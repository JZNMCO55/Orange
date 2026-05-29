// CommandStack Undo/Redo label peek 单测。
//
// 覆盖 GAP-2026-05-29-editor-undo-redo-action-label 的纯逻辑内核：
//   * ICommand::GetLabel 默认回退到 GetType / override 路径分流；
//   * CommandStack::PeekUndoLabel / PeekRedoLabel 跟随 Undo/Redo 游标；
//   * CommandGroup 的 label == 组名；
//   * Push 清 redo 历史后 PeekRedoLabel 归位。
//
// 菜单文案拼接（"Undo <label>"）的视觉/焦点不在此测——那是 EditorRenderLayer
// 的 ImGui 路径，单测覆盖到"label 数据正确"为止，剩余顺手 dogfood 一眼即可。
//
// 同 editor_hierarchy_test / material_file_io_test 模式：CommandStack.cpp 不属
// 引擎 lib，直接编进本测试 exe（CMake 加 tools/OrangeEditor include path）。零
// ImGui/GLFW/Vulkan 依赖，纯 ICommand + std。

#include "command/CommandStack.h"
#include "command/ICommand.h"
#include "command/LambdaCommand.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>

namespace
{

// GetType 与 GetLabel 刻意取不同值——验证 Peek 走 GetLabel（展示文案）而非
// GetType（coalesce 配对键）。type 同时用作 coalesce 键，测试里给每条不同 type
// 以规避 coalesce 干扰游标断言。
class LabeledCommand : public ICommand
{
public:
    LabeledCommand(const char* type, const char* label)
        : mType(type)
        , mLabel(label)
    {}

    void        Execute() override {}
    void        Undo() override {}
    const char* GetType() const override { return mType; }
    const char* GetLabel() const override { return mLabel; }

private:
    const char* mType;
    const char* mLabel;
};

bool StrEq(const char* a, const char* b)
{
    return a != nullptr && b != nullptr && std::strcmp(a, b) == 0;
}

std::unique_ptr<ICommand> MakeLabeled(const char* type, const char* label)
{
    return std::make_unique<LabeledCommand>(type, label);
}

}  // namespace

int main()
{
    // 1. 空栈：Undo/Redo 都不可用 → Peek 全 nullptr。
    {
        CommandStack s;
        assert(s.PeekUndoLabel() == nullptr);
        assert(s.PeekRedoLabel() == nullptr);
    }

    // 2. LambdaCommand 不 override GetLabel → 默认回退到 GetType。
    {
        CommandStack s;
        s.Push(std::make_unique<LambdaCommand>(
            "create_entity", [] {}, [] {}));
        assert(StrEq(s.PeekUndoLabel(), "create_entity"));
        assert(s.PeekRedoLabel() == nullptr);
    }

    // 3. GetLabel override 路径：菜单显示友好 label 而非机器味 type。
    {
        CommandStack s;
        s.Push(MakeLabeled("rename_entity", "Rename Entity"));
        assert(StrEq(s.PeekUndoLabel(), "Rename Entity"));
        assert(!StrEq(s.PeekUndoLabel(), "rename_entity"));
    }

    // 4. 两条命令 + Undo/Redo 游标跟随：PeekUndo 永远指"下次撤的那条"，
    //    PeekRedo 指"下次重做的那条"。
    {
        CommandStack s;
        s.Push(MakeLabeled("a", "First"));
        s.Push(MakeLabeled("b", "Second"));
        assert(StrEq(s.PeekUndoLabel(), "Second"));  // 下次 undo 撤 Second
        assert(s.PeekRedoLabel() == nullptr);        // 栈顶，无 redo

        s.Undo();
        assert(StrEq(s.PeekUndoLabel(), "First"));   // 下次 undo 撤 First
        assert(StrEq(s.PeekRedoLabel(), "Second"));  // 下次 redo 重做 Second

        s.Undo();
        assert(s.PeekUndoLabel() == nullptr);        // 全撤销，无可 undo
        assert(StrEq(s.PeekRedoLabel(), "First"));   // 下次 redo 重做 First

        s.Redo();
        assert(StrEq(s.PeekUndoLabel(), "First"));
        assert(StrEq(s.PeekRedoLabel(), "Second"));
    }

    // 5. 命令组：EndGroup 打包成 CommandGroup，其 label == 组名（GetLabel
    //    默认回退到 GetType，CommandGroup.GetType 返回组名）。用 Disable 规避
    //    同名组 merge。
    {
        CommandStack s;
        s.BeginGroup("Transform Drag", MergeMode::Disable);
        s.Push(MakeLabeled("set_field", "ignored-sub-label"));
        s.EndGroup();
        assert(StrEq(s.PeekUndoLabel(), "Transform Drag"));
    }

    // 6. Push 清 redo 历史后 PeekRedoLabel 归 nullptr（新命令截断 redo 分支）。
    {
        CommandStack s;
        s.Push(MakeLabeled("a", "First"));
        s.Push(MakeLabeled("b", "Second"));
        s.Undo();
        assert(StrEq(s.PeekRedoLabel(), "Second"));

        s.Push(MakeLabeled("c", "Third"));  // 截断 Second 的 redo 分支
        assert(StrEq(s.PeekUndoLabel(), "Third"));
        assert(s.PeekRedoLabel() == nullptr);
    }

    std::printf("command_stack_test: all assertions passed\n");
    return 0;
}
