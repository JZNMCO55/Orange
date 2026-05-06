// Core::SchemaVersion 实现
//
// 之所以脱内联到这里，是因为构造函数同时保存原始 namespace 字符串与
// 对应的 FNV-1a hash；把 hash 计算放在 TU 中，可以避免把 Hash.h 的
// constexpr 装置带给本类的每一个消费者。

#include "orange/engine/core/SchemaVersion.h"

namespace Orange::Engine
{

SchemaVersion::SchemaVersion(std::string_view namespaceName,
                             std::uint16_t major,
                             std::uint16_t minor)
    : mNamespace(namespaceName)
    , mNamespaceHash(Fnv1a64(namespaceName))
    , mMajor(major)
    , mMinor(minor)
{
}

}  // namespace Orange::Engine
