#ifndef ORANGE_EDITOR_EDITOR_GROUP_TRANSFORM_H
#define ORANGE_EDITOR_EDITOR_GROUP_TRANSFORM_H

// EditorGroupTransform —— 多选群组变换的纯数学基元（header-only inline，glm）。
//
// 抽出动机：多选 N 个实体一起变换时，gizmo 交互层（手感必 dogfood）与底层
// "每个实体新 transform 怎么算"两件事可拆开——后者是确定性数学，先单测锁住
// 正确性（gap 报告 §2.1/P1 "多选群组变换 pivot：数学可单测但手感必 dogfood"
// + §5 "先用单测把逻辑内核锁住，再 dogfood 交互层"）。
//
// 三种群组变换的 pivot 语义：
//   * translate —— pivot 无关：每个实体 position += 同一 delta（不需本文件，
//     调用方直接加）。
//   * rotate    —— 绕 pivot 旋转：position 绕 pivot 转，自身 rotation 左乘 q。
//   * scale     —— 绕 pivot 缩放：position 相对 pivot 缩放，自身 scale 乘 factor。
//
// 这里只提供"位置相对 pivot 怎么变"的基元 + 默认 pivot（质心）；实体自身的
// rotation/scale 复合由调用方按上面语义在 TransformComponent 上做。

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>

namespace Orange::Editor::Util
{

    // 一组点的算术质心 = 群组变换的默认 pivot。count == 0 → 返回原点（调用方
    // 通常此时不该触发群组变换；返回原点是安全兜底，不会 NaN）。
    inline glm::vec3 ComputeCentroid(const glm::vec3* points, std::size_t count)
    {
        if (points == nullptr || count == 0)
        {
            return glm::vec3(0.0f);
        }
        glm::vec3 sum(0.0f);
        for (std::size_t i = 0; i < count; ++i)
        {
            sum += points[i];
        }
        return sum / static_cast<float>(count);
    }

    // 点绕 pivot 按四元数 q 旋转后的新位置：pivot + q * (point - pivot)。
    // q 应为单位四元数（调用方负责归一化）。pivot == point → 原样返回。
    inline glm::vec3 RotateAroundPivot(const glm::vec3& point,
                                       const glm::vec3& pivot,
                                       const glm::quat& q)
    {
        return pivot + q * (point - pivot);
    }

    // 点绕 pivot 各轴独立缩放后的新位置：pivot + factor * (point - pivot)。
    // factor 为各轴比例（均匀缩放传 vec3(s)）。factor == 1 → 原样返回。
    inline glm::vec3 ScaleAroundPivot(const glm::vec3& point,
                                      const glm::vec3& pivot,
                                      const glm::vec3& factor)
    {
        return pivot + factor * (point - pivot);
    }

} // namespace Orange::Editor::Util

#endif // ORANGE_EDITOR_EDITOR_GROUP_TRANSFORM_H
