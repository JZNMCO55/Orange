/**
 * @file GraphicsSystem.cpp
 * @brief 图形系统入口实现
 */

#include "GraphicsSystem.h"
#include "RenderInterface/RenderInterface.h"
#include "RenderSync/RenderSync.h"
#include "RenderMemory/RenderMemory.h"

namespace Orange
{
    namespace Graphics
    {
        // 静态成员变量
        static bool s_Initialized = false;
        static RenderAPI s_ActiveBackend = RenderAPI::Unknown;
        static IRenderDevice *s_Device = nullptr;
        static IRenderContext *s_Context = nullptr;
        static IRenderCommandQueueFactory *s_CommandQueueFactory = nullptr;
        static IRenderCommandBufferFactory *s_CommandBufferFactory = nullptr;
        static IMemoryManager *s_MemoryManager = nullptr;
        static IRenderFenceFactory *s_FenceFactory = nullptr;
        static IRenderSemaphoreFactory *s_SemaphoreFactory = nullptr;
        static IRenderEventFactory *s_EventFactory = nullptr;
        static GraphicsSystemConfig s_Config;
        static uint64_t s_CurrentFrameIndex = 0;
        static uint64_t s_FrameCounter = 0;
        static uint32_t s_ActiveGPUIndex = 0;

        bool GraphicsSystem::Initialize(const GraphicsSystemConfig &config)
        {
            if (s_Initialized)
            {
                // 已经初始化，直接返回
                return true;
            }

            // 保存配置
            s_Config = config;

            // 初始化渲染系统
            if (!InitializeRenderSystem())
            {
                return false;
            }

            // 选择渲染后端
            s_ActiveBackend = static_cast<RenderAPI>(config.preferredBackend);

            // 创建设备创建信息
            DeviceCreateInfo deviceCreateInfo;
            deviceCreateInfo.api = config.preferredBackend;
            deviceCreateInfo.flags = CreateFlag::None;

            // 设置请求的特性
            if (config.requiredFeatures != static_cast<DeviceFeatureFlags>(DeviceFeatureFlagBits::None))
            {
                // TODO: 将DeviceFeatureFlags转换为DeviceFeatures
                // deviceCreateInfo.features = ConvertFeatureFlags(config.requiredFeatures);
            }

            // 设置窗口句柄（如果有）
            deviceCreateInfo.windowHandle = config.instanceUserData;

            // 根据后端创建设备
            s_Device = CreateRenderDevice(config.preferredBackend, deviceCreateInfo);
            if (!s_Device)
            {
                ShutdownRenderSystem();
                return false;
            }

            // 创建渲染上下文
            s_Context = s_Device->CreateContext();
            if (!s_Context)
            {
                DestroyRenderDevice(s_Device);
                s_Device = nullptr;
                ShutdownRenderSystem();
                return false;
            }

            // 初始化同步系统
            if (!InitializeSyncSystem(s_Device))
            {
                s_Context = nullptr; // 设备会自动清理上下文
                DestroyRenderDevice(s_Device);
                s_Device = nullptr;
                ShutdownRenderSystem();
                return false;
            }

            // 创建同步对象工厂
            s_FenceFactory = CreateFenceFactory(s_Device);
            s_SemaphoreFactory = CreateSemaphoreFactory(s_Device);
            s_EventFactory = CreateEventFactory(s_Device);

            if (!s_FenceFactory || !s_SemaphoreFactory || !s_EventFactory)
            {
                // 清理已创建的工厂
                if (s_EventFactory)
                    DestroyEventFactory(s_EventFactory);
                if (s_SemaphoreFactory)
                    DestroySemaphoreFactory(s_SemaphoreFactory);
                if (s_FenceFactory)
                    DestroyFenceFactory(s_FenceFactory);

                s_EventFactory = nullptr;
                s_SemaphoreFactory = nullptr;
                s_FenceFactory = nullptr;

                ShutdownSyncSystem(s_Device);
                s_Context = nullptr;
                DestroyRenderDevice(s_Device);
                s_Device = nullptr;
                ShutdownRenderSystem();
                return false;
            }

            // 初始化内存系统
            //if (!InitializeMemorySystem(s_Device))
            //{
            //    // 清理同步工厂
            //    DestroyEventFactory(s_EventFactory);
            //    DestroySemaphoreFactory(s_SemaphoreFactory);
            //    DestroyFenceFactory(s_FenceFactory);
            //    s_EventFactory = nullptr;
            //    s_SemaphoreFactory = nullptr;
            //    s_FenceFactory = nullptr;

            //    ShutdownSyncSystem(s_Device);
            //    s_Context = nullptr;
            //    DestroyRenderDevice(s_Device);
            //    s_Device = nullptr;
            //    ShutdownRenderSystem();
            //    return false;
            //}

            //// 创建内存管理器
            //s_MemoryManager = CreateMemoryManager(s_Device);
            //// 注意：简化实现中，内存管理器可能为nullptr，这是正常的
            //// 在这种情况下，我们使用设备内置的内存管理功能

            //// 注意：我们简化了架构，不使用单独的命令工厂
            //// 命令相关功能直接通过设备和上下文访问
            //s_CommandQueueFactory = nullptr;
            //s_CommandBufferFactory = nullptr;

            s_Initialized = true;
            return true;
        }

