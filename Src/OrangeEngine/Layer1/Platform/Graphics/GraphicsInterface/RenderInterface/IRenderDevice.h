/**
 * @file IRenderDevice.h
 * @brief 渲染设备接口，负责创建和管理图形资源
 */

#ifndef ORANGE_IRENDER_DEVICE_H
#define ORANGE_IRENDER_DEVICE_H

#include "../RenderCommon/RenderCommon.h"
#include "../RenderSync/IRenderFence.h"
#include "../RenderSync/IRenderSemaphore.h"

namespace Orange
{
    namespace Graphics
    {

        // 前向声明
        class IRenderContext;
        class IRenderPipeline;
        class IRenderPass;
        class ISwapChain;
        class IRenderBuffer;
        class IRenderTexture;
        class IRenderSampler;
        class IShaderModule;
        class IDescriptorSetLayout;
        class IDescriptorPool;
        class IDescriptorSet;
        class IPipelineLayout;

        /**
         * @brief 渲染设备接口
         *
         * 渲染设备是图形API抽象层的核心接口，负责创建和管理图形资源，
         * 如缓冲区、纹理、着色器、管线等。它封装了与特定图形API（如Vulkan、DirectX12、Metal）
         * 的设备创建和管理逻辑。
         */
        class IRenderDevice
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderDevice() = default;

            /**
             * @brief 初始化渲染设备
             * @param createInfo 设备创建信息
             * @return 是否成功初始化
             */
            virtual bool Initialize(const DeviceCreateInfo &createInfo) { return false; }

            /**
             * @brief 关闭渲染设备并释放资源
             */
            virtual void Shutdown() {}

            /**
             * @brief 检查设备是否支持指定特性
             * @param feature 要检查的渲染特性
             * @return 是否支持该特性
             */
            virtual bool SupportsFeature(RenderFeature feature) const { return false; }

            /**
             * @brief 获取设备属性
             * @param properties 输出的设备属性
             */
            virtual void GetDeviceProperties(DeviceProperties &properties) const {}

            /**
             * @brief 等待设备空闲（所有命令执行完毕）
             */
            virtual void WaitIdle() {}

            /**
             * @brief 创建渲染上下文
             * @return 新创建的渲染上下文指针，失败返回nullptr
             */
            virtual IRenderContext *CreateContext() { return nullptr; }

            /**
             * @brief 创建交换链
             * @param createInfo 交换链创建信息
             * @return 新创建的交换链指针，失败返回nullptr
             */
            virtual ISwapChain *CreateSwapChain(const SwapChainCreateInfo &createInfo) { return nullptr; }

            /**
             * @brief 创建缓冲区
             * @param createInfo 缓冲区创建信息
             * @return 新创建的缓冲区指针，失败返回nullptr
             */
            virtual IRenderBuffer *CreateBuffer(const BufferCreateInfo &createInfo) { return nullptr; }

            /**
             * @brief 创建纹理
             * @param createInfo 纹理创建信息
             * @return 新创建的纹理指针，失败返回nullptr
             */
            virtual IRenderTexture *CreateTexture(const TextureCreateInfo &createInfo) { return nullptr; }

            /**
             * @brief 创建采样器
             * @param createInfo 采样器创建信息
             * @return 新创建的采样器指针，失败返回nullptr
             */
            virtual IRenderSampler *CreateSampler(const SamplerCreateInfo &createInfo) { return nullptr; }

            /**
             * @brief 创建着色器模块
             * @param createInfo 着色器创建信息
             * @return 新创建的着色器模块指针，失败返回nullptr
             */
            virtual IShaderModule *CreateShaderModule(const ShaderCreateInfo &createInfo) { return nullptr; }

            /**
             * @brief 创建描述符集布局
             * @param createInfo 描述符集布局创建信息
             * @return 新创建的描述符集布局指针，失败返回nullptr
             */
            virtual IDescriptorSetLayout *CreateDescriptorSetLayout(const DescriptorSetLayoutCreateInfo &createInfo) { return nullptr; }

            /**
             * @brief 创建管线布局
             * @param createInfo 管线布局创建信息
             * @return 新创建的管线布局指针，失败返回nullptr
             */
            virtual IPipelineLayout *CreatePipelineLayout(const PipelineLayoutCreateInfo &createInfo) { return nullptr; }

            /**
             * @brief 创建图形渲染管线
             * @param createInfo 图形管线创建信息
             * @return 新创建的渲染管线指针，失败返回nullptr
             */
            virtual IRenderPipeline *CreateGraphicsPipeline(const GraphicsPipelineCreateInfo &createInfo) { return nullptr; }

            /**
             * @brief 创建计算渲染管线
             * @param createInfo 计算管线创建信息
             * @return 新创建的渲染管线指针，失败返回nullptr
             */
            virtual IRenderPipeline *CreateComputePipeline(const ComputePipelineCreateInfo &createInfo) { return nullptr; }

            /**
             * @brief 创建渲染通道
             * @param createInfo 渲染通道创建信息
             * @return 新创建的渲染通道指针，失败返回nullptr
             */
            virtual IRenderPass *CreateRenderPass(const RenderPassCreateInfo &createInfo) { return nullptr; }

            /**
             * @brief 创建描述符池
             * @param createInfo 描述符池创建信息
             * @return 新创建的描述符池指针，失败返回nullptr
             */
            virtual IDescriptorPool *CreateDescriptorPool(const DescriptorPoolCreateInfo &createInfo) { return nullptr; }

            /**
             * @brief 创建栅栏（用于CPU-GPU同步）
             * @param signaled 初始状态是否为已触发
             * @return 新创建的栅栏指针，失败返回nullptr
             */
            virtual IRenderFence *CreateFence(bool signaled = false) { return nullptr; }

            /**
             * @brief 创建信号量（用于GPU-GPU同步）
             * @return 新创建的信号量指针，失败返回nullptr
             */
            virtual IRenderSemaphore *CreateSemaphore() { return nullptr; }

            /**
             * @brief 获取GPU内存统计信息
             * @param stats 输出的内存统计信息
             */
            virtual void GetMemoryStats(GPUMemoryStats &stats) const {}

            /**
             * @brief 获取原生API设备句柄
             * @return 原生设备句柄（如VkDevice、ID3D12Device等）
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeDevice() const { return nullptr; }

            /**
             * @brief 获取原生API物理设备句柄
             * @return 原生物理设备句柄（如VkPhysicalDevice等）
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativePhysicalDevice() const { return nullptr; }

            /**
             * @brief 获取渲染API类型
             * @return 渲染API枚举值
             */
            virtual RenderAPI GetRenderAPI() const { return RenderAPI::Vulkan; }

            /**
             * @brief 设置调试名称
             * @param object 对象指针
             * @param objectType 对象类型（API特定）
             * @param name 调试名称
             */
            virtual void SetDebugName(void *object, uint32_t objectType, const char *name) {}
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IRENDER_DEVICE_H