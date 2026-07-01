#ifndef ORANGE_ENGINE_EVENT_EVENT_BUS_H
#define ORANGE_ENGINE_EVENT_EVENT_BUS_H

// ---------------------------------------------------------------------------
// EventBus —— 类型化事件总线 (typed event bus / pub-sub)。解耦通信的基础引擎
// 服务：发布方 Publish 一个事件、订阅方按事件类型订阅处理器，二者互不知晓
// （对标 Godot signals / Unreal delegates / Unity events）。
//
// 设计：type erasure（类型擦除）—— 内部按 std::type_index 存 std::function 的
// void 指针 thunk，Subscribe/Publish 的模板层负责把强类型 E 编译期擦成 const
// void*，派发时再 static_cast 回 const E*。故公共面完全泛型，实现无需为每个
// 事件类型生成新代码。
//
// 派发两种模式：Publish 立即同步派发；Enqueue + DispatchQueued 延迟到帧边界
// 统一 FIFO 派发（用于"派发中不宜改世界"的场景）。派发对再入 (re-entrant) 安全
// ——处理器回调里可以 Subscribe / Unsubscribe / Publish，靠快照 (snapshot) 迭代。
//
// 线程不安全：为单线程游戏循环设计，不做任何锁。公共头只依赖标准库
// （type_index / typeid 是标准 RTTI，不是反射库）。
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Orange::Engine::Event
{

// 订阅句柄 (subscription handle)：Subscribe 返回、Unsubscribe 用。id==0 表示无效
// （Subscribe 从 1 开始发号，0 永久保留给无效句柄）。
struct SubscriptionHandle
{
    std::uint64_t id{0};

    bool IsValid() const noexcept
    {
        return id != 0;
    }
};

// 类型化事件总线：按事件类型 E 订阅处理器、发布事件立即派发或入队延迟派发。
// 解耦发布方与订阅方——发布方不需知道谁在监听。线程不安全（单线程游戏循环用）。
class EventBus
{
public:
    EventBus() = default;

    // 不可拷贝 / 不可移动：延迟队列 (mQueue) 里的 thunk 捕获了 this，一旦 bus 被拷贝 /
    // 移动，thunk 仍绑定原对象 → 错误派发 / 悬垂 this (UAF)。且事件总线是单例式引擎
    // 服务，本不该被值语义搬运——用引用 / 指针 / unique_ptr 持有。
    EventBus(const EventBus&)            = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&)                 = delete;
    EventBus& operator=(EventBus&&)      = delete;

    // 订阅 E 类型事件的处理器（按订阅顺序被调用）。返回句柄供 Unsubscribe。
    template <typename E>
    SubscriptionHandle Subscribe(std::function<void(const E&)> handler)
    {
        const std::uint64_t id = mNextId++;
        // 把强类型 handler 包成 const void* thunk：派发期 DispatchToType 只持有
        // void 指针，thunk 内部 static_cast 回 const E*（类型擦除的还原点）。
        std::function<void(const void*)> thunk =
            [h = std::move(handler)](const void* p)
            {
                h(*static_cast<const E*>(p));
            };
        const std::type_index type(typeid(E));
        mHandlers[type].push_back(HandlerEntry{id, std::move(thunk)});
        mIdToType.emplace(id, type);
        return SubscriptionHandle{id};
    }

    // 立即把 event 派发给所有 E 订阅者（同步，按订阅顺序）。无订阅者 → no-op。
    template <typename E>
    void Publish(const E& event)
    {
        DispatchToType(std::type_index(typeid(E)), &event);
    }

    // 把 E 事件（值拷贝）入队，延迟到 DispatchQueued 时按 FIFO 派发。用于"派发中
    // 不宜改世界 / 想在帧边界统一处理"的场景。
    // **契约**：入队事件按值**浅拷贝**——含指针 / 引用成员的事件，其指向对象的生命
    // 周期必须覆盖到 DispatchQueued，否则派发时悬垂 (dangling)。优先用自包含值事件。
    template <typename E>
    void Enqueue(const E& event)
    {
        // 捕获 event 值拷贝：lambda 里 E 类型已知，DispatchQueued 时再走 Publish<E>
        // 的正常类型化派发（thunk 会 static_cast 回 const E*）。
        mQueue.push_back([this, event]() { this->Publish<E>(event); });
    }

    // 派发当前排队的所有事件（FIFO）。派发期间新 Enqueue 的事件留到下次
    // DispatchQueued（不在本次循环内处理，避免无限循环）。
    void DispatchQueued()
    {
        // 先 swap 出本批：派发中新 Enqueue 进的是新的空 mQueue，本次不处理它们。
        std::vector<std::function<void()>> batch;
        batch.swap(mQueue);
        for (auto& thunk : batch)
        {
            thunk();
        }
    }

    // 退订。无效 / 已退订句柄 → no-op。
    void Unsubscribe(SubscriptionHandle handle)
    {
        auto mit = mIdToType.find(handle.id);
        if (mit == mIdToType.end())
        {
            return;
        }
        auto lit = mHandlers.find(mit->second);
        if (lit != mHandlers.end())
        {
            // id 全局唯一，至多一条匹配：手动线性查找 + erase（保序），命中即
            // break。刻意不用 std::remove_if 以免多引一个 <algorithm> 头。
            auto& list = lit->second;
            for (auto i = list.begin(); i != list.end(); ++i)
            {
                if (i->id == handle.id)
                {
                    list.erase(i);
                    break;
                }
            }
        }
        mIdToType.erase(mit);
    }

    // 某事件类型 E 当前订阅者数（诊断 / 测试）。
    template <typename E>
    std::size_t HandlerCount() const
    {
        // 用 find 别用 operator[]：const 方法不能 operator[]（会插入），且不该
        // 因查询而给 mHandlers 插入空列表。
        auto it = mHandlers.find(std::type_index(typeid(E)));
        return it == mHandlers.end() ? std::size_t{0} : it->second.size();
    }

    // 清空所有订阅 + 排队事件。mNextId 不重置（继续递增，绝不复用旧句柄）。
    void Clear() noexcept
    {
        mHandlers.clear();
        mIdToType.clear();
        mQueue.clear();
    }

private:
    struct HandlerEntry
    {
        std::uint64_t                    id;
        std::function<void(const void*)> fn;  // 类型擦除：内部 static_cast 回 const E*
    };

    // 非模板派发助手：按 type 找处理器列表、快照 (snapshot) 迭代调用（抗再入）。
    void DispatchToType(std::type_index type, const void* eventPtr)
    {
        auto it = mHandlers.find(type);
        if (it == mHandlers.end())
        {
            return;
        }
        // 先把该 type 的 fn 快照拷进本地 vector 再遍历调用：处理器在回调里
        // Subscribe/Unsubscribe（改 mHandlers 导致底层 vector realloc / 迭代器失效）
        // 也安全——本次派发只面向订阅当刻已存在的那批处理器。
        std::vector<std::function<void(const void*)>> snapshot;
        snapshot.reserve(it->second.size());
        for (const HandlerEntry& entry : it->second)
        {
            snapshot.push_back(entry.fn);
        }
        for (auto& fn : snapshot)
        {
            fn(eventPtr);
        }
    }

    std::unordered_map<std::type_index, std::vector<HandlerEntry>> mHandlers;
    std::unordered_map<std::uint64_t, std::type_index>            mIdToType;   // id→type，Unsubscribe 用
    std::vector<std::function<void()>>                            mQueue;      // 延迟事件
    std::uint64_t                                                 mNextId{1};  // 0 保留给无效句柄
};

}  // namespace Orange::Engine::Event

#endif  // ORANGE_ENGINE_EVENT_EVENT_BUS_H
