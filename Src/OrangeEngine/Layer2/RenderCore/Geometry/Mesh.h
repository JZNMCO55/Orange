#ifndef ORANGE_RENDERCORE_MESH_H
#define ORANGE_RENDERCORE_MESH_H

#include "Vertex.h"
#include "Layer1/Platform/Graphics/GraphicsInterface/Buffer.h"
#include "Layer1/Core/Math/Math.h"
#include <vector>
#include <memory>
#include <string>

namespace Orange
{
    namespace RenderCore
    {
        /**
         * @brief 包围盒结构体
         * 用于视锥体裁剪和碰撞检测
         */
        struct BoundingBox
        {
            Math::Vec3 min{FLT_MAX, FLT_MAX, FLT_MAX};
            Math::Vec3 max{-FLT_MAX, -FLT_MAX, -FLT_MAX};

            BoundingBox() = default;
            BoundingBox(const Math::Vec3 &minPoint, const Math::Vec3 &maxPoint)
                : min(minPoint), max(maxPoint) {}

            /**
             * @brief 获取包围盒中心点
             */
            Math::Vec3 GetCenter() const { return (min + max) * 0.5f; }

            /**
             * @brief 获取包围盒尺寸
             */
            Math::Vec3 GetSize() const { return max - min; }

            /**
             * @brief 获取包围球半径
             */
            float GetRadius() const { return Math::Length(GetSize()) * 0.5f; }

            /**
             * @brief 扩展包围盒以包含指定点
             */
            void Expand(const Math::Vec3 &point);

            /**
             * @brief 扩展包围盒以包含另一个包围盒
             */
            void Expand(const BoundingBox &other);

            /**
             * @brief 重置包围盒
             */
            void Reset();

            /**
             * @brief 检查包围盒是否有效
             */
            bool IsValid() const;
        };

        /**
         * @brief 网格类
         * 管理几何体的顶点数据、索引数据和GPU缓冲区
         */
        class Mesh
        {
        public:
            /**
             * @brief 默认构造函数
             */
            Mesh();

            /**
             * @brief 析构函数
             */
            ~Mesh();

            /**
             * @brief 移动构造函数
             */
            Mesh(Mesh &&other) noexcept;

            /**
             * @brief 移动赋值运算符
             */
            Mesh &operator=(Mesh &&other) noexcept;

            // 禁用拷贝构造和拷贝赋值
            Mesh(const Mesh &) = delete;
            Mesh &operator=(const Mesh &) = delete;

            /**
             * @brief 设置顶点数据
             * @param vertices 顶点数组
             */
            void SetVertices(const std::vector<Vertex> &vertices);
            void SetVertices(std::vector<Vertex> &&vertices);

            /**
             * @brief 设置索引数据
             * @param indices 索引数组
             */
            void SetIndices(const std::vector<uint32_t> &indices);
            void SetIndices(std::vector<uint32_t> &&indices);

            /**
             * @brief 上传数据到GPU
             * 创建或更新GPU缓冲区
             */
            bool UploadToGPU();

            /**
             * @brief 绑定渲染状态
             * 绑定顶点缓冲区和索引缓冲区
             */
            void Bind() const;

            /**
             * @brief 解绑渲染状态
             */
            void Unbind() const;

            /**
             * @brief 绘制网格
             */
            void Draw() const;

            /**
             * @brief 获取顶点数组
             */
            const std::vector<Vertex> &GetVertices() const { return m_vertices; }
            std::vector<Vertex> &GetVertices() { return m_vertices; }

            /**
             * @brief 获取索引数组
             */
            const std::vector<uint32_t> &GetIndices() const { return m_indices; }
            std::vector<uint32_t> &GetIndices() { return m_indices; }

            /**
             * @brief 获取顶点数量
             */
            size_t GetVertexCount() const { return m_vertices.size(); }

            /**
             * @brief 获取索引数量
             */
            size_t GetIndexCount() const { return m_indices.size(); }

            /**
             * @brief 获取三角形数量
             */
            size_t GetTriangleCount() const { return m_indices.size() / 3; }

            /**
             * @brief 获取包围盒
             */
            const BoundingBox &GetBoundingBox() const { return m_boundingBox; }

            /**
             * @brief 计算并更新包围盒
             */
            void UpdateBoundingBox();

            /**
             * @brief 计算切线向量
             */
            void CalculateTangents();

            /**
             * @brief 计算法线向量（平滑法线）
             */
            void CalculateNormals();

            /**
             * @brief 优化网格（去除重复顶点等）
             */
            void Optimize();

            /**
             * @brief 检查网格是否已上传到GPU
             */
            bool IsUploaded() const { return m_isUploaded; }

            /**
             * @brief 检查网格数据是否有效
             */
            bool IsValid() const;

            /**
             * @brief 获取网格名称
             */
            const std::string &GetName() const { return m_name; }

            /**
             * @brief 设置网格名称
             */
            void SetName(const std::string &name) { m_name = name; }

            /**
             * @brief 清空网格数据
             */
            void Clear();

            /**
             * @brief 获取内存使用量（字节）
             */
            size_t GetMemoryUsage() const;

        private:
            std::vector<Vertex> m_vertices;  // CPU顶点数据
            std::vector<uint32_t> m_indices; // CPU索引数据

            std::unique_ptr<Graphics::Buffer> m_vertexBuffer; // GPU顶点缓冲区
            std::unique_ptr<Graphics::Buffer> m_indexBuffer;  // GPU索引缓冲区

            BoundingBox m_boundingBox;  // 包围盒
            std::string m_name;         // 网格名称
            bool m_isUploaded = false;  // 是否已上传到GPU
            bool m_needsUpdate = false; // 是否需要更新GPU数据

            /**
             * @brief 创建顶点缓冲区
             */
            bool CreateVertexBuffer();

            /**
             * @brief 创建索引缓冲区
             */
            bool CreateIndexBuffer();

            /**
             * @brief 更新顶点缓冲区
             */
            bool UpdateVertexBuffer();

            /**
             * @brief 更新索引缓冲区
             */
            bool UpdateIndexBuffer();
        };

        /**
         * @brief 网格工厂类型
         * 用于标识不同类型的预制网格
         */
        enum class PrimitiveMeshType
        {
            Cube,
            Sphere,
            Plane,
            Cylinder,
            Cone,
            Torus
        };

        /**
         * @brief 网格创建参数
         */
        struct MeshCreateParams
        {
            // 通用参数
            bool generateNormals = true;
            bool generateTangents = true;
            bool generateTexCoords = true;

            // 球体参数
            uint32_t sphereSegments = 32;
            uint32_t sphereRings = 16;

            // 圆柱体参数
            uint32_t cylinderSegments = 32;
            float cylinderHeight = 2.0f;
            float cylinderRadius = 1.0f;

            // 锥体参数
            uint32_t coneSegments = 32;
            float coneHeight = 2.0f;
            float coneRadius = 1.0f;

            // 环形体参数
            uint32_t torusMajorSegments = 32;
            uint32_t torusMinorSegments = 16;
            float torusMajorRadius = 1.0f;
            float torusMinorRadius = 0.3f;

            // 平面参数
            uint32_t planeSubdivisions = 1;
            Math::Vec2 planeSize{2.0f, 2.0f};
        };
    }
}

#endif // ORANGE_RENDERCORE_MESH_H