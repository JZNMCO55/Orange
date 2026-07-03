#ifndef ORANGE_ENGINE_RENDER_SUB_MESH_MATERIALS_COMPONENT_H
#define ORANGE_ENGINE_RENDER_SUB_MESH_MATERIALS_COMPONENT_H

// ---------------------------------------------------------------------------
// SubMeshMaterialsComponent —— 与 RenderableComponent 配对的可选组件，
// 给"单 mesh 多 material"实体提供 slot → material 的映射。
//
// slots[i] 对应 mesh 各 sub-mesh 的 materialSlot：渲染端遍历 MeshAsset
// 的 SubMeshes()，每段用 slots[subMesh.materialSlot] 选材质，越界 / 空
// 指针则回退到 RenderableComponent.materialInstance（slot 0 / 默认兜底）。
//
// 设计取舍：RenderableComponent 必须保持 trivially-copyable（EnTT
// archetype 整行迁移），不能内嵌 std::vector。因此把"多 material slot"
// 拆成独立的 side component，只挂给真正需要多材质的实体；单材质实体
// 不挂本组件，渲染端走整 mesh 单 material 路径，零额外成本。
//
// 生命周期：slots 里是**非拥有** MaterialInstance 指针，约束与
// RenderableComponent.materialInstance 完全一致——MaterialInstance 由
// sample / 游戏代码侧持有，必须活到 World 析构之后；典型做法是先析构
// World，再析构 instance 容器。
//
// 头隔离：本头是 public 头，只前置声明 MaterialInstance（不 include 其
// 定义，与 RenderableComponent.h / RenderScene.h 一致），不触碰任何
// 第三方渲染头。
// ---------------------------------------------------------------------------

#include <vector>

namespace Orange::Engine::Render
{

    class MaterialInstance;

    struct SubMeshMaterialsComponent
    {
        // 下标 = mesh sub-mesh 的 materialSlot；元素 = 该 slot 用的 material
        // 实例（非拥有指针，可为 nullptr 表示该 slot 回退默认材质）。
        std::vector<MaterialInstance*> slots;
    };

} // namespace Orange::Engine::Render

#endif // ORANGE_ENGINE_RENDER_SUB_MESH_MATERIALS_COMPONENT_H
