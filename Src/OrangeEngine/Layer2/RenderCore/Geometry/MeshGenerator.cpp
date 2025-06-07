#include "MeshGenerator.h"
#include <cmath>

namespace Orange
{
    namespace RenderCore
    {
        std::unique_ptr<Mesh> MeshGenerator::CreatePrimitive(PrimitiveMeshType type, const MeshCreateParams &params)
        {
            switch (type)
            {
            case PrimitiveMeshType::Cube:
                return CreateCube(Math::Vec3(1.0f), params);
            case PrimitiveMeshType::Sphere:
                return CreateSphere(1.0f, params.sphereSegments, params.sphereRings, params);
            case PrimitiveMeshType::Plane:
                return CreatePlane(params.planeSize, params.planeSubdivisions, params);
            case PrimitiveMeshType::Cylinder:
                return CreateCylinder(params.cylinderRadius, params.cylinderHeight, params.cylinderSegments, params);
            case PrimitiveMeshType::Cone:
                return CreateCone(params.coneRadius, params.coneHeight, params.coneSegments, params);
            case PrimitiveMeshType::Torus:
                return CreateTorus(params.torusMajorRadius, params.torusMinorRadius,
                                   params.torusMajorSegments, params.torusMinorSegments, params);
            default:
                return CreateCube(Math::Vec3(1.0f), params);
            }
        }

