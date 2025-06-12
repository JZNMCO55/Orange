/**
 * @file RendererTypes.h
 * @brief 渲染器基础类型定义
 * @details 定义了渲染器模块中使用的基础数据类型和类型别名
 * @author Hazel Engine Team
 */

#ifndef ORANGE_RENDERER_TYPES_H
#define ORANGE_RENDERER_TYPES_H

#include <cstdint>

namespace Orange 
{

    /**
     * @brief 渲染器对象唯一标识符类型
     * @details 用于标识渲染器中各种对象（如纹理、缓冲区、着色器等）的唯一ID
     *
     * 使用32位无符号整数作为标识符，提供足够的ID空间同时保持较小的内存占用。
     * 在渲染器内部，每个资源对象都会被分配一个唯一的RendererID。
     *
     * @note ID值0通常被保留作为无效ID或空ID使用
     * @see Texture, Buffer, Shader等渲染资源类
     */
    using RendererID = uint32_t;

}
#endif // ORANGE_RENDERER_TYPES_H