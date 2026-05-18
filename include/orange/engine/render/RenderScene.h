#ifndef ORANGE_ENGINE_RENDER_RENDER_SCENE_H
#define ORANGE_ENGINE_RENDER_RENDER_SCENE_H

// ---------------------------------------------------------------------------
// RenderScene —— 一帧的中间表示：
//   * 主相机 view + projection；
//   * 所有可见 Renderable 的 (worldMatrix, mesh, texture) 组成的扁平
//     drawable 列表。
//
// 流程：Pipeline 在每帧顶部调用 Collect(World)，扫一遍 EnTT view 把
// 上述两类信息从 ECS 数据形态转成"已合成 worldMatrix 的 drawable"形
// 态，让真实下发逻辑（OrangeRender RenderGraph）只关心
// 几何 / 纹理 / 矩阵，不再触碰 World / Component。
//
// 公共面刻意不暴露 entt 类型——本类的"内部"角色明确，但放在 public
// 头里是为了让 tests 与未来的 Material / PostProcess 子系统能直接观
// 察到 drawable 列表。Pipeline.cpp 内部使用本类作为 working buffer。
//
// **本阶段范围**：drawable 的 worldMatrix 直接来自 TransformComponent
// 的 local TRS 合成；Hierarchy 父子链的复合矩阵留待后续（关卡级
// 实体多于 100 时再上）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/render/Camera.h>

#include <glm/mat4x4.hpp>

#include <cstddef>
#include <vector>

namespace Orange::Engine
{
class World;
}

namespace Orange::Engine::Scene
{
class WorldPartition;
}

namespace Orange::Engine::Render
{

class MaterialInstance;

struct Drawable
{
    glm::mat4                            worldMatrix{1.0f};
    Asset::AssetHandle<Asset::MeshAsset> mesh{};
    // 非拥有 MaterialInstance 指针，由 RenderableComponent 透传。Pipeline
    // 后续按本字段路由 per-template Pipeline 缓存；nullptr 走 fallback。
    MaterialInstance*                    materialInstance{nullptr};
    // 由 RenderableComponent.castsShadow 透传：false 时 Pipeline shadow
    // pass 跳过本 drawable，主 pass 仍正常绘制。
    bool                                 castsShadow{true};
};

class ORANGE_ENGINE_API RenderScene
{
public:
    RenderScene() = default;

    // 重置内部状态：drawable 列表清空、相机标记为未设置。Pipeline 在
    // 每帧 Collect 之前先 Clear 一次，避免跨帧残留。
    void Clear() noexcept;

    // 扫 `world`，把第一个挂 Camera 组件的实体作为本帧主相机；并把
    // 所有同时具备 TransformComponent + RenderableComponent 且
    // visible=true 的实体翻成 Drawable 推入列表。`world` 当中没有
    // Camera 时 HasCamera() 为 false——调用方应据此决定是否仍然下发
    // 渲染。
    //
    // `partition` 非空时再加一层 "layer.visible" 过滤——对每个 drawable
    // 候选 entity 查 `partition->IsEntityVisible(world, e)`，false 即跳
    // 过。partition 空 → 不参与 layer 过滤（行为退化为旧版本）。
    void Collect(const ::Orange::Engine::World& world,
                 const ::Orange::Engine::Scene::WorldPartition* partition = nullptr);

    // 主相机访问。仅在 HasCamera() == true 时调用 MainCamera()，否
    // 则返回值未定义（默认构造的 Camera）。
    bool          HasCamera()  const noexcept { return mHasCamera; }
    const Camera& MainCamera() const noexcept { return mCamera; }

    // 覆写 main camera（编辑器 viewport 路径用）：Collect 之后调用，把
    // mCamera 替换成编辑器轨道相机的 view/projection；HasCamera 顺便置
    // true。这是 GAP-2026-05-15-camera-editor-vs-runtime-separation 落地
    // 的"engine-side EditorCameraContext"——编辑器无需再 mutate ECS 内的
    // Render::Camera 组件，Frustum gizmo 等 plugin 读 ECS Camera 取到的
    // 是游戏侧原始数据。
    void OverrideMainCamera(const Camera& cam) noexcept
    {
        mCamera    = cam;
        mHasCamera = true;
    }

    const std::vector<Drawable>& Drawables() const noexcept { return mDrawables; }
    std::size_t                  DrawableCount() const noexcept { return mDrawables.size(); }
    bool                         Empty() const noexcept { return mDrawables.empty(); }

private:
    Camera                mCamera{};
    bool                  mHasCamera{false};
    std::vector<Drawable> mDrawables;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_RENDER_SCENE_H
