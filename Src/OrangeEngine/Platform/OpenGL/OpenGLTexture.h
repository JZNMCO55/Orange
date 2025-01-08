#ifndef OPENGL_TEXTURE_H
#define OPENGL_TEXTURE_H

#include "OrangeExport.h"
#include "Renderer/Texture.h"

namespace Orange
{
    class ORANGE_API OpenGLTexture2D : public Texture2D
    {
    public:
        OpenGLTexture2D(const std::string& path);
        virtual ~OpenGLTexture2D();

        virtual uint32_t GetWidth() const override { return mWidth; }
        virtual uint32_t GetHeight() const override { return mHeight; }

        virtual void Bind(uint32_t slot = 0) const override;
    private:
        std::string mPath;
        uint32_t mWidth;
        uint32_t mHeight;
        uint32_t mRendererID;
    };
}

#endif // OPENGL_TEXTURE_H