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
            : mType(type), mLabel(label)
        {
        }

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

} // namespace

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
        assert(StrEq(s.PeekUndoLabel(), "Second")); // 下次 undo 撤 Second
        assert(s.PeekRedoLabel() == nullptr);       // 栈顶，无 redo

        s.Undo();
        assert(StrEq(s.PeekUndoLabel(), "First"));  // 下次 undo 撤 First
        assert(StrEq(s.PeekRedoLabel(), "Second")); // 下次 redo 重做 Second

        s.Undo();
        assert(s.PeekUndoLabel() == nullptr);      // 全撤销，无可 undo
        assert(StrEq(s.PeekRedoLabel(), "First")); // 下次 redo 重做 First

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

        s.Push(MakeLabeled("c", "Third")); // 截断 Second 的 redo 分支
        assert(StrEq(s.PeekUndoLabel(), "Third"));
        assert(s.PeekRedoLabel() == nullptr);
    }

    // 7. SetOnChanged：dirty 追踪钩子契约（编辑器靠它把 scene.dirty 置 true，
    //    撑起 unsaved-confirm 拦截 + autosave dirty gate）。触发时机：成功 Push
    //    （含组内 Push）/ Undo / Redo / 非空 EndGroup；**不**在 Clear 触发
    //    （Clear 是破坏性操作后清栈，不代表新脏化）；空组 EndGroup 不触发。
    {
        CommandStack s;
        int          changes = 0;
        s.SetOnChanged([&]
                       { ++changes; });

        s.Push(MakeLabeled("a", "A"));
        assert(changes == 1);
        s.Push(MakeLabeled("b", "B"));
        assert(changes == 2);

        s.Undo();
        assert(changes == 3);
        s.Redo();
        assert(changes == 4);

        // 关键：Clear 不触发（破坏性操作清栈不应再次置 dirty）。
        s.Clear();
        assert(changes == 4);

        // 非空组：组内 Push 触发 1 次 + 非空 EndGroup 触发 1 次 = +2。
        s.BeginGroup("g", MergeMode::Disable);
        s.Push(MakeLabeled("c", "C"));
        assert(changes == 5);
        s.EndGroup();
        assert(changes == 6);

        // 空组 EndGroup 不触发（无 visible 效果，不脏化）。
        s.BeginGroup("empty", MergeMode::Disable);
        s.EndGroup();
        assert(changes == 6);

        // nullptr 关闭通知，后续操作不回调（不崩）。
        s.SetOnChanged(nullptr);
        s.Push(MakeLabeled("d", "D"));
        assert(changes == 6);
    }

    // 8. Coalesce + group MergeMode 契约（命令栈核心：让一次 DragFloat 拖动 /
    //    gizmo 拖动塌缩成单条 undo）。用 Merge() 返回 true 的测试命令验证——
    //    `LabeledCommand` 不 Merge，覆盖不到这条路径。
    {
        // 同 type 时 Merge 吸收 newer 的 target 值并返回 true → 栈不增长、
        // 栈顶 re-execute 反映最新值（CommandStack::Push coalesce 分支）。
        struct MergeCmd : public ICommand
        {
            MergeCmd(const char* t, int* v, int tgt) : type(t), pv(v), target(tgt) {}
            void        Execute() override { *pv = target; }
            void        Undo() override {}
            const char* GetType() const override { return type; }
            bool        Merge(ICommand& n) override
            {
                target = static_cast<MergeCmd&>(n).target;
                return true;
            }
            const char* type;
            int*        pv;
            int         target;
        };
        // 全撤完计栈条目数（无 size() 访问器，用 Undo 深度推断）。
        auto depth = [](CommandStack& st)
        {
            int n = 0;
            while (st.CanUndo())
            {
                st.Undo();
                ++n;
            }
            return n;
        };

        // 同 type + Merge=true → 三次 Push coalesce 成单条；value=最新。
        {
            CommandStack s;
            int          v = 0;
            s.Push(std::make_unique<MergeCmd>("drag", &v, 1));
            s.Push(std::make_unique<MergeCmd>("drag", &v, 2));
            s.Push(std::make_unique<MergeCmd>("drag", &v, 3));
            assert(v == 3);
            assert(depth(s) == 1);
        }
        // 不同 type → 不 coalesce，独立条目。
        {
            CommandStack s;
            int          v = 0;
            s.Push(std::make_unique<MergeCmd>("a", &v, 1));
            s.Push(std::make_unique<MergeCmd>("b", &v, 2));
            assert(depth(s) == 2);
        }
        // Group MergeMode::Ends：两个同名 group 合并成单条（栈顶同名 merge）。
        {
            CommandStack s;
            int          v = 0;
            s.BeginGroup("Drag", MergeMode::Ends);
            s.Push(std::make_unique<MergeCmd>("drag", &v, 1));
            s.EndGroup();
            s.BeginGroup("Drag", MergeMode::Ends);
            s.Push(std::make_unique<MergeCmd>("drag", &v, 2));
            s.EndGroup();
            assert(depth(s) == 1);
        }
        // Group MergeMode::Disable：两个同名 group 不合并，各自独立条目。
        {
            CommandStack s;
            int          v = 0;
            s.BeginGroup("Drag", MergeMode::Disable);
            s.Push(std::make_unique<MergeCmd>("drag", &v, 1));
            s.EndGroup();
            s.BeginGroup("Drag", MergeMode::Disable);
            s.Push(std::make_unique<MergeCmd>("drag", &v, 2));
            s.EndGroup();
            assert(depth(s) == 2);
        }
    }

    // 9. multi-edit 群组 Undo 回初值（复刻 SchemaInspector 多选编辑的命令序列，
    //    锁 BUG-2026-06-01-multi-edit-group-undo-incomplete 的 CommandStack 侧
    //    契约）。多个"实体"共享同一 fieldKey（GetType），但 Merge 按 entityId
    //    区分——组内不同实体的命令**不**互相 coalesce、各自保留；一次 Undo 经
    //    CommandGroup 反序撤销把所有实体回到拖动前初值（而非只回到第一帧）。
    {
        // SetFieldValueCommand 的关键行为复刻：Execute/Undo 设值；GetType=key
        // （coalesce 配对键）；Merge 仅同 entityId 才吸收（不同实体拒绝合并，
        // 正是 SetFieldValueCommand::Merge 的 entity 检查）。不直接用
        // SetFieldValueCommand 以保持本测试零引擎依赖（无 Entity.h）。
        struct FieldCmd : public ICommand
        {
            FieldCmd(int* v, const char* key, int eid, int oldV, int newV)
                : pv(v), k(key), entityId(eid), oldVal(oldV), newVal(newV) {}
            void        Execute() override { *pv = newVal; }
            void        Undo() override { *pv = oldVal; }
            const char* GetType() const override { return k; }
            bool        Merge(ICommand& n) override
            {
                auto& o = static_cast<FieldCmd&>(n);
                if (o.entityId != entityId)
                {
                    return false;
                } // 不同实体不合并
                newVal = o.newVal;
                return true;
            }
            int*        pv;
            const char* k;
            int         entityId;
            int         oldVal;
            int         newVal;
        };

        CommandStack s;
        int          vP = 0, vF1 = 0, vF2 = 0; // primary + 2 follower，初值 0
        s.BeginGroup("Edit Field (multi-select)", MergeMode::Disable);
        // 帧 1：三个实体 0→1（同 key 不同 entityId → 组内各自 append、不合并）。
        s.Push(std::make_unique<FieldCmd>(&vP, "Transform.position", 1, 0, 1));
        s.Push(std::make_unique<FieldCmd>(&vF1, "Transform.position", 2, 0, 1));
        s.Push(std::make_unique<FieldCmd>(&vF2, "Transform.position", 3, 0, 1));
        // 帧 2：三个实体 1→5（继续拖动）。
        s.Push(std::make_unique<FieldCmd>(&vP, "Transform.position", 1, 1, 5));
        s.Push(std::make_unique<FieldCmd>(&vF1, "Transform.position", 2, 1, 5));
        s.Push(std::make_unique<FieldCmd>(&vF2, "Transform.position", 3, 1, 5));
        s.EndGroup();
        assert(vP == 5 && vF1 == 5 && vF2 == 5); // live preview 到最终值

        // 关键断言：一次 Undo 把三个实体**全部**回到初值 0（修复前因命令游离
        // 组外，一次 Undo 只撤一条、回不到初值）。整组是单条栈条目，撤完即空。
        s.Undo();
        assert(vP == 0 && vF1 == 0 && vF2 == 0);
        assert(!s.CanUndo());

        // 一次 Redo 恢复到最终值（CommandGroup 顺序 Execute）。
        s.Redo();
        assert(vP == 5 && vF1 == 5 && vF2 == 5);
        assert(!s.CanRedo());
    }

    std::printf("command_stack_test: all assertions passed\n");
    return 0;
}
