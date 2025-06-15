/**
 * @file VulkanAllocator.cpp
 * @brief Vulkan内存分配器实现文件
 * @details 实现基于VMA的GPU内存管理功能
 * @author Orange Engine
 */

#include "orgpch.h"
#include "VulkanAllocator.h"

#include "VulkanContext.h"

#include "Core/Utilities/StringUtils.h"

// 根据宏定义决定是否启用分配器日志
#if ORG_LOG_RENDERER_ALLOCATIONS
#define ORG_ALLOCATOR_LOG(...) ORG_CORE_TRACE(__VA_ARGS__)
#else
#define ORG_ALLOCATOR_LOG(...)
#endif

// 启用GPU内存分配跟踪
#define ORG_GPU_TRACK_MEMORY_ALLOCATION 1

namespace Orange
{

    /**
     * @brief Vulkan分配器数据结构
     * @details 存储VMA分配器实例和内存使用统计信息
     */
    struct VulkanAllocatorData
    {
        VmaAllocator Allocator;           ///< VMA分配器实例
        uint64_t TotalAllocatedBytes = 0; ///< 总分配字节数

        uint64_t MemoryUsage = 0; ///< 当前内存使用量（所有堆）
    };

    /**
     * @brief 分配类型枚举
     * @details 用于跟踪不同类型的内存分配
     */
    enum class AllocationType : uint8_t
    {
        None = 0,   ///< 无类型
        Buffer = 1, ///< 缓冲区分配
        Image = 2   ///< 图像分配
    };

    static VulkanAllocatorData *s_Data = nullptr; ///< 全局分配器数据

    /**
     * @brief 分配信息结构
     * @details 记录每个分配的大小和类型信息
     */
    struct AllocInfo
    {
        uint64_t AllocatedSize = 0;                 ///< 分配的内存大小
        AllocationType Type = AllocationType::None; ///< 分配类型
    };
    static std::map<VmaAllocation, AllocInfo> s_AllocationMap; ///< 分配跟踪映射表

    VulkanAllocator::VulkanAllocator(const std::string &tag)
        : m_Tag(tag)
    {
    }

    VulkanAllocator::~VulkanAllocator()
    {
    }

#if 0
	// 旧版本的内存分配方法（已废弃）
	void VulkanAllocator::Allocate(VkMemoryRequirements requirements, VkDeviceMemory* dest, VkMemoryPropertyFlags flags /*= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT*/)
	{
		ORG_CORE_ASSERT(m_Device);

		// TODO: 跟踪
		ORG_CORE_TRACE("VulkanAllocator ({0}): allocating {1}", m_Tag, Utils::BytesToString(requirements.size));

		{
			static uint64_t totalAllocatedBytes = 0;
			totalAllocatedBytes += requirements.size;
			ORG_CORE_TRACE("VulkanAllocator ({0}): total allocated since start is {1}", m_Tag, Utils::BytesToString(totalAllocatedBytes));
		}

		VkMemoryAllocateInfo memAlloc = {};
		memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAlloc.allocationSize = requirements.size;
		memAlloc.memoryTypeIndex = m_Device->GetPhysicalDevice()->GetMemoryTypeIndex(requirements.memoryTypeBits, flags);
		VK_CHECK_RESULT(vkAllocateMemory(m_Device->GetVulkanDevice(), &memAlloc, nullptr, dest));
	}
#endif

    VmaAllocation VulkanAllocator::AllocateBuffer(VkBufferCreateInfo bufferCreateInfo, VmaMemoryUsage usage, VkBuffer &outBuffer)
    {
        // 验证缓冲区大小必须大于0
        ORG_CORE_VERIFY(bufferCreateInfo.size > 0);

        // 设置VMA分配创建信息
        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = usage;

        // 使用VMA创建缓冲区
        VmaAllocation allocation;
        vmaCreateBuffer(s_Data->Allocator, &bufferCreateInfo, &allocCreateInfo, &outBuffer, &allocation, nullptr);

        // 检查分配是否成功
        if (allocation == nullptr)
        {
            ORG_CORE_ERROR_TAG("Renderer", "Failed to allocate GPU buffer!");
            ORG_CORE_ERROR_TAG("Renderer", "  Requested size: {}", Utils::BytesToString(bufferCreateInfo.size));
            auto stats = GetStats();
            ORG_CORE_ERROR_TAG("Renderer", "  GPU mem usage: {}/{}", Utils::BytesToString(stats.Used), Utils::BytesToString(stats.TotalAvailable));
        }

        // 获取分配信息并记录日志
        VmaAllocationInfo allocInfo{};
        vmaGetAllocationInfo(s_Data->Allocator, allocation, &allocInfo);
        ORG_ALLOCATOR_LOG("VulkanAllocator ({0}): allocating buffer; size = {1}", m_Tag, Utils::BytesToString(allocInfo.size));

        // 更新总分配字节数
        {
            s_Data->TotalAllocatedBytes += allocInfo.size;
            ORG_ALLOCATOR_LOG("VulkanAllocator ({0}): total allocated since start is {1}", m_Tag, Utils::BytesToString(s_Data->TotalAllocatedBytes));
        }

#if ORG_GPU_TRACK_MEMORY_ALLOCATION
        // 记录分配信息用于跟踪
        auto &allocTrack = s_AllocationMap[allocation];
        allocTrack.AllocatedSize = allocInfo.size;
        allocTrack.Type = AllocationType::Buffer;
        s_Data->MemoryUsage += allocInfo.size;
#endif

        return allocation;
    }

