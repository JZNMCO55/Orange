/**
 * @file Memory.cpp
 * @brief Orange 引擎内存管理系统实现
 * @details 实现自定义内存分配器、跟踪功能和全局操作符重载
 * @author Orange Engine Team
 * 
 * 依赖项:
 * - Memory.h: 内存管理器接口定义
 * - Logger.h: 日志系统，用于警告和错误报告
 * - Orange/Debug/Profiler.h: 性能分析工具（Tracy集成）
 * - <mutex>: 线程安全支持
 */

#include "Memory.h"

#include "../Base/Logger.h"
#include "../Debug/Profiler.h"

#include <mutex>

namespace Orange
{

	/// 全局内存分配统计信息
	static Orange::AllocationStats s_GlobalStats;

	/// 初始化状态标志，用于防止初始化过程中的递归调用
	static bool s_InInit = false;

	void Allocator::Init()
	{
		// 防止重复初始化
		if (s_Data)
			return;

		// 设置初始化标志，防止递归调用
		s_InInit = true;
		
		// 使用原始分配器分配 AllocatorData 结构
		AllocatorData* data = (AllocatorData*)Allocator::AllocateRaw(sizeof(AllocatorData));
		
		// 使用 placement new 构造 AllocatorData 对象
		new(data) AllocatorData();
		s_Data = data;
		
		// 清除初始化标志
		s_InInit = false;
	}

	void* Allocator::AllocateRaw(size_t size)
	{
		// 直接调用系统 malloc，不进行任何跟踪
		return malloc(size);
	}

	void* Allocator::Allocate(size_t size)
	{
		// 如果正在初始化过程中，使用原始分配避免递归
		if (s_InInit)
        {
			return AllocateRaw(size);
        }

		// 确保分配器已初始化
		if (!s_Data)
        {
			Init();
        }
		// 分配内存
		void* memory = malloc(size);

		// 线程安全地更新分配记录
		{
			std::scoped_lock<std::mutex> lock(s_Data->m_Mutex);
			Allocation& alloc = s_Data->m_AllocationMap[memory];
			alloc.Memory = memory;
			alloc.Size = size;
		
			// 更新全局统计
			s_GlobalStats.TotalAllocated += size;
		}

#if ORG_ENABLE_PROFILING
		// Tracy 性能分析器集成
		TracyAlloc(memory, size);
#endif

		return memory;
	}

	void* Allocator::Allocate(size_t size, const char* desc)
	{
		// 确保分配器已初始化
		if (!s_Data)
			Init();

		// 分配内存
		void* memory = malloc(size);

		// 线程安全地更新分配记录和分类统计
		{
			std::scoped_lock<std::mutex> lock(s_Data->m_Mutex);
			Allocation& alloc = s_Data->m_AllocationMap[memory];
			alloc.Memory = memory;
			alloc.Size = size;
			alloc.Category = desc;

			// 更新全局统计
			s_GlobalStats.TotalAllocated += size;
			
			// 更新分类统计（如果提供了描述）
			if (desc)
				s_Data->m_AllocationStatsMap[desc].TotalAllocated += size;
		}

#if ORG_ENABLE_PROFILING
		// Tracy 性能分析器集成
		TracyAlloc(memory, size);
#endif

		return memory;
	}

	void* Allocator::Allocate(size_t size, const char* file, int line)
	{
		// 确保分配器已初始化
		if (!s_Data)
			Init();

		// 分配内存
		void* memory = malloc(size);

		// 线程安全地更新分配记录，使用文件名作为分类
		{
			std::scoped_lock<std::mutex> lock(s_Data->m_Mutex);
			Allocation& alloc = s_Data->m_AllocationMap[memory];
			alloc.Memory = memory;
			alloc.Size = size;
			alloc.Category = file;

			// 更新全局和文件分类统计
			s_GlobalStats.TotalAllocated += size;
			s_Data->m_AllocationStatsMap[file].TotalAllocated += size;
		}

#if ORG_ENABLE_PROFILING
		// Tracy 性能分析器集成
		TracyAlloc(memory, size);
#endif

		return memory;
	}

