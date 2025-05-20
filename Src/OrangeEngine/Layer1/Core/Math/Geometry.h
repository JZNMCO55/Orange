#ifndef ORANGE_ENGINE_MATH_GEOMETRY_H
#define ORANGE_ENGINE_MATH_GEOMETRY_H

#include <memory>
#include "Vector.h"
#include "Matrix.h"

namespace Orange
{
    namespace Math
    {
        /**
         * @class Ray
         * @brief 表示从原点沿方向延伸的射线
         */
        class Ray
        {
        public:
            Ray();
            Ray(const Vector3 &origin, const Vector3 &direction);
            Ray(const Ray &other);
            Ray(Ray &&other) noexcept;
            ~Ray();

            Ray &operator=(const Ray &other);
            Ray &operator=(Ray &&other) noexcept;

            // 获取/设置属性
            Vector3 GetOrigin() const;
            void SetOrigin(const Vector3 &origin);

            Vector3 GetDirection() const;
            void SetDirection(const Vector3 &direction);

            // 射线操作
            Vector3 GetPoint(float distance) const;
            Ray Transform(const Matrix4 &matrix) const;

        private:
            class Impl;
            std::unique_ptr<Impl> pImpl;
        };

        /**
         * @class Plane
         * @brief 表示3D空间中的平面
         */
        class Plane
        {
        public:
            Plane();
            Plane(const Vector3 &normal, float distance);
            Plane(const Vector3 &normal, const Vector3 &point);
            Plane(const Vector3 &a, const Vector3 &b, const Vector3 &c);
            Plane(const Plane &other);
            Plane(Plane &&other) noexcept;
            ~Plane();

            Plane &operator=(const Plane &other);
            Plane &operator=(Plane &&other) noexcept;

            // 获取/设置属性
            Vector3 GetNormal() const;
            void SetNormal(const Vector3 &normal);

            float GetDistance() const;
            void SetDistance(float distance);

            // 平面操作
            float GetSignedDistance(const Vector3 &point) const;
            Vector3 ProjectPoint(const Vector3 &point) const;
            Plane Transform(const Matrix4 &matrix) const;
            Plane Normalize() const;

        private:
            class Impl;
            std::unique_ptr<Impl> pImpl;
        };

        /**
         * @class AABB
         * @brief 代表轴对齐包围盒
         */
        class AABB
        {
        public:
            AABB();
            AABB(const Vector3 &min, const Vector3 &max);
            AABB(const AABB &other);
            AABB(AABB &&other) noexcept;
            ~AABB();

            AABB &operator=(const AABB &other);
            AABB &operator=(AABB &&other) noexcept;

            // 获取/设置属性
            Vector3 GetMin() const;
            void SetMin(const Vector3 &min);

            Vector3 GetMax() const;
            void SetMax(const Vector3 &max);

            Vector3 GetCenter() const;
            Vector3 GetSize() const;
            Vector3 GetExtent() const;

            // AABB操作
            bool Contains(const Vector3 &point) const;
            bool Intersects(const AABB &other) const;
            AABB Transform(const Matrix4 &matrix) const;
            void Expand(float amount);
            void Expand(const Vector3 &amount);
            void Include(const Vector3 &point);
            void Include(const AABB &other);

        private:
            class Impl;
            std::unique_ptr<Impl> pImpl;
        };

        /**
         * @class Sphere
         * @brief 表示3D空间中的球体
         */
        class Sphere
        {
        public:
            Sphere();
            Sphere(const Vector3 &center, float radius);
            Sphere(const Sphere &other);
            Sphere(Sphere &&other) noexcept;
            ~Sphere();

            Sphere &operator=(const Sphere &other);
            Sphere &operator=(Sphere &&other) noexcept;

            // 获取/设置属性
            Vector3 GetCenter() const;
            void SetCenter(const Vector3 &center);

            float GetRadius() const;
            void SetRadius(float radius);

            // 球体操作
            bool Contains(const Vector3 &point) const;
            bool Intersects(const Sphere &other) const;
            bool Intersects(const AABB &box) const;
            Sphere Transform(const Matrix4 &matrix) const;

        private:
            class Impl;
            std::unique_ptr<Impl> pImpl;
        };

        /**
         * @class Intersection
         * @brief 几何交点检测静态函数集合
         */
        class Intersection
        {
        public:
            // 射线与平面相交
            static bool RayPlane(const Ray &ray, const Plane &plane, float &outDistance);

            // 射线与球体相交
            static bool RaySphere(const Ray &ray, const Sphere &sphere, float &outDistance);

            // 射线与AABB相交
            static bool RayAABB(const Ray &ray, const AABB &aabb, float &outDistance);

            // 平面与平面相交
            static bool PlanePlane(const Plane &plane1, const Plane &plane2, Ray &outLine);

            // AABB与AABB相交
            static bool AABBAABB(const AABB &a, const AABB &b);

            // 球体与球体相交
            static bool SphereSphere(const Sphere &a, const Sphere &b);

            // 球体与AABB相交
            static bool SphereAABB(const Sphere &sphere, const AABB &aabb);

            // 平面与AABB相交
            static bool PlaneAABB(const Plane &plane, const AABB &aabb);
        };
    }
}

#endif // ORANGE_ENGINE_MATH_GEOMETRY_H