    VmaAllocation VulkanAllocator::AllocateImage(VkImageCreateInfo imageCreateInfo, VmaMemoryUsage usage, VkImage &outImage, VkDeviceSize *allocatedSize)
    {
        // 设置VMA分配创建信息
        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = usage;

        // 使用VMA创建图像
        VmaAllocation allocation;
        vmaCreateImage(s_Data->Allocator, &imageCreateInfo, &allocCreateInfo, &outImage, &allocation, nullptr);

        // 检查分配是否成功
        if (allocation == nullptr)
        {
            ORG_CORE_ERROR_TAG("Renderer", "Failed to allocate GPU image!");
            ORG_CORE_ERROR_TAG("Renderer", "  Requested size: {}x{}x{}", imageCreateInfo.extent.width, imageCreateInfo.extent.height, imageCreateInfo.extent.depth);
            ORG_CORE_ERROR_TAG("Renderer", "  Mips: {}", imageCreateInfo.mipLevels);
            ORG_CORE_ERROR_TAG("Renderer", "  Layers: {}", imageCreateInfo.arrayLayers);
            auto stats = GetStats();
            ORG_CORE_ERROR_TAG("Renderer", "  GPU mem usage: {}/{}", Utils::BytesToString(stats.Used), Utils::BytesToString(stats.TotalAvailable));
        }

        // 获取分配信息并记录日志
        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo(s_Data->Allocator, allocation, &allocInfo);
        if (allocatedSize)
            *allocatedSize = allocInfo.size;
        ORG_ALLOCATOR_LOG("VulkanAllocator ({0}): allocating image; size = {1}", m_Tag, Utils::BytesToString(allocInfo.size));

        // 更新总分配字节数
        {
            s_Data->TotalAllocatedBytes += allocInfo.size;
            ORG_ALLOCATOR_LOG("VulkanAllocator ({0}): total allocated since start is {1}", m_Tag, Utils::BytesToString(s_Data->TotalAllocatedBytes));
        }

#if ORG_GPU_TRACK_MEMORY_ALLOCATION
        // 记录分配信息用于跟踪
        auto &allocTrack = s_AllocationMap[allocation];
        allocTrack.AllocatedSize = allocInfo.size;
        allocTrack.Type = AllocationType::Image;
        s_Data->MemoryUsage += allocInfo.size;
#endif

        return allocation;
    }

    void VulkanAllocator::Free(VmaAllocation allocation)
    {
        // 释放VMA内存分配
        vmaFreeMemory(s_Data->Allocator, allocation);

#if ORG_GPU_TRACK_MEMORY_ALLOCATION
        // 从跟踪映射表中移除分配记录
        auto it = s_AllocationMap.find(allocation);
        if (it != s_AllocationMap.end())
        {
            s_Data->MemoryUsage -= it->second.AllocatedSize;
            s_AllocationMap.erase(it);
        }
        else
        {
            ORG_CORE_ERROR("Could not find GPU memory allocation: {}", (void *)allocation);
        }
#endif
    }

    void VulkanAllocator::DestroyImage(VkImage image, VmaAllocation allocation)
    {
        ORG_CORE_ASSERT(image);
        ORG_CORE_ASSERT(allocation);

        // 销毁VMA图像及其分配
        vmaDestroyImage(s_Data->Allocator, image, allocation);

#if ORG_GPU_TRACK_MEMORY_ALLOCATION
        // 从跟踪映射表中移除分配记录
        auto it = s_AllocationMap.find(allocation);
        if (it != s_AllocationMap.end())
        {
            s_Data->MemoryUsage -= it->second.AllocatedSize;
            s_AllocationMap.erase(it);
        }
        else
        {
            ORG_CORE_ERROR("Could not find GPU memory allocation: {}", (void *)allocation);
        }
#endif
    }

