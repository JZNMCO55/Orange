#include "Vector.h"
#include "Internal/GLMUtils.h"
#include <sstream>
#include <iomanip>

namespace Orange
{
    namespace Math
    {
        class Vector2::Impl
        {
        public:
            glm::vec2 vec;

            Impl() : vec(0.0f) {}
            Impl(float x, float y) : vec(x, y) {}
            explicit Impl(float scalar) : vec(scalar) {}
            Impl(const Impl &other) : vec(other.vec) {}
        };

        // 构造函数
        Vector2::Vector2() : pImpl(std::make_unique<Impl>()) {}

        Vector2::Vector2(float x, float y) : pImpl(std::make_unique<Impl>(x, y)) {}

        Vector2::Vector2(float scalar) : pImpl(std::make_unique<Impl>(scalar)) {}

        Vector2::Vector2(const Vector2 &other) : pImpl(std::make_unique<Impl>(*other.pImpl)) {}

        Vector2::Vector2(Vector2 &&other) noexcept = default;

        Vector2::~Vector2() = default;

        // 赋值操作符
        Vector2 &Vector2::operator=(const Vector2 &other)
        {
            if (this != &other)
            {
                pImpl->vec = other.pImpl->vec;
            }
            return *this;
        }

        Vector2 &Vector2::operator=(Vector2 &&other) noexcept = default;

        // 获取/设置分量
        float Vector2::X() const { return pImpl->vec.x; }
        float Vector2::Y() const { return pImpl->vec.y; }
        void Vector2::SetX(float x) { pImpl->vec.x = x; }
        void Vector2::SetY(float y) { pImpl->vec.y = y; }

        // 常用向量操作
        float Vector2::Length() const { return glm::length(pImpl->vec); }
        float Vector2::LengthSquared() const { return glm::length2(pImpl->vec); }

        Vector2 Vector2::Normalized() const
        {
            Vector2 result;
            result.pImpl->vec = glm::normalize(pImpl->vec);
            return result;
        }

        void Vector2::Normalize() { pImpl->vec = glm::normalize(pImpl->vec); }

        // 点积
        float Vector2::Dot(const Vector2 &other) const { return glm::dot(pImpl->vec, other.pImpl->vec); }

        // 操作符重载
        Vector2 Vector2::operator+(const Vector2 &other) const
        {
            Vector2 result;
            result.pImpl->vec = pImpl->vec + other.pImpl->vec;
            return result;
        }

        Vector2 Vector2::operator-(const Vector2 &other) const
        {
            Vector2 result;
            result.pImpl->vec = pImpl->vec - other.pImpl->vec;
            return result;
        }

        Vector2 Vector2::operator*(float scalar) const
        {
            Vector2 result;
            result.pImpl->vec = pImpl->vec * scalar;
            return result;
        }

        Vector2 Vector2::operator/(float scalar) const
        {
            Vector2 result;
            result.pImpl->vec = pImpl->vec / scalar;
            return result;
        }

        Vector2 &Vector2::operator+=(const Vector2 &other)
        {
            pImpl->vec += other.pImpl->vec;
            return *this;
        }

        Vector2 &Vector2::operator-=(const Vector2 &other)
        {
            pImpl->vec -= other.pImpl->vec;
            return *this;
        }

        Vector2 &Vector2::operator*=(float scalar)
        {
            pImpl->vec *= scalar;
            return *this;
        }

        Vector2 &Vector2::operator/=(float scalar)
        {
            pImpl->vec /= scalar;
            return *this;
        }

        // 比较操作符
        bool Vector2::operator==(const Vector2 &other) const { return pImpl->vec == other.pImpl->vec; }
        bool Vector2::operator!=(const Vector2 &other) const { return pImpl->vec != other.pImpl->vec; }

        // 常用向量
        Vector2 Vector2::Zero() { return Vector2(0.0f, 0.0f); }
        Vector2 Vector2::One() { return Vector2(1.0f, 1.0f); }
        Vector2 Vector2::UnitX() { return Vector2(1.0f, 0.0f); }
        Vector2 Vector2::UnitY() { return Vector2(0.0f, 1.0f); }

        // 附加向量操作
        Vector2 Vector2::Reflect(const Vector2 &normal) const
        {
            Vector2 result;
            result.pImpl->vec = glm::reflect(pImpl->vec, normal.pImpl->vec);
            return result;
        }

        Vector2 Vector2::Lerp(const Vector2 &other, float t) const
        {
            Vector2 result;
            result.pImpl->vec = glm::mix(pImpl->vec, other.pImpl->vec, t);
            return result;
        }

        float Vector2::Distance(const Vector2 &other) const
        {
            return glm::distance(pImpl->vec, other.pImpl->vec);
        }

        float Vector2::DistanceSquared(const Vector2 &other) const
        {
            return glm::distance2(pImpl->vec, other.pImpl->vec);
        }

        // 字符串转换
        std::string Vector2::ToString() const
        {
            std::stringstream ss;
            ss << std::fixed << std::setprecision(2);
            ss << "Vector2(" << pImpl->vec.x << ", " << pImpl->vec.y << ")";
            return ss.str();
        }

        // 非成员操作符重载
        Vector2 operator*(float scalar, const Vector2 &vec)
        {
            return vec * scalar;
        }
    }
}