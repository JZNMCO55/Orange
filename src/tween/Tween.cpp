#include <orange/engine/tween/Tween.h>

#include <cmath>

namespace Orange::Engine::Tween
{

    namespace
    {

        // 从补间描述 + 已过时间算当前值：把 elapsed 减去 delay 得 progress time，按 duration
        // 归一化成原始进度 raw，再按 loop 模式折算成 t∈[0,1]，最后 EaseLerp。file-local 纯
        // 函数（收 desc + elapsed 而非私有 State，保持 State 对外不可见）。
        float ComputeValue(const TweenDesc& desc, float elapsed)
        {
            const float pt = elapsed - desc.delay; // progress time（延迟后经过的时间）
            if (pt <= 0.0f)
            {
                // 仍在 delay 内：值恒为 from。
                return desc.from;
            }

            // duration 下限守卫：<=0 的 duration（瞬间完成）除会得 inf/nan，钳到极小正数。
            const float dur = desc.duration > 1e-8f ? desc.duration : 1e-8f;
            const float raw = pt / dur;

            float t = 0.0f;
            switch (desc.loop)
            {
                case LoopMode::Once:
                    // 单次：raw clamp 到 [0,1]，到端点即停在端点。
                    t = raw > 1.0f ? 1.0f : (raw < 0.0f ? 0.0f : raw);
                    break;

                case LoopMode::Loop:
                    // 循环：取小数部分回绕（raw=2.5 → t=0.5）。
                    t = raw - std::floor(raw);
                    break;

                case LoopMode::PingPong:
                {
                    // 往返：偶数周期正向、奇数周期反向（raw=1.5 → cyc=1 奇 → t=1-0.5=0.5）。
                    const float     cyc = std::floor(raw);
                    const float     ph  = raw - cyc;
                    const long long ci  = static_cast<long long>(cyc);
                    t                   = (ci % 2 == 0) ? ph : 1.0f - ph;
                    break;
                }
            }

            return EaseLerp(desc.ease, desc.from, desc.to, t);
        }

    } // namespace

    TweenHandle TweenManager::Add(const TweenDesc& desc)
    {
        const std::uint64_t id = mNextId++;
        mTweens[id]            = State{desc, 0.0f};
        return TweenHandle{id};
    }

    void TweenManager::Update(float dt)
    {
        // 快照当前所有 key 再遍历：回调里可能 Add（新增 key，Add 后本次不推进它）/ Kill
        // （删 key）导致 rehash / 迭代器失效，靠"迭代 id 快照 + 每步 find"抗再入。
        std::vector<std::uint64_t> ids;
        ids.reserve(mTweens.size());
        for (const auto& kv : mTweens)
        {
            ids.push_back(kv.first);
        }

        std::vector<std::uint64_t> toRemove;

        for (std::uint64_t id : ids)
        {
            auto it = mTweens.find(id);
            if (it == mTweens.end())
            {
                // 已被前一个回调 Kill。
                continue;
            }

            it->second.elapsed += dt;

            // 在调回调前从 it 算好一切：回调里 Add 可能触发 rehash 使 it 失效，故此后不碰 it。
            const float value = ComputeValue(it->second.desc, it->second.elapsed);
            // 完成判定与 ComputeValue 的 pt<=0 守卫对齐：要求 pt>0（progress time 真正开始）才允许
            // 完成。否则 ① duration<=0 的瞬间补间在恰 pt==0 帧会以 value=from 完成（应为 to）；
            // ② 负 duration 会在 delay 内（pt<0）就满足 pt>=duration 而提前完成。pt>0 时 ComputeValue
            // 对 duration<=0 已 raw→huge→t=1→to，故完成帧观测值恒为 to，语义一致。
            const float pt   = it->second.elapsed - it->second.desc.delay;
            const bool  done = (it->second.desc.loop == LoopMode::Once) && (pt > 0.0f) &&
                              (pt >= it->second.desc.duration);

            // 拷贝出 std::function：回调体里 Add 可能 rehash / 搬走 it 指向的 State，
            // 直接调 it->second.desc.onUpdate 会在回调执行期间访问已失效内存。
            auto onUpd = it->second.desc.onUpdate;
            auto onCmp = done ? it->second.desc.onComplete : std::function<void()>{};

            // 此后不再触碰 it（可能已失效）。
            if (onUpd)
            {
                onUpd(value);
            }
            if (onCmp)
            {
                onCmp();
            }
            if (done)
            {
                toRemove.push_back(id);
            }
        }

        for (std::uint64_t id : toRemove)
        {
            mTweens.erase(id);
        }
    }

    void TweenManager::Kill(TweenHandle handle)
    {
        mTweens.erase(handle.id);
    }

    bool TweenManager::IsActive(TweenHandle handle) const
    {
        return mTweens.find(handle.id) != mTweens.end();
    }

    float TweenManager::GetValue(TweenHandle handle) const
    {
        auto it = mTweens.find(handle.id);
        if (it == mTweens.end())
        {
            return 0.0f;
        }
        return ComputeValue(it->second.desc, it->second.elapsed);
    }

    std::size_t TweenManager::ActiveCount() const
    {
        return mTweens.size();
    }

    void TweenManager::Clear()
    {
        // mNextId 不重置：Clear 后新 Add 继续递增发号，绝不复用旧句柄。
        mTweens.clear();
    }

} // namespace Orange::Engine::Tween
