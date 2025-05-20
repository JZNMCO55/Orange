#include "Matrix.h"
#include "Quaternion.h"
#include "Internal/GLMUtils.h"
#include <sstream>
#include <iomanip>

namespace Orange
{
    namespace Math
    {
        class Matrix4::Impl
        {
        public:
            glm::mat4 mat;

            Impl() : mat(1.0f) {} // 默认为单位矩阵
            explicit Impl(float scalar) : mat(scalar) {}
            Impl(const Impl &other) : mat(other.mat) {}
        };

        // 构造函数和析构函数
        Matrix4::Matrix4() : pImpl(std::make_unique<Impl>()) {}

        Matrix4::Matrix4(float scalar) : pImpl(std::make_unique<Impl>(scalar)) {}

        Matrix4::Matrix4(const Matrix4 &other) : pImpl(std::make_unique<Impl>(*other.pImpl)) {}

        Matrix4::Matrix4(Matrix4 &&other) noexcept = default;

        Matrix4::~Matrix4() = default;

        // 赋值操作符
        Matrix4 &Matrix4::operator=(const Matrix4 &other)
        {
            if (this != &other)
            {
                pImpl->mat = other.pImpl->mat;
            }
            return *this;
        }

        Matrix4 &Matrix4::operator=(Matrix4 &&other) noexcept = default;

        // 矩阵元素访问
        float Matrix4::Get(int row, int col) const
        {
            return pImpl->mat[col][row];
        }

        void Matrix4::Set(int row, int col, float value)
        {
            pImpl->mat[col][row] = value;
        }

        // 行和列访问
        Vector4 Matrix4::GetRow(int row) const
        {
            return Vector4(pImpl->mat[0][row], pImpl->mat[1][row], pImpl->mat[2][row], pImpl->mat[3][row]);
        }

        Vector4 Matrix4::GetColumn(int col) const
        {
            return Vector4(pImpl->mat[col][0], pImpl->mat[col][1], pImpl->mat[col][2], pImpl->mat[col][3]);
        }

        void Matrix4::SetRow(int row, const Vector4 &vector)
        {
            pImpl->mat[0][row] = vector.X();
            pImpl->mat[1][row] = vector.Y();
            pImpl->mat[2][row] = vector.Z();
            pImpl->mat[3][row] = vector.W();
        }

        void Matrix4::SetColumn(int col, const Vector4 &vector)
        {
            pImpl->mat[col][0] = vector.X();
            pImpl->mat[col][1] = vector.Y();
            pImpl->mat[col][2] = vector.Z();
            pImpl->mat[col][3] = vector.W();
        }

        // 矩阵变换
        Matrix4 Matrix4::Transpose() const
        {
            Matrix4 result;
            result.pImpl->mat = glm::transpose(pImpl->mat);
            return result;
        }

        Matrix4 Matrix4::Inverse() const
        {
            Matrix4 result;
            result.pImpl->mat = glm::inverse(pImpl->mat);
            return result;
        }

        float Matrix4::Determinant() const
        {
            return glm::determinant(pImpl->mat);
        }

        // 操作符重载
        Matrix4 Matrix4::operator+(const Matrix4 &other) const
        {
            Matrix4 result;
            result.pImpl->mat = pImpl->mat + other.pImpl->mat;
            return result;
        }

        Matrix4 Matrix4::operator-(const Matrix4 &other) const
        {
            Matrix4 result;
            result.pImpl->mat = pImpl->mat - other.pImpl->mat;
            return result;
        }

        Matrix4 Matrix4::operator*(const Matrix4 &other) const
        {
            Matrix4 result;
            result.pImpl->mat = pImpl->mat * other.pImpl->mat;
            return result;
        }

        Vector4 Matrix4::operator*(const Vector4 &vec) const
        {
            Vector4 result;
            glm::vec4 temp = pImpl->mat * glm::vec4(vec.X(), vec.Y(), vec.Z(), vec.W());
            return Vector4(temp.x, temp.y, temp.z, temp.w);
        }

        Matrix4 Matrix4::operator*(float scalar) const
        {
            Matrix4 result;
            result.pImpl->mat = pImpl->mat * scalar;
            return result;
        }

        Matrix4 &Matrix4::operator+=(const Matrix4 &other)
        {
            pImpl->mat += other.pImpl->mat;
            return *this;
        }

        Matrix4 &Matrix4::operator-=(const Matrix4 &other)
        {
            pImpl->mat -= other.pImpl->mat;
            return *this;
        }

        Matrix4 &Matrix4::operator*=(const Matrix4 &other)
        {
            pImpl->mat = pImpl->mat * other.pImpl->mat;
            return *this;
        }

        Matrix4 &Matrix4::operator*=(float scalar)
        {
            pImpl->mat *= scalar;
            return *this;
        }

        // 比较操作符
        bool Matrix4::operator==(const Matrix4 &other) const
        {
            return pImpl->mat == other.pImpl->mat;
        }

        bool Matrix4::operator!=(const Matrix4 &other) const
        {
            return !(*this == other);
        }

        // 创建变换矩阵
        Matrix4 Matrix4::Identity()
        {
            Matrix4 result;
            result.pImpl->mat = glm::mat4(1.0f);
            return result;
        }

