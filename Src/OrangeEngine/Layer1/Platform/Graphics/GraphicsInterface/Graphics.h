#ifndef ORANGE_GRAPHICS_H
#define ORANGE_GRAPHICS_H

// Orange Graphics Interface Layer
// 平台无关的图形抽象接口层

#include "GraphicsSystem.h"
#include "RenderDevice.h"
#include "Buffer.h"
#include "Texture.h"
#include "Pipeline.h"
#include "ShaderCompiler.h"

namespace Orange::Graphics
{

    /**
     * @brief 图形API类型枚举
     */
    enum class GraphicsAPI
    {
        None,
        Vulkan,
        DirectX12,
        OpenGL
    };

    /**
     * @brief 图形系统工厂类
     * 用于创建不同图形API的实现
     */
    class GraphicsFactory
    {
    public:
        /**
         * @brief 创建图形系统实例
         * @param api 图形API类型
         * @return 图形系统智能指针
         */
        static std::unique_ptr<GraphicsSystem> CreateGraphicsSystem(GraphicsAPI api);

        /**
         * @brief 获取默认图形API
         * @return 默认API类型
         */
        static GraphicsAPI GetDefaultAPI();

        /**
         * @brief 检查指定API是否支持
         * @param api 图形API类型
         * @return 支持返回true
         */
        static bool IsAPISupported(GraphicsAPI api);

        /**
         * @brief 获取所有支持的API列表
         * @return API列表
         */
        static std::vector<GraphicsAPI> GetSupportedAPIs();

        /**
         * @brief 获取API名称字符串
         * @param api 图形API类型
         * @return API名称
         */
        static std::string GetAPIName(GraphicsAPI api);
    };

} // namespace Orange::Graphics

#endif // ORANGE_GRAPHICS_H