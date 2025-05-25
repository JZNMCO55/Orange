/**
 * @file VulkanShaderUtils.h
 * @brief Vulkan着色器工具类
 */

#ifndef ORANGE_VULKAN_SHADER_UTILS_H
#define ORANGE_VULKAN_SHADER_UTILS_H

#ifdef ORANGE_VULKAN_ENABLED

#include "../VulkanCommon/VulkanCommon.h"
#include <vector>
#include <string>
#include <map>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {
            /**
             * @brief Vulkan着色器工具类
             */
            class VulkanShaderUtils
            {
            public:
                /**
                 * @brief 将十六进制字符串转换为字节数组
                 * @param hexString 十六进制字符串
                 * @return 字节数组
                 */
                static std::vector<uint8_t> HexStringToBytes(const std::string &hexString);

                /**
                 * @brief 获取内置的简单顶点着色器SPIR-V代码
                 * @return SPIR-V字节码
                 */
                static std::vector<uint8_t> GetSimpleVertexShaderSPIRV();

                /**
                 * @brief 获取内置的简单片段着色器SPIR-V代码
                 * @return SPIR-V字节码
                 */
                static std::vector<uint8_t> GetSimpleFragmentShaderSPIRV();

                /**
                 * @brief 获取三角形顶点着色器SPIR-V代码
                 * @return SPIR-V字节码
                 */
                static std::vector<uint8_t> GetTriangleVertexShaderSPIRV();

                /**
                 * @brief 获取三角形片段着色器SPIR-V代码
                 * @return SPIR-V字节码
                 */
                static std::vector<uint8_t> GetTriangleFragmentShaderSPIRV();

                /**
                 * @brief 验证SPIR-V字节码的有效性
                 * @param spirvCode SPIR-V字节码
                 * @return 是否有效
                 */
                static bool ValidateSPIRVCode(const std::vector<uint8_t> &spirvCode);

                /**
                 * @brief 从文件扩展名推断着色器类型
                 * @param filename 文件名
                 * @return 着色器类型
                 */
                static ShaderType DeduceShaderTypeFromFilename(const std::string &filename);

                /**
                 * @brief 获取着色器类型的字符串名称
                 * @param type 着色器类型
                 * @return 类型名称
                 */
                static std::string GetShaderTypeName(ShaderType type);

            private:
                // 禁止实例化
                VulkanShaderUtils() = delete;
                ~VulkanShaderUtils() = delete;
                VulkanShaderUtils(const VulkanShaderUtils &) = delete;
                VulkanShaderUtils &operator=(const VulkanShaderUtils &) = delete;
            };

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_ENABLED

#endif // ORANGE_VULKAN_SHADER_UTILS_H