#ifndef ORANGE_MEMORY_H
#define ORANGE_MEMORY_H

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <map>
#include <mutex>
#include <utility>

/**
 * @file Memory.h
 * @brief Orange 引擎内存管理系统
 * @details 提供自定义内存分配器、内存跟踪和分析功能
 * @author Orange Engine Team
 */

namespace Orange 
{
	/**
	 * @brief 内存分配统计信息结构体
	 * @details 用于跟踪和记录内存分配的统计数据
	 */
	struct AllocationStats
	{
		size_t TotalAllocated = 0;  ///< 总分配字节数
		size_t TotalFreed = 0;      ///< 总释放字节数
	};

	/**
	 * @brief 单个内存分配记录结构体
	 * @details 记录每个内存分配的详细信息，用于内存跟踪和调试
	 */
	struct Allocation
	{
		void* Memory = 0;           ///< 分配的内存地址
		size_t Size = 0;            ///< 分配的内存大小（字节）
		const char* Category = 0;   ///< 分配类别标识（文件名或描述）
	};

	/**
	 * @brief 内存统计命名空间
	 * @details 提供全局内存统计信息的访问接口
	 */
	namespace Memory
	{
		/**
		 * @brief 获取全局内存分配统计信息
		 * @return 全局内存分配统计信息的常量引用
		 */
		const AllocationStats& GetAllocationStats();
	}

	/**
	 * @brief 自定义内存分配器模板类
	 * @details 基于标准库 malloc/free 的自定义分配器，用于 STL 容器
	 * @tparam T 分配器管理的数据类型
	 * @note 主要用于内部数据结构，避免在跟踪系统中产生循环依赖
	 */
	template <class T>
	struct Mallocator
	{
		typedef T value_type;  ///< 分配器处理的值类型

		/**
		 * @brief 默认构造函数
		 */
		Mallocator() = default;
		
		/**
		 * @brief 类型转换构造函数
		 * @tparam U 源分配器的类型
		 * @param other 其他类型的分配器
		 */
		template <class U> constexpr Mallocator(const Mallocator <U>&) noexcept {}

		/**
		 * @brief 分配内存
		 * @param n 要分配的元素数量
		 * @return 指向分配内存的指针
		 * @throws std::bad_array_new_length 如果请求的大小过大
		 * @throws std::bad_alloc 如果内存分配失败
		 */
		T* allocate(std::size_t n)
		{
#undef max
			if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
            {
				throw std::bad_array_new_length();
            }

			if (auto p = static_cast<T*>(std::malloc(n * sizeof(T)))) 
            {
				return p;
			}

			throw std::bad_alloc();
		}

		/**
		 * @brief 释放内存
		 * @param p 要释放的内存指针
		 * @param n 元素数量（未使用）
		 * @note 参数 n 在此实现中未使用，保留以符合分配器接口
		 */
		void deallocate(T* p, std::size_t n) noexcept 
        {
			std::free(p);
		}
	};

	/**
	 * @brief 内存分配器数据存储结构
	 * @details 存储分配器运行时需要的所有数据结构和同步对象
	 */
	struct AllocatorData
	{
		using MapAlloc = Mallocator<std::pair<const void* const, Allocation>>;      ///< 分配映射表的分配器类型
		using StatsMapAlloc = Mallocator<std::pair<const char* const, AllocationStats>>; ///< 统计映射表的分配器类型

		using AllocationStatsMap = std::map<const char*, AllocationStats, std::less<const char*>, StatsMapAlloc>; ///< 分配统计映射表类型

		std::map<const void*, Allocation, std::less<const void*>, MapAlloc> m_AllocationMap;  ///< 内存地址到分配信息的映射表
		AllocationStatsMap m_AllocationStatsMap;  ///< 分类统计信息映射表

		std::mutex m_Mutex, m_StatsMutex;  ///< 线程同步互斥锁
	};

	/**
	 * @brief Orange 引擎核心内存分配器类
	 * @details 提供内存分配、释放和跟踪功能，支持分类统计和性能分析
	 * @note 这是一个单例类，通过静态方法提供全局内存管理服务
	 */
	class Allocator
	{
	public:
		/**
		 * @brief 初始化内存分配器
		 * @details 设置内部数据结构，只能调用一次
		 * @note 线程安全，重复调用无效果
		 */
		static void Init();

		/**
		 * @brief 原始内存分配（不跟踪）
		 * @param size 要分配的字节数
		 * @return 分配的内存指针
		 * @details 直接调用 malloc，不进行跟踪，主要用于内部初始化
		 */
		static void* AllocateRaw(size_t size);

		/**
		 * @brief 分配内存（带跟踪）
		 * @param size 要分配的字节数
		 * @return 分配的内存指针
		 * @details 分配内存并记录到跟踪系统中
		 */
		static void* Allocate(size_t size);
		
