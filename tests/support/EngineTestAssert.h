#ifndef ORANGE_ENGINE_TESTS_SUPPORT_ENGINE_TEST_ASSERT_H
#define ORANGE_ENGINE_TESTS_SUPPORT_ENGINE_TEST_ASSERT_H

// headless 状态断言 helper（header-only，tests 专用）。
//
// 业界自动化测试调研（2026-06-30）结论：白盒状态断言（直接读引擎内部状态做
// programmatic assert）是回归网的主力——比"看截图判断"更确定、更便宜、更快。
// 本 helper 把各 test 此前各自手搓的 FloatEq / view 计数 / 取组件等惯用法集中
// 成一处，供所有 headless 测试复用；EnTT 直读（World::Registry().view<T>()），
// 不绕 MCP/JSON。
//
// 全部返回 bool，兼容现存"裸 assert(cond && "msg")"风格（仓库约定不引第三方
// 测试框架）。放在 tests/ 下、只 include 引擎公共头（World/Entity）+ glm，故
// 不触 header-isolation invariant（lint 只扫 include/orange/engine/**）。

#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/World.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstddef>

namespace Orange::Engine::Test
{

    // ---- 标量 / 向量近似（返回 bool，直接喂 assert）------------------------------

    inline bool FloatEq(float a, float b, float eps = 1e-4f)
    {
        return std::fabs(a - b) <= eps;
    }

    inline bool InRange(float v, float lo, float hi)
    {
        return v >= lo && v <= hi;
    }

    inline bool Vec3Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
    {
        return FloatEq(a.x, b.x, eps) && FloatEq(a.y, b.y, eps) && FloatEq(a.z, b.z, eps);
    }

    inline bool Vec4Near(const glm::vec4& a, const glm::vec4& b, float eps = 1e-4f)
    {
        return FloatEq(a.x, b.x, eps) && FloatEq(a.y, b.y, eps) && FloatEq(a.z, b.z, eps) && FloatEq(a.w, b.w, eps);
    }

    // 四元数近似：含 ±q 同向（q 与 -q 表示同一旋转），故用 |dot| 接近 1 判定。
    inline bool QuatNear(const glm::quat& a, const glm::quat& b, float eps = 1e-4f)
    {
        const float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
        return std::fabs(d) >= 1.0f - eps;
    }

    // ---- EnTT 直读（World::Registry() 暴露底层 registry，见 World.h）-------------

    // 统计 world 里挂有组件 T 的实体数（集合大小语义）。
    template <class T>
    std::size_t CountComponents(World& world)
    {
        std::size_t n = 0;
        for (auto e : world.Registry().view<T>())
        {
            (void)e;
            ++n;
        }
        return n;
    }

    // entity 上是否挂有组件 T（存在性语义；包装 World::HasComponent 统一入口）。
    template <class T>
    bool HasComponent(World& world, Entity entity)
    {
        return world.HasComponent<T>(entity);
    }

    // "恰好一个"用例：取 world 里第一个挂 T 的实体的组件指针；无则 nullptr。
    template <class T>
    const T* FirstComponent(World& world)
    {
        auto& reg = world.Registry();
        for (auto e : reg.view<T>())
        {
            return &reg.get<T>(e);
        }
        return nullptr;
    }

    // 集合包含语义：world 中是否存在某个 T 满足谓词 pred(const T&)。
    template <class T, class Pred>
    bool AnyComponent(World& world, Pred pred)
    {
        auto& reg = world.Registry();
        for (auto e : reg.view<T>())
        {
            if (pred(reg.get<T>(e)))
            {
                return true;
            }
        }
        return false;
    }

} // namespace Orange::Engine::Test

#endif // ORANGE_ENGINE_TESTS_SUPPORT_ENGINE_TEST_ASSERT_H
