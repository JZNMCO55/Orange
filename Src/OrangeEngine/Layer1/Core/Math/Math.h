#ifndef ORANGE_ENGINE_MATH_H
#define ORANGE_ENGINE_MATH_H

/**
 * @file Math.h
 * @brief Orange引擎数学库的统一头文件
 *
 * 这个文件包含了Orange引擎数学库的所有组件。数学库基于GLM（OpenGL Mathematics）库进行封装，
 * 提供了向量、矩阵、四元数等数学类型和操作，用于图形和游戏开发。
 */

// 基本数学类型
#include "Vector.h"
#include "Matrix.h"
#include "Quaternion.h"

// 图形数学工具
#include "Transform.h"
#include "Geometry.h"

namespace Orange
{
    /**
     * @namespace Math
     * @brief 包含所有数学库函数和类型的命名空间
     */
    namespace Math
    {
        // 类型别名 - 简化常用类型名称
        using Vec2 = Vector2;
        using Vec3 = Vector3;
        using Vec4 = Vector4;
        using Mat4 = Matrix4;
        using Quat = Quaternion;

        // 数学常量
        constexpr float PI = 3.14159265358979323846f;
        constexpr float TWO_PI = 6.28318530717958647692f;
        constexpr float HALF_PI = 1.57079632679489661923f;
        constexpr float QUARTER_PI = 0.78539816339744830962f;
        constexpr float E = 2.71828182845904523536f;

        // 角度与弧度转换
        constexpr float DegToRad(float degrees) { return degrees * (PI / 180.0f); }
        constexpr float RadToDeg(float radians) { return radians * (180.0f / PI); }

        // 通用数学函数
        template <typename T>
        constexpr T Clamp(T value, T min, T max) { return (value < min) ? min : ((value > max) ? max : value); }

        template <typename T>
        constexpr T Lerp(T a, T b, float t) { return a + t * (b - a); }

        // 向量操作函数（直接使用别名类型的方法）
        inline float Length(const Vec3 &v) { return v.Length(); }
        inline Vec3 Normalize(const Vec3 &v) { return v.Normalized(); }
        inline float Dot(const Vec3 &a, const Vec3 &b) { return a.Dot(b); }
        inline Vec3 Cross(const Vec3 &a, const Vec3 &b) { return a.Cross(b); }

        // 矩阵操作函数（使用Matrix4的静态方法）
        inline Mat4 LookAt(const Vec3 &eye, const Vec3 &center, const Vec3 &up)
        {
            return Matrix4::LookAt(eye, center, up);
        }
        inline Mat4 Perspective(float fovy, float aspect, float near, float far)
        {
            return Matrix4::Perspective(fovy, aspect, near, far);
        }
        inline Mat4 Ortho(float left, float right, float bottom, float top, float near, float far)
        {
            return Matrix4::Orthographic(left, right, bottom, top, near, far);
        }
        inline Mat4 Inverse(const Mat4 &m) { return m.Inverse(); }

        // 其他通用数学工具函数...
    }
}

#endif // ORANGE_ENGINE_MATH_H