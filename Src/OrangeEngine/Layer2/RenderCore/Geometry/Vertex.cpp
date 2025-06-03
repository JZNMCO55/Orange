#include "Vertex.h"
#include <cstring>
#include <cmath>
#include <unordered_map>

namespace Orange
{
    namespace RenderCore
    {
        void Vertex::CalculateTangents(const Vertex &v0, const Vertex &v1, const Vertex &v2,
                                       Math::Vec3 &tangent, Math::Vec3 &bitangent)
        {
            // 计算三角形的边向量
            Math::Vec3 edge1 = v1.position - v0.position;
            Math::Vec3 edge2 = v2.position - v0.position;

            // 计算纹理坐标的边向量
            Math::Vec2 deltaUV1 = v1.texCoord - v0.texCoord;
            Math::Vec2 deltaUV2 = v2.texCoord - v0.texCoord;

            // 计算切线空间的基向量
            float f = 1.0f / (deltaUV1.X() * deltaUV2.Y() - deltaUV2.X() * deltaUV1.Y());

            tangent = Math::Vec3(
                f * (deltaUV2.Y() * edge1.X() - deltaUV1.Y() * edge2.X()),
                f * (deltaUV2.Y() * edge1.Y() - deltaUV1.Y() * edge2.Y()),
                f * (deltaUV2.Y() * edge1.Z() - deltaUV1.Y() * edge2.Z()));

            bitangent = Math::Vec3(
                f * (-deltaUV2.X() * edge1.X() + deltaUV1.X() * edge2.X()),
                f * (-deltaUV2.X() * edge1.Y() + deltaUV1.X() * edge2.Y()),
                f * (-deltaUV2.X() * edge1.Z() + deltaUV1.X() * edge2.Z()));

            // 标准化向量
            tangent = Math::Normalize(tangent);
            bitangent = Math::Normalize(bitangent);
        }

        void Vertex::CalculateTangents(std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices)
        {
            // 为每个顶点初始化切线累积器
            std::vector<Math::Vec3> tangentAccum(vertices.size(), Math::Vec3(0.0f));
            std::vector<Math::Vec3> bitangentAccum(vertices.size(), Math::Vec3(0.0f));
            std::vector<int> tangentCount(vertices.size(), 0);

            // 遍历所有三角形
            for (size_t i = 0; i < indices.size(); i += 3)
            {
                uint32_t i0 = indices[i];
                uint32_t i1 = indices[i + 1];
                uint32_t i2 = indices[i + 2];

                const Vertex &v0 = vertices[i0];
                const Vertex &v1 = vertices[i1];
                const Vertex &v2 = vertices[i2];

                Math::Vec3 tangent, bitangent;
                CalculateTangents(v0, v1, v2, tangent, bitangent);

                // 累积切线向量
                tangentAccum[i0] = tangentAccum[i0] + tangent;
                tangentAccum[i1] = tangentAccum[i1] + tangent;
                tangentAccum[i2] = tangentAccum[i2] + tangent;

                bitangentAccum[i0] = bitangentAccum[i0] + bitangent;
                bitangentAccum[i1] = bitangentAccum[i1] + bitangent;
                bitangentAccum[i2] = bitangentAccum[i2] + bitangent;

                tangentCount[i0]++;
                tangentCount[i1]++;
                tangentCount[i2]++;
            }

            // 平均化并标准化切线向量
            for (size_t i = 0; i < vertices.size(); ++i)
            {
                if (tangentCount[i] > 0)
                {
                    vertices[i].tangent = Math::Normalize(tangentAccum[i] * (1.0f / static_cast<float>(tangentCount[i])));
                    vertices[i].bitangent = Math::Normalize(bitangentAccum[i] * (1.0f / static_cast<float>(tangentCount[i])));

                    // 使用Gram-Schmidt正交化过程确保切线向量垂直于法线
                    vertices[i].tangent = Math::Normalize(vertices[i].tangent -
                                                          vertices[i].normal * Math::Dot(vertices[i].tangent, vertices[i].normal));

                    // 确保切线空间是右手坐标系
                    Math::Vec3 cross = Math::Cross(vertices[i].normal, vertices[i].tangent);
                    if (Math::Dot(cross, vertices[i].bitangent) < 0.0f)
                    {
                        vertices[i].tangent = Math::Vec3(0.0f) - vertices[i].tangent;
                    }

                    // 重新计算副切线确保正交性
                    vertices[i].bitangent = Math::Cross(vertices[i].normal, vertices[i].tangent);
                }
            }
        }

        bool Vertex::operator==(const Vertex &other) const
        {
            const float epsilon = 1e-6f;

            return Math::Length(position - other.position) < epsilon &&
                   Math::Length(normal - other.normal) < epsilon &&
                   Math::Length(Math::Vec3(texCoord.X() - other.texCoord.X(), texCoord.Y() - other.texCoord.Y(), 0.0f)) < epsilon &&
                   Math::Length(tangent - other.tangent) < epsilon &&
                   Math::Length(bitangent - other.bitangent) < epsilon &&
                   Math::Length(Math::Vec3(color.X() - other.color.X(), color.Y() - other.color.Y(), color.Z() - other.color.Z())) < epsilon;
        }

        void VertexInputLayout::AddAttribute(uint32_t location, uint32_t offset, uint32_t size,
                                             uint32_t componentCount, VertexAttribute::Type type)
        {
            VertexAttribute attr;
            attr.location = location;
            attr.offset = offset;
            attr.size = size;
            attr.componentCount = componentCount;
            attr.type = type;

            m_attributes.push_back(attr);
        }

        VertexInputLayout VertexInputLayout::GetStandardLayout()
        {
            VertexInputLayout layout;
            layout.SetStride(sizeof(Vertex));

            // Position (location 0) - Vec3
            layout.AddAttribute(0, offsetof(Vertex, position), sizeof(Math::Vec3), 3, VertexAttribute::Type::Float);

            // Normal (location 1) - Vec3
            layout.AddAttribute(1, offsetof(Vertex, normal), sizeof(Math::Vec3), 3, VertexAttribute::Type::Float);

            // TexCoord (location 2) - Vec2
            layout.AddAttribute(2, offsetof(Vertex, texCoord), sizeof(Math::Vec2), 2, VertexAttribute::Type::Float);

            // Tangent (location 3) - Vec3
            layout.AddAttribute(3, offsetof(Vertex, tangent), sizeof(Math::Vec3), 3, VertexAttribute::Type::Float);

            // Bitangent (location 4) - Vec3
            layout.AddAttribute(4, offsetof(Vertex, bitangent), sizeof(Math::Vec3), 3, VertexAttribute::Type::Float);

            // Color (location 5) - Vec4
            layout.AddAttribute(5, offsetof(Vertex, color), sizeof(Math::Vec4), 4, VertexAttribute::Type::Float);

            return layout;
        }

        VertexInputLayout VertexInputLayout::GetSimpleLayout()
        {
            VertexInputLayout layout;
            layout.SetStride(sizeof(SimpleVertex));

            // Position (location 0) - Vec3
            layout.AddAttribute(0, offsetof(SimpleVertex, position), sizeof(Math::Vec3), 3, VertexAttribute::Type::Float);

            // Color (location 1) - Vec4
            layout.AddAttribute(1, offsetof(SimpleVertex, color), sizeof(Math::Vec4), 4, VertexAttribute::Type::Float);

            return layout;
        }
    }
}