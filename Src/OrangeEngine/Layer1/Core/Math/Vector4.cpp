#include "Vector.h"
#include "Internal/GLMUtils.h"
#include <sstream>
#include <iomanip>

namespace Orange
{
    namespace Math
    {
        class Vector4::Impl
        {
        public:
            glm::vec4 vec;

            Impl() : vec(0.0f) {}
            Impl(float x, float y, float z, float w) : vec(x, y, z, w) {}
            explicit Impl(float scalar) : vec(scalar) {}
            Impl(const Impl &other) : vec(other.vec) {}
        };

        // 构造函数
        Vector4::Vector4() : pImpl(std::make_unique<Impl>()) {}

        Vector4::Vector4(float x, float y, float z, float w) : pImpl(std::make_unique<Impl>(x, y, z, w)) {}

        Vector4::Vector4(float scalar) : pImpl(std::make_unique<Impl>(scalar)) {}

        Vector4::Vector4(const Vector4 &other) : pImpl(std::make_unique<Impl>(*other.pImpl)) {}

        Vector4::Vector4(Vector4 &&other) noexcept = default;

        Vector4::Vector4(const Vector3 &vec, float w) : pImpl(std::make_unique<Impl>())
        {
            pImpl->vec = glm::vec4(vec.X(), vec.Y(), vec.Z(), w);
        }

        Vector4::~Vector4() = default;

        // 赋值操作符
        Vector4 &Vector4::operator=(const Vector4 &other)
        {
            if (this != &other)
            {
                pImpl->vec = other.pImpl->vec;
            }
            return *this;
        }

        Vector4 &Vector4::operator=(Vector4 &&other) noexcept = default;

        // 获取/设置分量
        float Vector4::X() const { return pImpl->vec.x; }
        float Vector4::Y() const { return pImpl->vec.y; }
        float Vector4::Z() const { return pImpl->vec.z; }
        float Vector4::W() const { return pImpl->vec.w; }
        void Vector4::SetX(float x) { pImpl->vec.x = x; }
        void Vector4::SetY(float y) { pImpl->vec.y = y; }
        void Vector4::SetZ(float z) { pImpl->vec.z = z; }
        void Vector4::SetW(float w) { pImpl->vec.w = w; }

        Vector3 Vector4::XYZ() const
        {
            return Vector3(pImpl->vec.x, pImpl->vec.y, pImpl->vec.z);
        }

        // 常用向量操作
        float Vector4::Length() const { return glm::length(pImpl->vec); }
        float Vector4::LengthSquared() const { return glm::length2(pImpl->vec); }

        Vector4 Vector4::Normalized() const
        {
            Vector4 result;
            result.pImpl->vec = glm::normalize(pImpl->vec);
            return result;
        }

        void Vector4::Normalize() { pImpl->vec = glm::normalize(pImpl->vec); }

        // 点积
        float Vector4::Dot(const Vector4 &other) const { return glm::dot(pImpl->vec, other.pImpl->vec); }

        // 操作符重载
        Vector4 Vector4::operator+(const Vector4 &other) const
        {
            Vector4 result;
            result.pImpl->vec = pImpl->vec + other.pImpl->vec;
            return result;
        }

        Vector4 Vector4::operator-(const Vector4 &other) const
        {
            Vector4 result;
            result.pImpl->vec = pImpl->vec - other.pImpl->vec;
            return result;
        }

        Vector4 Vector4::operator*(float scalar) const
        {
            Vector4 result;
            result.pImpl->vec = pImpl->vec * scalar;
            return result;
        }

        Vector4 Vector4::operator/(float scalar) const
        {
            Vector4 result;
            result.pImpl->vec = pImpl->vec / scalar;
            return result;
        }

        Vector4 &Vector4::operator+=(const Vector4 &other)
        {
            pImpl->vec += other.pImpl->vec;
            return *this;
        }

        Vector4 &Vector4::operator-=(const Vector4 &other)
        {
            pImpl->vec -= other.pImpl->vec;
            return *this;
        }

        Vector4 &Vector4::operator*=(float scalar)
        {
            pImpl->vec *= scalar;
            return *this;
        }

        Vector4 &Vector4::operator/=(float scalar)
        {
            pImpl->vec /= scalar;
            return *this;
        }

        // 比较操作符
        bool Vector4::operator==(const Vector4 &other) const { return pImpl->vec == other.pImpl->vec; }
        bool Vector4::operator!=(const Vector4 &other) const { return pImpl->vec != other.pImpl->vec; }

        // 常用向量
        Vector4 Vector4::Zero() { return Vector4(0.0f, 0.0f, 0.0f, 0.0f); }
        Vector4 Vector4::One() { return Vector4(1.0f, 1.0f, 1.0f, 1.0f); }

        // 附加向量操作
        Vector4 Vector4::Lerp(const Vector4 &other, float t) const
        {
            Vector4 result;
            result.pImpl->vec = glm::mix(pImpl->vec, other.pImpl->vec, t);
            return result;
        }

        float Vector4::Distance(const Vector4 &other) const
        {
            return glm::distance(pImpl->vec, other.pImpl->vec);
        }

        float Vector4::DistanceSquared(const Vector4 &other) const
        {
            return glm::distance2(pImpl->vec, other.pImpl->vec);
        }

        // 字符串转换
        std::string Vector4::ToString() const
        {
            std::stringstream ss;
            ss << std::fixed << std::setprecision(2);
            ss << "Vector4(" << pImpl->vec.x << ", " << pImpl->vec.y << ", "
               << pImpl->vec.z << ", " << pImpl->vec.w << ")";
            return ss.str();
        }

        // 非成员操作符重载
        Vector4 operator*(float scalar, const Vector4 &vec)
        {
            return vec * scalar;
        }
    }
}