#ifndef RENDERER_RESOURCE_H
#define RENDERER_RESOURCE_H

#include "Core/Base/Ref.h"

namespace Orange
{
    /**
     * @typedef ResourceDescriptorInfo
     * @brief 资源描述符信息类型定义
     *
     * 定义了资源描述符信息的类型，使用void*作为通用指针类型。
     * 这允许不同的图形API使用各自特定的描述符类型：
     *
     * - Vulkan：可能指向VkDescriptorSet或相关结构
     * - DirectX 12：可能指向D3D12_CPU_DESCRIPTOR_HANDLE
     * - OpenGL：可能指向纹理ID或其他OpenGL对象
     *
     * 使用void*提供了最大的灵活性，允许各个图形API实现
     * 使用最适合的描述符表示方式。
     */
    using ResourceDescriptorInfo = void *;

    /**
     * @class RendererResource
     * @brief 渲染器资源抽象基类
     *
     * 这个类是所有GPU资源的基类，继承自Asset类以获得资源管理功能。
     * RendererResource提供了GPU资源的统一接口，主要用于描述符管理
     * 和资源标识。
     *
     * 设计理念：
     * - 统一接口：为所有GPU资源提供一致的访问方式
     * - 描述符抽象：隐藏不同图形API的描述符差异
     * - 资源管理：集成到引擎的资产管理系统中
     * - 类型安全：通过继承提供类型检查
     *
     * 主要用途：
     * - 作为所有GPU资源类的基类
     * - 提供描述符信息的统一访问接口
     * - 支持资源的多态使用
     * - 集成资产系统的生命周期管理

     * 具体的实现由各个图形API的派生类提供，如：
     * - VulkanImage、VulkanBuffer等（Vulkan实现）
     * - D3D12Image、D3D12Buffer等（DirectX 12实现）
     */
    class RendererResource : public RefCounted //: public Asset
    // TODO: 添加Asset基类
    {
    public:
        /**
         * @brief 获取资源描述符信息
         * @return 资源的描述符信息指针
         *
         * 返回此资源的描述符信息，用于在渲染管线中绑定和使用资源。
         * 描述符信息的具体类型和内容取决于底层图形API的实现。
         *
         * @note 返回的指针的生命周期与资源对象相同
         * @note 调用者不应该释放返回的指针
         * @note 不同的资源类型可能返回不同类型的描述符信息
         */
        virtual ResourceDescriptorInfo GetDescriptorInfo() const = 0;
    };

}

#endif