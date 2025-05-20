#include "Vector.h"
#include "Internal/GLMUtils.h"
#include <sstream>
#include <iomanip>

namespace Orange
{
    namespace Math
    {
        class Vector3::Impl
        {
        public:
            glm::vec3 vec;

            Impl() : vec(0.0f) {}
            Impl(float x, float y, float z) : vec(x, y, z) {}
            explicit Impl(float scalar) : vec(scalar) {}
            Impl(const Impl &other) : vec(other.vec) {}
        };

        // 构造函数
        Vector3::Vector3() : pImpl(std::make_unique<Impl>()) {}

        Vector3::Vector3(float x, float y, float z) : pImpl(std::make_unique<Impl>(x, y, z)) {}

        Vector3::Vector3(float scalar) : pImpl(std::make_unique<Impl>(scalar)) {}

        Vector3::Vector3(const Vector3 &other) : pImpl(std::make_unique<Impl>(*other.pImpl)) {}

        Vector3::Vector3(Vector3 &&other) noexcept = default;

        Vector3::Vector3(const Vector2 &vec, float z) : pImpl(std::make_unique<Impl>())
        {
            pImpl->vec = glm::vec3(vec.X(), vec.Y(), z);
        }

        Vector3::~Vector3() = default;

        // 赋值操作符
        Vector3 &Vector3::operator=(const Vector3 &other)
        {
            if (this != &other)
            {
                pImpl->vec = other.pImpl->vec;
            }
            return *this;
        }

        Vector3 &Vector3::operator=(Vector3 &&other) noexcept = default;

        // 获取/设置分量
        float Vector3::X() const { return pImpl->vec.x; }
        float Vector3::Y() const { return pImpl->vec.y; }
        float Vector3::Z() const { return pImpl->vec.z; }
        void Vector3::SetX(float x) { pImpl->vec.x = x; }
        void Vector3::SetY(float y) { pImpl->vec.y = y; }
        void Vector3::SetZ(float z) { pImpl->vec.z = z; }

        Vector2 Vector3::XY() const
        {
            return Vector2(pImpl->vec.x, pImpl->vec.y);
        }

        // 常用向量操作
        float Vector3::Length() const { return glm::length(pImpl->vec); }
        float Vector3::LengthSquared() const { return glm::length2(pImpl->vec); }

        Vector3 Vector3::Normalized() const
        {
            Vector3 result;
            result.pImpl->vec = glm::normalize(pImpl->vec);
            return result;
        }

        void Vector3::Normalize() { pImpl->vec = glm::normalize(pImpl->vec); }

        // 点积和叉积
        float Vector3::Dot(const Vector3 &other) const { return glm::dot(pImpl->vec, other.pImpl->vec); }

        Vector3 Vector3::Cross(const Vector3 &other) const
        {
            Vector3 result;
            result.pImpl->vec = glm::cross(pImpl->vec, other.pImpl->vec);
            return result;
        }

        // 操作符重载
        Vector3 Vector3::operator+(const Vector3 &other) const
        {
            Vector3 result;
            result.pImpl->vec = pImpl->vec + other.pImpl->vec;
            return result;
        }

        Vector3 Vector3::operator-(const Vector3 &other) const
        {
            Vector3 result;
            result.pImpl->vec = pImpl->vec - other.pImpl->vec;
            return result;
        }

        Vector3 Vector3::operator*(float scalar) const
        {
            Vector3 result;
            result.pImpl->vec = pImpl->vec * scalar;
            return result;
        }

        Vector3 Vector3::operator/(float scalar) const
        {
            Vector3 result;
            result.pImpl->vec = pImpl->vec / scalar;
            return result;
        }

        Vector3 &Vector3::operator+=(const Vector3 &other)
        {
            pImpl->vec += other.pImpl->vec;
            return *this;
        }

        Vector3 &Vector3::operator-=(const Vector3 &other)
        {
            pImpl->vec -= other.pImpl->vec;
            return *this;
        }

        Vector3 &Vector3::operator*=(float scalar)
        {
            pImpl->vec *= scalar;
            return *this;
        }

        Vector3 &Vector3::operator/=(float scalar)
        {
            pImpl->vec /= scalar;
            return *this;
        }

        // 比较操作符
        bool Vector3::operator==(const Vector3 &other) const { return pImpl->vec == other.pImpl->vec; }
        bool Vector3::operator!=(const Vector3 &other) const { return pImpl->vec != other.pImpl->vec; }

        // 常用向量
        Vector3 Vector3::Zero() { return Vector3(0.0f, 0.0f, 0.0f); }
        Vector3 Vector3::One() { return Vector3(1.0f, 1.0f, 1.0f); }
        Vector3 Vector3::UnitX() { return Vector3(1.0f, 0.0f, 0.0f); }
        Vector3 Vector3::UnitY() { return Vector3(0.0f, 1.0f, 0.0f); }
        Vector3 Vector3::UnitZ() { return Vector3(0.0f, 0.0f, 1.0f); }
        Vector3 Vector3::Up() { return Vector3(0.0f, 1.0f, 0.0f); }
        Vector3 Vector3::Down() { return Vector3(0.0f, -1.0f, 0.0f); }
        Vector3 Vector3::Right() { return Vector3(1.0f, 0.0f, 0.0f); }
        Vector3 Vector3::Left() { return Vector3(-1.0f, 0.0f, 0.0f); }
        Vector3 Vector3::Forward() { return Vector3(0.0f, 0.0f, 1.0f); }
        Vector3 Vector3::Backward() { return Vector3(0.0f, 0.0f, -1.0f); }

        // 附加向量操作
        Vector3 Vector3::Reflect(const Vector3 &normal) const
        {
            Vector3 result;
            result.pImpl->vec = glm::reflect(pImpl->vec, normal.pImpl->vec);
            return result;
        }

        Vector3 Vector3::Refract(const Vector3 &normal, float eta) const
        {
            Vector3 result;
            result.pImpl->vec = glm::refract(pImpl->vec, normal.pImpl->vec, eta);
            return result;
        }

        Vector3 Vector3::Project(const Vector3 &normal) const
        {
            Vector3 result;
            float dotProduct = glm::dot(pImpl->vec, normal.pImpl->vec);
            float normalLengthSq = glm::dot(normal.pImpl->vec, normal.pImpl->vec);
            if (normalLengthSq > 0.0f)
            {
                result.pImpl->vec = normal.pImpl->vec * (dotProduct / normalLengthSq);
            }
            else
            {
                result.pImpl->vec = glm::vec3(0.0f);
            }
            return result;
        }

        Vector3 Vector3::Lerp(const Vector3 &other, float t) const
        {
            Vector3 result;
            result.pImpl->vec = glm::mix(pImpl->vec, other.pImpl->vec, t);
            return result;
        }

        float Vector3::Distance(const Vector3 &other) const
        {
            return glm::distance(pImpl->vec, other.pImpl->vec);
        }

        float Vector3::DistanceSquared(const Vector3 &other) const
        {
            return glm::distance2(pImpl->vec, other.pImpl->vec);
        }

        // 字符串转换
        std::string Vector3::ToString() const
        {
            std::stringstream ss;
            ss << std::fixed << std::setprecision(2);
            ss << "Vector3(" << pImpl->vec.x << ", " << pImpl->vec.y << ", " << pImpl->vec.z << ")";
            return ss.str();
        }

        // 非成员操作符重载
        Vector3 operator*(float scalar, const Vector3 &vec)
        {
            return vec * scalar;
        }
    }
}