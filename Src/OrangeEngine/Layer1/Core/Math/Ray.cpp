#include "Geometry.h"
#include "Transform.h"

namespace Orange
{
    namespace Math
    {
        class Ray::Impl
        {
        public:
            Vector3 origin;
            Vector3 direction;

            Impl()
                : origin(Vector3::Zero()), direction(Vector3::Forward())
            {
            }

            Impl(const Vector3 &orig, const Vector3 &dir)
                : origin(orig), direction(dir)
            {
                // 确保方向是单位向量
                if (direction.LengthSquared() != 1.0f)
                {
                    direction = direction.Normalized();
                }
            }

            Impl(const Impl &other)
                : origin(other.origin), direction(other.direction)
            {
            }
        };

        // 构造函数
        Ray::Ray() : pImpl(std::make_unique<Impl>()) {}

        Ray::Ray(const Vector3 &origin, const Vector3 &direction)
            : pImpl(std::make_unique<Impl>(origin, direction)) {}

        Ray::Ray(const Ray &other)
            : pImpl(std::make_unique<Impl>(*other.pImpl)) {}

        Ray::Ray(Ray &&other) noexcept = default;

        Ray::~Ray() = default;

        // 赋值操作符
        Ray &Ray::operator=(const Ray &other)
        {
            if (this != &other)
            {
                pImpl->origin = other.pImpl->origin;
                pImpl->direction = other.pImpl->direction;
            }
            return *this;
        }

        Ray &Ray::operator=(Ray &&other) noexcept = default;

        // 获取/设置属性
        Vector3 Ray::GetOrigin() const
        {
            return pImpl->origin;
        }

        void Ray::SetOrigin(const Vector3 &origin)
        {
            pImpl->origin = origin;
        }

        Vector3 Ray::GetDirection() const
        {
            return pImpl->direction;
        }

        void Ray::SetDirection(const Vector3 &direction)
        {
            pImpl->direction = direction.Normalized();
        }

        // 射线操作
        Vector3 Ray::GetPoint(float distance) const
        {
            return pImpl->origin + pImpl->direction * distance;
        }

        Ray Ray::Transform(const Matrix4 &matrix) const
        {
            // 变换起点 (作为点变换)
            Vector4 originTransformed = matrix * Vector4(pImpl->origin, 1.0f);
            Vector3 newOrigin = Vector3(
                originTransformed.X() / originTransformed.W(),
                originTransformed.Y() / originTransformed.W(),
                originTransformed.Z() / originTransformed.W());

            // 变换方向 (作为向量变换，不受平移影响)
            Vector4 dirTransformed = matrix * Vector4(pImpl->direction, 0.0f);
            Vector3 newDirection = Vector3(
                                       dirTransformed.X(),
                                       dirTransformed.Y(),
                                       dirTransformed.Z())
                                       .Normalized();

            return Ray(newOrigin, newDirection);
        }
    }
}