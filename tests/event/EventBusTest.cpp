// EventBusTest —— 类型化事件总线 (typed event bus) headless 验收。裸 <cassert> +
// 独立 main()，进程 exit 0 = pass。
//
// 子测试：
//   * 基本发布订阅：Subscribe<Foo> → Publish<Foo>{42} → handler 收到 42
//   * 多处理器 + 顺序：3 个 handler 全被调、按订阅顺序
//   * 类型隔离：Publish<Foo> 只触发 Foo handler 不触发 Bar
//   * 无订阅者：Publish 无 handler → no-op 不崩
//   * Unsubscribe：退订后不再收到 + 无效/重复退订 no-op + HandlerCount 反映增减
//   * Enqueue + DispatchQueued：入队不立即派发、DispatchQueued 按 FIFO 全派发
//   * 再入安全：handler 内 Subscribe/Unsubscribe/Publish<Bar> 不崩（快照迭代），
//     新订阅的 handler 下次 Publish 才生效
//   * Clear：Clear 后 HandlerCount==0 / Publish no-op / 排队事件被清

#include "orange/engine/event/EventBus.h"

#include <cassert>
#include <cstdio>
#include <type_traits>
#include <vector>

namespace Ev = Orange::Engine::Event;

// EventBus 不可拷贝 / 不可移动（延迟队列 thunk 捕获 this，值语义搬运会让 thunk 绑定
// 原对象 → 错误派发 / 悬垂 this UAF；对抗式复核逮到）。编译期锁死此契约。
static_assert(!std::is_copy_constructible<Ev::EventBus>::value, "EventBus 不可拷贝");
static_assert(!std::is_copy_assignable<Ev::EventBus>::value, "EventBus 不可拷贝赋值");
static_assert(!std::is_move_constructible<Ev::EventBus>::value, "EventBus 不可移动");
static_assert(!std::is_move_assignable<Ev::EventBus>::value, "EventBus 不可移动赋值");

namespace
{

    // ---- 测试事件类型 --------------------------------------------------------

    struct FooEvent
    {
        int value;
    };

    struct BarEvent
    {
        float x;
    };

    // ---- 基本发布订阅 --------------------------------------------------------

    void TestBasicPublishSubscribe()
    {
        Ev::EventBus           bus;
        int                    received = -1;
        Ev::SubscriptionHandle h        = bus.Subscribe<FooEvent>(
            [&received](const FooEvent& e)
            { received = e.value; });
        assert(h.IsValid());
        assert(bus.HandlerCount<FooEvent>() == 1);

        bus.Publish<FooEvent>(FooEvent{42});
        assert(received == 42);

        std::printf("[ok] basic publish/subscribe\n");
    }

    // ---- 多处理器 + 订阅顺序 -------------------------------------------------

    void TestMultipleHandlersOrder()
    {
        Ev::EventBus     bus;
        std::vector<int> order;
        bus.Subscribe<FooEvent>([&order](const FooEvent&)
                                { order.push_back(1); });
        bus.Subscribe<FooEvent>([&order](const FooEvent&)
                                { order.push_back(2); });
        bus.Subscribe<FooEvent>([&order](const FooEvent&)
                                { order.push_back(3); });
        assert(bus.HandlerCount<FooEvent>() == 3);

        bus.Publish<FooEvent>(FooEvent{0});
        // 全被调、按订阅顺序。
        assert(order.size() == 3);
        assert(order[0] == 1 && order[1] == 2 && order[2] == 3);

        std::printf("[ok] multiple handlers + order\n");
    }

    // ---- 类型隔离 ------------------------------------------------------------

    void TestTypeIsolation()
    {
        Ev::EventBus bus;
        int          fooHits = 0;
        int          barHits = 0;
        bus.Subscribe<FooEvent>([&fooHits](const FooEvent&)
                                { ++fooHits; });
        bus.Subscribe<BarEvent>([&barHits](const BarEvent&)
                                { ++barHits; });

        bus.Publish<FooEvent>(FooEvent{7});
        // Foo 触发一次、Bar 完全不动。
        assert(fooHits == 1);
        assert(barHits == 0);

        bus.Publish<BarEvent>(BarEvent{1.5f});
        assert(fooHits == 1);
        assert(barHits == 1);

        std::printf("[ok] type isolation\n");
    }

