#ifndef GLM_UTILS_H
#define GLM_UTILS_H

#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include "../Vector.h"
#include "../Matrix.h"
#include "../Quaternion.h"

namespace Orange
{
    namespace Math
    {
        namespace Internal
        {
            /**
             * @brief GLM工具类，用于在Orange的数学类型和GLM类型之间进行转换
             *
             * 这个类仅供内部使用，不应该被任何用户代码直接使用。
             * 所有公开的接口都应该使用Orange::Math命名空间中定义的类型。
             */
            class GLMUtils
            {
            public:
                // Vector转换
                static glm::vec2 ToGLM(const Vector2 &vec)
                {
                    return glm::vec2(vec.X(), vec.Y());
                }

                static glm::vec3 ToGLM(const Vector3 &vec)
                {
                    return glm::vec3(vec.X(), vec.Y(), vec.Z());
                }

                static glm::vec4 ToGLM(const Vector4 &vec)
                {
                    return glm::vec4(vec.X(), vec.Y(), vec.Z(), vec.W());
                }

                static Vector2 FromGLM(const glm::vec2 &vec)
                {
                    return Vector2(vec.x, vec.y);
                }

                static Vector3 FromGLM(const glm::vec3 &vec)
                {
                    return Vector3(vec.x, vec.y, vec.z);
                }

                static Vector4 FromGLM(const glm::vec4 &vec)
                {
                    return Vector4(vec.x, vec.y, vec.z, vec.w);
                }

                // Matrix转换
                static glm::mat3 ToGLM(const Matrix3 &mat)
                {
                    glm::mat3 result;
                    for (int row = 0; row < 3; ++row)
                    {
                        for (int col = 0; col < 3; ++col)
                        {
                            result[col][row] = mat.Get(row, col);
                        }
                    }
                    return result;
                }

                static glm::mat4 ToGLM(const Matrix4 &mat)
                {
                    glm::mat4 result;
                    for (int row = 0; row < 4; ++row)
                    {
                        for (int col = 0; col < 4; ++col)
                        {
                            result[col][row] = mat.Get(row, col);
                        }
                    }
                    return result;
                }

                static Matrix3 FromGLM(const glm::mat3 &mat)
                {
                    Matrix3 result;
                    for (int row = 0; row < 3; ++row)
                    {
                        for (int col = 0; col < 3; ++col)
                        {
                            result.Set(row, col, mat[col][row]);
                        }
                    }
                    return result;
                }

                static Matrix4 FromGLM(const glm::mat4 &mat)
                {
                    Matrix4 result;
                    for (int row = 0; row < 4; ++row)
                    {
                        for (int col = 0; col < 4; ++col)
                        {
                            result.Set(row, col, mat[col][row]);
                        }
                    }
                    return result;
                }

                // Quaternion转换
                static glm::quat ToGLM(const Quaternion &quat)
                {
                    return glm::quat(quat.W(), quat.X(), quat.Y(), quat.Z());
                }

                static Quaternion FromGLM(const glm::quat &quat)
                {
                    return Quaternion(quat.x, quat.y, quat.z, quat.w);
                }

                // 矩阵分解
                static bool DecomposeMatrix(
                    const glm::mat4 &transform,
                    glm::vec3 &scale,
                    glm::quat &rotation,
                    glm::vec3 &translation,
                    glm::vec3 &skew,
                    glm::vec4 &perspective)
                {
                    return glm::decompose(transform, scale, rotation, translation, skew, perspective);
                }
            };
        }
    }
}
#endif // GLM_UTILS_H