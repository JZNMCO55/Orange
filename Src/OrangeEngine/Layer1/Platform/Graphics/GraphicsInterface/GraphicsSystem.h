#ifndef ORANGE_GRAPHICS_GRAPHICSSYSTEM_H
#define ORANGE_GRAPHICS_GRAPHICSSYSTEM_H

#include <cstdint>

namespace Orange
{
    namespace Graphics
    {
        // 前向声明
        class RenderDevice;
        class ShaderCompiler;

        /**
         * @brief 图形系统接口
         *
         * 定义了图形系统的基本功能，包括初始化、渲染循环控制等
         */
        class GraphicsSystem
        {
        public:
            virtual ~GraphicsSystem() = default;

            /**
             * @brief 初始化图形系统
             * @return 是否初始化成功
             */
            virtual bool Initialize() = 0;

            /**
             * @brief 初始化图形系统（使用外部窗口）
             * @param windowHandle 外部窗口句柄 (GLFWwindow*)
             * @return 是否初始化成功
             */
            virtual bool Initialize(void *windowHandle) = 0;

            /**
             * @brief 关闭图形系统
             */
            virtual void Shutdown() = 0;

            /**
             * @brief 开始新的一帧
             */
            virtual void BeginFrame() = 0;

            /**
             * @brief 结束当前帧
             */
            virtual void EndFrame() = 0;

            /**
             * @brief 呈现渲染结果
             */
            virtual void Present() = 0;

            /**
             * @brief 设置清屏颜色
             * @param r 红色分量 (0.0-1.0)
             * @param g 绿色分量 (0.0-1.0)
             * @param b 蓝色分量 (0.0-1.0)
             * @param a 透明度分量 (0.0-1.0)
             */
            virtual void SetClearColor(float r, float g, float b, float a = 1.0f) = 0;

            /**
             * @brief 清除缓冲区
             */
            virtual void Clear() = 0;

            /**
             * @brief 获取后缓冲区宽度
             * @return 宽度（像素）
             */
            virtual uint32_t GetBackbufferWidth() const = 0;

            /**
             * @brief 获取后缓冲区高度
             * @return 高度（像素）
             */
            virtual uint32_t GetBackbufferHeight() const = 0;

            /**
             * @brief 获取渲染设备接口
             * @return 渲染设备指针，如果未初始化则返回nullptr
             */
            virtual class RenderDevice *GetRenderDevice() = 0;

            /**
             * @brief 获取着色器编译器接口
             * @return 着色器编译器指针，如果未初始化则返回nullptr
             */
            virtual class ShaderCompiler *GetShaderCompiler() = 0;
        };
    }
}

#endif // ORANGE_GRAPHICS_GRAPHICSSYSTEM_H