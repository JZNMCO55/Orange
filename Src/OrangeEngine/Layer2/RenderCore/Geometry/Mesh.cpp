#include "Mesh.h"
#include "Layer1/Platform/Graphics/GraphicsInterface/GraphicsSystem.h"
#include "Layer1/Core/Application/Application.h"
#include <algorithm>
#include <unordered_map>
#include <climits>

namespace Orange
{
    namespace RenderCore
    {
        // ==================== BoundingBox Implementation ====================

        void BoundingBox::Expand(const Math::Vec3 &point)
        {
            min = Math::Vec3(
                std::min(min.X(), point.X()),
                std::min(min.Y(), point.Y()),
                std::min(min.Z(), point.Z()));

            max = Math::Vec3(
                std::max(max.X(), point.X()),
                std::max(max.Y(), point.Y()),
                std::max(max.Z(), point.Z()));
        }

        void BoundingBox::Expand(const BoundingBox &other)
        {
            if (!other.IsValid())
                return;

            min = Math::Vec3(
                std::min(min.X(), other.min.X()),
                std::min(min.Y(), other.min.Y()),
                std::min(min.Z(), other.min.Z()));

            max = Math::Vec3(
                std::max(max.X(), other.max.X()),
                std::max(max.Y(), other.max.Y()),
                std::max(max.Z(), other.max.Z()));
        }

        void BoundingBox::Reset()
        {
            min = Math::Vec3(FLT_MAX, FLT_MAX, FLT_MAX);
            max = Math::Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        }

        bool BoundingBox::IsValid() const
        {
            return min.X() <= max.X() && min.Y() <= max.Y() && min.Z() <= max.Z();
        }

        // ==================== Mesh Implementation ====================

        Mesh::Mesh()
            : m_name("Unnamed Mesh")
        {
            m_boundingBox.Reset();
        }

        Mesh::~Mesh()
        {
            // 析构函数会自动清理unique_ptr管理的资源
        }

        Mesh::Mesh(Mesh &&other) noexcept
            : m_vertices(std::move(other.m_vertices)), m_indices(std::move(other.m_indices)), m_vertexBuffer(std::move(other.m_vertexBuffer)), m_indexBuffer(std::move(other.m_indexBuffer)), m_boundingBox(other.m_boundingBox), m_name(std::move(other.m_name)), m_isUploaded(other.m_isUploaded), m_needsUpdate(other.m_needsUpdate)
        {
            other.m_isUploaded = false;
            other.m_needsUpdate = false;
            other.m_boundingBox.Reset();
        }

        Mesh &Mesh::operator=(Mesh &&other) noexcept
        {
            if (this != &other)
            {
                m_vertices = std::move(other.m_vertices);
                m_indices = std::move(other.m_indices);
                m_vertexBuffer = std::move(other.m_vertexBuffer);
                m_indexBuffer = std::move(other.m_indexBuffer);
                m_boundingBox = other.m_boundingBox;
                m_name = std::move(other.m_name);
                m_isUploaded = other.m_isUploaded;
                m_needsUpdate = other.m_needsUpdate;

                other.m_isUploaded = false;
                other.m_needsUpdate = false;
                other.m_boundingBox.Reset();
            }
            return *this;
        }

        void Mesh::SetVertices(const std::vector<Vertex> &vertices)
        {
            m_vertices = vertices;
            m_needsUpdate = true;
            UpdateBoundingBox();
        }

        void Mesh::SetVertices(std::vector<Vertex> &&vertices)
        {
            m_vertices = std::move(vertices);
            m_needsUpdate = true;
            UpdateBoundingBox();
        }

        void Mesh::SetIndices(const std::vector<uint32_t> &indices)
        {
            m_indices = indices;
            m_needsUpdate = true;
        }

        void Mesh::SetIndices(std::vector<uint32_t> &&indices)
        {
            m_indices = std::move(indices);
            m_needsUpdate = true;
        }

        bool Mesh::UploadToGPU()
        {
            if (!IsValid())
            {
                return false;
            }

            bool success = true;

            // 创建或更新顶点缓冲区
            if (!m_vertexBuffer)
            {
                success &= CreateVertexBuffer();
            }
            else if (m_needsUpdate)
            {
                success &= UpdateVertexBuffer();
            }

            // 创建或更新索引缓冲区
            if (!m_indices.empty())
            {
                if (!m_indexBuffer)
                {
                    success &= CreateIndexBuffer();
                }
                else if (m_needsUpdate)
                {
                    success &= UpdateIndexBuffer();
                }
            }

            if (success)
            {
                m_isUploaded = true;
                m_needsUpdate = false;
            }

            return success;
        }

        void Mesh::Bind() const
        {
            // 这里需要实际的图形API绑定操作
            // 由于我们使用接口抽象，具体实现会在渲染器中完成
            // 这个方法主要用于验证网格状态
            if (!m_isUploaded)
            {
                // 记录警告：网格尚未上传到GPU
            }
        }

        void Mesh::Unbind() const
        {
            // 解绑操作
        }

        void Mesh::Draw() const
        {
            if (!m_isUploaded || !IsValid())
            {
                return;
            }

            // 这里需要实际的绘制命令
            // 具体实现会在渲染器中完成
        }

        void Mesh::UpdateBoundingBox()
        {
            m_boundingBox.Reset();

            if (m_vertices.empty())
            {
                return;
            }

            for (const auto &vertex : m_vertices)
            {
                m_boundingBox.Expand(vertex.position);
            }
        }

        void Mesh::CalculateTangents()
        {
            if (m_vertices.empty() || m_indices.empty())
            {
                return;
            }

            Vertex::CalculateTangents(m_vertices, m_indices);
            m_needsUpdate = true;
        }

