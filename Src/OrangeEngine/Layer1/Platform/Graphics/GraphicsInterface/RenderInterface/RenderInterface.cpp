/**
 * @file RenderInterface.cpp
 * @brief 渲染接口实现 - 设备工厂
 */

#include "RenderInterface.h"
#include "../../GraphicsAPI/Vulkan/VulkanInterface/VulkanDevice.h"
#include <stdexcept>
#include <vector>

namespace Orange
{
    namespace Graphics
    {
        IRenderDevice *CreateRenderDevice(RenderAPI api, const DeviceCreateInfo &createInfo)
        {
            switch (api)
            {
            case RenderAPI::Vulkan:
            {
                auto vulkanDevice = new Vulkan::VulkanDevice();
                if (vulkanDevice->Initialize(createInfo))
                {
                    return vulkanDevice;
                }
                delete vulkanDevice;
                return nullptr;
            }
            case RenderAPI::D3D12:
                // TODO: 实现D3D12设备创建
                throw std::runtime_error("D3D12 API not implemented yet");
            case RenderAPI::OpenGL:
                // TODO: 实现OpenGL设备创建
                throw std::runtime_error("OpenGL API not implemented yet");
            case RenderAPI::Metal:
                // TODO: 实现Metal设备创建
                throw std::runtime_error("Metal API not implemented yet");
            default:
                throw std::runtime_error("Unsupported render API");
            }
        }

        void DestroyRenderDevice(IRenderDevice *device)
        {
            if (device)
            {
                device->Shutdown();
                delete device;
            }
        }

        std::vector<RenderAPI> GetSupportedRenderAPIs()
        {
            std::vector<RenderAPI> supportedAPIs;

            // Vulkan总是支持的
            supportedAPIs.push_back(RenderAPI::Vulkan);

            // TODO: 检查其他API的支持情况
            // #ifdef ORANGE_SUPPORT_D3D12
            //     supportedAPIs.push_back(RenderAPI::D3D12);
            // #endif
            // #ifdef ORANGE_SUPPORT_OPENGL
            //     supportedAPIs.push_back(RenderAPI::OpenGL);
            // #endif
            // #ifdef ORANGE_SUPPORT_METAL
            //     supportedAPIs.push_back(RenderAPI::Metal);
            // #endif

            return supportedAPIs;
        }

        bool IsRenderAPIAvailable(RenderAPI api)
        {
            auto supportedAPIs = GetSupportedRenderAPIs();
            return std::find(supportedAPIs.begin(), supportedAPIs.end(), api) != supportedAPIs.end();
        }

        RenderAPI GetPreferredRenderAPI()
        {
            // 优先级顺序：Vulkan > D3D12 > OpenGL > Metal
            auto supportedAPIs = GetSupportedRenderAPIs();

            if (std::find(supportedAPIs.begin(), supportedAPIs.end(), RenderAPI::Vulkan) != supportedAPIs.end())
                return RenderAPI::Vulkan;
            if (std::find(supportedAPIs.begin(), supportedAPIs.end(), RenderAPI::D3D12) != supportedAPIs.end())
                return RenderAPI::D3D12;
            if (std::find(supportedAPIs.begin(), supportedAPIs.end(), RenderAPI::OpenGL) != supportedAPIs.end())
                return RenderAPI::OpenGL;
            if (std::find(supportedAPIs.begin(), supportedAPIs.end(), RenderAPI::Metal) != supportedAPIs.end())
                return RenderAPI::Metal;

            throw std::runtime_error("No supported render API found");
        }

        bool InitializeRenderSystem()
        {
            // 初始化渲染系统全局状态
            // 这里可以进行一些全局初始化工作，比如加载驱动、检查硬件支持等
            return true;
        }

        void ShutdownRenderSystem()
        {
            // 清理渲染系统全局状态
            // 这里可以进行一些全局清理工作
        }

        const char *GetRenderAPIName(RenderAPI api)
        {
            switch (api)
            {
            case RenderAPI::Vulkan:
                return "Vulkan";
            case RenderAPI::D3D12:
                return "Direct3D 12";
            case RenderAPI::OpenGL:
                return "OpenGL";
            case RenderAPI::Metal:
                return "Metal";
            default:
                return "Unknown";
            }
        }

        uint32_t GetRenderAPIVersion(RenderAPI api)
        {
            switch (api)
            {
            case RenderAPI::Vulkan:
                return VK_API_VERSION_1_3; // Vulkan 1.3
            case RenderAPI::D3D12:
                return 12; // D3D12
            case RenderAPI::OpenGL:
                return 460; // OpenGL 4.6
            case RenderAPI::Metal:
                return 3; // Metal 3
            default:
                return 0;
            }
        }

        bool ValidateDeviceCreateInfo(const DeviceCreateInfo &createInfo)
        {
            // 验证设备创建信息的有效性
            if (createInfo.api == RenderAPI::Vulkan)
            {
                // Vulkan特定验证
                return true;
            }

            // 其他API的验证
            return true;
        }

    } // namespace Graphics
} // namespace Orange
