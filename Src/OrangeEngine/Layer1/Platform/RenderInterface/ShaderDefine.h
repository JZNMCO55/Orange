#ifndef SHADER_DEFINE_H
#define SHADER_DEFINE_H

#include "Core/Base/Base.h"

namespace Orange::ShaderDefine
{
    /**
     * @enum AOMethod
     * @brief 环境光遮蔽(Ambient Occlusion)方法枚举
     *
     * 定义了支持的环境光遮蔽计算方法。环境光遮蔽是一种重要的
     * 光照技术，用于模拟环境光在几何体凹陷处的遮蔽效果。
     */
    enum class AOMethod
    {
        None = 0,     ///< 无环境光遮蔽
        GTAO = BIT(1) ///< Ground Truth Ambient Occlusion（地面真实环境光遮蔽）
    };

    /**
     * @brief 获取AO方法的索引
     * @param method AO方法枚举值
     * @return 对应的数组索引
     *
     * 将AOMethod枚举值转换为数组索引，用于在数组中查找
     * 对应的配置或数据。这个函数是constexpr的，可以在
     * 编译时计算结果。
     *
     * 索引映射：
     * - None -> 0
     * - GTAO -> 1
     *
     * @note 这是一个编译时常量函数
     */
    constexpr static std::underlying_type_t<AOMethod> GetMethodIndex(const AOMethod method)
    {
        switch (method)
        {
        case AOMethod::None:
            return 0;
        case AOMethod::GTAO:
            return 1;
        }
        return 0;
    }

    /**
     * @brief 只读AO方法数组
     *
     * 包含所有支持的AO方法的静态数组，用于遍历或索引访问。
     * 数组大小为4，但目前只使用前2个元素。
     *
     * 数组内容：
     * - [0] = AOMethod::None
     * - [1] = AOMethod::GTAO
     * - [2-3] = 未使用，预留给未来的AO方法
     */
    constexpr static ShaderDef::AOMethod ROMETHODS[4] = {AOMethod::None, AOMethod::GTAO};

    /**
     * @brief 根据GTAO启用状态获取AO方法
     * @param gtaoEnabled 是否启用GTAO
     * @return 对应的AO方法枚举值
     *
     * 这是一个便利函数，根据布尔值选择合适的AO方法。
     * 主要用于简化配置逻辑和条件编译。
     *
     * 选择逻辑：
     * - gtaoEnabled = true -> 返回AOMethod::GTAO
     * - gtaoEnabled = false -> 返回AOMethod::None
     *
     * 使用场景：
     * - 根据用户设置选择AO方法
     * - 在着色器编译时确定使用的AO技术
     * - 性能配置中的自动选择
     *
     * @note 这是一个编译时常量函数
     */
    constexpr static AOMethod GetAOMethod(const bool gtaoEnabled)
    {
        if (gtaoEnabled)
            return AOMethod::GTAO;

        return AOMethod::None;
    }
}

#endif