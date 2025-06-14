/**
 * @file Ref.h
 * @brief Orange引擎智能指针和引用计数系统
 * @author Orange Engine Team
 *
 * 该模块实现了完整的智能指针系统：
 * - RefCounted: 线程安全的引用计数基类
 * - Ref<T>: 强引用智能指针
 * - WeakRef<T>: 弱引用智能指针
 * - RefUtils: 生命周期跟踪工具
 *
 * 使用示例：
 * @code
 * class MyClass : public RefCounted
 * {
 *     // 类实现
 * };
 *
 * // 创建对象
 * Ref<MyClass> obj = Ref<MyClass>::Create();
 *
 * // 拷贝引用
 * Ref<MyClass> obj2 = obj;
 *
 * // 创建弱引用
 * WeakRef<MyClass> weakObj = obj;
 * @endcode
 */

#ifndef ORANGE_REF_H
#define ORANGE_REF_H

#include "../Memory/Memory.h"

#include <atomic>
#include <cstddef>
#include <type_traits>

namespace Orange
{

    /**
     * @brief 引用计数基类
     * @details 提供线程安全的引用计数功能，所有需要智能指针管理的类都应继承此类
     *
     * 使用原子操作确保多线程环境下的安全性。引用计数从0开始，
     * 当计数达到0时对象会被自动销毁。
     *
     * @note 该类禁用拷贝构造和赋值操作以确保引用计数的唯一性
     * @see Ref, WeakRef
     */
    class RefCounted
    {
    public:
        virtual ~RefCounted() = default;
        RefCounted() = default;

        /**
         * @brief 增加引用计数
         * @details 线程安全地将引用计数加1
         */
        void IncRefCount() const
        {
            ++m_RefCount;
        }

        /**
         * @brief 减少引用计数
         * @details 线程安全地将引用计数减1
         */
        void DecRefCount() const
        {
            --m_RefCount;
        }

        /**
         * @brief 获取当前引用计数
         * @return 当前的引用计数值
         */
        uint32_t GetRefCount() const { return m_RefCount.load(); }

    private:
        // 禁用拷贝和赋值操作
        RefCounted(const RefCounted &) = delete;
        RefCounted &operator=(const RefCounted &) = delete;

        mutable std::atomic<uint32_t> m_RefCount = 0;
    };

    /**
     * @brief 引用计数生命周期跟踪工具
     * @details 提供对象生命周期的调试和跟踪功能
     */
    namespace RefUtils
    {
        /**
         * @brief 将对象实例添加到存活引用列表
         * @param instance 对象实例指针
         */
        void AddToLiveReferences(void *instance);

        /**
         * @brief 从存活引用列表中移除对象实例
         * @param instance 对象实例指针
         */
        void RemoveFromLiveReferences(void *instance);

        /**
         * @brief 检查对象实例是否仍然存活
         * @param instance 对象实例指针
         * @return 如果对象仍然存活返回true，否则返回false
         */
        bool IsLive(void *instance);
    }

    /**
     * @brief 智能指针模板类
     * @tparam T 管理的对象类型，必须继承自RefCounted
     * @details 提供自动内存管理的强引用智能指针
     *
     * Ref<T>使用引用计数来管理对象生命周期。当最后一个Ref<T>
     * 实例被销毁时，所管理的对象也会被自动删除。
     *
     * 特性：
     * - 线程安全的引用计数
     * - 自动内存管理
     * - 类型安全的转换
     * - 完整的拷贝/移动语义
     *
     * @see RefCounted, WeakRef
     */
    template <typename T>
    class Ref
    {
    public:
        /**
         * @brief 默认构造函数
         * @details 创建一个空的智能指针
         */
        Ref()
            : m_Instance(nullptr)
        {
        }

        /**
         * @brief 空指针构造函数
         * @param n nullptr
         */
        Ref(std::nullptr_t n)
            : m_Instance(nullptr)
        {
        }

        /**
         * @brief 原始指针构造函数
         * @param instance 要管理的对象指针
         * @details 接管对象的所有权并增加引用计数
         */
        Ref(T *instance)
            : m_Instance(instance)
        {
            static_assert(std::is_base_of<RefCounted, T>::value, "Class is not RefCounted!");

            IncRef();
        }

        /**
         * @brief 类型转换拷贝构造函数
         * @tparam T2 源类型
         * @param other 源智能指针
         * @details 允许从派生类型到基类型的转换
         */
        template <typename T2>
        Ref(const Ref<T2> &other)
        {
            m_Instance = (T *)other.m_Instance;
            IncRef();
        }

