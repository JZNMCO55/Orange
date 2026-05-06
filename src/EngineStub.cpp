// orange_engine 静态库的占位 TU。
//
// CMake 的 `add_library(<name> STATIC ...)` 至少需要一个源文件来触发
// archiver。当前引擎还在骨架阶段，这个 TU 仅是为了满足该约束；它会作
// 为低成本的锚点保留下来，真正的实现会按模块逐步落到各自的
// `src/<module>/` 目录。
//
// 不要把真实引擎代码放在这里。新子系统应在各自的 `src/<module>/`
// 目录下落地。

namespace Orange::Engine
{
namespace
{

[[maybe_unused]] inline constexpr int sEngineStubSentinel = 0;

}  // namespace
}  // namespace Orange::Engine
