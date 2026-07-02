#ifndef ORANGE_ENGINE_CONTAINER_RING_BUFFER_H
#define ORANGE_ENGINE_CONTAINER_RING_BUFFER_H

// ---------------------------------------------------------------------------
// RingBuffer<T> —— 固定容量环形缓冲 (最近 N 项历史)，header-only 模板。
//
// Push 追加最新项，满则覆盖最旧项。下标以"最新优先"寻址：[0]=最新、[Size()-1]=最旧。
// 用于输入历史 / 跳跃缓冲 / 拖尾轨迹 (主角最近 N 帧位置画残影) / 滑动窗口统计等。
//
// 纯数据结构，确定性、无 glm 依赖 (只标准库)，headless 完全可测。容量 0 时 Push no-op、
// 恒空 (退化安全)。非线程安全 (每消费者各持一个)。
// ---------------------------------------------------------------------------

#include <cstddef>
#include <vector>

namespace Orange::Engine::Container
{

// 固定容量环形缓冲。满时 Push 覆盖最旧项；下标最新优先 ([0]=最新)。
template <typename T>
class RingBuffer
{
public:
    RingBuffer() = default;
    explicit RingBuffer(std::size_t capacity) { Reserve(capacity); }

    // 设容量并清空 (重新分配)。
    void Reserve(std::size_t capacity)
    {
        mBuffer.assign(capacity, T{});
        mHead = 0;
        mSize = 0;
    }

    std::size_t Capacity() const noexcept { return mBuffer.size(); }
    std::size_t Size() const noexcept { return mSize; }
    bool        Empty() const noexcept { return mSize == 0; }
    bool        Full() const noexcept { return !mBuffer.empty() && mSize == mBuffer.size(); }

    // 清空 (保留容量)。
    void Clear() noexcept
    {
        mHead = 0;
        mSize = 0;
    }

    // 追加最新项。满则覆盖最旧。容量 0 → no-op。
    void Push(const T& value)
    {
        if (mBuffer.empty())
        {
            return;
        }
        mBuffer[mHead] = value;
        mHead = (mHead + 1) % mBuffer.size();
        if (mSize < mBuffer.size())
        {
            ++mSize;
        }
    }

    // 最新优先寻址：i=0 最新、i=Size()-1 最旧。i 须 < Size() (调用者保证)。
    const T& operator[](std::size_t i) const
    {
        const std::size_t cap = mBuffer.size();
        // 最新项在 (mHead-1)；往回数 i 个。加 cap 防无符号回绕。
        const std::size_t idx = (mHead + cap - 1 - i) % cap;
        return mBuffer[idx];
    }

    const T& Newest() const { return (*this)[0]; }
    const T& Oldest() const { return (*this)[mSize - 1]; }

private:
    std::vector<T> mBuffer;      // 底层存储 (容量固定)
    std::size_t    mHead = 0;    // 下一个写入位置
    std::size_t    mSize = 0;    // 当前有效项数 (<=容量)
};

} // namespace Orange::Engine::Container

#endif // ORANGE_ENGINE_CONTAINER_RING_BUFFER_H
