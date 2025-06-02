/**
 * @file EditorLayer.h
 * @brief 编辑器层头文件
 */

#ifndef ORANGE_EDITOR_LAYER_H
#define ORANGE_EDITOR_LAYER_H

#include <Orange.h>
#include <cstdint>
#include <memory>

// 前向声明
namespace Orange::RenderCore
{
    class Camera;
}

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
         * @brief 清理图形资源
         */
        void CleanupGraphicsResources();

        /**
         * @brief 测试Camera系统功能
         */
        void TestCameraFunctionality();

    private:
        // 状态
        bool m_initialized = false;
        uint32_t m_frameCount = 0;

        // 从Application获取图形系统，不再自己管理
        // Graphics::IGraphicsSystem *m_graphicsSystem = nullptr; // 移除

        // Camera系统测试
        std::unique_ptr<RenderCore::Camera> m_testCamera;
    };

} // namespace Orange

#endif // ORANGE_EDITOR_LAYER_H
