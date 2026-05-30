#ifndef ORANGE_ENGINE_CORE_GUID_H
#define ORANGE_ENGINE_CORE_GUID_H

// ---------------------------------------------------------------------------
// Core::Guid —— 128-bit 稳定全局唯一标识。
//
// 用途：给逻辑实体一个**跨会话 / 跨文件 / 跨实例**恒定的身份标签，是 prefab、
// scene 引用、未来网络同步的共同地基（见 ADR-013）。与 `Entity` 句柄分工明确：
//   * Entity（64-bit handle，低 32 位 = EnTT entity）—— ECS 查询 key，运行时
//     有效，destroy+recycle 后复用，**不是稳定身份**。
//   * Guid（本类型）—— 持久身份标签，挂在 GuidComponent 上随场景序列化。
//
// 不遵循 RFC-4122 的 variant/version bits：我们不与外部 UUID 互操作，只需唯一。
// 纯随机 128-bit，引擎规模下碰撞概率可忽略（与 Hash.h 同款理由）。
//
// 全 0 视为 Invalid（未分配）；Generate() 保证产出 non-zero。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace Orange::Engine::Core
{

struct ORANGE_ENGINE_API Guid
{
    std::uint64_t high{0};
    std::uint64_t low{0};

    // 全 0 = 未分配（Invalid）。
    constexpr bool IsValid() const noexcept { return high != 0 || low != 0; }

    // 产出一个随机的 non-zero 128-bit GUID。线程安全（每线程独立 RNG）。
    static Guid Generate();

    // 32 个十六进制字符（high 16 + low 16，无分隔），与 FromString 互逆。
    std::string ToString() const;

    // 解析 ToString() 形态（恰好 32 个 hex 字符）。成功写入 out 并返回 true；
    // 长度 / 字符不合法返回 false（不改 out）。
    static bool FromString(std::string_view text, Guid& out);
};

constexpr bool operator==(const Guid& a, const Guid& b) noexcept
{
    return a.high == b.high && a.low == b.low;
}

constexpr bool operator!=(const Guid& a, const Guid& b) noexcept
{
    return !(a == b);
}

}  // namespace Orange::Engine::Core

#endif  // ORANGE_ENGINE_CORE_GUID_H
