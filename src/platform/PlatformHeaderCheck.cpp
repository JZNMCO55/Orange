// Platform 公共头自包含性检查。
//
// 所有 Platform 公共头都在这个单独 TU 中被 include，与任何其他引擎
// 代码隔离。镜像 Core 的同名 TU。任一公共头若泄漏 GLFW 类型、不能
// 自包含、或长出对兄弟私有头的依赖，编译都会在这里失败，而不是在下
// 游消费者处才暴露。

#include "orange/engine/platform/Window.h"
#include "orange/engine/platform/WindowEvent.h"

namespace Orange::Engine::Platform
{
    namespace
    {

        [[maybe_unused]] inline constexpr int sPlatformHeaderCheckSentinel = 0;

    } // namespace
} // namespace Orange::Engine::Platform
