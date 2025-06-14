#ifndef RENDERER2D_H
#define RENDERER2D_H

#include "Core/Base/Ref.h"

namespace Orange
{
    /**
     * @brief 2D渲染器配置项
     */
#ifdef TODO
    struct Renderer2DSpecification
    {
        bool SwapChainTarget = false; // 是否使用交换链目标
        uint32_t MaxQuads = 10000;    // 最大四边形数量
        uint32_t MaxLines = 2000;     // 最大线数量
    };

    class Renderer2D : public RefCounted
    {
    public:
        Renderer2D(const Renderer2DConfig& config);
        virtual ~Renderer2D();

        /**
         * @brief 初始化渲染器
         */
        void Init();

        /**
         * @brief 关闭渲染器
         */
        void Shutdown();

        struct DrawStatistics
        {
            uint32_t DrawCalls = 0; ///< 绘制调用次数
            uint32_t QuadCount = 0; ///< 四边形数量
            uint32_t LineCount = 0; ///< 线条数量

            /**
             * @brief 获取总顶点数
             * @return 顶点总数
             */
            uint32_t GetTotalVertexCount() { return QuadCount * 4 + LineCount * 2; }
            /**
             * @brief 获取总索引数
             * @return 索引总数
             */
            uint32_t GetTotalIndexCount() { return QuadCount * 6 + LineCount * 2; }
        };

        /**
         * @brief 内存统计信息结构体
         */
        struct MemoryStatistics
        {
            uint64_t Used = 0;           ///< 已使用内存
            uint64_t TotalAllocated = 0; ///< 总分配内存

            /**
             * @brief 获取每帧分配内存
             * @return 每帧分配内存大小
             */
            uint64_t GetAllocatedPerFrame() const;
        };

        /**
         * @brief 重置统计信息
         */
        void ResetStats();
        /**
         * @brief 获取绘制统计信息
         * @return 绘制统计信息
         */
        DrawStatistics GetDrawStats();
        /**
         * @brief 获取内存统计信息
         * @return 内存统计信息
         */
        MemoryStatistics GetMemoryStats();

        /**
         * @brief 获取渲染器配置
         * @return 渲染器配置引用
         */
        const Renderer2DSpecification& GetSpecification() const { return mSpecification; }

    private:
        Renderer2DSpecification mSpecification;
    };
#endif // TODO
}

#endif