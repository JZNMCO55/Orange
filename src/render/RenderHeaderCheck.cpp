// Render 公共头自包含性检查。每个公共头都被 include 一次、互相隔
// 离；任一头若不慎引入了 OrangeRender / Vulkan 的头（违反 CLAUDE.md
// "Header isolation" 不变量），构建会在这里立刻失败。新增
// `include/orange/engine/render/` 下的头文件时，记得追加一行 include。

#include "orange/engine/render/BuiltinMaterials.h"
#include "orange/engine/render/BuiltinPostProcessChain.h"
#include "orange/engine/render/BuiltinShadowShaders.h"
#include "orange/engine/render/Camera.h"
#include "orange/engine/render/IPostProcessPass.h"
#include "orange/engine/render/IRenderPass.h"
#include "orange/engine/render/LightComponent.h"
#include "orange/engine/render/Material.h"
#include "orange/engine/render/MaterialInstance.h"
#include "orange/engine/render/MaterialSystem.h"
#include "orange/engine/render/MaterialTypes.h"
#include "orange/engine/render/ParticleEmitterComponent.h"
#include "orange/engine/render/Pipeline.h"
#include "orange/engine/render/PostProcessChain.h"
#include "orange/engine/render/PostProcessPasses.h"
#include "orange/engine/render/RenderPassContext.h"
#include "orange/engine/render/RenderScene.h"
#include "orange/engine/render/RenderableComponent.h"
#include "orange/engine/render/ShadowConfig.h"
#include "orange/engine/render/VfxSystem.h"

namespace Orange::Engine::Render
{
    namespace
    {

        [[maybe_unused]] inline constexpr int sRenderHeaderCheckSentinel = 0;

    } // namespace
} // namespace Orange::Engine::Render