        Matrix4 Matrix4::Translation(const Vector3 &translation)
        {
            return Translation(translation.X(), translation.Y(), translation.Z());
        }

        Matrix4 Matrix4::Translation(float x, float y, float z)
        {
            Matrix4 result;
            result.pImpl->mat = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z));
            return result;
        }

        Matrix4 Matrix4::Rotation(const Vector3 &eulerAngles)
        {
            return Rotation(eulerAngles.X(), eulerAngles.Y(), eulerAngles.Z());
        }

        Matrix4 Matrix4::Rotation(float pitch, float yaw, float roll)
        {
            Matrix4 result;
            // 使用欧拉角创建旋转矩阵（顺序：先旋转X，再旋转Y，最后旋转Z）
            glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), pitch, glm::vec3(1.0f, 0.0f, 0.0f));
            glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), roll, glm::vec3(0.0f, 0.0f, 1.0f));

            // 组合旋转（Z * Y * X）
            result.pImpl->mat = rotationZ * rotationY * rotationX;
            return result;
        }

        Matrix4 Matrix4::RotationX(float angleRadians)
        {
            Matrix4 result;
            result.pImpl->mat = glm::rotate(glm::mat4(1.0f), angleRadians, glm::vec3(1.0f, 0.0f, 0.0f));
            return result;
        }

        Matrix4 Matrix4::RotationY(float angleRadians)
        {
            Matrix4 result;
            result.pImpl->mat = glm::rotate(glm::mat4(1.0f), angleRadians, glm::vec3(0.0f, 1.0f, 0.0f));
            return result;
        }

        Matrix4 Matrix4::RotationZ(float angleRadians)
        {
            Matrix4 result;
            result.pImpl->mat = glm::rotate(glm::mat4(1.0f), angleRadians, glm::vec3(0.0f, 0.0f, 1.0f));
            return result;
        }

        Matrix4 Matrix4::Scale(const Vector3 &scale)
        {
            return Scale(scale.X(), scale.Y(), scale.Z());
        }

        Matrix4 Matrix4::Scale(float x, float y, float z)
        {
            Matrix4 result;
            result.pImpl->mat = glm::scale(glm::mat4(1.0f), glm::vec3(x, y, z));
            return result;
        }

        Matrix4 Matrix4::FromQuaternion(const Quaternion &rotation)
        {
            Matrix4 result;
            // 将在Quaternion类实现中完成
            return result;
        }

        // 视图和投影矩阵
        Matrix4 Matrix4::LookAt(const Vector3 &eye, const Vector3 &center, const Vector3 &up)
        {
            Matrix4 result;
            result.pImpl->mat = glm::lookAt(
                glm::vec3(eye.X(), eye.Y(), eye.Z()),
                glm::vec3(center.X(), center.Y(), center.Z()),
                glm::vec3(up.X(), up.Y(), up.Z()));
            return result;
        }

        Matrix4 Matrix4::Perspective(float fov, float aspectRatio, float nearPlane, float farPlane)
        {
            Matrix4 result;
            result.pImpl->mat = glm::perspective(fov, aspectRatio, nearPlane, farPlane);
            return result;
        }

        Matrix4 Matrix4::Orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane)
        {
            Matrix4 result;
            result.pImpl->mat = glm::ortho(left, right, bottom, top, nearPlane, farPlane);
            return result;
        }

        // 从矩阵获取变换
        Vector3 Matrix4::GetTranslation() const
        {
            return Vector3(pImpl->mat[3][0], pImpl->mat[3][1], pImpl->mat[3][2]);
        }

        Vector3 Matrix4::GetScale() const
        {
            // 提取缩放因子（每个基向量的长度）
            float sx = glm::length(glm::vec3(pImpl->mat[0][0], pImpl->mat[0][1], pImpl->mat[0][2]));
            float sy = glm::length(glm::vec3(pImpl->mat[1][0], pImpl->mat[1][1], pImpl->mat[1][2]));
            float sz = glm::length(glm::vec3(pImpl->mat[2][0], pImpl->mat[2][1], pImpl->mat[2][2]));

            return Vector3(sx, sy, sz);
        }

        Quaternion Matrix4::GetRotation() const
        {
            // 将在Quaternion类实现后完成
            return Quaternion();
        }

        // 矩阵分解
        void Matrix4::Decompose(Vector3 &scale, Quaternion &rotation, Vector3 &translation) const
        {
            // 提取平移
            translation = GetTranslation();

            // 提取缩放
            scale = GetScale();

            // 提取旋转
            // 将在Quaternion类实现后完成
            rotation = Quaternion();
        }

        // 字符串转换
        std::string Matrix4::ToString() const
        {
            std::stringstream ss;
            ss << std::fixed << std::setprecision(3);
            ss << "Matrix4(\n";

            for (int row = 0; row < 4; ++row)
            {
                ss << "  ";
                for (int col = 0; col < 4; ++col)
                {
                    ss << pImpl->mat[col][row];
                    if (col < 3)
                        ss << ", ";
                }
                ss << "\n";
            }

            ss << ")";
            return ss.str();
        }

        // 非成员操作符重载
        Matrix4 operator*(float scalar, const Matrix4 &mat)
        {
            return mat * scalar;
        }
    }
}