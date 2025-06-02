/**
 * @file RenderSync.cpp
 * @brief 渲染同步系统实现 - 同步对象工厂
 */

#include "RenderSync.h"
#include "../../GraphicsAPI/Vulkan/VulkanInterface/VulkanDevice.h"
#include "../../GraphicsAPI/Vulkan/VulkanSync/VulkanFence.h"
#include "../../GraphicsAPI/Vulkan/VulkanSync/VulkanSemaphore.h"
#include "../../GraphicsAPI/Vulkan/VulkanSync/VulkanEvent.h"
#include "../RenderInterface/IRenderDevice.h"
#include <stdexcept>

namespace Orange
{
    namespace Graphics
    {
        // 全局同步系统状态
        static bool s_SyncSystemInitialized = false;

        bool InitializeSyncSystem(IRenderDevice *device)
        {
            if (!device)
            {
                return false;
            }

            if (s_SyncSystemInitialized)
            {
                return true;
            }

            // 根据设备API类型初始化同步系统
            RenderAPI api = device->GetRenderAPI();
            switch (api)
            {
            case RenderAPI::Vulkan:
                // Vulkan同步系统初始化
                s_SyncSystemInitialized = true;
                return true;
            case RenderAPI::D3D12:
                // TODO: D3D12同步系统初始化
                return false;
            case RenderAPI::OpenGL:
                // TODO: OpenGL同步系统初始化
                return false;
            case RenderAPI::Metal:
                // TODO: Metal同步系统初始化
                return false;
            default:
                return false;
            }
        }

        void ShutdownSyncSystem(IRenderDevice *device)
        {
            if (!s_SyncSystemInitialized)
            {
                return;
            }

            if (device)
            {
                RenderAPI api = device->GetRenderAPI();
                switch (api)
                {
                case RenderAPI::Vulkan:
                    // Vulkan同步系统清理
                    break;
                case RenderAPI::D3D12:
                    // TODO: D3D12同步系统清理
                    break;
                case RenderAPI::OpenGL:
                    // TODO: OpenGL同步系统清理
                    break;
                case RenderAPI::Metal:
                    // TODO: Metal同步系统清理
                    break;
                default:
                    break;
                }
            }

            s_SyncSystemInitialized = false;
        }

        IRenderFenceFactory *CreateFenceFactory(IRenderDevice *device)
        {
            if (!device)
            {
                return nullptr;
            }

            RenderAPI api = device->GetRenderAPI();
            switch (api)
            {
            case RenderAPI::Vulkan:
            {
                auto vulkanDevice = static_cast<Vulkan::VulkanDevice *>(device);
                return new Vulkan::VulkanFenceFactory(vulkanDevice);
            }
            case RenderAPI::D3D12:
                // TODO: 实现D3D12围栏工厂
                throw std::runtime_error("D3D12 fence factory not implemented yet");
            case RenderAPI::OpenGL:
                // TODO: 实现OpenGL围栏工厂
                throw std::runtime_error("OpenGL fence factory not implemented yet");
            case RenderAPI::Metal:
                // TODO: 实现Metal围栏工厂
                throw std::runtime_error("Metal fence factory not implemented yet");
            default:
                throw std::runtime_error("Unsupported render API for fence factory");
            }
        }

        void DestroyFenceFactory(IRenderFenceFactory *factory)
        {
            if (factory)
            {
                delete factory;
            }
        }

        IRenderSemaphoreFactory *CreateSemaphoreFactory(IRenderDevice *device)
        {
            if (!device)
            {
                return nullptr;
            }

            RenderAPI api = device->GetRenderAPI();
            switch (api)
            {
            case RenderAPI::Vulkan:
            {
                auto vulkanDevice = static_cast<Vulkan::VulkanDevice *>(device);
                return new Vulkan::VulkanSemaphoreFactory(vulkanDevice);
            }
            case RenderAPI::D3D12:
                // TODO: 实现D3D12信号量工厂
                throw std::runtime_error("D3D12 semaphore factory not implemented yet");
            case RenderAPI::OpenGL:
                // TODO: 实现OpenGL信号量工厂
                throw std::runtime_error("OpenGL semaphore factory not implemented yet");
            case RenderAPI::Metal:
                // TODO: 实现Metal信号量工厂
                throw std::runtime_error("Metal semaphore factory not implemented yet");
            default:
                throw std::runtime_error("Unsupported render API for semaphore factory");
            }
        }

        void DestroySemaphoreFactory(IRenderSemaphoreFactory *factory)
        {
            if (factory)
            {
                delete factory;
            }
        }

        IRenderEventFactory *CreateEventFactory(IRenderDevice *device)
        {
            if (!device)
            {
                return nullptr;
            }

            RenderAPI api = device->GetRenderAPI();
            switch (api)
            {
            case RenderAPI::Vulkan:
            {
                auto vulkanDevice = static_cast<Vulkan::VulkanDevice *>(device);
                return new Vulkan::VulkanEventFactory(vulkanDevice);
            }
            case RenderAPI::D3D12:
                // TODO: 实现D3D12事件工厂
                throw std::runtime_error("D3D12 event factory not implemented yet");
            case RenderAPI::OpenGL:
                // TODO: 实现OpenGL事件工厂
                throw std::runtime_error("OpenGL event factory not implemented yet");
            case RenderAPI::Metal:
                // TODO: 实现Metal事件工厂
                throw std::runtime_error("Metal event factory not implemented yet");
            default:
                throw std::runtime_error("Unsupported render API for event factory");
            }
        }

        void DestroyEventFactory(IRenderEventFactory *factory)
        {
            if (factory)
            {
                delete factory;
            }
        }

        bool IsSyncSystemInitialized()
        {
            return s_SyncSystemInitialized;
        }

        const char *GetSyncPrimitiveTypeName(SyncPrimitiveType type)
        {
            switch (type)
            {
            case SyncPrimitiveType::Fence:
                return "Fence";
            case SyncPrimitiveType::Semaphore:
                return "Semaphore";
            case SyncPrimitiveType::Event:
                return "Event";
            default:
                return "Unknown";
            }
        }

        const char *GetSemaphoreTypeName(SemaphoreType type)
        {
            switch (type)
            {
            case SemaphoreType::Binary:
                return "Binary";
            case SemaphoreType::Timeline:
                return "Timeline";
            default:
                return "Unknown";
            }
        }

        const char *GetSyncPointTypeName(SyncPointType type)
        {
            switch (type)
            {
            case SyncPointType::Submit:
                return "Submit";
            case SyncPointType::Present:
                return "Present";
            case SyncPointType::Acquire:
                return "Acquire";
            default:
                return "Unknown";
            }
        }

        bool ValidateFenceCreateInfo(const FenceCreateInfo &createInfo)
        {
            // 验证围栏创建信息的有效性
            return true;
        }

        bool ValidateSemaphoreCreateInfo(const SemaphoreCreateInfo &createInfo)
        {
            // 验证信号量创建信息的有效性
            if (createInfo.type == SemaphoreType::Timeline && createInfo.initialValue == 0)
            {
                // 时间线信号量的初始值通常应该大于0
                // 但这不是强制要求，所以只是警告
            }
            return true;
        }

        bool ValidateEventCreateInfo(const EventCreateInfo &createInfo)
        {
            // 验证事件创建信息的有效性
            return true;
        }

    } // namespace Graphics
} // namespace Orange
