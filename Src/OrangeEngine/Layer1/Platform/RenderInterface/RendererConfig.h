/**
 * @file RendererConfig.h
 * @brief 渲染器配置参数
 * @details 定义了渲染器运行时的各种配置选项和参数
 * @author Orange Engine Team
 */

#ifndef ORANGE_RENDERER_CONFIG_H
#define ORANGE_RENDERER_CONFIG_H

#include <string>

namespace Orange 
{

    /**
     * @brief 渲染器配置结构体
     * @details 包含渲染器初始化和运行时需要的各种配置参数
     *
     * 该结构体允许应用程序自定义渲染器的行为，包括性能设置、
     * 质量选项和资源路径等。配置参数在渲染器初始化时设置。
     */
    struct RendererConfig
    {
        /**
         * @brief 飞行帧数
         * @details 同时在GPU上处理的帧数，影响延迟和性能
         *
         * 更多的飞行帧可以提高GPU利用率，但会增加输入延迟。
         * 典型值为2-3帧，VR应用可能需要更少的飞行帧以减少延迟。
         *
         * @note 该值不能超过交换链的后缓冲区数量
         */
        uint32_t FramesInFlight = 3;

        /**
         * @brief 是否启用环境贴图计算
         * @details 控制是否在GPU上实时计算环境贴图
         *
         * 启用时会在GPU上计算IBL（基于图像的光照）相关贴图，
         * 包括辐照度贴图和预过滤环境贴图。禁用可以提高性能但会影响光照质量。
         */
        bool ComputeEnvironmentMaps = true;

        /**
         * @brief 分层设置
         * @details 以下参数用于控制渲染质量的分层设置
         */

         /**
          * @brief 环境贴图分辨率
          * @details 环境贴图（天空盒）的分辨率大小
          *
          * 更高的分辨率提供更好的环境反射质量，但会消耗更多显存和计算资源。
          * 常用值：512（低质量）、1024（中等质量）、2048（高质量）。
          */
        uint32_t EnvironmentMapResolution = 1024;

        /**
         * @brief 辐照度贴图计算采样数
         * @details 计算辐照度贴图时使用的采样数量
         *
         * 更多的采样数可以减少噪点，提高辐照度贴图的质量，
         * 但会增加计算时间。典型值为256-1024。
         */
        uint32_t IrradianceMapComputeSamples = 512;

        /**
         * @brief 着色器包文件路径
         * @details 预编译着色器包的文件路径
         *
         * 如果指定了路径，渲染器将从着色器包中加载预编译的着色器，
         * 这可以显著减少应用程序启动时间。空字符串表示不使用着色器包。
         *
         * @note 着色器包必须与当前图形API和硬件兼容
         */
        std::string ShaderPackPath;
    };

}
#endif // ORANGE_RENDERER_CONFIG_H