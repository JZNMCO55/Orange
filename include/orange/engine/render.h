#ifndef ORANGE_ENGINE_RENDER_H
#define ORANGE_ENGINE_RENDER_H

// Render 子系统便利聚合头：一次性引入 <orange/engine/render/*> 全部公共头。
// 维护：render/ 新增公共头时在此追加一行（check_invariants.py 的
// aggregator-completeness 规则会强制同步）。

#include <orange/engine/render/BuiltinMaterials.h>
#include <orange/engine/render/BuiltinPostProcessChain.h>
#include <orange/engine/render/BuiltinShadowShaders.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/DebugDrawScene.h>
#include <orange/engine/render/EnvironmentComponent.h>
#include <orange/engine/render/IAuxPassProvider.h>
#include <orange/engine/render/IblBaker.h>
#include <orange/engine/render/IPostProcessPass.h>
#include <orange/engine/render/IRenderPass.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/Material.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/MaterialTypes.h>
#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/PostProcessChain.h>
#include <orange/engine/render/PostProcessComponent.h>
#include <orange/engine/render/PostProcessPasses.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/render/RenderPassContext.h>
#include <orange/engine/render/RenderScene.h>
#include <orange/engine/render/ShadowConfig.h>
#include <orange/engine/render/SubMeshMaterialsComponent.h>
#include <orange/engine/render/VfxSystem.h>

#endif // ORANGE_ENGINE_RENDER_H
