/**
 * @file VulkanDevice.cpp
 * @brief Vulkan渲染设备实现
 */

#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "VulkanRenderPass.h"
#include "VulkanContext.h"
#include "../VulkanResources/VulkanBuffer.h"
#include "../VulkanResources/VulkanTexture.h"
#include "../VulkanResources/VulkanSampler.h"
#include "../VulkanResources/VulkanShader.h"
#include "../VulkanInterface/VulkanPipeline.h"
#include "../VulkanSync/VulkanSyncAll.h"
#include <iostream>
#include <set>
#include <cstring>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            // 验证层
            const std::vector<const char *> validationLayers = {
                "VK_LAYER_KHRONOS_validation"};

            // 设备扩展
            const std::vector<const char *> deviceExtensions = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME};

            VulkanDevice::VulkanDevice()
            {
            }

            VulkanDevice::~VulkanDevice()
            {
                Shutdown();
            }

            bool VulkanDevice::Initialize(const DeviceCreateInfo &createInfo)
            {
                m_createInfo = createInfo;
                m_enableValidationLayers = (createInfo.flags & static_cast<uint32_t>(CreateFlag::Validation)) != 0;

                std::cout << "VulkanDevice::Initialize: 开始初始化Vulkan设备" << std::endl;

                if (!CreateInstance())
                {
                    std::cerr << "VulkanDevice::Initialize: 创建Vulkan实例失败" << std::endl;
                    return false;
                }

                if (m_enableValidationLayers && !SetupDebugMessenger())
                {
                    std::cerr << "VulkanDevice::Initialize: 设置调试信使失败" << std::endl;
                    return false;
                }

                if (!CreateSurface())
                {
                    std::cerr << "VulkanDevice::Initialize: 创建表面失败" << std::endl;
                    return false;
                }

                if (!PickPhysicalDevice())
                {
                    std::cerr << "VulkanDevice::Initialize: 选择物理设备失败" << std::endl;
                    return false;
                }

                if (!CreateLogicalDevice())
                {
                    std::cerr << "VulkanDevice::Initialize: 创建逻辑设备失败" << std::endl;
                    return false;
                }

                if (!CreateCommandPool())
                {
                    std::cerr << "VulkanDevice::Initialize: 创建命令池失败" << std::endl;
                    return false;
                }

                std::cout << "VulkanDevice::Initialize: Vulkan设备初始化成功" << std::endl;
                return true;
            }

            void VulkanDevice::Shutdown()
            {
                if (m_device != VK_NULL_HANDLE)
                {
                    vkDeviceWaitIdle(m_device);

                    if (m_commandPool != VK_NULL_HANDLE)
                    {
                        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
                        m_commandPool = VK_NULL_HANDLE;
                    }

                    vkDestroyDevice(m_device, nullptr);
                    m_device = VK_NULL_HANDLE;
                }

                if (m_surface != VK_NULL_HANDLE)
                {
                    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
                    m_surface = VK_NULL_HANDLE;
                }

                if (m_enableValidationLayers && m_debugMessenger != VK_NULL_HANDLE)
                {
                    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
                    if (func != nullptr)
                    {
                        func(m_instance, m_debugMessenger, nullptr);
                    }
                    m_debugMessenger = VK_NULL_HANDLE;
                }

                if (m_instance != VK_NULL_HANDLE)
                {
                    vkDestroyInstance(m_instance, nullptr);
                    m_instance = VK_NULL_HANDLE;
                }

                std::cout << "VulkanDevice::Shutdown: Vulkan设备已关闭" << std::endl;
            }

            bool VulkanDevice::SupportsFeature(RenderFeature feature) const
            {
                // 这里应该根据物理设备特性来判断
                // 暂时返回false，需要根据实际需求实现
                return false;
            }

            void VulkanDevice::WaitIdle()
            {
                if (m_device != VK_NULL_HANDLE)
                {
                    vkDeviceWaitIdle(m_device);
                }
            }

            IRenderBuffer *VulkanDevice::CreateBuffer(const BufferCreateInfo &createInfo)
            {
                auto buffer = new VulkanBuffer(this);
                if (!buffer->Initialize(createInfo))
                {
                    delete buffer;
                    return nullptr;
                }
                return buffer;
            }

            IRenderTexture *VulkanDevice::CreateTexture(const TextureCreateInfo &createInfo)
            {
                auto texture = new VulkanTexture(this);
                if (!texture->Initialize(createInfo))
                {
                    delete texture;
                    return nullptr;
                }
                return texture;
            }

            IRenderSampler *VulkanDevice::CreateSampler(const SamplerCreateInfo &createInfo)
            {
                auto sampler = new VulkanSampler(this);
                if (!sampler->Initialize(createInfo))
                {
                    delete sampler;
                    return nullptr;
                }
                return sampler;
            }

            IShaderModule *VulkanDevice::CreateShaderModule(const ShaderCreateInfo &createInfo)
            {
                auto shader = new VulkanShader(this);
                if (!shader->Initialize(createInfo))
                {
                    delete shader;
                    return nullptr;
                }
                return shader;
            }

            IRenderPipeline *VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineCreateInfo &createInfo)
            {
                auto pipeline = new VulkanPipeline(this);
                if (!pipeline->InitializeGraphics(createInfo))
                {
                    delete pipeline;
                    return nullptr;
                }
                return pipeline;
            }

            IRenderPass *VulkanDevice::CreateRenderPass(const RenderPassCreateInfo &createInfo)
            {
                auto renderPass = new VulkanRenderPass(this);
                if (!renderPass->Initialize(createInfo))
                {
                    delete renderPass;
                    return nullptr;
                }
                return renderPass;
            }

            ISwapChain *VulkanDevice::CreateSwapChain(const SwapChainCreateInfo &createInfo)
            {
                auto swapChain = new VulkanSwapChain(this);
                if (!swapChain->Initialize(createInfo))
                {
                    delete swapChain;
                    return nullptr;
                }
                return swapChain;
            }

            IRenderFence*VulkanDevice::CreateFence(bool signaled)
            {
                auto fence = new VulkanFence(this);
                if (!fence->Initialize(signaled))
                {
                    delete fence;
                    return nullptr;
                }
                return fence;
            }

            IRenderSemaphore *VulkanDevice::CreateSemaphore()
            {
                auto semaphore = new VulkanSemaphore(this);
                if (!semaphore->Initialize())
                {
                    delete semaphore;
                    return nullptr;
                }
                return semaphore;
            }

            IRenderContext *VulkanDevice::CreateContext()
            {
                auto context = new VulkanContext(this);
                if (!context->Initialize())
                {
                    delete context;
                    return nullptr;
                }
                return context;
            }

            bool VulkanDevice::CreateInstance()
            {
                if (m_enableValidationLayers && !CheckValidationLayerSupport())
                {
                    std::cerr << "VulkanDevice::CreateInstance: 验证层不可用" << std::endl;
                    return false;
                }

                VkApplicationInfo appInfo{};
                appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
                appInfo.pApplicationName = "Orange Engine";
                appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
                appInfo.pEngineName = "Orange Engine";
                appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
                appInfo.apiVersion = VK_API_VERSION_1_0;

                VkInstanceCreateInfo createInfo{};
                createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
                createInfo.pApplicationInfo = &appInfo;

                auto extensions = GetRequiredExtensions();
                createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
                createInfo.ppEnabledExtensionNames = extensions.data();

                VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
                if (m_enableValidationLayers)
                {
                    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
                    createInfo.ppEnabledLayerNames = validationLayers.data();

                    debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
                    debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                                      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                    debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                                  VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                                  VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                    debugCreateInfo.pfnUserCallback = DebugCallback;

                    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
                }
                else
                {
                    createInfo.enabledLayerCount = 0;
                    createInfo.pNext = nullptr;
                }

                if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
                {
                    std::cerr << "VulkanDevice::CreateInstance: 创建Vulkan实例失败" << std::endl;
                    return false;
                }

                return true;
            }

            bool VulkanDevice::SetupDebugMessenger()
            {
                if (!m_enableValidationLayers)
                    return true;

                VkDebugUtilsMessengerCreateInfoEXT createInfo{};
                createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
                createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
                createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
                createInfo.pfnUserCallback = DebugCallback;

                auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
                if (func != nullptr)
                {
                    return func(m_instance, &createInfo, nullptr, &m_debugMessenger) == VK_SUCCESS;
                }
                else
                {
                    return false;
                }
            }

            bool VulkanDevice::CreateSurface()
            {
                // 这里需要根据平台创建表面
                // 暂时假设表面已经在外部创建
                // 在实际实现中，需要根据窗口句柄创建表面
                return true;
            }

            bool VulkanDevice::PickPhysicalDevice()
            {
                uint32_t deviceCount = 0;
                vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

                if (deviceCount == 0)
                {
                    std::cerr << "VulkanDevice::PickPhysicalDevice: 没有找到支持Vulkan的GPU" << std::endl;
                    return false;
                }

                std::vector<VkPhysicalDevice> devices(deviceCount);
                vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

                for (const auto &device : devices)
                {
                    if (IsDeviceSuitable(device))
                    {
                        m_physicalDevice = device;
                        break;
                    }
                }

                if (m_physicalDevice == VK_NULL_HANDLE)
                {
                    std::cerr << "VulkanDevice::PickPhysicalDevice: 没有找到合适的GPU" << std::endl;
                    return false;
                }

                return true;
            }

            bool VulkanDevice::CreateLogicalDevice()
            {
                m_queueFamilyIndices = FindQueueFamilies(m_physicalDevice, m_surface);

                std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
                std::set<uint32_t> uniqueQueueFamilies = {
                    m_queueFamilyIndices.graphicsFamily.value(),
                    m_queueFamilyIndices.presentFamily.value()};

                float queuePriority = 1.0f;
                for (uint32_t queueFamily : uniqueQueueFamilies)
                {
                    VkDeviceQueueCreateInfo queueCreateInfo{};
                    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                    queueCreateInfo.queueFamilyIndex = queueFamily;
                    queueCreateInfo.queueCount = 1;
                    queueCreateInfo.pQueuePriorities = &queuePriority;
                    queueCreateInfos.push_back(queueCreateInfo);
                }

                VkPhysicalDeviceFeatures deviceFeatures{};

                VkDeviceCreateInfo createInfo{};
                createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
                createInfo.pQueueCreateInfos = queueCreateInfos.data();
                createInfo.pEnabledFeatures = &deviceFeatures;
                createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
                createInfo.ppEnabledExtensionNames = deviceExtensions.data();

                if (m_enableValidationLayers)
                {
                    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
                    createInfo.ppEnabledLayerNames = validationLayers.data();
                }
                else
                {
                    createInfo.enabledLayerCount = 0;
                }

                if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
                {
                    std::cerr << "VulkanDevice::CreateLogicalDevice: 创建逻辑设备失败" << std::endl;
                    return false;
                }

                vkGetDeviceQueue(m_device, m_queueFamilyIndices.graphicsFamily.value(), 0, &m_graphicsQueue);
                vkGetDeviceQueue(m_device, m_queueFamilyIndices.presentFamily.value(), 0, &m_presentQueue);

                return true;
            }

            bool VulkanDevice::CreateCommandPool()
            {
                VkCommandPoolCreateInfo poolInfo{};
                poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                poolInfo.queueFamilyIndex = m_queueFamilyIndices.graphicsFamily.value();

                if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
                {
                    std::cerr << "VulkanDevice::CreateCommandPool: 创建命令池失败" << std::endl;
                    return false;
                }

                return true;
            }

            bool VulkanDevice::IsDeviceSuitable(VkPhysicalDevice device)
            {
                QueueFamilyIndices indices = FindQueueFamilies(device, m_surface);

                bool extensionsSupported = CheckDeviceExtensionSupport(device, deviceExtensions);

                bool swapChainAdequate = false;
                if (extensionsSupported)
                {
                    SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device, m_surface);
                    swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
                }

                return indices.isComplete() && extensionsSupported && swapChainAdequate;
            }

            std::vector<const char *> VulkanDevice::GetRequiredExtensions()
            {
                std::vector<const char *> extensions;

                // 添加平台特定的扩展
                // 这里需要根据实际平台添加相应的扩展
                // 例如：VK_KHR_surface, VK_KHR_win32_surface等

                if (m_enableValidationLayers)
                {
                    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                }

                return extensions;
            }

            bool VulkanDevice::CheckValidationLayerSupport()
            {
                uint32_t layerCount;
                vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

                std::vector<VkLayerProperties> availableLayers(layerCount);
                vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

                for (const char *layerName : validationLayers)
                {
                    bool layerFound = false;

                    for (const auto &layerProperties : availableLayers)
                    {
                        if (strcmp(layerName, layerProperties.layerName) == 0)
                        {
                            layerFound = true;
                            break;
                        }
                    }

                    if (!layerFound)
                    {
                        return false;
                    }
                }

                return true;
            }

            VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::DebugCallback(
                VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                VkDebugUtilsMessageTypeFlagsEXT messageType,
                const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                void *pUserData)
            {
                std::cerr << "Vulkan验证层: " << pCallbackData->pMessage << std::endl;
                return VK_FALSE;
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange