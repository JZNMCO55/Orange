#ifndef RENDER_COMMAND_BUFFER_H
#define RENDER_COMMAND_BUFFER_H

#include "Core/Base/Ref.h"
#include "Pipeline.h"

namespace Orange
{
    /**
     * @class RenderCommandBuffer
     * @brief 渲染命令缓冲区抽象基类
     *
     * 这个类提供了渲染命令缓冲区的统一接口，封装了命令记录、执行和性能查询功能。
     * 命令缓冲区的主要功能包括：
     * - 记录渲染命令序列
     * - 批量提交命令给GPU执行
     * - 提供GPU执行时间和管线统计信息
     * - 支持时间戳查询用于性能分析
     *
     * 具体的实现由各个图形API的派生类提供（如VulkanRenderCommandBuffer）。
     */
    class RenderCommandBuffer : public RefCounted
    {
    public:
        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理资源。
         */
        virtual ~RenderCommandBuffer() = default;

        /**
         * @brief 开始记录命令
         *
         * 开始向命令缓冲区记录渲染命令。调用此方法后，可以向缓冲区
         * 添加各种渲染命令，直到调用End()方法结束记录。
         */
        virtual void Begin() = 0;

        /**
         * @brief 结束记录命令
         *
         * 结束向命令缓冲区记录渲染命令。调用此方法后，命令缓冲区
         * 进入可提交状态，可以通过Submit()方法提交给GPU执行。
         */
        virtual void End() = 0;

        /**
         * @brief 提交命令缓冲区
         *
         * 将记录的命令提交给GPU执行。命令缓冲区必须处于已结束记录的状态
         * （即已调用End()方法）才能提交。提交后的命令将在GPU上异步执行。
         */
        virtual void Submit() = 0;

        /**
         * @brief 获取GPU执行时间
         * @param frameIndex 帧索引，用于查询特定帧的执行时间
         * @param queryIndex 查询索引，默认为0
         * @return GPU执行时间（以毫秒为单位）
         *
         * 返回指定帧和查询索引对应的GPU执行时间。这个时间反映了
         * 命令缓冲区中的命令在GPU上实际执行所花费的时间。
         */
        virtual float GetExecutionGPUTime(uint32_t frameIndex, uint32_t queryIndex = 0) const = 0;

        /**
         * @brief 获取管线统计信息
         * @param frameIndex 帧索引，用于查询特定帧的统计信息
         * @return 管线统计信息的常量引用
         *
         * 返回指定帧的渲染管线统计信息，包括顶点处理数量、
         * 图元数量、着色器调用次数等详细的性能数据。
         */
        virtual const PipelineStatistics &GetPipelineStatistics(uint32_t frameIndex) const = 0;

        /**
         * @brief 开始时间戳查询
         * @return 查询ID，用于后续的EndTimestampQuery调用
         *
         * 开始一个时间戳查询，用于测量GPU上特定操作的执行时间。
         * 返回的查询ID必须传递给对应的EndTimestampQuery调用。
         */
        virtual uint32_t BeginTimestampQuery() = 0;

        /**
         * @brief 结束时间戳查询
         * @param queryID 由BeginTimestampQuery返回的查询ID
         *
         * 结束一个时间戳查询。与BeginTimestampQuery配对使用，
         * 用于测量两个时间点之间GPU操作的执行时间。
         */
        virtual void EndTimestampQuery(uint32_t queryID) = 0;

        /**
         * @brief 创建渲染命令缓冲区实例
         * @param count 命令缓冲区数量，默认为0（使用默认数量）
         * @param debugName 调试名称，用于调试和性能分析
         * @return 创建的命令缓冲区实例智能指针
         *
         * 创建一个新的渲染命令缓冲区实例。具体的实现类型
         * 取决于当前使用的渲染API。
         */
        static Ref<RenderCommandBuffer> Create(uint32_t count = 0, const std::string &debugName = "");

        /**
         * @brief 从交换链创建渲染命令缓冲区实例
         * @param debugName 调试名称，用于调试和性能分析
         * @return 创建的命令缓冲区实例智能指针
         *
         * 创建一个与交换链关联的渲染命令缓冲区实例。这种命令缓冲区
         * 通常用于最终的屏幕渲染操作。
         */
        static Ref<RenderCommandBuffer> CreateFromSwapChain(const std::string &debugName = "");
    };

}
#endif