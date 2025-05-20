#include "Quaternion.h"
#include "Internal/GLMUtils.h"
#include <sstream>
#include <iomanip>

namespace Orange
{
    namespace Math
    {
        class Quaternion::Impl
        {
        public:
            glm::quat quat;

            Impl() : quat(1.0f, 0.0f, 0.0f, 0.0f) {}                       // 默认为单位四元数，GLM是(w, x, y, z)顺序
            Impl(float x, float y, float z, float w) : quat(w, x, y, z) {} // GLM是(w, x, y, z)顺序
            Impl(const Impl &other) : quat(other.quat) {}
        };

        // 构造函数和析构函数
        Quaternion::Quaternion() : pImpl(std::make_unique<Impl>()) {}

        Quaternion::Quaternion(float x, float y, float z, float w) : pImpl(std::make_unique<Impl>(x, y, z, w)) {}

        Quaternion::Quaternion(const Quaternion &other) : pImpl(std::make_unique<Impl>(*other.pImpl)) {}

        Quaternion::Quaternion(Quaternion &&other) noexcept = default;

        Quaternion::~Quaternion() = default;

        // 从轴角构造
        Quaternion::Quaternion(const Vector3 &axis, float angleRadians) : pImpl(std::make_unique<Impl>())
        {
            // 创建一个从轴和角度的四元数
            glm::vec3 glmAxis(axis.X(), axis.Y(), axis.Z());
            pImpl->quat = glm::angleAxis(angleRadians, glm::normalize(glmAxis));
        }

        // 赋值操作符
        Quaternion &Quaternion::operator=(const Quaternion &other)
        {
            if (this != &other)
            {
                pImpl->quat = other.pImpl->quat;
            }
            return *this;
        }

        Quaternion &Quaternion::operator=(Quaternion &&other) noexcept = default;

        // 从欧拉角创建四元数
        Quaternion Quaternion::FromEulerAngles(const Vector3 &eulerAngles)
        {
            return FromEulerAngles(eulerAngles.X(), eulerAngles.Y(), eulerAngles.Z());
        }

        Quaternion Quaternion::FromEulerAngles(float pitch, float yaw, float roll)
        {
            Quaternion result;
            // GLM使用的是(roll, pitch, yaw)顺序，对应绕(Z, X, Y)轴
            result.pImpl->quat = glm::quat(glm::vec3(pitch, yaw, roll));
            return result;
        }

        // 从旋转矩阵创建四元数
        Quaternion Quaternion::FromRotationMatrix(const Matrix3 &rotationMatrix)
        {
            Quaternion result;
            // 使用友元关系和GLM工具类来正确访问矩阵
            glm::mat3 glmMat = Orange::Math::Internal::GLMUtils::ToGLM(rotationMatrix);
            result.pImpl->quat = glm::quat_cast(glmMat);
            return result;
        }

        Quaternion Quaternion::FromRotationMatrix(const Matrix4 &rotationMatrix)
        {
            Quaternion result;
            // 使用友元关系和GLM工具类来正确访问矩阵
            glm::mat4 glmMat = Orange::Math::Internal::GLMUtils::ToGLM(rotationMatrix);
            result.pImpl->quat = glm::quat_cast(glmMat);
            return result;
        }

        // 获取/设置分量
        float Quaternion::X() const { return pImpl->quat.x; }
        float Quaternion::Y() const { return pImpl->quat.y; }
        float Quaternion::Z() const { return pImpl->quat.z; }
        float Quaternion::W() const { return pImpl->quat.w; }
        void Quaternion::SetX(float x) { pImpl->quat.x = x; }
        void Quaternion::SetY(float y) { pImpl->quat.y = y; }
        void Quaternion::SetZ(float z) { pImpl->quat.z = z; }
        void Quaternion::SetW(float w) { pImpl->quat.w = w; }

        // 获取欧拉角表示
        Vector3 Quaternion::ToEulerAngles() const
        {
            glm::vec3 euler = glm::eulerAngles(pImpl->quat);
            return Vector3(euler.x, euler.y, euler.z);
        }

        // 获取旋转矩阵表示
        Matrix3 Quaternion::ToMatrix3() const
        {
            Matrix3 result;
            // 使用GLMUtils进行转换
            glm::mat3 rotMat = glm::mat3_cast(pImpl->quat);
            return Orange::Math::Internal::GLMUtils::FromGLM(rotMat);
        }

        Matrix4 Quaternion::ToMatrix4() const
        {
            Matrix4 result;
            // 使用GLMUtils进行转换
            glm::mat4 rotMat = glm::mat4_cast(pImpl->quat);
            return Orange::Math::Internal::GLMUtils::FromGLM(rotMat);
        }

