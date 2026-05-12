#ifndef ORANGE_EDITOR_COMMAND_SETFIELDVALUECOMMAND_H
#define ORANGE_EDITOR_COMMAND_SETFIELDVALUECOMMAND_H

#include "ICommand.h"

#include <orange/engine/scene/Entity.h>

#include <functional>
#include <string>
#include <utility>

// SetFieldValueCommand<T>：Inspector 字段编辑的可撤销命令，支持 coalesce。
//
// 设计：
//   * mFieldKey 由 "componentName.fieldName" 格式的字符串字面量构成，与
//     mEntity 一起唯一标识"哪个 entity 的哪个字段"，用于 Merge 匹配。
//   * Merge 只更新 mNewValue，不改 mOldValue —— 这样整段 DragFloat 拖动
//     只在栈里留一条命令，Ctrl+Z 一次还原到拖动前的原始值。
//   * mApply 是捕获 World* + Entity 的 lambda，CommandStack 的生命周期
//     绑定 World（切 World 时 Clear()），不会出现悬空引用。

template<typename T>
class SetFieldValueCommand : public ICommand
{
public:
    using ApplyFn = std::function<void(const T&)>;

    SetFieldValueCommand(Orange::Engine::Entity entity,
                         std::string           fieldKey,
                         T                     oldValue,
                         T                     newValue,
                         ApplyFn               apply)
        : mEntity(entity)
        , mFieldKey(std::move(fieldKey))
        , mOldValue(std::move(oldValue))
        , mNewValue(std::move(newValue))
        , mApply(std::move(apply))
    {}

    void Execute() override { mApply(mNewValue); }
    void Undo()    override { mApply(mOldValue); }

    // 返回 fieldKey 本身作为类型标识 —— 让 CommandStack 的 coalesce 匹配
    // 精确到字段粒度，避免不同 T 的 SetFieldValueCommand 跨类型 Merge。
    const char* GetType() const override { return mFieldKey.c_str(); }

    bool Merge(ICommand& newer) override
    {
        auto& n = static_cast<SetFieldValueCommand<T>&>(newer);
        if (n.mEntity != mEntity || n.mFieldKey != mFieldKey) { return false; }
        mNewValue = std::move(n.mNewValue);
        return true;
    }

private:
    Orange::Engine::Entity mEntity;
    std::string            mFieldKey;
    T                      mOldValue;
    T                      mNewValue;
    ApplyFn                mApply;
};

#endif  // ORANGE_EDITOR_COMMAND_SETFIELDVALUECOMMAND_H
