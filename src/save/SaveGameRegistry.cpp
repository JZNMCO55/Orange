// SaveGameRegistry —— type-erased entry 的存储 + 重名检查。模板包装层
// 在 SaveGameRegistry.h 内 inline 完成；.cpp 只负责注册时把 entry 追加
// 到 vector，并维护 name → index 的反查表。
//
// 头隔离：本 .cpp 不直接 include `<nlohmann/json.hpp>`；公共面只露 Json
// Reader / Writer 类型本身。

#include "orange/engine/save/SaveGameRegistry.h"

#include "orange/engine/core/Log.h"

#include <utility>

namespace Orange::Engine::Save
{

const SaveGameComponentEntry* SaveGameRegistry::Find(std::string_view name) const noexcept
{
    // unordered_map 的 find 在 C++20 起支持透明 lookup（heterogeneous），
    // 但要求 hash + equal_to 都标注为 transparent。这里 key 是 std::string，
    // 默认 hasher / equal_to 不支持 string_view 直接 lookup——退而求其次
    // 现场临时构造一个 std::string；该路径仅在 Save / Load 主流程内每个
    // component key 调一次，不进 hot path。
    const std::string key(name);
    const auto it = mNameToIndex.find(key);
    if (it == mNameToIndex.end())
    {
        return nullptr;
    }
    return &mEntries[it->second];
}

Result<void, ResultCode> SaveGameRegistry::RegisterErased(SaveGameComponentEntry entry)
{
    // entry.name / entry.version / entry.{Has,Write,Read} 已由模板包装
    // 层填好且校验过非空——这里再做一遍重名检查与表追加，剩下的合法
    // 性约束（callbacks 不空、name 非空、version 有效）由模板层保证。
    if (mNameToIndex.find(entry.name) != mNameToIndex.end())
    {
        ORANGE_LOG_ERROR("SaveGameRegistry: 已存在同名 component 注册 (name='{}')",
                         entry.name);
        return ResultCode::AlreadyExists;
    }

    const std::size_t newIndex = mEntries.size();
    // 先把 name 拷出来给 map 用，再 move entry 进 vector——否则 move 之
    // 后 entry.name 状态未指定，map insert 拿到的可能是空 string。
    std::string nameCopy = entry.name;
    mEntries.push_back(std::move(entry));
    mNameToIndex.emplace(std::move(nameCopy), newIndex);

    return {};
}

Result<void, ResultCode> SaveGameRegistry::RegisterMigrator(
    std::string_view componentName,
    SchemaVersion    from,
    SchemaVersion    to,
    std::function<bool(const JsonReader&, std::string_view,
                       JsonWriter&, std::string_view)> fn)
{
    if (componentName.empty())
    {
        return ResultCode::InvalidArgument;
    }
    if (!from.IsValid() || !to.IsValid())
    {
        return ResultCode::InvalidArgument;
    }
    // from == to 视为非法 —— migrator 总是产生 schema 升级。同版本无需
    // 迁移，CanRead 会直接放行；写者注册同版本 migrator 八成是手误。
    if (from == to)
    {
        return ResultCode::InvalidArgument;
    }
    if (!fn)
    {
        return ResultCode::InvalidArgument;
    }

    const std::string nameKey(componentName);
    auto              it = mNameToIndex.find(nameKey);
    if (it == mNameToIndex.end())
    {
        ORANGE_LOG_ERROR(
            "SaveGameRegistry: 给未注册的 component 追加 migrator (name='{}')",
            nameKey);
        return ResultCode::NotFound;
    }

    auto& entry = mEntries[it->second];

    // 同 (component, from) 重复注册 → AlreadyExists。同一 from 应只对
    // 应一条 to —— 否则 chain 走到该 from 时分叉，行为未定义。
    for (const auto& m : entry.migrators)
    {
        if (m.from == from)
        {
            ORANGE_LOG_ERROR(
                "SaveGameRegistry: 重复注册 migrator (component='{}', from=ns='{}' v{}.{})",
                nameKey, from.Namespace(), from.Major(), from.Minor());
            return ResultCode::AlreadyExists;
        }
    }

    SaveGameComponentEntry::Migrator m{};
    m.from = std::move(from);
    m.to   = std::move(to);
    m.fn   = std::move(fn);
    entry.migrators.push_back(std::move(m));

    return {};
}

}  // namespace Orange::Engine::Save