        /**
         * @brief 类型转换移动构造函数
         * @tparam T2 源类型
         * @param other 源智能指针（移动）
         */
        template <typename T2>
        Ref(Ref<T2> &&other)
        {
            m_Instance = (T *)other.m_Instance;
            other.m_Instance = nullptr;
        }

        /**
         * @brief 创建不增加引用计数的拷贝
         * @param other 源智能指针
         * @return 新的智能指针实例
         * @warning 这是一个危险的操作，仅在特殊情况下使用
         */
        static Ref<T> CopyWithoutIncrement(const Ref<T> &other)
        {
            Ref<T> result = nullptr;
            result.m_Instance = other.m_Instance;
            return result;
        }

        /**
         * @brief 析构函数
         * @details 减少引用计数，如果为0则删除对象
         */
        ~Ref()
        {
            DecRef();
        }

        /**
         * @brief 拷贝构造函数
         * @param other 源智能指针
         */
        Ref(const Ref<T> &other)
            : m_Instance(other.m_Instance)
        {
            IncRef();
        }

        /**
         * @brief nullptr赋值操作符
         * @param 忽略的nullptr
         * @return 当前对象引用
         */
        Ref &operator=(std::nullptr_t)
        {
            DecRef();
            m_Instance = nullptr;
            return *this;
        }

        /**
         * @brief 拷贝赋值操作符
         * @param other 源智能指针
         * @return 当前对象引用
         */
        Ref &operator=(const Ref<T> &other)
        {
            if (this == &other)
                return *this;

            other.IncRef();
            DecRef();

            m_Instance = other.m_Instance;
            return *this;
        }

        /**
         * @brief 类型转换拷贝赋值操作符
         * @tparam T2 源类型
         * @param other 源智能指针
         * @return 当前对象引用
         */
        template <typename T2>
        Ref &operator=(const Ref<T2> &other)
        {
            other.IncRef();
            DecRef();

            m_Instance = other.m_Instance;
            return *this;
        }

        /**
         * @brief 类型转换移动赋值操作符
         * @tparam T2 源类型
         * @param other 源智能指针（移动）
         * @return 当前对象引用
         */
        template <typename T2>
        Ref &operator=(Ref<T2> &&other)
        {
            DecRef();

            m_Instance = other.m_Instance;
            other.m_Instance = nullptr;
            return *this;
        }

        /**
         * @brief 布尔转换操作符
         * @return 如果持有有效对象返回true
         */
        operator bool() { return m_Instance != nullptr; }

        /**
         * @brief 常量布尔转换操作符
         * @return 如果持有有效对象返回true
         */
        operator bool() const { return m_Instance != nullptr; }

        /**
         * @brief 成员访问操作符
         * @return 对象指针
         */
        T *operator->() { return m_Instance; }

        /**
         * @brief 常量成员访问操作符
         * @return 常量对象指针
         */
        const T *operator->() const { return m_Instance; }

        /**
         * @brief 解引用操作符
         * @return 对象引用
         */
        T &operator*() { return *m_Instance; }

        /**
         * @brief 常量解引用操作符
         * @return 常量对象引用
         */
        const T &operator*() const { return *m_Instance; }

        /**
         * @brief 获取原始指针
         * @return 原始对象指针
         */
        T *Raw() { return m_Instance; }

        /**
         * @brief 获取常量原始指针
         * @return 常量原始对象指针
         */
        const T *Raw() const { return m_Instance; }

        /**
         * @brief 重置智能指针
         * @param instance 新的对象指针，默认为nullptr
         * @details 释放当前对象并设置新对象
         */
        void Reset(T *instance = nullptr)
        {
            DecRef();
            m_Instance = instance;
        }

        /**
         * @brief 类型转换
         * @tparam T2 目标类型
         * @return 转换后的智能指针
         * @details 使用动态转换将当前对象转换为其他类型
         */
        template <typename T2>
        Ref<T2> As() const
        {
            return Ref<T2>(*this);
        }

        /**
         * @brief 创建对象实例
         * @tparam Args 构造函数参数类型
         * @param args 构造函数参数
         * @return 新创建的智能指针
         * @details 使用完美转发创建对象实例
         */
        template <typename... Args>
        static Ref<T> Create(Args &&...args)
        {
#if ORG_TRACK_MEMORY && defined(ORG_PLATFORM_WINDOWS)
            return Ref<T>(new (typeid(T).name()) T(std::forward<Args>(args)...));
#else
            return Ref<T>(new T(std::forward<Args>(args)...));
#endif
        }

