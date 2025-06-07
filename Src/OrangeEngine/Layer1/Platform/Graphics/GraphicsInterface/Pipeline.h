#ifndef ORANGE_GRAPHICS_PIPELINE_H
#define ORANGE_GRAPHICS_PIPELINE_H

#include <cstdint>
#include <string>

namespace Orange::Graphics
{

    /**
     * @brief 渲染管线接口
     * 提供图形渲染管线的基本操作功能
     */
    class Pipeline
    {
    public:
        virtual ~Pipeline() = default;

        /**
         * @brief 绑定管线到当前渲染上下文
         */
        virtual void Bind() = 0;

        /**
         * @brief 设置统一变量
         * @param name 变量名称
         * @param data 数据指针
         * @param size 数据大小（字节）
         */
        virtual void SetUniform(const std::string &name, const void *data, size_t size) = 0;

        /**
         * @brief 设置整型统一变量
         * @param name 变量名称
         * @param value 值
         */
        virtual void SetUniformInt(const std::string &name, int32_t value) = 0;

        /**
         * @brief 设置浮点型统一变量
         * @param name 变量名称
         * @param value 值
         */
        virtual void SetUniformFloat(const std::string &name, float value) = 0;

        /**
         * @brief 设置向量统一变量
         * @param name 变量名称
         * @param x X分量
         * @param y Y分量
         * @param z Z分量
         * @param w W分量
         */
        virtual void SetUniformVector4(const std::string &name, float x, float y, float z, float w) = 0;

        /**
         * @brief 设置矩阵统一变量
         * @param name 变量名称
         * @param matrix 4x4矩阵数据指针
         */
        virtual void SetUniformMatrix4(const std::string &name, const float *matrix) = 0;

        /**
         * @brief 绑定纹理到指定插槽
         * @param slot 纹理插槽
         * @param texture 纹理指针
         */
        virtual void BindTexture(uint32_t slot, class Texture *texture) = 0;

        /**
         * @brief 绑定缓冲区到指定插槽
         * @param slot 缓冲区插槽
         * @param buffer 缓冲区指针
         */
        virtual void BindBuffer(uint32_t slot, class Buffer *buffer) = 0;

        /**
         * @brief 绘制图元
         * @param vertexCount 顶点数量
         * @param instanceCount 实例数量
         * @param firstVertex 起始顶点索引
         * @param firstInstance 起始实例索引
         */
        virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                          uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;

        /**
         * @brief 索引绘制图元
         * @param indexCount 索引数量
         * @param instanceCount 实例数量
         * @param firstIndex 起始索引
         * @param vertexOffset 顶点偏移
         * @param firstInstance 起始实例索引
         */
        virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                                 uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                                 uint32_t firstInstance = 0) = 0;

        /**
         * @brief 获取管线调试名称
         * @return 调试名称字符串
         */
        virtual const std::string &GetDebugName() const = 0;

        /**
         * @brief 检查管线是否有效
         * @return 有效返回true
         */
        virtual bool IsValid() const = 0;

        /**
         * @brief 重新编译管线
         * 当着色器或状态发生变化时重新编译
         * @return 成功返回true
         */
        virtual bool Recompile() = 0;
    };

} // namespace Orange::Graphics

#endif // ORANGE_GRAPHICS_PIPELINE_H