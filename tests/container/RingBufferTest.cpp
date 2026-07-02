// RingBuffer<T> 的 headless 单元测试：固定容量环形缓冲（最近 N 项历史）。
// 纯数据结构、确定性，断言全为精确值。裸 main() + <cassert>。
//
// 覆盖点：
//   * 初始空 + 容量。
//   * Push 未满 —— Size 递增 + 最新优先下标 ([0]=最新) + Newest/Oldest。
//   * Push 满 —— Full + 覆盖最旧（★环形覆盖后下标与 Newest/Oldest 仍正确）。
//   * Clear —— 归空保留容量。
//   * 容量 0 退化 —— Push no-op、恒空。
// 仅 orange_engine + 标准库，headless 完全可验。

#include "orange/engine/container/RingBuffer.h"

#include <cassert>
#include <cstdio>

using ::Orange::Engine::Container::RingBuffer;

namespace
{
int gChecks = 0;
void Check(bool cond, const char* what)
{
    ++gChecks;
    if (!cond)
    {
        std::fprintf(stderr, "[RingBufferTest] FAILED: %s\n", what);
        assert(cond);
    }
}
} // namespace

int main()
{
    // —— 初始 + 未满 Push ——
    {
        RingBuffer<int> rb(3);
        Check(rb.Capacity() == 3 && rb.Empty() && rb.Size() == 0, "初始：容量3 / 空");
        Check(!rb.Full(), "初始：未满");

        rb.Push(10);
        rb.Push(20);
        Check(rb.Size() == 2 && !rb.Full(), "Push 2：size2 未满");
        Check(rb[0] == 20 && rb[1] == 10, "最新优先：[0]=最新20 / [1]=旧10");
        Check(rb.Newest() == 20 && rb.Oldest() == 10, "Newest/Oldest 正确");

        rb.Push(30);
        Check(rb.Full() && rb.Size() == 3, "Push 满：full size3");
        Check(rb[0] == 30 && rb[1] == 20 && rb[2] == 10, "满：[0]=30 [1]=20 [2]=10");
    }

    // —— 满后覆盖最旧（环形绕回）——
    {
        RingBuffer<int> rb(3);
        rb.Push(1);
        rb.Push(2);
        rb.Push(3); // 满：[3,2,1]
        rb.Push(4); // 覆盖最旧 1 → [4,3,2]
        Check(rb.Size() == 3 && rb.Full(), "覆盖后仍满 size3");
        Check(rb[0] == 4 && rb[1] == 3 && rb[2] == 2, "覆盖：[0]=4 [1]=3 [2]=2（旧1被挤出）");
        Check(rb.Newest() == 4 && rb.Oldest() == 2, "覆盖后 Newest=4 / Oldest=2");

        rb.Push(5); // → [5,4,3]
        rb.Push(6); // → [6,5,4]
        Check(rb[0] == 6 && rb[1] == 5 && rb[2] == 4, "连续覆盖绕回下标正确");
        Check(rb.Oldest() == 4, "连续覆盖后 Oldest=4");
    }

    // —— Clear ——
    {
        RingBuffer<int> rb(4);
        rb.Push(7);
        rb.Push(8);
        rb.Clear();
        Check(rb.Empty() && rb.Size() == 0 && rb.Capacity() == 4, "Clear：归空保留容量");
        rb.Push(9);
        Check(rb.Size() == 1 && rb.Newest() == 9, "Clear 后可再用");
    }

    // —— 容量 0 退化 ——
    {
        RingBuffer<int> rb; // 默认容量 0
        Check(rb.Capacity() == 0 && rb.Empty(), "默认：容量0 空");
        rb.Push(1);
        Check(rb.Empty() && rb.Size() == 0, "容量0：Push no-op、恒空");
    }

    std::printf("[RingBufferTest] all %d checks passed\n", gChecks);
    return 0;
}
