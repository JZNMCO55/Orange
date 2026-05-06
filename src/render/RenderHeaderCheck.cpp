// Render 公共头自包含性检查。每个公共头都被 include 一次、互相隔
// 离；任一头若不慎引入了 OrangeRender / Vulkan 的头（违反 CLAUDE.md
// "Header isolation" 不变量），构建会在这里立刻失败。新增
// `include/orange/engine/render/` 下的头文件时，记得追加一行 include。

#include "orange/engine/render/Camera.h"
#include "orange/engine/render/Pipeline.h"
#include "orange/engine/render/RenderableComponent.h"

namespace Orange::Engine::Render
{
namespace
{

[[maybe_unused]] inline constexpr int sRenderHeaderCheckSentinel = 0;

}  // namespace
}  // namespace Orange::Engine::Render