        std::unique_ptr<Mesh> MeshGenerator::CreateCube(const Math::Vec3 &size, const MeshCreateParams &params)
        {
            auto mesh = std::make_unique<Mesh>();
            mesh->SetName("Cube");

            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            // 立方体的8个顶点（标准单位立方体，然后缩放）
            Math::Vec3 halfSize = size * 0.5f;

            // 定义立方体的6个面
            // 前面 (Z+)
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), -halfSize.Y(), halfSize.Z()), Math::Vec3(0, 0, 1), Math::Vec2(0, 0)});
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), -halfSize.Y(), halfSize.Z()), Math::Vec3(0, 0, 1), Math::Vec2(1, 0)});
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), halfSize.Y(), halfSize.Z()), Math::Vec3(0, 0, 1), Math::Vec2(1, 1)});
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), halfSize.Y(), halfSize.Z()), Math::Vec3(0, 0, 1), Math::Vec2(0, 1)});

            // 后面 (Z-)
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), -halfSize.Y(), -halfSize.Z()), Math::Vec3(0, 0, -1), Math::Vec2(0, 0)});
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), -halfSize.Y(), -halfSize.Z()), Math::Vec3(0, 0, -1), Math::Vec2(1, 0)});
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), halfSize.Y(), -halfSize.Z()), Math::Vec3(0, 0, -1), Math::Vec2(1, 1)});
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), halfSize.Y(), -halfSize.Z()), Math::Vec3(0, 0, -1), Math::Vec2(0, 1)});

            // 右面 (X+)
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), -halfSize.Y(), halfSize.Z()), Math::Vec3(1, 0, 0), Math::Vec2(0, 0)});
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), -halfSize.Y(), -halfSize.Z()), Math::Vec3(1, 0, 0), Math::Vec2(1, 0)});
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), halfSize.Y(), -halfSize.Z()), Math::Vec3(1, 0, 0), Math::Vec2(1, 1)});
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), halfSize.Y(), halfSize.Z()), Math::Vec3(1, 0, 0), Math::Vec2(0, 1)});

            // 左面 (X-)
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), -halfSize.Y(), -halfSize.Z()), Math::Vec3(-1, 0, 0), Math::Vec2(0, 0)});
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), -halfSize.Y(), halfSize.Z()), Math::Vec3(-1, 0, 0), Math::Vec2(1, 0)});
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), halfSize.Y(), halfSize.Z()), Math::Vec3(-1, 0, 0), Math::Vec2(1, 1)});
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), halfSize.Y(), -halfSize.Z()), Math::Vec3(-1, 0, 0), Math::Vec2(0, 1)});

            // 上面 (Y+)
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), halfSize.Y(), halfSize.Z()), Math::Vec3(0, 1, 0), Math::Vec2(0, 0)});
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), halfSize.Y(), halfSize.Z()), Math::Vec3(0, 1, 0), Math::Vec2(1, 0)});
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), halfSize.Y(), -halfSize.Z()), Math::Vec3(0, 1, 0), Math::Vec2(1, 1)});
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), halfSize.Y(), -halfSize.Z()), Math::Vec3(0, 1, 0), Math::Vec2(0, 1)});

            // 下面 (Y-)
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), -halfSize.Y(), -halfSize.Z()), Math::Vec3(0, -1, 0), Math::Vec2(0, 0)});
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), -halfSize.Y(), -halfSize.Z()), Math::Vec3(0, -1, 0), Math::Vec2(1, 0)});
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), -halfSize.Y(), halfSize.Z()), Math::Vec3(0, -1, 0), Math::Vec2(1, 1)});
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), -halfSize.Y(), halfSize.Z()), Math::Vec3(0, -1, 0), Math::Vec2(0, 1)});

            // 索引数据（每个面2个三角形）
            for (uint32_t face = 0; face < 6; ++face)
            {
                uint32_t baseVertex = face * 4;
                indices.push_back(baseVertex + 0);
                indices.push_back(baseVertex + 1);
                indices.push_back(baseVertex + 2);
                indices.push_back(baseVertex + 0);
                indices.push_back(baseVertex + 2);
                indices.push_back(baseVertex + 3);
            }

            mesh->SetVertices(std::move(vertices));
            mesh->SetIndices(std::move(indices));

            if (params.generateTangents)
            {
                mesh->CalculateTangents();
            }

            return mesh;
        }

        std::unique_ptr<Mesh> MeshGenerator::CreateSphere(float radius, uint32_t segments, uint32_t rings, const MeshCreateParams &params)
        {
            auto mesh = std::make_unique<Mesh>();
            mesh->SetName("Sphere");

            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            // 生成球体顶点
            for (uint32_t ring = 0; ring <= rings; ++ring)
            {
                float phi = static_cast<float>(ring) / static_cast<float>(rings) * Math::PI;
                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);

                for (uint32_t segment = 0; segment <= segments; ++segment)
                {
                    float theta = static_cast<float>(segment) / static_cast<float>(segments) * 2.0f * Math::PI;
                    float sinTheta = std::sin(theta);
                    float cosTheta = std::cos(theta);

                    // 球面坐标转笛卡尔坐标
                    Math::Vec3 position = SphericalToCartesian(theta, phi, radius);
                    Math::Vec3 normal = Math::Normalize(position);

                    // 球面纹理坐标
                    Math::Vec2 texCoord(
                        static_cast<float>(segment) / static_cast<float>(segments),
                        static_cast<float>(ring) / static_cast<float>(rings));

                    vertices.push_back(Vertex{position, normal, texCoord});
                }
            }

            // 生成索引
            for (uint32_t ring = 0; ring < rings; ++ring)
            {
                for (uint32_t segment = 0; segment < segments; ++segment)
                {
                    uint32_t current = ring * (segments + 1) + segment;
                    uint32_t next = current + segments + 1;

                    // 第一个三角形
                    indices.push_back(current);
                    indices.push_back(next);
                    indices.push_back(current + 1);

                    // 第二个三角形
                    indices.push_back(current + 1);
                    indices.push_back(next);
                    indices.push_back(next + 1);
                }
            }

            mesh->SetVertices(std::move(vertices));
            mesh->SetIndices(std::move(indices));

            if (params.generateTangents)
            {
                mesh->CalculateTangents();
            }

            return mesh;
        }

        std::unique_ptr<Mesh> MeshGenerator::CreatePlane(const Math::Vec2 &size, uint32_t subdivisions, const MeshCreateParams &params)
        {
            auto mesh = std::make_unique<Mesh>();
            mesh->SetName("Plane");

            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            Math::Vec2 halfSize = size * 0.5f;
            uint32_t verticesPerRow = subdivisions + 2;

            // 生成平面顶点
            for (uint32_t z = 0; z < verticesPerRow; ++z)
            {
                for (uint32_t x = 0; x < verticesPerRow; ++x)
                {
                    float u = static_cast<float>(x) / static_cast<float>(subdivisions + 1);
                    float v = static_cast<float>(z) / static_cast<float>(subdivisions + 1);

                    Math::Vec3 position(
                        (u - 0.5f) * size.X(),
                        0.0f,
                        (v - 0.5f) * size.Y());

                    Math::Vec3 normal(0.0f, 1.0f, 0.0f);
                    Math::Vec2 texCoord(u, v);

                    vertices.push_back(Vertex{position, normal, texCoord});
                }
            }

            // 生成索引
            for (uint32_t z = 0; z < subdivisions + 1; ++z)
            {
                for (uint32_t x = 0; x < subdivisions + 1; ++x)
                {
                    uint32_t topLeft = z * verticesPerRow + x;
                    uint32_t topRight = topLeft + 1;
                    uint32_t bottomLeft = (z + 1) * verticesPerRow + x;
                    uint32_t bottomRight = bottomLeft + 1;

                    // 第一个三角形
                    indices.push_back(topLeft);
                    indices.push_back(bottomLeft);
                    indices.push_back(topRight);

                    // 第二个三角形
                    indices.push_back(topRight);
                    indices.push_back(bottomLeft);
                    indices.push_back(bottomRight);
                }
            }

            mesh->SetVertices(std::move(vertices));
            mesh->SetIndices(std::move(indices));

            if (params.generateTangents)
            {
                mesh->CalculateTangents();
            }

            return mesh;
        }

        std::unique_ptr<Mesh> MeshGenerator::CreateTriangle(float size)
        {
            auto mesh = std::make_unique<Mesh>();
            mesh->SetName("Triangle");

            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            // 等边三角形的三个顶点
            float height = size * std::sqrt(3.0f) / 2.0f;
            vertices.push_back(Vertex{Math::Vec3(0.0f, height * 2.0f / 3.0f, 0.0f), Math::Vec3(0, 0, 1), Math::Vec2(0.5f, 1.0f)});
            vertices.push_back(Vertex{Math::Vec3(-size * 0.5f, -height / 3.0f, 0.0f), Math::Vec3(0, 0, 1), Math::Vec2(0.0f, 0.0f)});
            vertices.push_back(Vertex{Math::Vec3(size * 0.5f, -height / 3.0f, 0.0f), Math::Vec3(0, 0, 1), Math::Vec2(1.0f, 0.0f)});

            indices = {0, 1, 2};

            mesh->SetVertices(std::move(vertices));
            mesh->SetIndices(std::move(indices));

            return mesh;
        }

        std::unique_ptr<Mesh> MeshGenerator::CreateQuad(const Math::Vec2 &size)
        {
            auto mesh = std::make_unique<Mesh>();
            mesh->SetName("Quad");

            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            Math::Vec2 halfSize = size * 0.5f;

            // 四个顶点
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), -halfSize.Y(), 0.0f), Math::Vec3(0, 0, 1), Math::Vec2(0, 0)});
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), -halfSize.Y(), 0.0f), Math::Vec3(0, 0, 1), Math::Vec2(1, 0)});
            vertices.push_back(Vertex{Math::Vec3(halfSize.X(), halfSize.Y(), 0.0f), Math::Vec3(0, 0, 1), Math::Vec2(1, 1)});
            vertices.push_back(Vertex{Math::Vec3(-halfSize.X(), halfSize.Y(), 0.0f), Math::Vec3(0, 0, 1), Math::Vec2(0, 1)});

            indices = {0, 1, 2, 0, 2, 3};

            mesh->SetVertices(std::move(vertices));
            mesh->SetIndices(std::move(indices));

            return mesh;
        }

        // 辅助函数实现
        Math::Vec3 MeshGenerator::SphericalToCartesian(float theta, float phi, float radius)
        {
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            return Math::Vec3(
                radius * sinPhi * cosTheta,
                radius * cosPhi,
                radius * sinPhi * sinTheta);
        }

        // 暂时使用空实现的其他函数
        std::unique_ptr<Mesh> MeshGenerator::CreateCylinder(float radius, float height, uint32_t segments, const MeshCreateParams &params)
        {
            // TODO: 实现圆柱体生成
            return CreateCube(Math::Vec3(1.0f), params);
        }

        std::unique_ptr<Mesh> MeshGenerator::CreateCone(float radius, float height, uint32_t segments, const MeshCreateParams &params)
        {
            // TODO: 实现锥体生成
            return CreateCube(Math::Vec3(1.0f), params);
        }

        std::unique_ptr<Mesh> MeshGenerator::CreateTorus(float majorRadius, float minorRadius, uint32_t majorSegments, uint32_t minorSegments, const MeshCreateParams &params)
        {
            // TODO: 实现环形体生成
            return CreateCube(Math::Vec3(1.0f), params);
        }

        std::unique_ptr<Mesh> MeshGenerator::CreateGrid(float width, float height, uint32_t widthSegments, uint32_t heightSegments, const MeshCreateParams &params)
        {
            // TODO: 实现网格生成
            return CreatePlane(Math::Vec2(width, height), std::max(widthSegments, heightSegments), params);
        }

        std::unique_ptr<Mesh> MeshGenerator::CreateTeapot(const MeshCreateParams &params)
        {
            // TODO: 实现茶壶生成
            return CreateSphere(1.0f, 16, 8, params);
        }

        std::unique_ptr<Mesh> MeshGenerator::CreateWireCube(const Math::Vec3 &size)
        {
            // TODO: 实现线框立方体
            return CreateCube(size);
        }

        std::unique_ptr<Mesh> MeshGenerator::CreateAxis(float length)
        {
            // TODO: 实现坐标轴
            return CreateTriangle(length);
        }

        void MeshGenerator::GenerateTangents(Mesh *mesh, const MeshCreateParams &params)
        {
            if (mesh && params.generateTangents)
            {
                mesh->CalculateTangents();
            }
        }

        void MeshGenerator::GenerateNormals(Mesh *mesh, const MeshCreateParams &params)
        {
            if (mesh && params.generateNormals)
            {
                mesh->CalculateNormals();
            }
        }

        void MeshGenerator::GenerateTexCoords(Mesh *mesh, const MeshCreateParams &params)
        {
            // 纹理坐标在生成时已经设置，这里可以做后处理
        }

        Math::Vec2 MeshGenerator::CalculatePlanarTexCoords(const Math::Vec3 &position, const Math::Vec3 &normal)
        {
            // 平面纹理坐标映射
            return Math::Vec2(position.X(), position.Z());
        }

        Math::Vec2 MeshGenerator::CalculateSphericalTexCoords(const Math::Vec3 &position)
        {
            // 球面纹理坐标映射
            Math::Vec3 normalized = Math::Normalize(position);
            float u = 0.5f + std::atan2(normalized.Z(), normalized.X()) / (2.0f * Math::PI);
            float v = 0.5f - std::asin(normalized.Y()) / Math::PI;
            return Math::Vec2(u, v);
        }

        Math::Vec2 MeshGenerator::CalculateCylindricalTexCoords(const Math::Vec3 &position, float height)
        {
            // 圆柱形纹理坐标映射
            float u = 0.5f + std::atan2(position.Z(), position.X()) / (2.0f * Math::PI);
            float v = (position.Y() + height * 0.5f) / height;
            return Math::Vec2(u, v);
        }
    }
}
