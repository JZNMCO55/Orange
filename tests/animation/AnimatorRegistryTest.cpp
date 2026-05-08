// AnimatorRegistryTest —— backend factory 注册器端到端验证。
//
// 覆盖：
//   * RegisterBackend 成功 → HasBackend / Create 返回非空 instance；
//   * 重名 → AlreadyExists，表内不被覆盖（Create 仍走第一次注册的 factory）；
//   * 空 factory → InvalidArgument；
//   * 未注册 name → Create 返回 nullptr；
//   * factory 自身返 nullptr → Create 透传 nullptr；
//   * 注册一个 echo backend（Tick 累计计数 / IsFinished 永假）—— Task 01
//     验收里要求"扩展点字面可用"的那条。

#include "orange/engine/animation/AnimatorRegistry.h"
#include "orange/engine/animation/IAnimator.h"
#include "orange/engine/core/Result.h"

#include <cassert>
#include <memory>
#include <string_view>

namespace Anim = Orange::Engine::Animation;
using Orange::Engine::ResultCode;

namespace
{

class EchoAnimator final : public Anim::IAnimator
{
public:
    void Tick(float dt) override { mElapsed += dt; ++mTickCount; }
    bool IsFinished() const noexcept override { return false; }
    std::string_view BackendName() const noexcept override { return "echo"; }

    int   TickCount() const noexcept { return mTickCount; }
    float Elapsed()   const noexcept { return mElapsed; }

private:
    int   mTickCount{0};
    float mElapsed{0.0f};
};

void TestRegisterAndCreate()
{
    Anim::AnimatorRegistry reg;
    assert(reg.BackendCount() == 0);

    auto rc = reg.RegisterBackend("echo",
                                  [] { return std::make_unique<EchoAnimator>(); });
    assert(rc.IsOk());
    assert(reg.HasBackend("echo"));
    assert(reg.BackendCount() == 1);

    auto instance = reg.Create("echo");
    assert(instance != nullptr);
    assert(instance->BackendName() == "echo");
    assert(!instance->IsFinished());

    // Tick 透传：echo 后端的 elapsed 走起来
    auto* echo = static_cast<EchoAnimator*>(instance.get());
    instance->Tick(0.5f);
    instance->Tick(0.25f);
    assert(echo->TickCount() == 2);
    assert(echo->Elapsed() > 0.74f && echo->Elapsed() < 0.76f);
}

void TestRegisterDuplicateRejected()
{
    Anim::AnimatorRegistry reg;
    int v1 = 0;
    int v2 = 0;
    reg.RegisterBackend("dup", [&] { v1 = 1; return std::unique_ptr<EchoAnimator>(new EchoAnimator); });
    auto rc = reg.RegisterBackend("dup", [&] { v2 = 1; return std::unique_ptr<EchoAnimator>(new EchoAnimator); });
    assert(rc.IsErr());
    assert(rc.Error() == ResultCode::AlreadyExists);
    // 表内仍是第一次的 factory
    auto inst = reg.Create("dup");
    assert(inst != nullptr);
    assert(v1 == 1);
    assert(v2 == 0);
}

void TestEmptyFactoryRejected()
{
    Anim::AnimatorRegistry reg;
    auto rc = reg.RegisterBackend("nope", {});
    assert(rc.IsErr());
    assert(rc.Error() == ResultCode::InvalidArgument);
    assert(reg.BackendCount() == 0);
}

void TestUnknownNameReturnsNull()
{
    Anim::AnimatorRegistry reg;
    assert(reg.Create("missing") == nullptr);
    assert(!reg.HasBackend("missing"));
}

void TestFactoryReturnsNull()
{
    Anim::AnimatorRegistry reg;
    reg.RegisterBackend("ghost", [] { return std::unique_ptr<EchoAnimator>(); });
    assert(reg.Create("ghost") == nullptr);
    assert(reg.HasBackend("ghost"));  // 注册过，仅 factory 主动失败
}

}  // namespace

int main()
{
    TestRegisterAndCreate();
    TestRegisterDuplicateRejected();
    TestEmptyFactoryRejected();
    TestUnknownNameReturnsNull();
    TestFactoryReturnsNull();
    return 0;
}
