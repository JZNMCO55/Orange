// ComponentSchemaRegistry —— 单例 store 实现。见 ComponentSchemaRegistry.h
// 的设计说明。
//
// Register / Find / Unregister 自 M7 起改为 header inline（让 game.dll 的 schema
// 注册通道能在自己二进制内编译出这段代码、操作编辑器传入的 registry 引用）。本
// .cpp 只留 Instance() 单例 —— out-of-line，故仅编辑器 exe 定义这一份实例；game.dll
// 绝不引用 Instance()，只操作编辑器经 schema proc 传入的 registry 引用。

#include "ComponentSchemaRegistry.h"

namespace Orange::Editor::Schema
{

    ComponentSchemaRegistry& ComponentSchemaRegistry::Instance()
    {
        static ComponentSchemaRegistry sInstance;
        return sInstance;
    }

} // namespace Orange::Editor::Schema
