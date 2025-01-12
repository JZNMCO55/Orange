#ifndef OPENGL_TEXTURE_H
#define OPENGL_TEXTURE_H

#include "OrangeExport.h"
#include "Renderer/Texture.h"

namespace Orange
{
    class ORANGE_API OpenGLTexture2D : public Texture2D
    {
    public:
        OpenGLTexture2D(uint32_t width, uint32_t height);
        OpenGLTexture2D(const std::string& path);
        virtual ~OpenGLTexture2D();

        virtual uint32_t GetWidth() const override { return mWidth; }
        virtual uint32_t GetHeight() const override { return mHeight; }

        virtual void Bind(uint32_t slot = 0) const override;

        virtual void SetData(void* data, uint32_t size);
    private:
        std::string mPath;
        uint32_t mWidth;
        uint32_t mHeight;
        uint32_t mRendererID;
        unsigned int mInternalFormat, mDataFormat;
    };
}

#endif // OPENGL_TEXTURE_H