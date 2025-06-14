#ifndef RENDERER_CONTEXT_H
#define RENDERER_CONTEXT_H

#include "Core/Base/Ref.h"

namespace Orange
{
    /**
     * @class RendererContext
     * @brief 渲染器上下文抽象基类
     *
     * 这个类提供了渲染器上下文的统一接口，封装了不同图形API的
     * 初始化和管理逻辑。渲染器上下文是整个渲染系统的基础，
     * 负责建立与GPU的连接并提供渲染环境。
     *
     * 设计理念：
     * - 抽象化：隐藏不同图形API的初始化差异
     * - 统一接口：为所有图形API提供一致的上下文管理
     * - 生命周期管理：自动处理资源的创建和销毁
     * - 平台无关：支持多种操作系统和图形驱动
     *
     * 主要职责：
     * - 图形API初始化：设置Vulkan、DirectX环境
     * - 设备管理：选择和配置图形设备
     * - 上下文创建：建立渲染上下文和命令队列
     * - 扩展加载：加载必要的图形API扩展
     * - 调试支持：配置调试层和验证层
     *
     * 生命周期：
     * 1. 创建：通过Create()工厂方法创建实例
     * 2. 初始化：调用Init()方法初始化图形API
     * 3. 使用：为渲染系统提供上下文支持
     * 4. 销毁：析构时自动清理资源
     *
     * 具体实现：
     * - VulkanContext：Vulkan API的上下文实现
     * - D3D12Context：DirectX 12的上下文实现
     *
     * 与窗口系统的关系：
     * 渲染器上下文通常与窗口系统（如GLFW）紧密集成，
     * 需要窗口句柄来创建渲染表面和交换链。
     */
    class RendererContext : public RefCounted
    {
    public:
        /**
         * @brief 默认构造函数
         *
         * 创建一个未初始化的渲染器上下文实例。
         */
        RendererContext() = default;

        /**
         * @brief 虚析构函数
         *
         * 确保派生类能够正确清理图形API资源和上下文。
         * 析构时会自动释放所有相关的GPU资源。
         */
        virtual ~RendererContext() = default;

        /**
         * @brief 初始化渲染器上下文
         *
         * 执行图形API的初始化过程，包括：
         * - 加载图形API库和函数
         * - 创建图形实例和设备
         * - 设置调试和验证层
         * - 配置渲染表面和交换链
         * - 创建命令队列和内存分配器
         *
         * 初始化过程是同步的，完成后渲染系统即可开始工作。
         * 如果初始化失败，会抛出异常或返回错误状态。
         *
         * @note 此方法只能调用一次，重复调用可能导致未定义行为
         * @note 初始化可能需要较长时间，特别是在首次运行时
         */
        virtual void Init() = 0;

        /**
         * @brief 创建渲染器上下文实例
         * @return 创建的渲染器上下文实例智能指针
         *
         * 工厂方法，根据当前配置的图形API创建相应的上下文实例。
         * 具体创建的上下文类型取决于编译时配置和运行时检测：
         *
         * 选择逻辑：
         * 1. 检查用户指定的API偏好
         * 2. 检测系统支持的图形API
         * 3. 根据平台特性选择最佳API
         * 4. 创建对应的上下文实现
         *
         * 支持的图形API：
         * - Vulkan：现代跨平台图形API，性能最佳
         * - DirectX 12：Windows平台的现代图形API
         *
         * 创建的上下文尚未初始化，需要调用Init()方法完成初始化。
         *
         * @note 返回的上下文实例使用智能指针管理生命周期
         * @note 如果无法创建任何支持的上下文，可能返回nullptr
         */
        static Ref<RendererContext> Create();
    };
}

#endif