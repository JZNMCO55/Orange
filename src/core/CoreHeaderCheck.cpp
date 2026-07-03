// Core 公共头自包含性检查。
//
// 所有 Core 公共头都在这个单独 TU 中被 include，与任何其他引擎代码
// 隔离。任何一个头若缺 include、依赖兄弟头泄漏的声明、或不慎引用了
// 私有标识符，都会在这里编译失败，而不是在下游消费者处才暴露。
//
// 本 TU 没有任何运行时逻辑；下面的 sentinel 仅用于让产生的目标文件
// 非空，链接器不会丢掉它。每新增一个 `include/orange/engine/core/`
// 下的头文件，记得在这里追加一行 include。

#include "orange/engine/core/Config.h"
#include "orange/engine/core/Hash.h"
#include "orange/engine/core/Handle.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/core/Result.h"
#include "orange/engine/core/SchemaVersion.h"
#include "orange/engine/core/Serialization.h"
#include "orange/engine/core/Time.h"

namespace Orange::Engine::Core
{
    namespace
    {

        [[maybe_unused]] inline constexpr int sCoreHeaderCheckSentinel = 0;

    } // namespace
} // namespace Orange::Engine::Core
