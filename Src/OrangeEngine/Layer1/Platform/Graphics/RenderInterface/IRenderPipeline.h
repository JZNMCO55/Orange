/**
 * @file IRenderPipeline.h
 * @brief 渲染管线接口，用于管理图形和计算管线
 */

#ifndef ORANGE_IRENDER_PIPELINE_H
#define ORANGE_IRENDER_PIPELINE_H

#include "../RenderCommon/RenderCommon.h"

namespace Orange
{
    namespace Graphics
    {

        // 前向声明
        class IRenderDevice;
        class IPipelineLayout;
        class IRenderPass;

        /**
         * @brief 渲染管线接口
         *
         * 渲染管线封装了图形或计算管线状态，包括着色器、输入布局、光栅化状态等。
         * 它定义了GPU如何处理顶点和片段数据。
         */
        class IRenderPipeline
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IRenderPipeline() = default;

            /**
             * @brief 获取管线类型
             * @return 管线类型（图形/计算）
             */
            virtual PipelineType GetType() const = 0;

            /**
             * @brief 获取管线布局
             * @return 管线布局
             */
            virtual IPipelineLayout *GetLayout() const = 0;

            /**
             * @brief 获取渲染通道（仅图形管线）
             * @return 渲染通道
             */
            virtual IRenderPass *GetRenderPass() const = 0;

            /**
             * @brief 获取子通道索引（仅图形管线）
             * @return 子通道索引
             */
            virtual uint32_t GetSubpass() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生管线句柄
             * @return 原生管线句柄（如VkPipeline、ID3D12PipelineState等）
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativePipeline() const = 0;
        };

        /**
         * @brief 管线布局接口
         *
         * 管线布局定义了着色器如何访问资源，包括描述符集布局和推送常量范围。
         */
        class IPipelineLayout
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IPipelineLayout() = default;

            /**
             * @brief 获取描述符集布局数量
             * @return 描述符集布局数量
             */
            virtual uint32_t GetDescriptorSetLayoutCount() const = 0;

            /**
             * @brief 获取描述符集布局
             * @param index 索引
             * @return 描述符集布局
             */
            virtual void *GetDescriptorSetLayout(uint32_t index) const = 0;

            /**
             * @brief 获取推送常量范围数量
             * @return 推送常量范围数量
             */
            virtual uint32_t GetPushConstantRangeCount() const = 0;

            /**
             * @brief 获取推送常量范围
             * @param index 索引
             * @return 推送常量范围
             */
            virtual PushConstantRange GetPushConstantRange(uint32_t index) const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生管线布局句柄
             * @return 原生管线布局句柄（如VkPipelineLayout、ID3D12RootSignature等）
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativePipelineLayout() const = 0;
        };

        /**
         * @brief 描述符集布局接口
         *
         * 描述符集布局定义了一组描述符绑定，包括绑定点、类型和着色器阶段可见性。
         */
        class IDescriptorSetLayout
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IDescriptorSetLayout() = default;

            /**
             * @brief 获取绑定数量
             * @return 绑定数量
             */
            virtual uint32_t GetBindingCount() const = 0;

            /**
             * @brief 获取绑定
             * @param index 索引
             * @return 描述符绑定
             */
            virtual DescriptorBinding GetBinding(uint32_t index) const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生描述符集布局句柄
             * @return 原生描述符集布局句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeDescriptorSetLayout() const = 0;
        };

        /**
         * @brief 描述符池接口
         *
         * 描述符池用于分配描述符集，管理描述符资源。
         */
        class IDescriptorPool
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IDescriptorPool() = default;

            /**
             * @brief 分配描述符集
             * @param allocateInfo 分配信息
             * @return 新分配的描述符集，失败返回nullptr
             */
            virtual IDescriptorSet *AllocateDescriptorSet(const DescriptorSetAllocateInfo &allocateInfo) = 0;

            /**
             * @brief 释放描述符集
             * @param descriptorSet 要释放的描述符集
             * @return 是否成功释放
             */
            virtual bool FreeDescriptorSet(IDescriptorSet *descriptorSet) = 0;

            /**
             * @brief 重置描述符池
             * @return 是否成功重置
             */
            virtual bool Reset() = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生描述符池句柄
             * @return 原生描述符池句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeDescriptorPool() const = 0;
        };

        /**
         * @brief 描述符集接口
         *
         * 描述符集包含一组资源绑定，可绑定到管线使用。
         */
        class IDescriptorSet
        {
        public:
            /**
             * @brief 虚析构函数
             */
            virtual ~IDescriptorSet() = default;

            /**
             * @brief 更新缓冲区描述符
             * @param binding 绑定点
             * @param buffer 缓冲区
             * @param offset 偏移量
             * @param range 范围
             * @param arrayElement 数组元素索引
             */
            virtual void UpdateBuffer(uint32_t binding,
                                      void *buffer,
                                      uint64_t offset,
                                      uint64_t range,
                                      uint32_t arrayElement = 0) = 0;

            /**
             * @brief 更新纹理描述符
             * @param binding 绑定点
             * @param texture 纹理
             * @param sampler 采样器
             * @param layout 布局
             * @param arrayElement 数组元素索引
             */
            virtual void UpdateTexture(uint32_t binding,
                                       void *texture,
                                       void *sampler = nullptr,
                                       ResourceState layout = ResourceState::ShaderRead,
                                       uint32_t arrayElement = 0) = 0;

            /**
             * @brief 更新采样器描述符
             * @param binding 绑定点
             * @param sampler 采样器
             * @param arrayElement 数组元素索引
             */
            virtual void UpdateSampler(uint32_t binding,
                                       void *sampler,
                                       uint32_t arrayElement = 0) = 0;

            /**
             * @brief 获取描述符集布局
             * @return 描述符集布局
             */
            virtual IDescriptorSetLayout *GetLayout() const = 0;

            /**
             * @brief 获取描述符池
             * @return 描述符池
             */
            virtual IDescriptorPool *GetPool() const = 0;

            /**
             * @brief 获取所属渲染设备
             * @return 渲染设备
             */
            virtual IRenderDevice *GetDevice() const = 0;

            /**
             * @brief 获取原生描述符集句柄
             * @return 原生描述符集句柄
             * @note 仅用于高级用法，应避免直接使用
             */
            virtual void *GetNativeDescriptorSet() const = 0;
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_IRENDER_PIPELINE_H