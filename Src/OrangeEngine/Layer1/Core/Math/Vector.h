#ifndef ORANGE_ENGINE_MATH_VECTOR_H
#define ORANGE_ENGINE_MATH_VECTOR_H

#include <memory>
#include <string>

namespace Orange
{
    namespace Math
    {
        // 前向声明
        class Vector3;
        class Vector4;

        /**
         * @class Vector2
         * @brief 2D向量类，内部使用GLM库实现
         */
        class Vector2
        {
        public:
            // 构造函数
            Vector2();
            Vector2(float x, float y);
            explicit Vector2(float scalar);
            Vector2(const Vector2 &other);
            Vector2(Vector2 &&other) noexcept;
            ~Vector2();

            // 赋值操作符
            Vector2 &operator=(const Vector2 &other);
            Vector2 &operator=(Vector2 &&other) noexcept;

            // 获取/设置分量
            float X() const;
            float Y() const;
            void SetX(float x);
            void SetY(float y);

            // 常用向量操作
            float Length() const;
            float LengthSquared() const;
            Vector2 Normalized() const;
            void Normalize();

            // 点积
            float Dot(const Vector2 &other) const;

            // 操作符重载
            Vector2 operator+(const Vector2 &other) const;
            Vector2 operator-(const Vector2 &other) const;
            Vector2 operator*(float scalar) const;
            Vector2 operator/(float scalar) const;
            Vector2 &operator+=(const Vector2 &other);
            Vector2 &operator-=(const Vector2 &other);
            Vector2 &operator*=(float scalar);
            Vector2 &operator/=(float scalar);

            // 比较操作符
            bool operator==(const Vector2 &other) const;
            bool operator!=(const Vector2 &other) const;

            // 常用向量
            static Vector2 Zero();
            static Vector2 One();
            static Vector2 UnitX();
            static Vector2 UnitY();

            // 附加向量操作
            Vector2 Reflect(const Vector2 &normal) const;
            Vector2 Lerp(const Vector2 &other, float t) const;
            float Distance(const Vector2 &other) const;
            float DistanceSquared(const Vector2 &other) const;

            // 字符串转换
            std::string ToString() const;

        private:
            // 实现细节
            class Impl;
            std::unique_ptr<Impl> pImpl;

            // 友元类
            friend class Vector3;
            friend class Vector4;
        };

        /**
         * @class Vector3
         * @brief 3D向量类，内部使用GLM库实现
         */
        class Vector3
        {
        public:
            // 构造函数
            Vector3();
            Vector3(float x, float y, float z);
            explicit Vector3(float scalar);
            Vector3(const Vector3 &other);
            Vector3(Vector3 &&other) noexcept;
            Vector3(const Vector2 &vec, float z);
            ~Vector3();

            // 赋值操作符
            Vector3 &operator=(const Vector3 &other);
            Vector3 &operator=(Vector3 &&other) noexcept;

            // 获取/设置分量
            float X() const;
            float Y() const;
            float Z() const;
            void SetX(float x);
            void SetY(float y);
            void SetZ(float z);
            Vector2 XY() const;

            // 常用向量操作
            float Length() const;
            float LengthSquared() const;
            Vector3 Normalized() const;
            void Normalize();

            // 点积和叉积
            float Dot(const Vector3 &other) const;
            Vector3 Cross(const Vector3 &other) const;

            // 操作符重载
            Vector3 operator+(const Vector3 &other) const;
            Vector3 operator-(const Vector3 &other) const;
            Vector3 operator*(float scalar) const;
            Vector3 operator/(float scalar) const;
            Vector3 &operator+=(const Vector3 &other);
            Vector3 &operator-=(const Vector3 &other);
            Vector3 &operator*=(float scalar);
            Vector3 &operator/=(float scalar);

            // 比较操作符
            bool operator==(const Vector3 &other) const;
            bool operator!=(const Vector3 &other) const;

            // 常用向量
            static Vector3 Zero();
            static Vector3 One();
            static Vector3 UnitX();
            static Vector3 UnitY();
            static Vector3 UnitZ();
            static Vector3 Up();
            static Vector3 Down();
            static Vector3 Right();
            static Vector3 Left();
            static Vector3 Forward();
            static Vector3 Backward();

            // 附加向量操作
            Vector3 Reflect(const Vector3 &normal) const;
            Vector3 Refract(const Vector3 &normal, float eta) const;
            Vector3 Project(const Vector3 &normal) const;
            Vector3 Lerp(const Vector3 &other, float t) const;
            float Distance(const Vector3 &other) const;
            float DistanceSquared(const Vector3 &other) const;

            // 字符串转换
            std::string ToString() const;

        private:
            // 实现细节
            class Impl;
            std::unique_ptr<Impl> pImpl;

            // 友元类
            friend class Vector4;
        };

        /**
         * @class Vector4
         * @brief 4D向量类，内部使用GLM库实现
         */
        class Vector4
        {
        public:
            // 构造函数
            Vector4();
            Vector4(float x, float y, float z, float w);
            explicit Vector4(float scalar);
            Vector4(const Vector4 &other);
            Vector4(Vector4 &&other) noexcept;
            Vector4(const Vector3 &vec, float w);
            ~Vector4();

            // 赋值操作符
            Vector4 &operator=(const Vector4 &other);
            Vector4 &operator=(Vector4 &&other) noexcept;

            // 获取/设置分量
            float X() const;
            float Y() const;
            float Z() const;
            float W() const;
            void SetX(float x);
            void SetY(float y);
            void SetZ(float z);
            void SetW(float w);
            Vector3 XYZ() const;

            // 常用向量操作
            float Length() const;
            float LengthSquared() const;
            Vector4 Normalized() const;
            void Normalize();

            // 点积
            float Dot(const Vector4 &other) const;

            // 操作符重载
            Vector4 operator+(const Vector4 &other) const;
            Vector4 operator-(const Vector4 &other) const;
            Vector4 operator*(float scalar) const;
            Vector4 operator/(float scalar) const;
            Vector4 &operator+=(const Vector4 &other);
            Vector4 &operator-=(const Vector4 &other);
            Vector4 &operator*=(float scalar);
            Vector4 &operator/=(float scalar);

            // 比较操作符
            bool operator==(const Vector4 &other) const;
            bool operator!=(const Vector4 &other) const;

            // 常用向量
            static Vector4 Zero();
            static Vector4 One();

            // 附加向量操作
            Vector4 Lerp(const Vector4 &other, float t) const;
            float Distance(const Vector4 &other) const;
            float DistanceSquared(const Vector4 &other) const;

            // 字符串转换
            std::string ToString() const;

        private:
            // 实现细节
            class Impl;
            std::unique_ptr<Impl> pImpl;
        };

        // 非成员操作符重载
        Vector2 operator*(float scalar, const Vector2 &vec);
        Vector3 operator*(float scalar, const Vector3 &vec);
        Vector4 operator*(float scalar, const Vector4 &vec);

        // 整数向量类型声明
        class Vector2i;
        class Vector3i;
        class Vector4i;
    }
}

#endif // ORANGE_ENGINE_MATH_VECTOR_H