#ifndef MATH_H
#define MATH_H

namespace Orange::Math
{
    bool DecomposeTransform(const glm::mat4& transform, glm::vec3& outTranslation,
                        glm::vec3& outRotation, glm::vec3& outScale);
}

#endif // MATH_H