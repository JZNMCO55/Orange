#ifndef ORANGE_GRAPHICS_TEXTURE_H
#define ORANGE_GRAPHICS_TEXTURE_H

#include <cstdint>
#include <string>
#include "RenderDevice.h" // 为了使用TextureFormat等枚举

namespace Orange::Graphics
{

    /**
     * @brief 纹理接口
     * 提供纹理资源的基本操作功能
     */
    class Texture
    {
    public:
        virtual ~Texture() = default;

        /**
         * @brief 获取纹理宽度
         * @return 纹理宽度（像素）
         */
        virtual uint32_t GetWidth() const = 0;

        /**
         * @brief 获取纹理高度
         * @return 纹理高度（像素）
         */
        virtual uint32_t GetHeight() const = 0;

        /**
         * @brief 获取纹理深度（用于3D纹理）
         * @return 纹理深度
         */
        virtual uint32_t GetDepth() const = 0;

        /**
         * @brief 获取Mipmap层级数
         * @return Mipmap层级数
         */
        virtual uint32_t GetMipLevels() const = 0;

        /**
         * @brief 获取纹理格式
         * @return 纹理格式
         */
        virtual TextureFormat GetFormat() const = 0;

        /**
         * @brief 获取纹理类型
         * @return 纹理类型
         */
        virtual TextureType GetType() const = 0;

        /**
         * @brief 上传纹理数据
         * @param data 纹理数据指针
         * @param size 数据大小（字节）
         * @param mipLevel Mipmap层级
         * @param arrayLayer 数组层（用于纹理数组）
         */
        virtual void UploadData(const void *data, size_t size, uint32_t mipLevel = 0, uint32_t arrayLayer = 0) = 0;

        /**
         * @brief 下载纹理数据
         * @param data 目标数据缓冲区
         * @param size 缓冲区大小（字节）
         * @param mipLevel Mipmap层级
         * @param arrayLayer 数组层
         */
        virtual void DownloadData(void *data, size_t size, uint32_t mipLevel = 0, uint32_t arrayLayer = 0) = 0;

        /**
         * @brief 生成Mipmap
         * 自动生成所有Mipmap层级
         */
        virtual void GenerateMipmaps() = 0;

        /**
         * @brief 获取纹理调试名称
         * @return 调试名称字符串
         */
        virtual const std::string &GetDebugName() const = 0;

        /**
         * @brief 获取指定Mipmap层级的大小信息
         * @param mipLevel Mipmap层级
         * @param width 输出宽度
         * @param height 输出高度
         * @param depth 输出深度
         */
        virtual void GetMipLevelSize(uint32_t mipLevel, uint32_t &width, uint32_t &height, uint32_t &depth) const = 0;

        /**
         * @brief 计算纹理数据大小
         * @param mipLevel Mipmap层级
         * @return 数据大小（字节）
         */
        virtual size_t CalculateDataSize(uint32_t mipLevel = 0) const = 0;

        /**
         * @brief 检查纹理是否支持写入
         * @return 支持写入返回true
         */
        virtual bool IsWritable() const = 0;

        /**
         * @brief 检查纹理是否支持读取
         * @return 支持读取返回true
         */
        virtual bool IsReadable() const = 0;
    };

} // namespace Orange::Graphics

#endif // ORANGE_GRAPHICS_TEXTURE_H