    // ---- 无订阅者 no-op ------------------------------------------------------

    void TestNoSubscribers()
    {
        Ev::EventBus bus;
        // 无任何订阅者：Publish 应安全 no-op 不崩。
        assert(bus.HandlerCount<FooEvent>() == 0);
        bus.Publish<FooEvent>(FooEvent{123});
        assert(bus.HandlerCount<FooEvent>() == 0);

        std::printf("[ok] publish with no subscribers is no-op\n");
    }

    // ---- Unsubscribe ---------------------------------------------------------

    void TestUnsubscribe()
    {
        Ev::EventBus           bus;
        int                    hits = 0;
        Ev::SubscriptionHandle h =
            bus.Subscribe<FooEvent>([&hits](const FooEvent&)
                                    { ++hits; });
        assert(bus.HandlerCount<FooEvent>() == 1);

        bus.Publish<FooEvent>(FooEvent{0});
        assert(hits == 1);

        bus.Unsubscribe(h);
        assert(bus.HandlerCount<FooEvent>() == 0);
        bus.Publish<FooEvent>(FooEvent{0});
        assert(hits == 1); // 退订后不再收到

        // 无效句柄 no-op。
        bus.Unsubscribe(Ev::SubscriptionHandle{});
        // 重复退订（已退订的句柄）no-op。
        bus.Unsubscribe(h);
        assert(bus.HandlerCount<FooEvent>() == 0);

        // 多订阅者下退订中间那个，其余仍在 + 顺序不乱。
        std::vector<int>       order;
        Ev::SubscriptionHandle a =
            bus.Subscribe<FooEvent>([&order](const FooEvent&)
                                    { order.push_back(1); });
        Ev::SubscriptionHandle b =
            bus.Subscribe<FooEvent>([&order](const FooEvent&)
                                    { order.push_back(2); });
        Ev::SubscriptionHandle c =
            bus.Subscribe<FooEvent>([&order](const FooEvent&)
                                    { order.push_back(3); });
        assert(bus.HandlerCount<FooEvent>() == 3);
        (void)a;
        (void)c;
        bus.Unsubscribe(b);
        assert(bus.HandlerCount<FooEvent>() == 2);
        bus.Publish<FooEvent>(FooEvent{0});
        assert(order.size() == 2);
        assert(order[0] == 1 && order[1] == 3);

        std::printf("[ok] unsubscribe (+ invalid/double no-op, count reflects)\n");
    }

    // ---- Enqueue + DispatchQueued（延迟 FIFO）--------------------------------

    void TestEnqueueDispatchQueued()
    {
        Ev::EventBus     bus;
        std::vector<int> received;
        bus.Subscribe<FooEvent>(
            [&received](const FooEvent& e)
            { received.push_back(e.value); });

        bus.Enqueue<FooEvent>(FooEvent{10});
        bus.Enqueue<FooEvent>(FooEvent{20});
        bus.Enqueue<FooEvent>(FooEvent{30});
        // Enqueue 不立即派发。
        assert(received.empty());

        bus.DispatchQueued();
        // 按 FIFO 全派发。
        assert(received.size() == 3);
        assert(received[0] == 10 && received[1] == 20 && received[2] == 30);

        // 空队列 DispatchQueued no-op。
        received.clear();
        bus.DispatchQueued();
        assert(received.empty());

        std::printf("[ok] enqueue + dispatch queued (FIFO)\n");
    }

    // ---- DispatchQueued 期间再 Enqueue 留到下次 ------------------------------

