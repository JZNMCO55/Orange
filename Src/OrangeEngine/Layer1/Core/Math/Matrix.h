#ifndef ORANGE_ENGINE_MATH_MATRIX_H
#define ORANGE_ENGINE_MATH_MATRIX_H

#include <memory>
#include <string>
#include "Vector.h"

namespace Orange
{
    namespace Math
    {
        // 前向声明
        class Quaternion;

        /**
         * @class Matrix4
         * @brief 4x4矩阵类，内部使用GLM库实现
         */
        class Matrix4
        {
        public:
            // 构造函数
            Matrix4();
            explicit Matrix4(float scalar);
            Matrix4(const Matrix4 &other);
            Matrix4(Matrix4 &&other) noexcept;
            ~Matrix4();

            // 赋值操作符
            Matrix4 &operator=(const Matrix4 &other);
            Matrix4 &operator=(Matrix4 &&other) noexcept;

            // 矩阵元素访问 (行列)
            float Get(int row, int col) const;
            void Set(int row, int col, float value);

            // 行和列访问
            Vector4 GetRow(int row) const;
            Vector4 GetColumn(int col) const;
            void SetRow(int row, const Vector4 &vector);
            void SetColumn(int col, const Vector4 &vector);

            // 矩阵变换
            Matrix4 Transpose() const;
            Matrix4 Inverse() const;
            float Determinant() const;

            // 操作符重载
            Matrix4 operator+(const Matrix4 &other) const;
            Matrix4 operator-(const Matrix4 &other) const;
            Matrix4 operator*(const Matrix4 &other) const;
            Vector4 operator*(const Vector4 &vec) const;
            Matrix4 operator*(float scalar) const;

            Matrix4 &operator+=(const Matrix4 &other);
            Matrix4 &operator-=(const Matrix4 &other);
            Matrix4 &operator*=(const Matrix4 &other);
            Matrix4 &operator*=(float scalar);

            // 比较操作符
            bool operator==(const Matrix4 &other) const;
            bool operator!=(const Matrix4 &other) const;

            // 创建变换矩阵
            static Matrix4 Identity();
            static Matrix4 Translation(const Vector3 &translation);
            static Matrix4 Translation(float x, float y, float z);
            static Matrix4 Rotation(const Vector3 &eulerAngles);
            static Matrix4 Rotation(float pitch, float yaw, float roll);
            static Matrix4 RotationX(float angleRadians);
            static Matrix4 RotationY(float angleRadians);
            static Matrix4 RotationZ(float angleRadians);
            static Matrix4 Scale(const Vector3 &scale);
            static Matrix4 Scale(float x, float y, float z);
            static Matrix4 FromQuaternion(const Quaternion &rotation);

            // 视图和投影矩阵
            static Matrix4 LookAt(const Vector3 &eye, const Vector3 &center, const Vector3 &up);
            static Matrix4 Perspective(float fov, float aspectRatio, float nearPlane, float farPlane);
            static Matrix4 Orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane);

            // 从矩阵获取变换
            Vector3 GetTranslation() const;
            Vector3 GetScale() const;
            Quaternion GetRotation() const;

            // 矩阵分解
            void Decompose(Vector3 &scale, Quaternion &rotation, Vector3 &translation) const;

            // 字符串转换
            std::string ToString() const;

        private:
            // 实现细节
            class Impl;
            std::unique_ptr<Impl> pImpl;

            // 友元类
            friend class Matrix3;
            friend class Quaternion;
        };
        /**
         * @class Matrix3
         * @brief 3x3矩阵类，内部使用GLM库实现
         */
        class Matrix3
        {
        public:
            // 构造函数
            Matrix3();
            explicit Matrix3(float scalar);
            Matrix3(const Matrix3 &other);
            Matrix3(Matrix3 &&other) noexcept;
            Matrix3(const Matrix4 &mat4); // 从4x4矩阵提取3x3部分
            ~Matrix3();

            // 赋值操作符
            Matrix3 &operator=(const Matrix3 &other);
            Matrix3 &operator=(Matrix3 &&other) noexcept;

            // 矩阵元素访问 (行列)
            float Get(int row, int col) const;
            void Set(int row, int col, float value);

            // 行和列访问
            Vector3 GetRow(int row) const;
            Vector3 GetColumn(int col) const;
            void SetRow(int row, const Vector3 &vector);
            void SetColumn(int col, const Vector3 &vector);

            // 矩阵变换
            Matrix3 Transpose() const;
            Matrix3 Inverse() const;
            float Determinant() const;

            // 操作符重载
            Matrix3 operator+(const Matrix3 &other) const;
            Matrix3 operator-(const Matrix3 &other) const;
            Matrix3 operator*(const Matrix3 &other) const;
            Vector3 operator*(const Vector3 &vec) const;
            Matrix3 operator*(float scalar) const;

            Matrix3 &operator+=(const Matrix3 &other);
            Matrix3 &operator-=(const Matrix3 &other);
            Matrix3 &operator*=(const Matrix3 &other);
            Matrix3 &operator*=(float scalar);

            // 比较操作符
            bool operator==(const Matrix3 &other) const;
            bool operator!=(const Matrix3 &other) const;

            // 创建变换矩阵
            static Matrix3 Identity();
            static Matrix3 Rotation(float angleRadians);
            static Matrix3 Scale(const Vector2 &scale);
            static Matrix3 Scale(float x, float y);
            static Matrix3 FromQuaternion(const Quaternion &rotation);

            // 字符串转换
            std::string ToString() const;

        private:
            // 实现细节
            class Impl;
            std::unique_ptr<Impl> pImpl;

            // 友元类
            friend class Matrix4;
            friend class Quaternion;
        };
        // 非成员操作符重载
        Matrix3 operator*(float scalar, const Matrix3 &mat);
        Matrix4 operator*(float scalar, const Matrix4 &mat);
    }
}

#endif // ORANGE_ENGINE_MATH_MATRIX_H