    void VulkanAllocator::DestroyBuffer(VkBuffer buffer, VmaAllocation allocation)
    {
        ORG_CORE_ASSERT(buffer);
        ORG_CORE_ASSERT(allocation);

        // 销毁VMA缓冲区及其分配
        vmaDestroyBuffer(s_Data->Allocator, buffer, allocation);

#if ORG_GPU_TRACK_MEMORY_ALLOCATION
        // 从跟踪映射表中移除分配记录
        auto it = s_AllocationMap.find(allocation);
        if (it != s_AllocationMap.end())
        {
            s_Data->MemoryUsage -= it->second.AllocatedSize;
            s_AllocationMap.erase(it);
        }
        else
        {
            ORG_CORE_ERROR("Could not find GPU memory allocation: {}", (void *)allocation);
        }
#endif
    }

    void VulkanAllocator::UnmapMemory(VmaAllocation allocation)
    {
        // 解除VMA内存映射
        vmaUnmapMemory(s_Data->Allocator, allocation);
    }

    void VulkanAllocator::DumpStats()
    {
        // 获取物理设备内存属性
        const auto &memoryProps = VulkanContext::GetCurrentDevice()->GetPhysicalDevice()->GetMemoryProperties();
        std::vector<VmaBudget> budgets(memoryProps.memoryHeapCount);
        vmaGetBudget(s_Data->Allocator, budgets.data());

        // 输出每个内存堆的预算信息
        ORG_CORE_WARN("-----------------------------------");
        for (VmaBudget &b : budgets)
        {
            ORG_CORE_WARN("VmaBudget.allocationBytes = {0}", Utils::BytesToString(b.allocationBytes));
            ORG_CORE_WARN("VmaBudget.blockBytes = {0}", Utils::BytesToString(b.blockBytes));
            ORG_CORE_WARN("VmaBudget.usage = {0}", Utils::BytesToString(b.usage));
            ORG_CORE_WARN("VmaBudget.budget = {0}", Utils::BytesToString(b.budget));
        }
        ORG_CORE_WARN("-----------------------------------");
    }

    GPUMemoryStats VulkanAllocator::GetStats()
    {
        // 获取物理设备内存属性
        const auto &memoryProps = VulkanContext::GetCurrentDevice()->GetPhysicalDevice()->GetMemoryProperties();
        std::vector<VmaBudget> budgets(memoryProps.memoryHeapCount);
        vmaGetBudget(s_Data->Allocator, budgets.data());

        // 计算总预算
        uint64_t budget = 0;
        for (VmaBudget &b : budgets)
            budget += b.budget;

        // 统计不同类型的分配信息
        GPUMemoryStats result;
        for (const auto &[k, v] : s_AllocationMap)
        {
            if (v.Type == AllocationType::Buffer)
            {
                result.BufferAllocationCount++;
                result.BufferAllocationSize += v.AllocatedSize;
            }
            else if (v.Type == AllocationType::Image)
            {
                result.ImageAllocationCount++;
                result.ImageAllocationSize += v.AllocatedSize;
            }
        }

        // 填充统计结果
        result.AllocationCount = s_AllocationMap.size();
        result.Used = s_Data->MemoryUsage;
        result.TotalAvailable = budget;
        return result;

#if 0
		// 旧版本的统计方法（已废弃）
		VmaStats stats;
		vmaCalculateStats(s_Data->Allocator, &stats);

		uint64_t usedMemory = stats.total.usedBytes;
		uint64_t freeMemory = stats.total.unusedBytes;

		return { usedMemory, freeMemory };
#endif
    }

    void VulkanAllocator::Init(Ref<VulkanDevice> device)
    {
        // 创建分配器数据实例
        s_Data = hnew VulkanAllocatorData();

        // 初始化VulkanMemoryAllocator
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
        allocatorInfo.physicalDevice = device->GetPhysicalDevice()->GetVulkanPhysicalDevice();
        allocatorInfo.device = device->GetVulkanDevice();
        allocatorInfo.instance = VulkanContext::GetInstance();

        // 创建VMA分配器
        vmaCreateAllocator(&allocatorInfo, &s_Data->Allocator);
    }

    void VulkanAllocator::Shutdown()
    {
        // 销毁VMA分配器
        vmaDestroyAllocator(s_Data->Allocator);

        // 清理分配器数据
        delete s_Data;
        s_Data = nullptr;
    }

    VmaAllocator &VulkanAllocator::GetVMAAllocator()
    {
        return s_Data->Allocator;
    }

}
