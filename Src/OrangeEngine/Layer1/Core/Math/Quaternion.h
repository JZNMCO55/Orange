#ifndef ORANGE_ENGINE_MATH_QUATERNION_H
#define ORANGE_ENGINE_MATH_QUATERNION_H

#include <memory>
#include <string>
#include "Vector.h"
#include "Matrix.h"

namespace Orange
{
    namespace Math
    {
        /**
         * @class Quaternion
         * @brief 四元数类，用于表示3D旋转，内部使用GLM库实现
         *
         * 用于表示3D旋转的四元数类，提供与欧拉角和旋转矩阵之间的转换，
         * 以及平滑插值等常用四元数操作。
         */
        class Quaternion
        {
        public:
            // 构造函数
            Quaternion(); // 默认为单位四元数
            Quaternion(float x, float y, float z, float w);
            Quaternion(const Quaternion &other);
            Quaternion(Quaternion &&other) noexcept;

            // 从轴角构造四元数
            Quaternion(const Vector3 &axis, float angleRadians);

            ~Quaternion();

            // 赋值操作符
            Quaternion &operator=(const Quaternion &other);
            Quaternion &operator=(Quaternion &&other) noexcept;

            // 从欧拉角构造四元数 (弧度)
            static Quaternion FromEulerAngles(const Vector3 &eulerAngles);
            static Quaternion FromEulerAngles(float pitch, float yaw, float roll);

            // 从旋转矩阵构造四元数
            static Quaternion FromRotationMatrix(const Matrix3 &rotationMatrix);
            static Quaternion FromRotationMatrix(const Matrix4 &rotationMatrix);

            // 获取/设置分量
            float X() const;
            float Y() const;
            float Z() const;
            float W() const;
            void SetX(float x);
            void SetY(float y);
            void SetZ(float z);
            void SetW(float w);

            // 获取欧拉角表示 (弧度)
            Vector3 ToEulerAngles() const;

            // 获取旋转矩阵表示
            Matrix3 ToMatrix3() const;
            Matrix4 ToMatrix4() const;

            // 获取轴角表示
            void ToAxisAngle(Vector3 &outAxis, float &outAngleRadians) const;

            // 四元数操作
            float Length() const;
            float LengthSquared() const;
            Quaternion Normalized() const;
            void Normalize();
            Quaternion Conjugate() const;
            Quaternion Inverse() const;

            // 应用旋转到向量
            Vector3 RotateVector(const Vector3 &vec) const;

            // 球面插值
            static Quaternion Slerp(const Quaternion &a, const Quaternion &b, float t);

            // 线性插值
            static Quaternion Lerp(const Quaternion &a, const Quaternion &b, float t);

            // 操作符重载
            Quaternion operator*(const Quaternion &other) const;
            Vector3 operator*(const Vector3 &vec) const;
            Quaternion operator*(float scalar) const;
            Quaternion operator/(float scalar) const;
            Quaternion &operator*=(const Quaternion &other);
            Quaternion &operator*=(float scalar);
            Quaternion &operator/=(float scalar);

            // 比较操作符
            bool operator==(const Quaternion &other) const;
            bool operator!=(const Quaternion &other) const;

            // 常用四元数
            static Quaternion Identity();

            // 字符串转换
            std::string ToString() const;

        private:
            // 实现细节
            class Impl;
            std::unique_ptr<Impl> pImpl;

            // 友元类
            friend class Matrix3;
            friend class Matrix4;
        };

        // 非成员操作符重载
        Quaternion operator*(float scalar, const Quaternion &quat);
    }
}

#endif // ORANGE_ENGINE_MATH_QUATERNION_H