    void TestEnqueueDuringDispatchDefers()
    {
        Ev::EventBus     bus;
        std::vector<int> received;
        // 处理 value==1 的事件时再 Enqueue 一个 value==2：后者应留到下一次
        // DispatchQueued（swap-out 本批语义，避免本轮无限循环）。
        bus.Subscribe<FooEvent>(
            [&bus, &received](const FooEvent& e)
            {
                received.push_back(e.value);
                if (e.value == 1)
                {
                    bus.Enqueue<FooEvent>(FooEvent{2});
                }
            });

        bus.Enqueue<FooEvent>(FooEvent{1});
        bus.DispatchQueued();
        // 本轮只处理最初入队的 1；派发中新入队的 2 未在本轮处理。
        assert(received.size() == 1);
        assert(received[0] == 1);

        bus.DispatchQueued();
        // 下一轮处理 2。
        assert(received.size() == 2);
        assert(received[1] == 2);

        std::printf("[ok] enqueue during dispatch defers to next round\n");
    }

    // ---- 再入安全（快照迭代）------------------------------------------------

    void TestReentrantSafety()
    {
        Ev::EventBus bus;
        int          fooA    = 0;
        int          fooB    = 0;
        int          barHits = 0;

        bus.Subscribe<BarEvent>([&barHits](const BarEvent&)
                                { ++barHits; });

        // 首个 Foo handler：回调里同时 Subscribe<Foo> 新 handler + Unsubscribe 自己
        // + Publish<Bar>。本次 Publish<Foo> 必须不崩（快照迭代抗 mHandlers 变更），
        // 且新订阅的 handler 本轮不生效。
        Ev::SubscriptionHandle self;
        self = bus.Subscribe<FooEvent>(
            [&](const FooEvent&)
            {
                ++fooA;
                bus.Subscribe<FooEvent>([&fooB](const FooEvent&)
                                        { ++fooB; });
                bus.Unsubscribe(self);
                bus.Publish<BarEvent>(BarEvent{0.0f});
            });

        bus.Publish<FooEvent>(FooEvent{0});
        // 首个 handler 跑了一次；回调里的 Publish<Bar> 触发了 Bar handler。
        assert(fooA == 1);
        assert(barHits == 1);
        // 新订阅的 handler 本轮不生效（快照只含订阅当刻已存在的处理器）。
        assert(fooB == 0);
        // self 已退订；新 handler 仍在 → 计数 == 1。
        assert(bus.HandlerCount<FooEvent>() == 1);

        // 下一次 Publish<Foo>：只有新 handler 生效（self 已退订不再触发）。
        bus.Publish<FooEvent>(FooEvent{0});
        assert(fooA == 1); // 未再增（self 退订）
        assert(fooB == 1); // 新 handler 本轮生效

        std::printf("[ok] re-entrant safety (snapshot iteration)\n");
    }

    // ---- Clear ---------------------------------------------------------------

    void TestClear()
    {
        Ev::EventBus bus;
        int          hits = 0;
        bus.Subscribe<FooEvent>([&hits](const FooEvent&)
                                { ++hits; });
        bus.Subscribe<BarEvent>([&hits](const BarEvent&)
                                { ++hits; });
        bus.Enqueue<FooEvent>(FooEvent{1});
        assert(bus.HandlerCount<FooEvent>() == 1);
        assert(bus.HandlerCount<BarEvent>() == 1);

        bus.Clear();
        // 订阅全清。
        assert(bus.HandlerCount<FooEvent>() == 0);
        assert(bus.HandlerCount<BarEvent>() == 0);
        // Publish no-op。
        bus.Publish<FooEvent>(FooEvent{9});
        assert(hits == 0);
        // 排队事件被清：DispatchQueued 不再派发之前入队的 Foo。
        bus.DispatchQueued();
        assert(hits == 0);

        // Clear 后 mNextId 不复位：新句柄仍全局唯一有效。
        Ev::SubscriptionHandle h =
            bus.Subscribe<FooEvent>([&hits](const FooEvent&)
                                    { ++hits; });
        assert(h.IsValid());
        bus.Publish<FooEvent>(FooEvent{0});
        assert(hits == 1);

        std::printf("[ok] clear\n");
    }

} // namespace

int main()
{
    TestBasicPublishSubscribe();
    TestMultipleHandlersOrder();
    TestTypeIsolation();
    TestNoSubscribers();
    TestUnsubscribe();
    TestEnqueueDispatchQueued();
    TestEnqueueDuringDispatchDefers();
    TestReentrantSafety();
    TestClear();

    std::printf("EventBusTest: all subtests passed\n");
    return 0;
}