	void Allocator::Free(void* memory)
	{
		// 空指针检查
		if (memory == nullptr)
			return;

		// 查找并更新分配记录
		{
			bool found = false;
			{
				std::scoped_lock<std::mutex> lock(s_Data->m_Mutex);
				auto allocMapIt = s_Data->m_AllocationMap.find(memory);
				found = allocMapIt != s_Data->m_AllocationMap.end();
				if (found)
				{
					const Allocation& alloc = allocMapIt->second;
					
					// 更新释放统计
					s_GlobalStats.TotalFreed += alloc.Size;
					if (alloc.Category)
						s_Data->m_AllocationStatsMap[alloc.Category].TotalFreed += alloc.Size;

					// 从映射表中移除记录
					s_Data->m_AllocationMap.erase(memory);
				}
			}

#if ORG_ENABLE_PROFILING
			// 通知 Tracy 性能分析器
			TracyFree(memory);
#endif

#ifndef ORG_DIST
			// 在非发布版本中检查内存泄漏
			if (!found)
				ORG_CORE_WARN_TAG("Memory", "Memory block {0} not present in alloc map", memory);
#endif
		}
		
		// 释放系统内存
		free(memory);
	}
	
	namespace Memory {

		const AllocationStats& GetAllocationStats() { return s_GlobalStats; }
	}
}

// 条件编译：Windows 平台的全局操作符重载
#if ORG_TRACK_MEMORY && ORG_PLATFORM_WINDOWS

/**
 * @brief 全局 new 操作符重载实现
 * @details 使用 Orange 分配器替换标准 new 操作符
 */
_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new(size_t size)
{
	return Orange::Allocator::Allocate(size);
}

/**
 * @brief 全局 new[] 操作符重载实现（数组版本）
 */
_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new[](size_t size)
{
	return Orange::Allocator::Allocate(size);
}

/**
 * @brief 全局 new 操作符重载实现（带描述版本）
 */
_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new(size_t size, const char* desc)
{
	return Orange::Allocator::Allocate(size, desc);
}

/**
 * @brief 全局 new[] 操作符重载实现（带描述的数组版本）
 */
_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new[](size_t size, const char* desc)
{
	return Orange::Allocator::Allocate(size, desc);
}

/**
 * @brief 全局 new 操作符重载实现（带文件行号版本）
 */
_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new(size_t size, const char* file, int line)
{
	return Orange::Allocator::Allocate(size, file, line);
}

/**
 * @brief 全局 new[] 操作符重载实现（带文件行号的数组版本）
 */
_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new[](size_t size, const char* file, int line)
{
	return Orange::Allocator::Allocate(size, file, line);
}

/**
 * @brief 全局 delete 操作符重载实现
 */
void __CRTDECL operator delete(void* memory)
{
	return Orange::Allocator::Free(memory);
}

/**
 * @brief 全局 delete 操作符重载实现（带描述版本）
 * @note desc 参数在释放时不需要使用
 */
void __CRTDECL operator delete(void* memory, const char* desc)
{
	return Orange::Allocator::Free(memory);
}

/**
 * @brief 全局 delete 操作符重载实现（带文件行号版本）
 * @note file 和 line 参数在释放时不需要使用
 */
void __CRTDECL operator delete(void* memory, const char* file, int line)
{
	return Orange::Allocator::Free(memory);
}

/**
 * @brief 全局 delete[] 操作符重载实现（数组版本）
 */
void __CRTDECL operator delete[](void* memory)
{
	return Orange::Allocator::Free(memory);
}

/**
 * @brief 全局 delete[] 操作符重载实现（带描述的数组版本）
 * @note desc 参数在释放时不需要使用
 */
void __CRTDECL operator delete[](void* memory, const char* desc)
{
	return Orange::Allocator::Free(memory);
}

/**
 * @brief 全局 delete[] 操作符重载实现（带文件行号的数组版本）
 * @note file 和 line 参数在释放时不需要使用
 */
void __CRTDECL operator delete[](void* memory, const char* file, int line)
{
	return Orange::Allocator::Free(memory);
}

#endif
