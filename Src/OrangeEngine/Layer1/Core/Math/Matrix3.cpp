#include "Matrix.h"
#include "Quaternion.h"
#include "Internal/GLMUtils.h"
#include <sstream>
#include <iomanip>

namespace Orange
{
    namespace Math
    {
        class Matrix3::Impl
        {
        public:
            glm::mat3 mat;

            Impl() : mat(1.0f) {} // 默认为单位矩阵
            explicit Impl(float scalar) : mat(scalar) {}
            Impl(const Impl &other) : mat(other.mat) {}
        };

        // 构造函数和析构函数
        Matrix3::Matrix3() : pImpl(std::make_unique<Impl>()) {}

        Matrix3::Matrix3(float scalar) : pImpl(std::make_unique<Impl>(scalar)) {}

        Matrix3::Matrix3(const Matrix3 &other) : pImpl(std::make_unique<Impl>(*other.pImpl)) {}

        Matrix3::Matrix3(Matrix3 &&other) noexcept = default;

        Matrix3::~Matrix3() = default;

        // 从Matrix4构造
        Matrix3::Matrix3(const Matrix4 &mat4) : pImpl(std::make_unique<Impl>())
        {
            // 从4x4矩阵中提取3x3部分
            for (int row = 0; row < 3; ++row)
            {
                for (int col = 0; col < 3; ++col)
                {
                    pImpl->mat[col][row] = mat4.Get(row, col);
                }
            }
        }

        // 赋值操作符
        Matrix3 &Matrix3::operator=(const Matrix3 &other)
        {
            if (this != &other)
            {
                pImpl->mat = other.pImpl->mat;
            }
            return *this;
        }

        Matrix3 &Matrix3::operator=(Matrix3 &&other) noexcept = default;

        // 矩阵元素访问
        float Matrix3::Get(int row, int col) const
        {
            return pImpl->mat[col][row];
        }

        void Matrix3::Set(int row, int col, float value)
        {
            pImpl->mat[col][row] = value;
        }

        // 行和列访问
        Vector3 Matrix3::GetRow(int row) const
        {
            return Vector3(pImpl->mat[0][row], pImpl->mat[1][row], pImpl->mat[2][row]);
        }

        Vector3 Matrix3::GetColumn(int col) const
        {
            glm::vec3 columnVec = pImpl->mat[col];
            return Vector3(columnVec.x, columnVec.y, columnVec.z);
        }

        void Matrix3::SetRow(int row, const Vector3 &vector)
        {
            pImpl->mat[0][row] = vector.X();
            pImpl->mat[1][row] = vector.Y();
            pImpl->mat[2][row] = vector.Z();
        }

        void Matrix3::SetColumn(int col, const Vector3 &vector)
        {
            pImpl->mat[col][0] = vector.X();
            pImpl->mat[col][1] = vector.Y();
            pImpl->mat[col][2] = vector.Z();
        }

        // 矩阵变换
        Matrix3 Matrix3::Transpose() const
        {
            Matrix3 result;
            result.pImpl->mat = glm::transpose(pImpl->mat);
            return result;
        }

        Matrix3 Matrix3::Inverse() const
        {
            Matrix3 result;
            result.pImpl->mat = glm::inverse(pImpl->mat);
            return result;
        }

        float Matrix3::Determinant() const
        {
            return glm::determinant(pImpl->mat);
        }

        // 操作符重载
        Matrix3 Matrix3::operator+(const Matrix3 &other) const
        {
            Matrix3 result;
            result.pImpl->mat = pImpl->mat + other.pImpl->mat;
            return result;
        }

        Matrix3 Matrix3::operator-(const Matrix3 &other) const
        {
            Matrix3 result;
            result.pImpl->mat = pImpl->mat - other.pImpl->mat;
            return result;
        }

        Matrix3 Matrix3::operator*(const Matrix3 &other) const
        {
            Matrix3 result;
            result.pImpl->mat = pImpl->mat * other.pImpl->mat;
            return result;
        }

        Vector3 Matrix3::operator*(const Vector3 &vec) const
        {
            Vector3 result;
            glm::vec3 temp = pImpl->mat * glm::vec3(vec.X(), vec.Y(), vec.Z());
            return Vector3(temp.x, temp.y, temp.z);
        }

        Matrix3 Matrix3::operator*(float scalar) const
        {
            Matrix3 result;
            result.pImpl->mat = pImpl->mat * scalar;
            return result;
        }

        Matrix3 &Matrix3::operator+=(const Matrix3 &other)
        {
            pImpl->mat += other.pImpl->mat;
            return *this;
        }

        Matrix3 &Matrix3::operator-=(const Matrix3 &other)
        {
            pImpl->mat -= other.pImpl->mat;
            return *this;
        }

        Matrix3 &Matrix3::operator*=(const Matrix3 &other)
        {
            pImpl->mat *= other.pImpl->mat;
            return *this;
        }

        Matrix3 &Matrix3::operator*=(float scalar)
        {
            pImpl->mat *= scalar;
            return *this;
        }

        // 比较操作符
        bool Matrix3::operator==(const Matrix3 &other) const
        {
            return pImpl->mat == other.pImpl->mat;
        }

        bool Matrix3::operator!=(const Matrix3 &other) const
        {
            return !(*this == other);
        }

        // 创建变换矩阵
        Matrix3 Matrix3::Identity()
        {
            Matrix3 result;
            result.pImpl->mat = glm::mat3(1.0f);
            return result;
        }

        Matrix3 Matrix3::Rotation(float angleRadians)
        {
            Matrix3 result;
            float c = std::cos(angleRadians);
            float s = std::sin(angleRadians);

            // 创建2D旋转矩阵（嵌入到3x3）
            result.pImpl->mat[0][0] = c;
            result.pImpl->mat[0][1] = s;
            result.pImpl->mat[1][0] = -s;
            result.pImpl->mat[1][1] = c;

            return result;
        }

        Matrix3 Matrix3::Scale(const Vector2 &scale)
        {
            return Scale(scale.X(), scale.Y());
        }

        Matrix3 Matrix3::Scale(float x, float y)
        {
            Matrix3 result;
            result.pImpl->mat[0][0] = x;
            result.pImpl->mat[1][1] = y;
            return result;
        }

        Matrix3 Matrix3::FromQuaternion(const Quaternion &rotation)
        {
            // 使用GLMUtils来正确访问Quaternion中的四元数并进行转换
            glm::quat glmQuat = Orange::Math::Internal::GLMUtils::ToGLM(rotation);
            glm::mat3 rotMat = glm::mat3_cast(glmQuat);
            return Orange::Math::Internal::GLMUtils::FromGLM(rotMat);
        }

        // 字符串转换
        std::string Matrix3::ToString() const
        {
            std::stringstream ss;
            ss << std::fixed << std::setprecision(3);
            ss << "Matrix3(\n";

            for (int row = 0; row < 3; ++row)
            {
                ss << "  ";
                for (int col = 0; col < 3; ++col)
                {
                    ss << pImpl->mat[col][row];
                    if (col < 2)
                        ss << ", ";
                }
                ss << "\n";
            }

            ss << ")";
            return ss.str();
        }

        // 非成员操作符重载
        Matrix3 operator*(float scalar, const Matrix3 &mat)
        {
            return mat * scalar;
        }
    }
}