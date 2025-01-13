#ifndef ORANGEEXPORT_H
#define ORANGEEXPORT_H

#include <memory>
#ifdef ORANGE_STATIC
#define ORANGE_API
#else
#ifdef ORANGE_EXPORTS
#define ORANGE_API __declspec(dllexport)
#else
#define ORANGE_API __declspec(dllimport)
#endif // ORANGE_EXPORTS
#endif // ORANGE_STATIC
#define BIT(x) (1 << x)

#define ORANGE_BIND_EVENT_FN(fn) std::bind(&fn, this, std::placeholders::_1)

namespace Orange
{
    template<typename T>
    using Ref = std::shared_ptr<T>;
    template<typename T, typename ... Args>
    constexpr Ref<T> CreateRef(Args&& ... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    using Scope = std::unique_ptr<T>;
    template<typename T, typename ... Args>
    constexpr Scope<T> CreateScope(Args&& ... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }
}

#endif // ORANGEEXPORT_H