        // 获取轴角表示
        void Quaternion::ToAxisAngle(Vector3 &outAxis, float &outAngleRadians) const
        {
            // 转换为轴角表示
            float angle = 2.0f * std::acos(pImpl->quat.w);

            // 计算sin(angle/2)
            float s = std::sqrt(1.0f - pImpl->quat.w * pImpl->quat.w);

            // 防止除以接近零的数
            if (s < 0.001f)
            {
                // 如果s接近零，方向不重要，使用任意轴
                outAxis = Vector3(1.0f, 0.0f, 0.0f);
            }
            else
            {
                outAxis = Vector3(pImpl->quat.x / s, pImpl->quat.y / s, pImpl->quat.z / s);
            }

            outAngleRadians = angle;
        }

        // 四元数操作
        float Quaternion::Length() const
        {
            return glm::length(pImpl->quat);
        }

        float Quaternion::LengthSquared() const
        {
            return glm::length2(pImpl->quat);
        }

        Quaternion Quaternion::Normalized() const
        {
            Quaternion result;
            result.pImpl->quat = glm::normalize(pImpl->quat);
            return result;
        }

        void Quaternion::Normalize()
        {
            pImpl->quat = glm::normalize(pImpl->quat);
        }

        Quaternion Quaternion::Conjugate() const
        {
            Quaternion result;
            result.pImpl->quat = glm::conjugate(pImpl->quat);
            return result;
        }

        Quaternion Quaternion::Inverse() const
        {
            Quaternion result;
            result.pImpl->quat = glm::inverse(pImpl->quat);
            return result;
        }

        // 应用旋转到向量
        Vector3 Quaternion::RotateVector(const Vector3 &vec) const
        {
            // 将向量转换为临时glm::vec3
            glm::vec3 v(vec.X(), vec.Y(), vec.Z());

            // 使用四元数旋转向量
            glm::vec3 rotated = glm::rotate(pImpl->quat, v);

            // 返回旋转后的向量
            return Vector3(rotated.x, rotated.y, rotated.z);
        }

        // 球面插值
        Quaternion Quaternion::Slerp(const Quaternion &a, const Quaternion &b, float t)
        {
            Quaternion result;
            result.pImpl->quat = glm::slerp(a.pImpl->quat, b.pImpl->quat, t);
            return result;
        }

        // 线性插值
        Quaternion Quaternion::Lerp(const Quaternion &a, const Quaternion &b, float t)
        {
            Quaternion result;
            result.pImpl->quat = glm::mix(a.pImpl->quat, b.pImpl->quat, t);
            return result;
        }

        // 操作符重载
        Quaternion Quaternion::operator*(const Quaternion &other) const
        {
            Quaternion result;
            result.pImpl->quat = pImpl->quat * other.pImpl->quat;
            return result;
        }

        Vector3 Quaternion::operator*(const Vector3 &vec) const
        {
            return RotateVector(vec);
        }

        Quaternion Quaternion::operator*(float scalar) const
        {
            Quaternion result;
            result.pImpl->quat = pImpl->quat * scalar;
            return result;
        }

        Quaternion Quaternion::operator/(float scalar) const
        {
            Quaternion result;
            result.pImpl->quat = pImpl->quat / scalar;
            return result;
        }

        Quaternion &Quaternion::operator*=(const Quaternion &other)
        {
            pImpl->quat *= other.pImpl->quat;
            return *this;
        }

        Quaternion &Quaternion::operator*=(float scalar)
        {
            pImpl->quat *= scalar;
            return *this;
        }

        Quaternion &Quaternion::operator/=(float scalar)
        {
            pImpl->quat /= scalar;
            return *this;
        }

        // 比较操作符
        bool Quaternion::operator==(const Quaternion &other) const
        {
            return pImpl->quat == other.pImpl->quat;
        }

        bool Quaternion::operator!=(const Quaternion &other) const
        {
            return !(*this == other);
        }

        // 常用四元数
        Quaternion Quaternion::Identity()
        {
            return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
        }

        // 字符串转换
        std::string Quaternion::ToString() const
        {
            std::stringstream ss;
            ss << std::fixed << std::setprecision(3);
            ss << "Quaternion(" << pImpl->quat.x << ", " << pImpl->quat.y << ", "
               << pImpl->quat.z << ", " << pImpl->quat.w << ")";
            return ss.str();
        }

        // 非成员操作符重载
        Quaternion operator*(float scalar, const Quaternion &quat)
        {
            return quat * scalar;
        }
    }
}