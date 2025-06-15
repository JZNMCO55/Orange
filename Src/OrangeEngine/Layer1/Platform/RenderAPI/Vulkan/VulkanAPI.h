/**
 * @file VulkanAPI.h
 * @brief Vulkan API工具函数头文件
 * @details 提供Vulkan API的便捷工具函数
 * @author Orange Engine Team
 */

#pragma once

#include "vulkan/vulkan.h"

namespace Orange::Vulkan
{

    /**
     * @brief 创建描述符集合分配信息
     * @param layouts 描述符集合布局数组指针
     * @param count 布局数量，默认为1
     * @param pool 描述符池，默认为nullptr
     * @return VkDescriptorSetAllocateInfo结构
     * @details 便捷函数，用于创建描述符集合分配时需要的信息结构
     */
    VkDescriptorSetAllocateInfo DescriptorSetAllocInfo(const VkDescriptorSetLayout *layouts, uint32_t count = 1, VkDescriptorPool pool = nullptr);

    /**
     * @brief 创建采样器
     * @param samplerCreateInfo 采样器创建信息
     * @return VkSampler采样器句柄
     * @details 根据提供的创建信息创建Vulkan采样器
     */
    VkSampler CreateSampler(VkSamplerCreateInfo samplerCreateInfo);

    /**
     * @brief 销毁采样器
     * @param sampler 要销毁的采样器句柄
     * @details 安全地销毁Vulkan采样器并释放相关资源
     */
    void DestroySampler(VkSampler sampler);

}
