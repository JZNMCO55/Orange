#include "Transform.h"

namespace Orange
{
    namespace Math
    {
        class Transform::Impl
        {
        public:
            Vector3 position;
            Quaternion rotation;
            Vector3 scale;

            Impl()
                : position(Vector3::Zero()), rotation(Quaternion::Identity()), scale(Vector3::One())
            {
            }

            Impl(const Vector3 &pos)
                : position(pos), rotation(Quaternion::Identity()), scale(Vector3::One())
            {
            }

            Impl(const Vector3 &pos, const Quaternion &rot)
                : position(pos), rotation(rot), scale(Vector3::One())
            {
            }

            Impl(const Vector3 &pos, const Quaternion &rot, const Vector3 &scl)
                : position(pos), rotation(rot), scale(scl)
            {
            }

            Impl(const Matrix4 &matrix)
            {
                matrix.Decompose(scale, rotation, position);
            }

            Impl(const Impl &other)
                : position(other.position), rotation(other.rotation), scale(other.scale)
            {
            }
        };

        // 构造函数
        Transform::Transform() : pImpl(std::make_unique<Impl>()) {}

        Transform::Transform(const Vector3 &position) : pImpl(std::make_unique<Impl>(position)) {}

        Transform::Transform(const Vector3 &position, const Quaternion &rotation)
            : pImpl(std::make_unique<Impl>(position, rotation)) {}

        Transform::Transform(const Vector3 &position, const Quaternion &rotation, const Vector3 &scale)
            : pImpl(std::make_unique<Impl>(position, rotation, scale)) {}

        Transform::Transform(const Matrix4 &transformMatrix)
            : pImpl(std::make_unique<Impl>(transformMatrix)) {}

        Transform::Transform(const Transform &other)
            : pImpl(std::make_unique<Impl>(*other.pImpl)) {}

        Transform::Transform(Transform &&other) noexcept = default;

        Transform::~Transform() = default;

        // 赋值操作符
        Transform &Transform::operator=(const Transform &other)
        {
            if (this != &other)
            {
                pImpl->position = other.pImpl->position;
                pImpl->rotation = other.pImpl->rotation;
                pImpl->scale = other.pImpl->scale;
            }
            return *this;
        }

        Transform &Transform::operator=(Transform &&other) noexcept = default;

        // 获取/设置位置
        Vector3 Transform::GetPosition() const
        {
            return pImpl->position;
        }

        void Transform::SetPosition(const Vector3 &position)
        {
            pImpl->position = position;
        }

        // 获取/设置旋转
        Quaternion Transform::GetRotation() const
        {
            return pImpl->rotation;
        }

        void Transform::SetRotation(const Quaternion &rotation)
        {
            pImpl->rotation = rotation;
        }

        // 获取/设置缩放
        Vector3 Transform::GetScale() const
        {
            return pImpl->scale;
        }

        void Transform::SetScale(const Vector3 &scale)
        {
            pImpl->scale = scale;
        }

        void Transform::SetScale(float uniformScale)
        {
            pImpl->scale = Vector3(uniformScale, uniformScale, uniformScale);
        }

        // 获取变换矩阵
        Matrix4 Transform::GetMatrix() const
        {
            // 构建TRS矩阵 (Translation * Rotation * Scale)
            Matrix4 scaleMatrix = Matrix4::Scale(pImpl->scale);
            Matrix4 rotationMatrix = pImpl->rotation.ToMatrix4();
            Matrix4 translationMatrix = Matrix4::Translation(pImpl->position);

            return translationMatrix * rotationMatrix * scaleMatrix;
        }

        // 从变换矩阵更新
        void Transform::SetFromMatrix(const Matrix4 &matrix)
        {
            matrix.Decompose(pImpl->scale, pImpl->rotation, pImpl->position);
        }

        // 移动变换
        void Transform::Translate(const Vector3 &translation)
        {
            pImpl->position = pImpl->position + translation;
        }

        void Transform::Translate(float x, float y, float z)
        {
            Translate(Vector3(x, y, z));
        }

        // 旋转变换
        void Transform::Rotate(const Quaternion &rotation)
        {
            // 将新旋转应用到当前旋转上 (注意左右乘顺序)
            pImpl->rotation = rotation * pImpl->rotation;
        }

        void Transform::Rotate(const Vector3 &eulerAngles)
        {
            Rotate(Quaternion::FromEulerAngles(eulerAngles));
        }

        void Transform::Rotate(const Vector3 &axis, float angleRadians)
        {
            Rotate(Quaternion(axis, angleRadians));
        }

        void Transform::Rotate(float yaw, float pitch, float roll)
        {
            Rotate(Quaternion::FromEulerAngles(pitch, yaw, roll));
        }

