/**
 * @file Base.h
 * @brief Orange引擎核心基础组件
 * @details 提供引擎的基础设施，包括平台检测、编译器适配、工具函数等
 * @author Orange Engine Team
 * 
 * 该模块包含：
 * - 平台和编译器检测宏
 * - 核心初始化和关闭函数
 * - 基础工具函数和类型定义
 * - 智能指针别名和工厂函数
 * - 原子标志和普通标志类
 * 
 * 使用示例：
 * @code
 * #include "Base/Base.h"
 * 
 * // 初始化核心系统
 * Orange::InitializeCore();
 * 
 * // 使用智能指针
 * auto ptr = Orange::CreateScope<MyClass>();
 * 
 * // 使用原子标志
 * Orange::AtomicFlag flag;
 * flag.SetDirty();
 * 
 * // 关闭核心系统
 * Orange::ShutdownCore();
 * @endcode
 */

#ifndef ORANGE_BASE_H
#define ORANGE_BASE_H

#include <functional>
#include <memory>
#include <atomic>

// =================================================================
// 平台检测
// =================================================================

/**
 * @brief 平台检测宏
 * @details 自动检测编译目标平台，由CMake配置或编译器预定义宏确定
 */
#if defined(_WIN32) || defined(_WIN64)
	#define ORG_PLATFORM_WINDOWS
#elif defined(__linux__)
	#define ORG_PLATFORM_LINUX
#elif defined(__APPLE__)
	#define ORG_PLATFORM_MACOS
#endif

#if !defined(ORG_PLATFORM_WINDOWS) && !defined(ORG_PLATFORM_LINUX) && !defined(ORG_PLATFORM_MACOS)
	#error "Unsupported platform! Only Windows, Linux and macOS are supported."
#endif

// =================================================================
// 编译器检测
// =================================================================

/**
 * @brief 编译器检测宏
 * @details 识别当前使用的编译器类型，用于编译器特定的优化和适配
 */
#if defined(__GNUC__)
	#if defined(__clang__)
		#define ORG_COMPILER_CLANG
	#else
		#define ORG_COMPILER_GCC
	#endif
#elif defined(_MSC_VER)
	#define ORG_COMPILER_MSVC
#endif

// =================================================================
// 编译器相关宏定义
// =================================================================

/**
 * @brief 位操作宏
 * @param x 位偏移量
 * @return 1左移x位的结果
 */
#define BIT(x) (1u << x)

/**
 * @brief 事件绑定宏
 * @param fn 成员函数名
 * @return 绑定到当前对象的函数对象
 */
#define ORG_BIND_EVENT_FN(fn) std::bind(&fn, this, std::placeholders::_1)

/**
 * @brief 编译器特定的强制内联宏
 */
#ifdef ORG_COMPILER_MSVC
	#define ORG_FORCE_INLINE __forceinline
	#define ORG_EXPLICIT_STATIC static
#elif defined(__GNUC__)
	#define ORG_FORCE_INLINE __attribute__((always_inline)) inline
	#define ORG_EXPLICIT_STATIC
#else
	#define ORG_FORCE_INLINE inline
	#define ORG_EXPLICIT_STATIC
#endif

namespace Orange 
{

	// =================================================================
	// 核心系统函数
	// =================================================================

	/**
	 * @brief 初始化Orange引擎核心系统
	 * @details 初始化内存管理器、日志系统等核心组件
	 * 
	 * 该函数必须在使用引擎的任何其他功能之前调用。
	 * 通常在应用程序的main函数开始处调用。
	 * 
	 * @note 该函数是线程安全的，可以多次调用（但只有第一次调用有效）
	 * @see ShutdownCore()
	 */
	void InitializeCore();

	/**
	 * @brief 关闭Orange引擎核心系统
	 * @details 清理核心系统资源，包括内存管理器、日志系统等
	 * 
	 * 该函数应该在应用程序退出前调用，用于清理引擎资源。
	 * 通常在应用程序的main函数结束前调用。
	 * 
	 * @note 调用此函数后，不应再使用引擎的任何功能
	 * @see InitializeCore()
	 */
	void ShutdownCore();

	// =================================================================
	// 数学工具函数
	// =================================================================

	/**
	 * @brief 向下取整到指定因数的倍数
	 * @tparam T 数值类型
	 * @param x 输入值
	 * @param fac 因数
	 * @return 不超过x的最大fac的倍数
	 * 
	 * @code
	 * int result = RoundDown(17, 4); // 返回16
	 * @endcode
	 */
	template<typename T>
	T RoundDown(T x, T fac) { return x / fac * fac; }

	/**
	 * @brief 向上取整到指定因数的倍数
	 * @tparam T 数值类型
	 * @param x 输入值
	 * @param fac 因数
	 * @return 不小于x的最小fac的倍数
	 * 
	 * @code
	 * int result = RoundUp(17, 4); // 返回20
	 * @endcode
	 */
	template<typename T>
	T RoundUp(T x, T fac) { return RoundDown(x + fac - 1, fac); }

