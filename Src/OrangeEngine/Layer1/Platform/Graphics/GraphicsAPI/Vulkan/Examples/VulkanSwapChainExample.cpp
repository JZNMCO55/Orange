/**
 * @file VulkanSwapChainExample.cpp
 * @brief Vulkan交换链使用示例实现
 */
#ifdef VULKAN_SWAP_CHAIN_EXAMPLE_H_
#include "VulkanSwapChainExample.h"
#include <iostream>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            VulkanSwapChainExample::VulkanSwapChainExample(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanSwapChainExample::~VulkanSwapChainExample()
            {
                Cleanup();
            }

            bool VulkanSwapChainExample::Initialize(VkSurfaceKHR surface, uint32_t width, uint32_t height)
            {
                if (!m_device)
                {
                    std::cerr << "VulkanSwapChainExample::Initialize: 无效的设备指针" << std::endl;
                    return false;
                }

                std::cout << "VulkanSwapChainExample::Initialize: 开始初始化交换链示例" << std::endl;

                // 创建交换链创建信息
                SwapChainCreateInfo createInfo{};
                createInfo.surface = surface;
                createInfo.width = width;
                createInfo.height = height;
                createInfo.format = PixelFormat::BGRA8_SRGB;
                createInfo.imageCount = 3; // 三重缓冲
                createInfo.vsync = true;
                createInfo.name = "示例交换链";

                // 创建交换链
                auto swapChain = m_device->CreateSwapChain(createInfo);
                if (!swapChain)
                {
                    std::cerr << "VulkanSwapChainExample::Initialize: 创建交换链失败" << std::endl;
                    return false;
                }

                m_swapChain = std::shared_ptr<VulkanSwapChain>(static_cast<VulkanSwapChain *>(swapChain));

                if (!ValidateSwapChain())
                {
                    std::cerr << "VulkanSwapChainExample::Initialize: 交换链验证失败" << std::endl;
                    return false;
                }

                PrintSwapChainInfo();

                std::cout << "VulkanSwapChainExample::Initialize: 交换链示例初始化成功" << std::endl;
                return true;
            }

            void VulkanSwapChainExample::Cleanup()
            {
                if (m_swapChain)
                {
                    m_swapChain.reset();
                    std::cout << "VulkanSwapChainExample::Cleanup: 交换链示例已清理" << std::endl;
                }
            }

            bool VulkanSwapChainExample::DemoBasicSwapChainOperations()
            {
                if (!m_swapChain)
                {
                    std::cerr << "VulkanSwapChainExample::DemoBasicSwapChainOperations: 交换链未初始化" << std::endl;
                    return false;
                }

                std::cout << "=== 演示基本交换链操作 ===" << std::endl;

                // 获取交换链信息
                std::cout << "交换链图像数量: " << m_swapChain->GetImageCount() << std::endl;
                std::cout << "交换链尺寸: " << m_swapChain->GetWidth() << "x" << m_swapChain->GetHeight() << std::endl;
                std::cout << "当前图像索引: " << m_swapChain->GetCurrentImageIndex() << std::endl;
                std::cout << "垂直同步状态: " << (m_swapChain->IsVSyncEnabled() ? "启用" : "禁用") << std::endl;

                // 获取交换链图像
                for (uint32_t i = 0; i < m_swapChain->GetImageCount(); ++i)
                {
                    auto image = m_swapChain->GetImage(i);
                    if (image)
                    {
                        std::cout << "图像 " << i << ": 宽度=" << image->GetWidth()
                                  << ", 高度=" << image->GetHeight()
                                  << ", 格式=" << static_cast<int>(image->GetFormat()) << std::endl;
                    }
                    else
                    {
                        std::cerr << "获取图像 " << i << " 失败" << std::endl;
                        return false;
                    }
                }

                // 获取当前图像
                auto currentImage = m_swapChain->GetCurrentImage();
                if (currentImage)
                {
                    std::cout << "当前图像: 宽度=" << currentImage->GetWidth()
                              << ", 高度=" << currentImage->GetHeight() << std::endl;
                }

                // 模拟获取下一个图像（不使用信号量）
                uint32_t nextImageIndex = m_swapChain->AcquireNextImage(nullptr);
                if (nextImageIndex != UINT32_MAX)
                {
                    std::cout << "成功获取下一个图像，索引: " << nextImageIndex << std::endl;
                }
                else
                {
                    std::cout << "获取下一个图像失败（可能需要重新创建交换链）" << std::endl;
                }

                std::cout << "=== 基本交换链操作演示完成 ===" << std::endl;
                return true;
            }

            bool VulkanSwapChainExample::DemoSwapChainResize(uint32_t newWidth, uint32_t newHeight)
            {
                if (!m_swapChain)
                {
                    std::cerr << "VulkanSwapChainExample::DemoSwapChainResize: 交换链未初始化" << std::endl;
                    return false;
                }

                std::cout << "=== 演示交换链调整大小 ===" << std::endl;

                uint32_t oldWidth = m_swapChain->GetWidth();
                uint32_t oldHeight = m_swapChain->GetHeight();

                std::cout << "原始尺寸: " << oldWidth << "x" << oldHeight << std::endl;
                std::cout << "新尺寸: " << newWidth << "x" << newHeight << std::endl;

                if (m_swapChain->Resize(newWidth, newHeight))
                {
                    std::cout << "交换链调整大小成功" << std::endl;
                    std::cout << "调整后尺寸: " << m_swapChain->GetWidth() << "x" << m_swapChain->GetHeight() << std::endl;

                    // 验证调整后的交换链
                    if (!ValidateSwapChain())
                    {
                        std::cerr << "调整大小后交换链验证失败" << std::endl;
                        return false;
                    }
                }
                else
                {
                    std::cerr << "交换链调整大小失败" << std::endl;
                    return false;
                }

                std::cout << "=== 交换链调整大小演示完成 ===" << std::endl;
                return true;
            }

            bool VulkanSwapChainExample::DemoVSyncToggle()
            {
                if (!m_swapChain)
                {
                    std::cerr << "VulkanSwapChainExample::DemoVSyncToggle: 交换链未初始化" << std::endl;
                    return false;
                }

                std::cout << "=== 演示垂直同步切换 ===" << std::endl;

                bool originalVSync = m_swapChain->IsVSyncEnabled();
                std::cout << "原始垂直同步状态: " << (originalVSync ? "启用" : "禁用") << std::endl;

                // 切换垂直同步
                m_swapChain->SetVSyncEnabled(!originalVSync);
                bool newVSync = m_swapChain->IsVSyncEnabled();
                std::cout << "切换后垂直同步状态: " << (newVSync ? "启用" : "禁用") << std::endl;

                if (newVSync != originalVSync)
                {
                    std::cout << "垂直同步切换成功" << std::endl;
                }
                else
                {
                    std::cout << "垂直同步切换可能失败或不支持" << std::endl;
                }

                // 恢复原始状态
                m_swapChain->SetVSyncEnabled(originalVSync);
                std::cout << "恢复原始垂直同步状态: " << (m_swapChain->IsVSyncEnabled() ? "启用" : "禁用") << std::endl;

                std::cout << "=== 垂直同步切换演示完成 ===" << std::endl;
                return true;
            }

            bool VulkanSwapChainExample::ValidateSwapChain()
            {
                if (!m_swapChain)
                {
                    std::cerr << "VulkanSwapChainExample::ValidateSwapChain: 交换链为空" << std::endl;
                    return false;
                }

                // 检查基本属性
                if (m_swapChain->GetImageCount() == 0)
                {
                    std::cerr << "VulkanSwapChainExample::ValidateSwapChain: 交换链图像数量为0" << std::endl;
                    return false;
                }

                if (m_swapChain->GetWidth() == 0 || m_swapChain->GetHeight() == 0)
                {
                    std::cerr << "VulkanSwapChainExample::ValidateSwapChain: 交换链尺寸无效" << std::endl;
                    return false;
                }

                if (m_swapChain->GetFormat() == PixelFormat::UNKNOWN)
                {
                    std::cerr << "VulkanSwapChainExample::ValidateSwapChain: 交换链格式未知" << std::endl;
                    return false;
                }

                // 检查所有图像是否有效
                for (uint32_t i = 0; i < m_swapChain->GetImageCount(); ++i)
                {
                    auto image = m_swapChain->GetImage(i);
                    if (!image)
                    {
                        std::cerr << "VulkanSwapChainExample::ValidateSwapChain: 图像 " << i << " 无效" << std::endl;
                        return false;
                    }

                    if (image->GetWidth() != m_swapChain->GetWidth() ||
                        image->GetHeight() != m_swapChain->GetHeight())
                    {
                        std::cerr << "VulkanSwapChainExample::ValidateSwapChain: 图像 " << i << " 尺寸不匹配" << std::endl;
                        return false;
                    }
                }

                // 检查Vulkan特定对象
                if (m_swapChain->GetVkSwapChain() == VK_NULL_HANDLE)
                {
                    std::cerr << "VulkanSwapChainExample::ValidateSwapChain: Vulkan交换链句柄无效" << std::endl;
                    return false;
                }

                if (m_swapChain->GetVkSurface() == VK_NULL_HANDLE)
                {
                    std::cerr << "VulkanSwapChainExample::ValidateSwapChain: Vulkan表面句柄无效" << std::endl;
                    return false;
                }

                std::cout << "VulkanSwapChainExample::ValidateSwapChain: 交换链验证通过" << std::endl;
                return true;
            }

            void VulkanSwapChainExample::PrintSwapChainInfo()
            {
                if (!m_swapChain)
                {
                    std::cout << "交换链信息: 未初始化" << std::endl;
                    return;
                }

                std::cout << "=== 交换链信息 ===" << std::endl;
                std::cout << "图像数量: " << m_swapChain->GetImageCount() << std::endl;
                std::cout << "尺寸: " << m_swapChain->GetWidth() << "x" << m_swapChain->GetHeight() << std::endl;
                std::cout << "格式: " << static_cast<int>(m_swapChain->GetFormat()) << std::endl;
                std::cout << "当前图像索引: " << m_swapChain->GetCurrentImageIndex() << std::endl;
                std::cout << "垂直同步: " << (m_swapChain->IsVSyncEnabled() ? "启用" : "禁用") << std::endl;
                std::cout << "设备: " << (m_swapChain->GetDevice() ? "有效" : "无效") << std::endl;
                std::cout << "Vulkan交换链句柄: " << m_swapChain->GetVkSwapChain() << std::endl;
                std::cout << "Vulkan表面句柄: " << m_swapChain->GetVkSurface() << std::endl;
                std::cout << "==================" << std::endl;
            }

            bool RunVulkanSwapChainExample(VulkanDevice *device, VkSurfaceKHR surface, uint32_t width, uint32_t height)
            {
                if (!device)
                {
                    std::cerr << "RunVulkanSwapChainExample: 设备指针无效" << std::endl;
                    return false;
                }

                if (surface == VK_NULL_HANDLE)
                {
                    std::cerr << "RunVulkanSwapChainExample: 表面句柄无效" << std::endl;
                    return false;
                }

                std::cout << "开始运行Vulkan交换链示例..." << std::endl;

                VulkanSwapChainExample example(device);

                // 初始化示例
                if (!example.Initialize(surface, width, height))
                {
                    std::cerr << "RunVulkanSwapChainExample: 初始化示例失败" << std::endl;
                    return false;
                }

                // 演示基本操作
                if (!example.DemoBasicSwapChainOperations())
                {
                    std::cerr << "RunVulkanSwapChainExample: 基本操作演示失败" << std::endl;
                    return false;
                }

                // 演示调整大小
                if (!example.DemoSwapChainResize(width * 2, height * 2))
                {
                    std::cerr << "RunVulkanSwapChainExample: 调整大小演示失败" << std::endl;
                    return false;
                }

                // 演示垂直同步切换
                if (!example.DemoVSyncToggle())
                {
                    std::cerr << "RunVulkanSwapChainExample: 垂直同步切换演示失败" << std::endl;
                    return false;
                }

                std::cout << "Vulkan交换链示例运行完成！" << std::endl;
                return true;
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // VULKAN_SWAP_CHAIN_EXAMPLE_H_