        // 缩放变换
        void Transform::Scale(const Vector3 &scale)
        {
            pImpl->scale.SetX(pImpl->scale.X() * scale.X());
            pImpl->scale.SetY(pImpl->scale.Y() * scale.Y());
            pImpl->scale.SetZ(pImpl->scale.Z() * scale.Z());
        }

        void Transform::Scale(float x, float y, float z)
        {
            Scale(Vector3(x, y, z));
        }

        void Transform::Scale(float uniformScale)
        {
            Scale(Vector3(uniformScale, uniformScale, uniformScale));
        }

        // 变换向量和点
        Vector3 Transform::TransformPoint(const Vector3 &point) const
        {
            // 点变换 p' = T * R * S * p
            return pImpl->position + pImpl->rotation.RotateVector(Vector3(
                                         point.X() * pImpl->scale.X(),
                                         point.Y() * pImpl->scale.Y(),
                                         point.Z() * pImpl->scale.Z()));
        }

        Vector3 Transform::TransformVector(const Vector3 &vector) const
        {
            // 向量变换 v' = R * S * v (不应用平移)
            return pImpl->rotation.RotateVector(Vector3(
                vector.X() * pImpl->scale.X(),
                vector.Y() * pImpl->scale.Y(),
                vector.Z() * pImpl->scale.Z()));
        }

        // 在局部空间和世界空间之间转换
        Vector3 Transform::TransformDirection(const Vector3 &localDirection) const
        {
            // 方向变换 d' = R * d (只应用旋转)
            return pImpl->rotation.RotateVector(localDirection);
        }

        Vector3 Transform::InverseTransformDirection(const Vector3 &worldDirection) const
        {
            // 逆方向变换 d = R^-1 * d'
            return pImpl->rotation.Inverse().RotateVector(worldDirection);
        }

        Vector3 Transform::InverseTransformPoint(const Vector3 &worldPoint) const
        {
            // 逆点变换 p = S^-1 * R^-1 * (p' - T)
            Vector3 relativePoint = worldPoint - pImpl->position;
            Vector3 rotatedPoint = pImpl->rotation.Inverse().RotateVector(relativePoint);

            return Vector3(
                rotatedPoint.X() / pImpl->scale.X(),
                rotatedPoint.Y() / pImpl->scale.Y(),
                rotatedPoint.Z() / pImpl->scale.Z());
        }

        Vector3 Transform::InverseTransformVector(const Vector3 &worldVector) const
        {
            // 逆向量变换 v = S^-1 * R^-1 * v'
            Vector3 rotatedVector = pImpl->rotation.Inverse().RotateVector(worldVector);

            return Vector3(
                rotatedVector.X() / pImpl->scale.X(),
                rotatedVector.Y() / pImpl->scale.Y(),
                rotatedVector.Z() / pImpl->scale.Z());
        }

        // 从一个空间到另一个空间的变换
        Vector3 Transform::TransformPoint(const Transform &transform, const Vector3 &point)
        {
            return transform.TransformPoint(point);
        }

        Vector3 Transform::TransformVector(const Transform &transform, const Vector3 &vector)
        {
            return transform.TransformVector(vector);
        }

        Vector3 Transform::InverseTransformPoint(const Transform &transform, const Vector3 &worldPoint)
        {
            return transform.InverseTransformPoint(worldPoint);
        }

        Vector3 Transform::InverseTransformVector(const Transform &transform, const Vector3 &worldVector)
        {
            return transform.InverseTransformVector(worldVector);
        }

        // 插值
        Transform Transform::Lerp(const Transform &a, const Transform &b, float t)
        {
            Vector3 position = a.pImpl->position.Lerp(b.pImpl->position, t);
            Quaternion rotation = Quaternion::Slerp(a.pImpl->rotation, b.pImpl->rotation, t);
            Vector3 scale = a.pImpl->scale.Lerp(b.pImpl->scale, t);

            return Transform(position, rotation, scale);
        }

        // 变换组合
        Transform Transform::operator*(const Transform &other) const
        {
            // 组合变换 (先应用 other，再应用 this)
            Vector3 newPosition = TransformPoint(other.pImpl->position);
            Quaternion newRotation = pImpl->rotation * other.pImpl->rotation;
            Vector3 newScale = Vector3(
                pImpl->scale.X() * other.pImpl->scale.X(),
                pImpl->scale.Y() * other.pImpl->scale.Y(),
                pImpl->scale.Z() * other.pImpl->scale.Z());

            return Transform(newPosition, newRotation, newScale);
        }

        Transform &Transform::operator*=(const Transform &other)
        {
            *this = *this * other;
            return *this;
        }

        // 一些有用的静态变换
        Transform Transform::Identity()
        {
            return Transform();
        }
    }
}