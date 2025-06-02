#ifndef ORANGE_GRAPHICS_IRENDERDEVICE_H
#define ORANGE_GRAPHICS_IRENDERDEVICE_H

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace Orange::Graphics
{

    // 前向声明
    class IBuffer;
    class ITexture;
    class IPipeline;

    /**
     * @brief 缓冲区使用类型
     */
    enum class BufferUsage
    {
        Vertex,  // 顶点缓冲区
        Index,   // 索引缓冲区
        Uniform, // 统一缓冲区
        Storage, // 存储缓冲区
        Staging  // 暂存缓冲区
    };

    /**
     * @brief 内存属性类型
     */
    enum class MemoryProperty
    {
        DeviceLocal, // 设备本地内存（GPU）
        HostVisible, // 主机可见内存（CPU可访问）
        HostCoherent // 主机一致性内存
    };

    /**
     * @brief 缓冲区创建信息
     */
    struct BufferCreateInfo
    {
        size_t size = 0;
        BufferUsage usage = BufferUsage::Vertex;
        MemoryProperty memoryProperty = MemoryProperty::DeviceLocal;
        std::string debugName; // 调试名称
    };

    /**
     * @brief 纹理格式
     */
    enum class TextureFormat
    {
        R8G8B8A8_UNORM,      // 8位RGBA
        R8G8B8A8_SRGB,       // 8位SRGB
        R32G32B32A32_SFLOAT, // 32位浮点RGBA
        D32_SFLOAT,          // 32位深度
        D24_UNORM_S8_UINT    // 24位深度8位模板
    };

    /**
     * @brief 纹理类型
     */
    enum class TextureType
    {
        Texture2D,
        TextureCube,
        Texture3D
    };

    /**
     * @brief 纹理创建信息
     */
    struct TextureCreateInfo
    {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t mipLevels = 1;
        TextureFormat format = TextureFormat::R8G8B8A8_UNORM;
        TextureType type = TextureType::Texture2D;
        std::string debugName;
    };

    /**
     * @brief 着色器阶段
     */
    enum class ShaderStage
    {
        Vertex,
        Fragment,
        Geometry,
        Compute,
        TessellationControl,
        TessellationEvaluation
    };

    /**
     * @brief 管线创建信息
     */
    struct PipelineCreateInfo
    {
        std::vector<std::pair<ShaderStage, std::vector<uint32_t>>> shaders; // 着色器SPIR-V字节码
        std::string debugName;
    };

    /**
     * @brief 渲染设备接口
     * 负责管理和创建各种渲染资源
     */
    class IRenderDevice
    {
    public:
        virtual ~IRenderDevice() = default;

        /**
         * @brief 创建缓冲区
         * @param info 缓冲区创建信息
         * @return 缓冲区智能指针，失败返回nullptr
         */
        virtual std::unique_ptr<IBuffer> CreateBuffer(const BufferCreateInfo &info) = 0;

        /**
         * @brief 创建纹理
         * @param info 纹理创建信息
         * @return 纹理智能指针，失败返回nullptr
         */
        virtual std::unique_ptr<ITexture> CreateTexture(const TextureCreateInfo &info) = 0;

        /**
         * @brief 创建渲染管线
         * @param info 管线创建信息
         * @return 管线智能指针，失败返回nullptr
         */
        virtual std::unique_ptr<IPipeline> CreatePipeline(const PipelineCreateInfo &info) = 0;

        /**
         * @brief 等待设备空闲
         * 等待所有提交的命令完成执行
         */
        virtual void WaitIdle() = 0;

        /**
         * @brief 获取设备名称
         * @return 设备名称字符串
         */
        virtual std::string GetDeviceName() const = 0;

        /**
         * @brief 获取可用内存大小
         * @return 内存大小（字节）
         */
        virtual uint64_t GetAvailableMemory() const = 0;

        /**
         * @brief 获取已使用内存大小
         * @return 内存大小（字节）
         */
        virtual uint64_t GetUsedMemory() const = 0;

        /**
         * @brief 获取设备类型描述
         * @return 设备类型字符串
         */
        virtual std::string GetDeviceType() const = 0;

        /**
         * @brief 获取驱动版本
         * @return 驱动版本字符串
         */
        virtual std::string GetDriverVersion() const = 0;

        /**
         * @brief 检查设备是否支持指定功能
         * @param feature 功能名称
         * @return 支持返回true，不支持返回false
         */
        virtual bool SupportsFeature(const std::string &feature) const = 0;
    };

} // namespace Orange::Graphics

#endif // ORANGE_GRAPHICS_IRENDERDEVICE_H