		/**
		 * @brief 分配内存（带描述和跟踪）
		 * @param size 要分配的字节数
		 * @param desc 分配描述字符串（用于分类统计）
		 * @return 分配的内存指针
		 * @details 分配内存并记录到指定类别的统计中
		 */
		static void* Allocate(size_t size, const char* desc);
		
		/**
		 * @brief 分配内存（带文件行号信息）
		 * @param size 要分配的字节数
		 * @param file 源文件名
		 * @param line 源代码行号
		 * @return 分配的内存指针
		 * @details 分配内存并记录源代码位置信息，用于调试
		 */
		static void* Allocate(size_t size, const char* file, int line);
		
		/**
		 * @brief 释放内存
		 * @param memory 要释放的内存指针
		 * @details 释放内存并更新跟踪统计信息
		 * @note 如果传入空指针，函数安全返回
		 * @warning 只能释放通过 Allocator 分配的内存
		 */
		static void Free(void* memory);

		/**
		 * @brief 获取分配统计信息映射表
		 * @return 分配统计信息映射表的常量引用
		 * @details 返回按类别分组的内存分配统计信息
		 */
		static const AllocatorData::AllocationStatsMap& GetAllocationStats() { return s_Data->m_AllocationStatsMap; }
	private:
		inline static AllocatorData* s_Data = nullptr;  ///< 分配器数据存储指针
	};

}

// 条件编译：内存跟踪功能
#if ORG_TRACK_MEMORY

// 平台相关：Windows 平台的全局 new/delete 重载
#ifdef ORG_PLATFORM_WINDOWS

/**
 * @brief 重载全局 new 操作符
 * @param size 要分配的字节数
 * @return 分配的内存指针
 * @details Windows 平台下使用 Orange 分配器重载标准 new 操作符
 */
_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new(size_t size);

/**
 * @brief 重载全局 new[] 操作符（数组版本）
 * @param size 要分配的字节数
 * @return 分配的内存指针
 */
_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new[](size_t size);

/**
 * @brief 重载全局 new 操作符（带描述版本）
 * @param size 要分配的字节数
 * @param desc 分配描述
 * @return 分配的内存指针
 */
_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new(size_t size, const char* desc);

/**
 * @brief 重载全局 new[] 操作符（带描述的数组版本）
 * @param size 要分配的字节数
 * @param desc 分配描述
 * @return 分配的内存指针
 */
_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new[](size_t size, const char* desc);

/**
 * @brief 重载全局 new 操作符（带文件行号版本）
 * @param size 要分配的字节数
 * @param file 源文件名
 * @param line 源代码行号
 * @return 分配的内存指针
 */
_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new(size_t size, const char* file, int line);

/**
 * @brief 重载全局 new[] 操作符（带文件行号的数组版本）
 * @param size 要分配的字节数
 * @param file 源文件名
 * @param line 源代码行号
 * @return 分配的内存指针
 */
_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(size) _VCRT_ALLOCATOR
void* __CRTDECL operator new[](size_t size, const char* file, int line);

/**
 * @brief 重载全局 delete 操作符
 * @param memory 要释放的内存指针
 */
void __CRTDECL operator delete(void* memory);

/**
 * @brief 重载全局 delete 操作符（带描述版本）
 * @param memory 要释放的内存指针
 * @param desc 分配描述（未使用）
 */
void __CRTDECL operator delete(void* memory, const char* desc);

/**
 * @brief 重载全局 delete 操作符（带文件行号版本）
 * @param memory 要释放的内存指针
 * @param file 源文件名（未使用）
 * @param line 源代码行号（未使用）
 */
void __CRTDECL operator delete(void* memory, const char* file, int line);

/**
 * @brief 重载全局 delete[] 操作符（数组版本）
 * @param memory 要释放的内存指针
 */
void __CRTDECL operator delete[](void* memory);

/**
 * @brief 重载全局 delete[] 操作符（带描述的数组版本）
 * @param memory 要释放的内存指针
 * @param desc 分配描述（未使用）
 */
void __CRTDECL operator delete[](void* memory, const char* desc);

/**
 * @brief 重载全局 delete[] 操作符（带文件行号的数组版本）
 * @param memory 要释放的内存指针
 * @param file 源文件名（未使用）
 * @param line 源代码行号（未使用）
 */
void __CRTDECL operator delete[](void* memory, const char* file, int line);

/**
 * @def onew
 * @brief Orange 引擎自定义 new 宏
 * @details 在调试模式下包含文件名和行号信息的 new 操作
 */
#define onew new(__FILE__, __LINE__)

/**
 * @def odelete  
 * @brief Orange 引擎自定义 delete 宏
 * @details 对应 onew 的 delete 操作
 */
#define odelete delete

#else
#warning "Memory tracking not available on non-Windows platform"
#define onew new
#define odelete delete

#endif

#else

#define onew new
#define odelete delete

#endif
#endif // ORANGE_MEMORY_H