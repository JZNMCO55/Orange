// ComponentSchemaRegistry —— 单例 store 实现。见 ComponentSchemaRegistry.h
// 的设计说明。

#include "ComponentSchemaRegistry.h"

#include <orange/engine/core/Log.h>

#include <cassert>
#include <cstdio>

namespace Orange::Editor::Schema
{

    ComponentSchemaRegistry& ComponentSchemaRegistry::Instance()
    {
        static ComponentSchemaRegistry sInstance;
        return sInstance;
    }

    void ComponentSchemaRegistry::Register(std::type_index typeIdx,
                                           ComponentSchema schema)
    {
        // 重复注册视为开发期 bug——任何同类型的两次 Register 都意味着
        // RegisterBuiltinSchemas 或游戏侧扩展点逻辑错误。debug 期 assert；
        // release 期保留首次注册条目（不覆盖），并 stderr 警告。
        if (auto it = mByType.find(typeIdx); it != mByType.end())
        {
            ORANGE_LOG_ERROR("[OrangeEditor] ComponentSchemaRegistry: 重复注册 '{}' —— "
                             "保留首次注册条目",
                             schema.typeName ? schema.typeName : "(nullptr)");
            assert(false && "ComponentSchemaRegistry duplicate registration");
            return;
        }

        const std::size_t index = mSchemas.size();
        mSchemas.push_back(std::move(schema));
        mByType.emplace(typeIdx, index);
    }

    const ComponentSchema*
    ComponentSchemaRegistry::Find(std::type_index typeIdx) const
    {
        auto it = mByType.find(typeIdx);
        if (it == mByType.end())
        {
            return nullptr;
        }
        return &mSchemas[it->second];
    }

} // namespace Orange::Editor::Schema
