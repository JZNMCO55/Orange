#ifndef ORANGE_RENDERCORE_VERTEX_H
#define ORANGE_RENDERCORE_VERTEX_H

#include "Layer1/Core/Math/Math.h"
#include <vector>
#include <array>

namespace Orange
{
    namespace RenderCore
    {
        /**
         * @brief 标准顶点结构体，用于PBR渲染
         * 包含位置、法线、纹理坐标、切线等PBR渲染所需的完整属性
         */
        struct Vertex
        {
            Math::Vec3 position{0.0f, 0.0f, 0.0f};    // 顶点位置
            Math::Vec3 normal{0.0f, 1.0f, 0.0f};      // 法线向量
            Math::Vec2 texCoord{0.0f, 0.0f};          // 纹理坐标
            Math::Vec3 tangent{1.0f, 0.0f, 0.0f};     // 切线向量（用于法线贴图）
            Math::Vec3 bitangent{0.0f, 0.0f, 1.0f};   // 副切线向量
            Math::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; // 顶点颜色

            /**
             * @brief 默认构造函数
             */
            Vertex() = default;

            /**
             * @brief 位置构造函数
             * @param pos 顶点位置
             */
            explicit Vertex(const Math::Vec3 &pos) : position(pos) {}

            /**
             * @brief 完整构造函数
             * @param pos 顶点位置
             * @param norm 法线向量
             * @param tex 纹理坐标
             */
            Vertex(const Math::Vec3 &pos, const Math::Vec3 &norm, const Math::Vec2 &tex)
                : position(pos), normal(norm), texCoord(tex) {}

            /**
             * @brief 计算切线和副切线向量
             * @param v0, v1, v2 三角形的三个顶点
             * @param tangent 输出切线向量
             * @param bitangent 输出副切线向量
             */
            static void CalculateTangents(const Vertex &v0, const Vertex &v1, const Vertex &v2,
                                          Math::Vec3 &tangent, Math::Vec3 &bitangent);

            /**
             * @brief 为一组顶点计算切线和副切线
             * @param vertices 顶点数组
             * @param indices 索引数组
             */
            static void CalculateTangents(std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices);

            /**
             * @brief 检查两个顶点是否相等
             */
            bool operator==(const Vertex &other) const;
            bool operator!=(const Vertex &other) const { return !(*this == other); }
        };

        /**
         * @brief 简化顶点结构体（仅位置和颜色）
         * 用于简单的线框渲染或调试可视化
         */
        struct SimpleVertex
        {
            Math::Vec3 position{0.0f, 0.0f, 0.0f};
            Math::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

            SimpleVertex() = default;
            SimpleVertex(const Math::Vec3 &pos, const Math::Vec4 &col = Math::Vec4(1.0f))
                : position(pos), color(col) {}
        };

        /**
         * @brief 顶点属性描述
         * 用于描述顶点数据在GPU中的布局
         */
        struct VertexAttribute
        {
            uint32_t location;       // 着色器中的location
            uint32_t offset;         // 在顶点结构中的偏移量
            uint32_t size;           // 属性大小（字节）
            uint32_t componentCount; // 组件数量（如Vec3是3个）

            enum class Type
            {
                Float,
                Int,
                UInt
            } type;
        };

        /**
         * @brief 顶点输入布局描述
         * 定义完整的顶点数据布局，用于创建渲染管线
         */
        class VertexInputLayout
        {
        public:
            VertexInputLayout() = default;

            /**
             * @brief 添加顶点属性
             * @param location 着色器location
             * @param offset 偏移量
             * @param size 大小
             * @param componentCount 组件数
             * @param type 数据类型
             */
            void AddAttribute(uint32_t location, uint32_t offset, uint32_t size,
                              uint32_t componentCount, VertexAttribute::Type type);

            /**
             * @brief 获取标准Vertex结构的布局
             */
            static VertexInputLayout GetStandardLayout();

            /**
             * @brief 获取SimpleVertex结构的布局
             */
            static VertexInputLayout GetSimpleLayout();

            const std::vector<VertexAttribute> &GetAttributes() const { return m_attributes; }
            uint32_t GetStride() const { return m_stride; }
            void SetStride(uint32_t stride) { m_stride = stride; }

        private:
            std::vector<VertexAttribute> m_attributes;
            uint32_t m_stride = 0;
        };
    }
}

#endif // ORANGE_RENDERCORE_VERTEX_H