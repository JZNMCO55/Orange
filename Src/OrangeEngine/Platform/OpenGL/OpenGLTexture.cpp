#include "OpenGLpch.h"
#include "OpenGLTexture.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
namespace Orange
{
    OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height)
    : mWidth(width), mHeight(height)
    {
        ORG_PROFILE_FUNCTION();

        mInternalFormat = GL_RGBA8;
        mDataFormat = GL_RGBA;
        glCreateTextures(GL_TEXTURE_2D, 1, &mRendererID);
        glTextureStorage2D(mRendererID, 1, mInternalFormat, mWidth, mHeight);
        glTextureParameteri(mRendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(mRendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(mRendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(mRendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    OpenGLTexture2D::OpenGLTexture2D(const std::string& path)
    {
        ORG_PROFILE_FUNCTION();

        int width, height, channels;
        stbi_set_flip_vertically_on_load(1);
        stbi_uc* data;
        
        {
            ORG_PROFILE_SCOPE("stbi_load-OpenGLTexture2D::OpenGLTexture2D(const std::string& path)");
            data = stbi_load(path.c_str(), &width, &height, &channels, 0);

        }

        ORANGE_CORE_ASSERT(data, "Failed to load image {0}", path);

        mWidth = width;
        mHeight = height;

        GLenum internalFormat = 0, dataFormat = 0;
        if (channels == 4)
        {
            mInternalFormat = GL_RGBA8;
            mDataFormat = GL_RGBA;
        }
        else if (channels == 3)
        {
            mInternalFormat = GL_RGB8;
            mDataFormat = GL_RGB;
        }
        else
        {
            ORANGE_CORE_ASSERT(false, "Image format not supported");
        }

        glCreateTextures(GL_TEXTURE_2D, 1, &mRendererID);
        glTextureStorage2D(mRendererID, 1, mInternalFormat, mWidth, mHeight);

        glTextureParameteri(mRendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(mRendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glTextureParameteri(mRendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(mRendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glTextureSubImage2D(mRendererID, 0, 0, 0, mWidth, mHeight, mDataFormat, GL_UNSIGNED_BYTE, data);

        stbi_image_free(data);
    }

    OpenGLTexture2D::~OpenGLTexture2D()
    {
        ORG_PROFILE_FUNCTION();

        glDeleteTextures(1, &mRendererID);
    }

    void OpenGLTexture2D::Bind(uint32_t slot) const
    {
        ORG_PROFILE_FUNCTION();

        glBindTextureUnit(slot, mRendererID);
    }

    void OpenGLTexture2D::SetData(void* data, uint32_t size)
    {
        ORG_PROFILE_FUNCTION();

        uint32_t bpp = mDataFormat == GL_RGBA ? 4 : 3;
        ORANGE_CORE_ASSERT(size == mWidth * mHeight * bpp, "Data must be entire texture!");
        glTextureSubImage2D(mRendererID, 0, 0, 0, mWidth, mHeight, mDataFormat, GL_UNSIGNED_BYTE, data);
    }
}