// App 公共头自包含性检查。每个公共头都被 include 一次、互相隔离；
// 任一头若不慎引入了多余依赖、长出隐式 include、或自身不再可独立编
// 译，构建会在这里失败，而不是延后到下游消费者那一刻。每新增一个
// `include/orange/engine/app/` 下的头文件，记得在这里追加一行 include。

#include "orange/engine/app/AppConfig.h"
#include "orange/engine/app/FrameContext.h"
#include "orange/engine/app/Layer.h"
#include "orange/engine/app/LayerStack.h"

namespace Orange::Engine
{
namespace
{

[[maybe_unused]] inline constexpr int sAppHeaderCheckSentinel = 0;

}  // namespace
}  // namespace Orange::Engine