        void Mesh::CalculateNormals()
        {
            if (m_vertices.empty() || m_indices.empty())
            {
                return;
            }

            // 首先重置所有法线
            for (auto &vertex : m_vertices)
            {
                vertex.normal = Math::Vec3(0.0f);
            }

            // 计算每个三角形的法线并累积到顶点
            for (size_t i = 0; i < m_indices.size(); i += 3)
            {
                uint32_t i0 = m_indices[i];
                uint32_t i1 = m_indices[i + 1];
                uint32_t i2 = m_indices[i + 2];

                if (i0 >= m_vertices.size() || i1 >= m_vertices.size() || i2 >= m_vertices.size())
                {
                    continue;
                }

                const Math::Vec3 &v0 = m_vertices[i0].position;
                const Math::Vec3 &v1 = m_vertices[i1].position;
                const Math::Vec3 &v2 = m_vertices[i2].position;

                // 计算三角形法线
                Math::Vec3 edge1 = v1 - v0;
                Math::Vec3 edge2 = v2 - v0;
                Math::Vec3 normal = Math::Cross(edge1, edge2);

                // 累积到顶点法线
                m_vertices[i0].normal += normal;
                m_vertices[i1].normal += normal;
                m_vertices[i2].normal += normal;
            }

            // 标准化法线向量
            for (auto &vertex : m_vertices)
            {
                vertex.normal = Math::Normalize(vertex.normal);
            }

            m_needsUpdate = true;
        }

        void Mesh::Optimize()
        {
            if (m_vertices.empty())
            {
                return;
            }

            // 使用哈希表来查找重复顶点
            std::unordered_map<size_t, uint32_t> vertexMap;
            std::vector<Vertex> optimizedVertices;
            std::vector<uint32_t> indexRemap(m_vertices.size());

            // 简单的顶点哈希函数
            auto vertexHash = [](const Vertex &v) -> size_t
            {
                size_t h1 = std::hash<float>{}(v.position.X());
                size_t h2 = std::hash<float>{}(v.position.Y());
                size_t h3 = std::hash<float>{}(v.position.Z());
                size_t h4 = std::hash<float>{}(v.texCoord.X());
                size_t h5 = std::hash<float>{}(v.texCoord.Y());
                return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4);
            };

            // 去除重复顶点
            for (size_t i = 0; i < m_vertices.size(); ++i)
            {
                size_t hash = vertexHash(m_vertices[i]);
                auto it = vertexMap.find(hash);

                if (it != vertexMap.end())
                {
                    // 找到重复顶点，使用现有索引
                    indexRemap[i] = it->second;
                }
                else
                {
                    // 新顶点，添加到优化列表
                    uint32_t newIndex = static_cast<uint32_t>(optimizedVertices.size());
                    optimizedVertices.push_back(m_vertices[i]);
                    vertexMap[hash] = newIndex;
                    indexRemap[i] = newIndex;
                }
            }

            // 更新索引数组
            for (auto &index : m_indices)
            {
                if (index < indexRemap.size())
                {
                    index = indexRemap[index];
                }
            }

            // 应用优化结果
            m_vertices = std::move(optimizedVertices);
            m_needsUpdate = true;

            UpdateBoundingBox();
        }

        bool Mesh::IsValid() const
        {
            return !m_vertices.empty() &&
                   (m_indices.empty() || m_indices.size() % 3 == 0);
        }

        void Mesh::Clear()
        {
            m_vertices.clear();
            m_indices.clear();
            m_vertexBuffer.reset();
            m_indexBuffer.reset();
            m_boundingBox.Reset();
            m_isUploaded = false;
            m_needsUpdate = false;
        }

        size_t Mesh::GetMemoryUsage() const
        {
            size_t cpuMemory = m_vertices.size() * sizeof(Vertex) +
                               m_indices.size() * sizeof(uint32_t);

            size_t gpuMemory = 0;
            if (m_vertexBuffer)
            {
                gpuMemory += m_vertexBuffer->GetSize();
            }
            if (m_indexBuffer)
            {
                gpuMemory += m_indexBuffer->GetSize();
            }

            return cpuMemory + gpuMemory;
        }

        bool Mesh::CreateVertexBuffer()
        {
            if (m_vertices.empty())
            {
                return false;
            }

            // 这里需要通过图形系统创建缓冲区
            // 由于我们还没有完整的渲染系统，先返回true
            // TODO: 实现实际的缓冲区创建

            // auto* graphicsSystem = Application::GetInstance().GetGraphicsSystem();
            // if (!graphicsSystem)
            // {
            //     return false;
            // }

            // BufferCreateInfo createInfo{};
            // createInfo.size = m_vertices.size() * sizeof(Vertex);
            // createInfo.usage = BufferUsage::Vertex;
            // createInfo.memoryProps = MemoryProperty::HostVisible | MemoryProperty::HostCoherent;

            // m_vertexBuffer = graphicsSystem->GetRenderDevice()->CreateBuffer(createInfo);
            // if (!m_vertexBuffer)
            // {
            //     return false;
            // }

            // m_vertexBuffer->CopyFrom(m_vertices.data(), createInfo.size);

            return true;
        }

        bool Mesh::CreateIndexBuffer()
        {
            if (m_indices.empty())
            {
                return false;
            }

            // TODO: 实现实际的索引缓冲区创建
            return true;
        }

        bool Mesh::UpdateVertexBuffer()
        {
            if (!m_vertexBuffer || m_vertices.empty())
            {
                return CreateVertexBuffer();
            }

            // TODO: 更新现有缓冲区数据
            return true;
        }

        bool Mesh::UpdateIndexBuffer()
        {
            if (!m_indexBuffer || m_indices.empty())
            {
                return CreateIndexBuffer();
            }

            // TODO: 更新现有缓冲区数据
            return true;
        }
    }
}