	// =================================================================
	// 智能指针别名和工厂函数
	// =================================================================

	/**
	 * @brief 独占所有权智能指针别名
	 * @tparam T 管理的对象类型
	 * @details std::unique_ptr的别名，用于表示独占所有权
	 */
	template<typename T>
	using Scope = std::unique_ptr<T>;

	/**
	 * @brief 创建独占所有权智能指针
	 * @tparam T 要创建的对象类型
	 * @tparam Args 构造函数参数类型
	 * @param args 构造函数参数
	 * @return 新创建的Scope<T>对象
	 * 
	 * @code
	 * auto ptr = CreateScope<MyClass>(arg1, arg2);
	 * @endcode
	 */
	template<typename T, typename ... Args>
	constexpr Scope<T> CreateScope(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	// =================================================================
	// 基础类型定义
	// =================================================================

	/**
	 * @brief 字节类型别名
	 * @details uint8_t的别名，用于表示字节数据
	 */
	using byte = uint8_t;

	// =================================================================
	// 原子标志类
	// =================================================================

	/**
	 * @brief 线程安全的原子标志类
	 * @details 基于std::atomic_flag的线程安全标志，主要用于脏标记
	 * 
	 * 该类包装了std::atomic_flag，提供了更直观的接口用于脏标记模式。
	 * 对象可以被标记为"脏"状态，然后在检查时自动重置。
	 * 
	 * 特性：
	 * - 线程安全
	 * - 可拷贝（拷贝后的标志会被重置）
	 * - 无锁操作
	 * 
	 * 使用场景：
	 * - 缓存失效标记
	 * - 状态变更检测
	 * - 多线程同步
	 * 
	 * @code
	 * AtomicFlag flag;
	 * flag.SetDirty();
	 * if (flag.CheckAndResetIfDirty()) {
	 *     // 处理脏状态
	 * }
	 * @endcode
	 */
	struct AtomicFlag
	{
		/**
		 * @brief 设置脏标记
		 * @details 将标志设置为脏状态（线程安全）
		 */
		ORG_FORCE_INLINE void SetDirty() { flag.clear(); }

		/**
		 * @brief 检查并重置脏标记
		 * @return 如果之前是脏状态返回true，同时重置标志
		 * @details 原子操作：检查当前状态并重置为非脏状态
		 */
		ORG_FORCE_INLINE bool CheckAndResetIfDirty() { return !flag.test_and_set(); }

		/**
		 * @brief 默认构造函数
		 * @details 初始化为非脏状态
		 */
		explicit AtomicFlag() noexcept { flag.test_and_set(); }

		/**
		 * @brief 拷贝构造函数
		 * @details 新对象初始化为非脏状态（不拷贝源对象的状态）
		 */
		AtomicFlag(const AtomicFlag&) noexcept {}

		/**
		 * @brief 拷贝赋值操作符
		 * @details 不改变当前对象状态（不拷贝源对象的状态）
		 */
		AtomicFlag& operator=(const AtomicFlag&) noexcept { return *this; }

		/**
		 * @brief 移动构造函数
		 * @details 新对象初始化为非脏状态
		 */
		AtomicFlag(AtomicFlag&&) noexcept {};

		/**
		 * @brief 移动赋值操作符
		 * @details 不改变当前对象状态
		 */
		AtomicFlag& operator=(AtomicFlag&&) noexcept { return *this; }

	private:
		std::atomic_flag flag;
	};

	/**
	 * @brief 非线程安全的普通标志类
	 * @details 简单的布尔标志类，用于单线程环境下的脏标记
	 * 
	 * 相比AtomicFlag，该类不使用原子操作，性能更好但不线程安全。
	 * 适用于单线程环境或已有外部同步机制的场景。
	 * 
	 * 特性：
	 * - 非线程安全
	 * - 高性能（无原子操作开销）
	 * - 可拷贝
	 * - 提供状态查询功能
	 * 
	 * @code
	 * Flag flag;
	 * flag.SetDirty();
	 * if (flag.IsDirty()) {
	 *     // 检查脏状态但不重置
	 * }
	 * if (flag.CheckAndResetIfDirty()) {
	 *     // 检查并重置脏状态
	 * }
	 * @endcode
	 */
	struct Flag
	{
		/**
		 * @brief 设置脏标记
		 * @details 将标志设置为脏状态
		 */
		ORG_FORCE_INLINE void SetDirty() noexcept { flag = true; }

		/**
		 * @brief 检查并重置脏标记
		 * @return 如果之前是脏状态返回true，同时重置标志
		 * @details 检查当前状态并重置为非脏状态
		 */
		ORG_FORCE_INLINE bool CheckAndResetIfDirty() noexcept
		{
			if (flag)
				return !(flag = !flag);
			else
				return false;
		}

		/**
		 * @brief 检查脏标记状态
		 * @return 如果当前是脏状态返回true
		 * @details 只检查状态，不重置标志
		 */
		ORG_FORCE_INLINE bool IsDirty() const noexcept { return flag; }

	private:
		bool flag = false;
	};

}

#endif // ORANGE_BASE_H