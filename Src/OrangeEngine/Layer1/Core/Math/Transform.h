#ifndef ORANGE_ENGINE_MATH_TRANSFORM_H
#define ORANGE_ENGINE_MATH_TRANSFORM_H

#include <memory>
#include "Vector.h"
#include "Matrix.h"
#include "Quaternion.h"

namespace Orange
{
    namespace Math
    {
        /**
         * @class Transform
         * @brief 表示3D空间中的变换（平移、旋转、缩放）
         *
         * 提供了一种简单的方式来处理和组合3D变换，可以从位置、旋转和缩放创建，
         * 或从一个变换矩阵中提取这些信息。
         */
        class Transform
        {
        public:
            // 构造函数
            Transform();
            Transform(const Vector3 &position);
            Transform(const Vector3 &position, const Quaternion &rotation);
            Transform(const Vector3 &position, const Quaternion &rotation, const Vector3 &scale);
            Transform(const Matrix4 &transformMatrix);
            Transform(const Transform &other);
            Transform(Transform &&other) noexcept;
            ~Transform();

            // 赋值操作符
            Transform &operator=(const Transform &other);
            Transform &operator=(Transform &&other) noexcept;

            // 获取/设置位置
            Vector3 GetPosition() const;
            void SetPosition(const Vector3 &position);

            // 获取/设置旋转
            Quaternion GetRotation() const;
            void SetRotation(const Quaternion &rotation);

            // 获取/设置缩放
            Vector3 GetScale() const;
            void SetScale(const Vector3 &scale);
            void SetScale(float uniformScale);

            // 获取变换矩阵
            Matrix4 GetMatrix() const;

            // 从变换矩阵更新
            void SetFromMatrix(const Matrix4 &matrix);

            // 移动变换
            void Translate(const Vector3 &translation);
            void Translate(float x, float y, float z);

            // 旋转变换
            void Rotate(const Quaternion &rotation);
            void Rotate(const Vector3 &eulerAngles);
            void Rotate(const Vector3 &axis, float angleRadians);
            void Rotate(float yaw, float pitch, float roll);

            // 缩放变换
            void Scale(const Vector3 &scale);
            void Scale(float x, float y, float z);
            void Scale(float uniformScale);

            // 变换向量和点
            Vector3 TransformVector(const Vector3 &vector) const;
            Vector3 TransformPoint(const Vector3 &point) const;

            // 在局部空间和世界空间之间转换
            Vector3 TransformDirection(const Vector3 &localDirection) const;
            Vector3 InverseTransformDirection(const Vector3 &worldDirection) const;
            Vector3 InverseTransformPoint(const Vector3 &worldPoint) const;
            Vector3 InverseTransformVector(const Vector3 &worldVector) const;

            // 从一个空间到另一个空间的变换
            static Vector3 TransformPoint(const Transform &transform, const Vector3 &point);
            static Vector3 TransformVector(const Transform &transform, const Vector3 &vector);
            static Vector3 InverseTransformPoint(const Transform &transform, const Vector3 &worldPoint);
            static Vector3 InverseTransformVector(const Transform &transform, const Vector3 &worldVector);

            // 插值
            static Transform Lerp(const Transform &a, const Transform &b, float t);

            // 变换组合
            Transform operator*(const Transform &other) const;
            Transform &operator*=(const Transform &other);

            // 一些有用的静态变换
            static Transform Identity();

        private:
            // 实现细节
            class Impl;
            std::unique_ptr<Impl> pImpl;
        };
    }
}

#endif // ORANGE_ENGINE_MATH_TRANSFORM_H