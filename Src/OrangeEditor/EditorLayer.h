/**
 * @file EditorLayer.h
 * @brief 编辑器层头文件
 */

#ifndef ORANGE_EDITOR_LAYER_H
#define ORANGE_EDITOR_LAYER_H

#include "Orange.h"
#include "Layer1/Platform/Graphics/GraphicsInterface/GraphicsSystem.h"
#include "Layer1/Platform/Graphics/GraphicsInterface/RenderInterface/RenderInterface.h"
#include "Layer1/Platform/Graphics/GraphicsInterface/RenderSync/RenderSync.h"
#include "Layer1/Core/Application/Layer.h"

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
         * @return 是否成功初始化
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
        void OnUpdate() override;

        /**
         * @brief 渲染编辑器层
         */
        void OnRender();

        /**
         * @brief 处理事件
         * @param event 事件对象
         */
        void OnEvent(Core::Event& event);

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
        // 图形系统相关
        Graphics::IRenderDevice *m_renderDevice = nullptr;
        Graphics::IRenderContext *m_renderContext = nullptr;
        Graphics::IRenderFenceFactory *m_fenceFactory = nullptr;
        Graphics::IRenderSemaphoreFactory *m_semaphoreFactory = nullptr;
        Graphics::IMemoryManager *m_memoryManager = nullptr;

        // 三角形渲染资源
        Graphics::IRenderBuffer *m_triangleVertexBuffer = nullptr;
        Graphics::IRenderFence *m_renderFence = nullptr;
        Graphics::IRenderSemaphore *m_renderSemaphore = nullptr;

        // 状态
        bool m_initialized = false;
        uint32_t m_frameCount = 0;
    };

} // namespace Orange

#endif // ORANGE_EDITOR_LAYER_H
