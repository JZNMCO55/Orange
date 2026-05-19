#include <orange/engine/core/Memory.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Orange::Engine::Core::Memory
{

namespace
{

struct State
{
    std::vector<CategorySnapshot>                    cats;
    std::unordered_map<std::string_view, std::size_t> nameToIndex;
};

State& GetState()
{
    static State s;
    return s;
}

}  // namespace

void RegisterCategory(const char* name)
{
    if (name == nullptr) { return; }
    auto& s = GetState();
    if (s.nameToIndex.find(std::string_view{name}) != s.nameToIndex.end())
    {
        return;
    }
    CategorySnapshot snap{};
    snap.name = name;
    s.cats.push_back(snap);
    s.nameToIndex.emplace(std::string_view{name}, s.cats.size() - 1);
}

void AddBytes(const char* category, std::uint64_t bytes) noexcept
{
    if (category == nullptr) { return; }
    auto& s = GetState();
    auto it = s.nameToIndex.find(std::string_view{category});
    if (it == s.nameToIndex.end()) { return; }
    auto& c = s.cats[it->second];
    c.bytesCurrent += bytes;
    if (c.bytesCurrent > c.bytesHighWater)
    {
        c.bytesHighWater = c.bytesCurrent;
    }
    ++c.allocCount;
}

void SubBytes(const char* category, std::uint64_t bytes) noexcept
{
    if (category == nullptr) { return; }
    auto& s = GetState();
    auto it = s.nameToIndex.find(std::string_view{category});
    if (it == s.nameToIndex.end()) { return; }
    auto& c = s.cats[it->second];
    if (bytes >= c.bytesCurrent)
    {
        c.bytesCurrent = 0;  // 钳到 0 防止 double-free 类计数错误 underflow
    }
    else
    {
        c.bytesCurrent -= bytes;
    }
    ++c.freeCount;
}

std::span<const CategorySnapshot> Snapshot() noexcept
{
    const auto& s = GetState();
    return std::span<const CategorySnapshot>{s.cats};
}

std::size_t CategoryCount() noexcept
{
    return GetState().cats.size();
}

void Reset() noexcept
{
    auto& s = GetState();
    s.cats.clear();
    s.nameToIndex.clear();
}

}  // namespace Orange::Engine::Core::Memory
