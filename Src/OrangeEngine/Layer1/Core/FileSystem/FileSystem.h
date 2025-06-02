#ifndef ORANGE_CORE_FILESYSTEM_H
#define ORANGE_CORE_FILESYSTEM_H

#include <string>
#include <vector>
#include <fstream>

namespace Orange
{
    namespace Core
    {
        /**
         * @brief 文件系统类，负责文件读取操作
         */
        class FileSystem
        {
        public:
            /**
             * @brief 读取文本文件内容
             * @param filename 文件路径
             * @return 文件内容字符串
             */
            static std::string ReadTextFile(const std::string &filename);

            /**
             * @brief 读取二进制文件内容
             * @param filename 文件路径
             * @return 文件内容字节数组
             */
            static std::vector<uint8_t> ReadBinaryFile(const std::string &filename);

            /**
             * @brief 读取二进制文件内容为 uint32_t 数组 (用于 SPIR-V)
             * @param filename 文件路径
             * @return uint32_t 数组
             */
            static std::vector<uint32_t> ReadBinaryFileAsUint32(const std::string &filename);

            /**
             * @brief 检查文件是否存在
             * @param filename 文件路径
             * @return 文件是否存在
             */
            static bool FileExists(const std::string &filename);

            /**
             * @brief 获取文件扩展名
             * @param filename 文件路径
             * @return 文件扩展名（不包含点）
             */
            static std::string GetFileExtension(const std::string &filename);
        };
    }
}

#endif // ORANGE_CORE_FILESYSTEM_H