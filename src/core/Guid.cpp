// Core::Guid 实现。生成走每线程独立的 mt19937_64（random_device 播种）；
// 字符串往返用 32 hex 紧凑形态。

#include "orange/engine/core/Guid.h"

#include <charconv>
#include <cstdio>
#include <random>

namespace Orange::Engine::Core
{

Guid Guid::Generate()
{
    // 每线程一份 RNG，避免跨线程竞争同时省掉锁；random_device 播种保证不同
    // 进程 / 线程序列各异。
    static thread_local std::mt19937_64 sRng{std::random_device{}()};

    Guid g;
    do
    {
        g.high = sRng();
        g.low  = sRng();
    } while (!g.IsValid());  // 全 0（概率 ~2^-128）会撞 Invalid，重抽

    return g;
}

std::string Guid::ToString() const
{
    char buf[33];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                  static_cast<unsigned long long>(high),
                  static_cast<unsigned long long>(low));
    return std::string(buf, 32);
}

bool Guid::FromString(std::string_view text, Guid& out)
{
    if (text.size() != 32)
    {
        return false;
    }

    const auto parseHex = [](std::string_view part, std::uint64_t& value) -> bool
    {
        const char* begin = part.data();
        const char* end   = part.data() + part.size();
        auto [ptr, ec]    = std::from_chars(begin, end, value, 16);
        return ec == std::errc{} && ptr == end;
    };

    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
    if (!parseHex(text.substr(0, 16), hi) || !parseHex(text.substr(16, 16), lo))
    {
        return false;
    }

    out.high = hi;
    out.low  = lo;
    return true;
}

}  // namespace Orange::Engine::Core
