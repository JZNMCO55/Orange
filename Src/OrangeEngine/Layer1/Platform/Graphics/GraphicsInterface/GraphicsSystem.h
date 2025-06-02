/**
 * @file GraphicsSystem.h
 * @brief 图形系统入口头文件
 */

#ifndef ORANGE_GRAPHICS_SYSTEM_H
#define ORANGE_GRAPHICS_SYSTEM_H

#include "RenderCommon/RenderCommon.h"
#include "RenderCommand/RenderCommand.h"
#include "RenderSync/RenderSync.h"
#include "RenderMemory/RenderMemory.h"

namespace Orange
{
    namespace Graphics
    {
        // 前向声明
        class IRenderDevice;
        class IRenderContext;
        class ISwapChain;
        class IRenderCommandQueueFactory;
        class IRenderCommandBufferFactory;
        class IRenderCommandQueue;
        class IMemoryManager;
        class IRenderFenceFactory;
        class IRenderSemaphoreFactory;
        class IRenderEventFactory;

        /**
         * @brief 图形系统配置
         */
        struct GraphicsSystemConfig
        {
            RenderAPI preferredBackend = RenderAPI::Vulkan;                                                     ///< 首选渲染后端
            bool enableValidation = true;                                                                       ///< 是否启用验证层
            bool enableDebugMarkers = true;                                                                     ///< 是否启用调试标记
            uint32_t maxFramesInFlight = 2;                                                                     ///< 最大同时处理帧数
            const char *applicationName = "OrangeEngine";                                                       ///< 应用程序名称
            uint32_t applicationVersion = 1;                                                                    ///< 应用程序版本
            uint32_t requiredDeviceExtensionCount = 0;                                                          ///< 必需设备扩展数量
            const char **requiredDeviceExtensions = nullptr;                                                    ///< 必需设备扩展列表
            uint32_t optionalDeviceExtensionCount = 0;                                                          ///< 可选设备扩展数量
            const char **optionalDeviceExtensions = nullptr;                                                    ///< 可选设备扩展列表
            DeviceFeatureFlags requiredFeatures = static_cast<DeviceFeatureFlags>(DeviceFeatureFlagBits::None); ///< 必需设备特性
            DeviceFeatureFlags optionalFeatures = static_cast<DeviceFeatureFlags>(DeviceFeatureFlagBits::None); ///< 可选设备特性
            uint32_t gpuPreference = 0;                                                                         ///< GPU首选项（0=默认，1=集成，2=独立）
            bool forceWarp = false;                                                                             ///< 是否强制使用软件渲染
            void *instanceUserData = nullptr;                                                                   ///< 实例用户数据
            uint64_t memoryBudget = 0;                                                                          ///< 内存预算，0表示自动
        };

        /**
         * @brief 图形系统
         */
        class GraphicsSystem
        {
        public:
            /**
             * @brief 初始化图形系统
             * @param config 配置
             * @return 是否成功初始化
             */
            static bool Initialize(const GraphicsSystemConfig &config);

            /**
             * @brief 关闭图形系统
             */
            static void Shutdown();

            /**
             * @brief 获取是否已初始化
             * @return 是否已初始化
             */
            static bool IsInitialized();

            /**
             * @brief 获取活跃的渲染后端
             * @return 渲染后端
             */
            static RenderAPI GetActiveBackend();

            /**
             * @brief 获取渲染设备
             * @return 渲染设备
             */
            static IRenderDevice *GetDevice();

            /**
             * @brief 获取渲染上下文
             * @return 渲染上下文
             */
            static IRenderContext *GetContext();

            /**
             * @brief 获取命令队列工厂
             * @return 命令队列工厂
             */
            static IRenderCommandQueueFactory *GetCommandQueueFactory();

            /**
             * @brief 获取命令缓冲区工厂
             * @return 命令缓冲区工厂
             */
            static IRenderCommandBufferFactory *GetCommandBufferFactory();

            /**
             * @brief 获取主图形队列
             * @return 主图形队列
             */
            static IRenderCommandQueue *GetMainGraphicsQueue();

            /**
             * @brief 获取主计算队列
             * @return 主计算队列
             */
            static IRenderCommandQueue *GetMainComputeQueue();

            /**
             * @brief 获取主传输队列
             * @return 主传输队列
             */
            static IRenderCommandQueue *GetMainTransferQueue();

            /**
             * @brief 获取内存管理器
             * @return 内存管理器
             */
            static IMemoryManager *GetMemoryManager();

            /**
             * @brief 获取围栏工厂
             * @return 围栏工厂
             */
            static IRenderFenceFactory *GetFenceFactory();

            /**
             * @brief 获取信号量工厂
             * @return 信号量工厂
             */
            static IRenderSemaphoreFactory *GetSemaphoreFactory();

            /**
             * @brief 获取事件工厂
             * @return 事件工厂
             */
            static IRenderEventFactory *GetEventFactory();

            /**
             * @brief 获取系统配置
             * @return 系统配置
             */
            static const GraphicsSystemConfig &GetConfig();

            /**
             * @brief 设置帧开始标记
             * 用于性能分析和资源跟踪
             * @param frameIndex 帧索引
             */
            static void BeginFrame(uint64_t frameIndex);

            /**
             * @brief 设置帧结束标记
             * 用于性能分析和资源跟踪
             */
            static void EndFrame();

            /**
             * @brief 获取当前帧索引
             * @return 当前帧索引
             */
            static uint64_t GetCurrentFrameIndex();

            /**
             * @brief 获取帧计数器（自系统初始化以来）
             * @return 帧计数器
             */
            static uint64_t GetFrameCounter();

            /**
             * @brief 等待GPU空闲
             * 等待所有命令队列完成
             */
            static void WaitIdle();

            /**
             * @brief 获取GPU属性
             * @param index GPU索引
             * @param outProperties 输出属性
             * @return 是否成功获取
             */
            static bool GetGPUProperties(uint32_t index, GPUProperties &outProperties);

            /**
             * @brief 获取可用的GPU数量
             * @return GPU数量
             */
            static uint32_t GetGPUCount();

            /**
             * @brief 获取已使用的GPU索引
             * @return GPU索引
             */
            static uint32_t GetActiveGPUIndex();

            /**
             * @brief 创建交换链
             * @param window 窗口句柄
             * @param width 宽度
             * @param height 高度
             * @param vsync 是否开启垂直同步
             * @return 交换链，失败返回nullptr
             */
            static ISwapChain *CreateSwapChain(void *window, uint32_t width, uint32_t height, bool vsync = true);

            /**
             * @brief 销毁交换链
             * @param swapChain 交换链
             */
            static void DestroySwapChain(ISwapChain *swapChain);

        private:
            // 私有实现，禁止直接创建实例
            GraphicsSystem() = delete;
            ~GraphicsSystem() = delete;
            GraphicsSystem(const GraphicsSystem &) = delete;
            GraphicsSystem &operator=(const GraphicsSystem &) = delete;
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_GRAPHICS_SYSTEM_H