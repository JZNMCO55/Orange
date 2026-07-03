#ifndef ORANGE_ENGINE_SCENE_TRANSFORM_COMPONENT_H
#define ORANGE_ENGINE_SCENE_TRANSFORM_COMPONENT_H

// ---------------------------------------------------------------------------
// TransformComponent —— 实体在世界空间的位姿。
//
// 单实体本地（local）TRS：position（世界单位米）+ rotation（单位四元数）
// + scale（每轴乘数）。世界变换矩阵的合成是 Render 模块在收集 drawable
// list 时的事情，不在 component 上缓存——避免双源真相、避免序列化
// schema 把派生量也写出来。
//
// 与 Hierarchy 的关系：HierarchyComponent 负责"谁是谁的父节点"，
// TransformComponent 只描述自身相对父的本地位姿。一个实体若没有
// HierarchyComponent，则其本地位姿即世界位姿。
//
// 旋转保存为四元数 (x, y, z, w) —— Euler 在边界处不连续、矩阵 3x3 太
// 重，quat 是 ECS-storage 层面最经济也最稳定的选择。详见
// vendor/Orange-Wiki/wiki/concepts/foundations/quaternions.md。
// ---------------------------------------------------------------------------

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace Orange::Engine::Scene
{

    struct TransformComponent
    {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // (w, x, y, z) —— glm 默认 (w,x,y,z) 构造序
        glm::vec3 scale{1.0f, 1.0f, 1.0f};
    };

} // namespace Orange::Engine::Scene

#endif // ORANGE_ENGINE_SCENE_TRANSFORM_COMPONENT_H
