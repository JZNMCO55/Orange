/**
 * @file RenderInterface.h
 * @brief 包含所有渲染系统接口头文件
 */

#ifndef ORANGE_RENDER_INTERFACE_H
#define ORANGE_RENDER_INTERFACE_H

#include "IRenderDevice.h"
#include "IRenderContext.h"
#include "IRenderPipeline.h"
#include "IRenderPass.h"
#include "IRenderResources.h"
#include "ISwapChain.h"

namespace Orange
{
    namespace Graphics
    {

        /**
         * @brief 创建平台特定的渲染设备
         * @param api 渲染API类型
         * @param createInfo 创建信息
         * @return 渲染设备接口指针，失败返回nullptr
         */
        IRenderDevice *CreateRenderDevice(RenderAPI api, const DeviceCreateInfo &createInfo);

        /**
         * @brief 获取支持的渲染API
         * @return 支持的渲染API列表
         */
        std::vector<RenderAPI> GetSupportedRenderAPIs();

        /**
         * @brief 检查指定渲染API是否可用
         * @param api 渲染API类型
         * @return 是否可用
         */
        bool IsRenderAPIAvailable(RenderAPI api);

        /**
         * @brief 获取平台首选的渲染API
         * @return 首选渲染API
         */
        RenderAPI GetPreferredRenderAPI();

        /**
         * @brief 初始化渲染系统
         * @return 是否成功初始化
         */
        bool InitializeRenderSystem();

        /**
         * @brief 关闭渲染系统
         */
        void ShutdownRenderSystem();

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_RENDER_INTERFACE_H