        void GraphicsSystem::Shutdown()
        {
            if (!s_Initialized)
            {
                return;
            }

            // 等待GPU空闲
            WaitIdle();

            // 清理内存管理器
            if (s_MemoryManager)
            {
                DestroyMemoryManager(s_MemoryManager);
                s_MemoryManager = nullptr;
            }

            // 关闭内存系统
            ShutdownMemorySystem(s_Device);

            // 清理同步工厂
            if (s_EventFactory)
            {
                DestroyEventFactory(s_EventFactory);
                s_EventFactory = nullptr;
            }

            if (s_SemaphoreFactory)
            {
                DestroySemaphoreFactory(s_SemaphoreFactory);
                s_SemaphoreFactory = nullptr;
            }

            if (s_FenceFactory)
            {
                DestroyFenceFactory(s_FenceFactory);
                s_FenceFactory = nullptr;
            }

            // 关闭同步系统
            ShutdownSyncSystem(s_Device);

            // 重置命令工厂指针（它们指向设备，不需要单独释放）
            s_CommandBufferFactory = nullptr;
            s_CommandQueueFactory = nullptr;

            // 清理上下文（设备会自动清理）
            s_Context = nullptr;

            // 清理设备
            if (s_Device)
            {
                DestroyRenderDevice(s_Device);
                s_Device = nullptr;
            }

            // 关闭渲染系统
            ShutdownRenderSystem();

            s_Initialized = false;
            s_ActiveBackend = RenderAPI::Unknown;
        }

        bool GraphicsSystem::IsInitialized()
        {
            return s_Initialized;
        }

        RenderAPI GraphicsSystem::GetActiveBackend()
        {
            return s_ActiveBackend;
        }

        IRenderDevice *GraphicsSystem::GetDevice()
        {
            return s_Device;
        }

        IRenderContext *GraphicsSystem::GetContext()
        {
            return s_Context;
        }

        IRenderCommandQueueFactory *GraphicsSystem::GetCommandQueueFactory()
        {
            return s_CommandQueueFactory;
        }

        IRenderCommandBufferFactory *GraphicsSystem::GetCommandBufferFactory()
        {
            return s_CommandBufferFactory;
        }

        IRenderCommandQueue *GraphicsSystem::GetMainGraphicsQueue()
        {
            // 直接通过设备获取主图形队列（如果设备支持）
            // 这里需要根据具体的设备实现来调整
            return nullptr;
        }

        IRenderCommandQueue *GraphicsSystem::GetMainComputeQueue()
        {
            // 直接通过设备获取主计算队列（如果设备支持）
            // 这里需要根据具体的设备实现来调整
            return nullptr;
        }

        IRenderCommandQueue *GraphicsSystem::GetMainTransferQueue()
        {
            // 直接通过设备获取主传输队列（如果设备支持）
            // 这里需要根据具体的设备实现来调整
            return nullptr;
        }

        IMemoryManager *GraphicsSystem::GetMemoryManager()
        {
            return s_MemoryManager;
        }

        IRenderFenceFactory *GraphicsSystem::GetFenceFactory()
        {
            return s_FenceFactory;
        }

        IRenderSemaphoreFactory *GraphicsSystem::GetSemaphoreFactory()
        {
            return s_SemaphoreFactory;
        }

        IRenderEventFactory *GraphicsSystem::GetEventFactory()
        {
            return s_EventFactory;
        }

        const GraphicsSystemConfig &GraphicsSystem::GetConfig()
        {
            return s_Config;
        }

        void GraphicsSystem::BeginFrame(uint64_t frameIndex)
        {
            s_CurrentFrameIndex = frameIndex;
            s_FrameCounter++;

            // 帧开始处理逻辑
            if (s_MemoryManager)
            {
                // 触发内存垃圾回收（如果需要）
                s_MemoryManager->TriggerGarbageCollection();
            }
        }

        void GraphicsSystem::EndFrame()
        {
            // 帧结束处理逻辑
            // 这里可以进行一些帧结束的清理工作
        }

        uint64_t GraphicsSystem::GetCurrentFrameIndex()
        {
            return s_CurrentFrameIndex;
        }

        uint64_t GraphicsSystem::GetFrameCounter()
        {
            return s_FrameCounter;
        }

        void GraphicsSystem::WaitIdle()
        {
            if (s_Device)
            {
                s_Device->WaitIdle();
            }
        }

        bool GraphicsSystem::GetGPUProperties(uint32_t index, GPUProperties &outProperties)
        {
            if (s_Device && index == s_ActiveGPUIndex)
            {
                // TODO: 实现GPU属性获取
                // return s_Device->GetGPUProperties(index, outProperties);
                return false;
            }
            return false;
        }

        uint32_t GraphicsSystem::GetGPUCount()
        {
            if (s_Device)
            {
                // TODO: 实现GPU数量获取
                // return s_Device->GetGPUCount();
                return 1; // 暂时返回1
            }
            return 0;
        }

        uint32_t GraphicsSystem::GetActiveGPUIndex()
        {
            return s_ActiveGPUIndex;
        }

        ISwapChain *GraphicsSystem::CreateSwapChain(void *window, uint32_t width, uint32_t height, bool vsync)
        {
            if (s_Device)
            {
                SwapChainCreateInfo createInfo;
                createInfo.surface = window;
                createInfo.width = width;
                createInfo.height = height;
                createInfo.vsync = vsync;
                createInfo.name = "MainSwapChain";

                return s_Device->CreateSwapChain(createInfo);
            }
            return nullptr;
        }

        void GraphicsSystem::DestroySwapChain(ISwapChain *swapChain)
        {
            if (s_Device && swapChain)
            {
                // 交换链通常由设备管理，这里可能需要特殊处理
                delete swapChain;
            }
        }

    } // namespace Graphics
} // namespace Orange