/**
 * @file RendererCapabilities.h
 * @brief 渲染器硬件能力查询
 * @details 定义了用于查询和存储图形硬件能力信息的数据结构
 * @author Orange Engine Team
 */

#ifndef ORANGE_RENDERER_CAPABILITIES_H
#define ORANGE_RENDERER_CAPABILITIES_H

#include <string>

namespace Orange 
{
    /**
     * @brief 渲染器硬件能力信息结构体
     * @details 存储当前图形硬件和驱动程序的能力信息，用于运行时查询和适配
     *
     * 该结构体包含了渲染器初始化时从图形API获取的硬件信息，
     * 应用程序可以根据这些信息来调整渲染设置以获得最佳性能。
     */
    struct RendererCapabilities
    {
        /**
         * @brief 图形硬件厂商名称
         * @details 如 "NVIDIA Corporation", "AMD", "Intel" 等
         */
        std::string Vendor;

        /**
         * @brief 图形设备名称
         * @details 具体的显卡型号，如 "GeForce RTX 4090", "Radeon RX 7900 XTX" 等
         */
        std::string Device;

        /**
         * @brief 图形驱动程序版本
         * @details 当前安装的图形驱动程序版本信息
         */
        std::string Version;

        /**
         * @brief 支持的最大多重采样数
         * @details 硬件支持的最大MSAA采样数，用于抗锯齿设置
         * @note 值为0表示不支持多重采样
         */
        int MaxSamples = 0;

        /**
         * @brief 支持的最大各向异性过滤级别
         * @details 纹理采样时支持的最大各向异性过滤倍数
         * @note 值为0.0f表示不支持各向异性过滤
         */
        float MaxAnisotropy = 0.0f;

        /**
         * @brief 支持的最大纹理单元数量
         * @details 着色器中可以同时使用的纹理单元数量上限
         * @note 现代GPU通常支持16个或更多纹理单元
         */
        int MaxTextureUnits = 0;
    };
}

#endif // ORANGE_RENDERER_CAPABILITIES_H
