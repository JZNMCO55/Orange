#include "Graphics.h"
#include "../GraphicsAPI/Vulkan/VulkanGraphicsSystem.h"
#include <iostream>

namespace Orange::Graphics
{

    std::unique_ptr<GraphicsSystem> GraphicsFactory::CreateGraphicsSystem(GraphicsAPI api)
    {
        switch (api)
        {
        case GraphicsAPI::Vulkan:
            return std::make_unique<Vulkan::VulkanGraphicsSystem>();

        case GraphicsAPI::DirectX12:
            std::cerr << "DirectX12 not implemented yet" << std::endl;
            return nullptr;

        case GraphicsAPI::OpenGL:
            std::cerr << "OpenGL not implemented yet" << std::endl;
            return nullptr;

        case GraphicsAPI::None:
        default:
            std::cerr << "Invalid graphics API specified" << std::endl;
            return nullptr;
        }
    }

    GraphicsAPI GraphicsFactory::GetDefaultAPI()
    {
#ifdef ORANGE_VULKAN_ENABLED
        return GraphicsAPI::Vulkan;
#else
        return GraphicsAPI::None;
#endif
    }

    bool GraphicsFactory::IsAPISupported(GraphicsAPI api)
    {
        switch (api)
        {
        case GraphicsAPI::Vulkan:
#ifdef ORANGE_VULKAN_ENABLED
            return true;
#else
            return false;
#endif

        case GraphicsAPI::DirectX12:
            // TODO: 检查DirectX12支持
            return false;

        case GraphicsAPI::OpenGL:
            // TODO: 检查OpenGL支持
            return false;

        case GraphicsAPI::None:
        default:
            return false;
        }
    }

    std::vector<GraphicsAPI> GraphicsFactory::GetSupportedAPIs()
    {
        std::vector<GraphicsAPI> supportedAPIs;

        if (IsAPISupported(GraphicsAPI::Vulkan))
        {
            supportedAPIs.push_back(GraphicsAPI::Vulkan);
        }

        if (IsAPISupported(GraphicsAPI::DirectX12))
        {
            supportedAPIs.push_back(GraphicsAPI::DirectX12);
        }

        if (IsAPISupported(GraphicsAPI::OpenGL))
        {
            supportedAPIs.push_back(GraphicsAPI::OpenGL);
        }

        return supportedAPIs;
    }

    std::string GraphicsFactory::GetAPIName(GraphicsAPI api)
    {
        switch (api)
        {
        case GraphicsAPI::Vulkan:
            return "Vulkan";
        case GraphicsAPI::DirectX12:
            return "DirectX 12";
        case GraphicsAPI::OpenGL:
            return "OpenGL";
        case GraphicsAPI::None:
            return "None";
        default:
            return "Unknown";
        }
    }

} // namespace Orange::Graphics