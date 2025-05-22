/**
 * @file GraphicsTypes.h
 * @brief 定义渲染系统中使用的基本数据类型
 */

#ifndef ORANGE_GRAPHICS_TYPES_H
#define ORANGE_GRAPHICS_TYPES_H

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace Orange
{
    namespace Graphics
    {

        // 基础尺寸类型
        struct Extent2D
        {
            uint32_t width;
            uint32_t height;

            Extent2D() : width(0), height(0) {}
            Extent2D(uint32_t w, uint32_t h) : width(w), height(h) {}
        };

        struct Extent3D
        {
            uint32_t width;
            uint32_t height;
            uint32_t depth;

            Extent3D() : width(0), height(0), depth(0) {}
            Extent3D(uint32_t w, uint32_t h, uint32_t d) : width(w), height(h), depth(d) {}
        };

        // 基础偏移类型
        struct Offset2D
        {
            int32_t x;
            int32_t y;

            Offset2D() : x(0), y(0) {}
            Offset2D(int32_t x_, int32_t y_) : x(x_), y(y_) {}
        };

        struct Offset3D
        {
            int32_t x;
            int32_t y;
            int32_t z;

            Offset3D() : x(0), y(0), z(0) {}
            Offset3D(int32_t x_, int32_t y_, int32_t z_) : x(x_), y(y_), z(z_) {}
        };

        // 矩形区域
        struct Rect2D
        {
            Offset2D offset;
            Extent2D extent;

            Rect2D() = default;
            Rect2D(Offset2D off, Extent2D ext) : offset(off), extent(ext) {}
            Rect2D(int32_t x, int32_t y, uint32_t width, uint32_t height)
                : offset(x, y), extent(width, height) {}
        };

        // 颜色值
        struct Color4f
        {
            float r, g, b, a;

            Color4f() : r(0.0f), g(0.0f), b(0.0f), a(1.0f) {}
            Color4f(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
        };

        // 顶点格式描述
        struct VertexAttribute
        {
            uint32_t location; // 着色器中的位置
            uint32_t binding;  // 绑定点
            uint32_t offset;   // 属性偏移
            uint32_t stride;   // 顶点步长

            VertexAttribute() : location(0), binding(0), offset(0), stride(0) {}
            VertexAttribute(uint32_t loc, uint32_t bind, uint32_t off, uint32_t str)
                : location(loc), binding(bind), offset(off), stride(str) {}
        };

        // 视口定义
        struct Viewport
        {
            float x;
            float y;
            float width;
            float height;
            float minDepth;
            float maxDepth;

            Viewport()
                : x(0.0f), y(0.0f), width(0.0f), height(0.0f), minDepth(0.0f), maxDepth(1.0f) {}

            Viewport(float x_, float y_, float w, float h, float min = 0.0f, float max = 1.0f)
                : x(x_), y(y_), width(w), height(h), minDepth(min), maxDepth(max) {}
        };

        // 缓冲区范围
        struct BufferRange
        {
            uint64_t offset;
            uint64_t size;

            BufferRange() : offset(0), size(0) {}
            BufferRange(uint64_t off, uint64_t sz) : offset(off), size(sz) {}
        };

        // 缓冲区拷贝区域
        struct BufferCopyRegion
        {
            uint64_t srcOffset;
            uint64_t dstOffset;
            uint64_t size;

            BufferCopyRegion() : srcOffset(0), dstOffset(0), size(0) {}
            BufferCopyRegion(uint64_t src, uint64_t dst, uint64_t sz) : srcOffset(src), dstOffset(dst), size(sz) {}
        };

        // 纹理拷贝区域
        struct TextureCopyRegion
        {
            uint32_t srcMipLevel;
            uint32_t srcLayer;
            Offset3D srcOffset;
            uint32_t dstMipLevel;
            uint32_t dstLayer;
            Offset3D dstOffset;
            Extent3D extent;

            TextureCopyRegion()
                : srcMipLevel(0), srcLayer(0), dstMipLevel(0), dstLayer(0) {}
        };

        // 缓冲区到纹理的拷贝区域
        struct BufferTextureCopyRegion
        {
            uint64_t bufferOffset;
            uint32_t bufferRowLength;
            uint32_t bufferImageHeight;
            uint32_t mipLevel;
            uint32_t baseArrayLayer;
            uint32_t layerCount;
            Offset3D imageOffset;
            Extent3D imageExtent;

            BufferTextureCopyRegion()
                : bufferOffset(0), bufferRowLength(0), bufferImageHeight(0),
                  mipLevel(0), baseArrayLayer(0), layerCount(1) {}
        };

    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_GRAPHICS_TYPES_H