        /**
         * @brief 相等比较操作符
         * @param other 比较对象
         * @return 如果指向同一对象返回true
         */
        bool operator==(const Ref<T> &other) const
        {
            return m_Instance == other.m_Instance;
        }

        /**
         * @brief 不等比较操作符
         * @param other 比较对象
         * @return 如果指向不同对象返回true
         */
        bool operator!=(const Ref<T> &other) const
        {
            return !(*this == other);
        }

        /**
         * @brief 对象内容相等比较
         * @param other 比较对象
         * @return 如果对象内容相等返回true
         * @details 比较对象的内容而不是指针地址
         */
        bool EqualsObject(const Ref<T> &other)
        {
            if (!m_Instance || !other.m_Instance)
                return false;

            return *m_Instance == *other.m_Instance;
        }

    private:
        /**
         * @brief 增加引用计数
         * @details 私有方法，用于内部引用计数管理
         */
        void IncRef() const
        {
            if (m_Instance)
            {
                m_Instance->IncRefCount();
                RefUtils::AddToLiveReferences((void *)m_Instance);
            }
        }

        /**
         * @brief 减少引用计数
         * @details 私有方法，用于内部引用计数管理
         */
        void DecRef() const
        {
            if (m_Instance)
            {
                m_Instance->DecRefCount();

                if (m_Instance->GetRefCount() == 0)
                {
                    delete m_Instance;
                    RefUtils::RemoveFromLiveReferences((void *)m_Instance);
                    m_Instance = nullptr;
                }
            }
        }

        template <class T2>
        friend class Ref;
        mutable T *m_Instance;
    };

    /**
     * @brief 弱引用智能指针模板类
     * @tparam T 管理的对象类型，必须继承自RefCounted
     * @details 提供不影响对象生命周期的弱引用
     *
     * WeakRef<T>不会增加对象的引用计数，因此不会阻止对象被销毁。
     * 在使用弱引用指向的对象前，应该检查对象是否仍然有效。
     *
     * 使用场景：
     * - 避免循环引用
     * - 观察者模式
     * - 缓存机制
     *
     * @see Ref, RefCounted
     */
    template <typename T>
    class WeakRef
    {
    public:
        /**
         * @brief 默认构造函数
         * @details 创建一个空的弱引用
         */
        WeakRef() = default;

        /**
         * @brief 从强引用构造
         * @param ref 强引用对象
         */
        WeakRef(Ref<T> ref)
        {
            m_Instance = ref.Raw();
        }

        /**
         * @brief 从原始指针构造
         * @param instance 原始指针
         */
        WeakRef(T *instance)
        {
            m_Instance = instance;
        }

        /**
         * @brief 成员访问操作符
         * @return 对象指针
         * @warning 使用前应检查IsValid()
         */
        T *operator->() { return m_Instance; }

        /**
         * @brief 常量成员访问操作符
         * @return 常量对象指针
         * @warning 使用前应检查IsValid()
         */
        const T *operator->() const { return m_Instance; }

        /**
         * @brief 解引用操作符
         * @return 对象引用
         * @warning 使用前应检查IsValid()
         */
        T &operator*() { return *m_Instance; }

        /**
         * @brief 常量解引用操作符
         * @return 常量对象引用
         * @warning 使用前应检查IsValid()
         */
        const T &operator*() const { return *m_Instance; }

        /**
         * @brief 检查弱引用是否有效
         * @return 如果指向的对象仍然存活返回true
         * @details 通过RefUtils检查对象是否仍在存活引用列表中
         */
        bool IsValid() const { return m_Instance ? RefUtils::IsLive(m_Instance) : false; }

        /**
         * @brief 布尔转换操作符
         * @return 等同于IsValid()的结果
         */
        operator bool() const { return IsValid(); }

        /**
         * @brief 类型转换
         * @tparam T2 目标类型
         * @return 转换后的弱引用
         */
        template <typename T2>
        WeakRef<T2> As() const
        {
            return WeakRef<T2>(dynamic_cast<T2 *>(m_Instance));
        }

    private:
        T *m_Instance = nullptr;
    };

}

#endif // ORANGE_REF_H
