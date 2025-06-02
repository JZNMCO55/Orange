/**
 * @file EditorLayer.h
 * @brief 编辑器层头文件
 */

#ifndef ORANGE_EDITOR_LAYER_H
#define ORANGE_EDITOR_LAYER_H

#include <Orange.h>
#include <cstdint>

namespace Orange
{
    /**
     * @brief 编辑器层类
     * 负责编辑器的渲染和更新逻辑
     */
    class EditorLayer : public Core::Layer
    {
    public:
        EditorLayer();
        ~EditorLayer();

        /**
         * @brief 初始化编辑器层
         * @return 是否成功
         */
        bool Initialize();

        /**
         * @brief 关闭编辑器层
         */
        void Shutdown();

        /**
         * @brief 更新编辑器层
         * @param deltaTime 帧时间间隔
         */
        virtual void OnUpdate() override;

        virtual void OnAttach() override;
        virtual void OnDetach() override;

        /**
         * @brief 处理事件
         * @param event 事件对象
         */
        virtual void OnEvent(Core::Event &event) override;

    private:
        /**
         * @brief 初始化图形资源
         * @return 是否成功
         */
        bool InitializeGraphicsResources();

        /**
         * @brief 创建三角形渲染资源
         * @return 是否成功
         */
        bool CreateTriangleResources();

        /**
         * @brief 渲染三角形
         */
        void RenderTriangle();

        /**
         * @brief 清理图形资源
         */
        void CleanupGraphicsResources();

    private:
        // 状态
        bool m_initialized = false;
        uint32_t m_frameCount = 0;
    };

} // namespace Orange

#endif // ORANGE_EDITOR_LAYER_H
