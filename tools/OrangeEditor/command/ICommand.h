#ifndef ORANGE_EDITOR_COMMAND_ICOMMAND_H
#define ORANGE_EDITOR_COMMAND_ICOMMAND_H

// 编辑器命令基接口。所有可撤销操作均实现此接口并通过 CommandStack 管理。
//
// Merge：用于 coalesce（合并）—— Inspector DragFloat 拖动期间每帧 push 一条
// 同类命令时，CommandStack 会调 Merge 尝试合并；合并成功则重新执行已在栈上的
// 命令，不再插入新条目。这样一次完整的 DragFloat 拖动序列在撤销栈里只留一条。

class ICommand
{
public:
    virtual ~ICommand() = default;

    virtual void Execute() = 0;
    virtual void Undo()    = 0;

    // 返回命令类型字符串（字面量，不需释放）。CommandStack 用此判断两条命令
    // 是否同类，同类后才调 Merge 做进一步检查。
    virtual const char* GetType() const = 0;

    // 返回面向用户的可读动作名，供 Edit 菜单显示 "Undo <label>" / 未来命令
    // 历史面板用。与 GetType 刻意分离：GetType 是 coalesce 配对键（稳定机器
    // 串，绝不可本地化 / 美化，改了会破坏合并语义），GetLabel 是纯展示文案。
    // 默认回退到 GetType()——对组名本就友好的 CommandGroup（GetType==组名）
    // 与"字段键即标签"的 SetFieldValue 已够用；机器味重的命令（create_entity
    // 等）按需 override 成 "Create Entity"。返回指针生命周期同命令本身。
    virtual const char* GetLabel() const { return GetType(); }

    // 尝试把 newer（更新的同类命令）的变化吸收进本命令。
    // 返回 true 表示合并成功——调用方将重新 Execute 本命令而不 push newer。
    // 默认不合并；需要 coalesce 的命令（SetFieldValue / Rename）覆盖此方法。
    virtual bool Merge(ICommand& /*newer*/) { return false; }
};

#endif  // ORANGE_EDITOR_COMMAND_ICOMMAND_H
