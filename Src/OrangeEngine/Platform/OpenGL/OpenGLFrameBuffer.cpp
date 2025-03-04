#include "OpenGLPch.h"
#include "OpenGLFrameBuffer.h"

namespace Orange
{
    namespace Utils 
    {

        static GLenum TextureTarget(bool multisampled)
        {
            return multisampled ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
        }

        static void CreateTextures(bool multisampled, uint32_t* outID, uint32_t count)
        {
            glCreateTextures(TextureTarget(multisampled), count, outID);
        }

        static void BindTexture(bool multisampled, uint32_t id)
        {
            glBindTexture(TextureTarget(multisampled), id);
        }

        static void AttachColorTexture(uint32_t id, int samples, GLenum internalFormat, GLenum format, uint32_t width, uint32_t height, int index)
        {
            bool multisampled = samples > 1;
            if (multisampled)
            {
                glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, internalFormat, width, height, GL_FALSE);
            }
            else
            {
                glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, nullptr);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, TextureTarget(multisampled), id, 0);
        }

        static void AttachDepthTexture(uint32_t id, int samples, GLenum format, GLenum attachmentType, uint32_t width, uint32_t height)
        {
            bool multisampled = samples > 1;
            if (multisampled)
            {
                glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, format, width, height, GL_FALSE);
            }
            else
            {
                glTexStorage2D(GL_TEXTURE_2D, 1, format, width, height);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }

            glFramebufferTexture2D(GL_FRAMEBUFFER, attachmentType, TextureTarget(multisampled), id, 0);
        }

        static bool IsDepthFormat(FramebufferTextureFormat format)
        {
            switch (format)
            {
            case FramebufferTextureFormat::DEPTH24STENCIL8:  return true;
            }

            return false;
        }

    }

    static const uint32_t sMaxFramebufferSize = 8192;
    OpenGLFrameBuffer::OpenGLFrameBuffer(const FrameBufferSpecification& spec)
        : mSpecification(spec)
    {
        for (auto spec : mSpecification.attachment.Attachments)
        {
            if (!Utils::IsDepthFormat(spec.TextureFormat))
            {
                mColorAttachmentSpecifications.emplace_back(spec);
            }
            else
            {
                mDepthAttachmentSpecification = spec;
            }
        }


        Invalidate();
    }

    OpenGLFrameBuffer::~OpenGLFrameBuffer()
    {
        glDeleteFramebuffers(1, &mRendererID);
        glDeleteTextures(mColorAttachment.size(), mColorAttachment.data());
        glDeleteTextures(1, &mDepthAttachment);
    }


    void OpenGLFrameBuffer::Invalidate()
    {
        if (mRendererID)
        {
            glDeleteFramebuffers(1, &mRendererID);
            glDeleteTextures(mColorAttachment.size(), mColorAttachment.data());
            glDeleteTextures(1, &mDepthAttachment);

            mColorAttachment.clear();
            mDepthAttachment = 0;
        }


        glCreateFramebuffers(1, &mRendererID);
        glBindFramebuffer(GL_FRAMEBUFFER, mRendererID);

        bool multisample = mSpecification.samples > 1;

        // Attachments
        if (mColorAttachmentSpecifications.size())
        {
            mColorAttachment.resize(mColorAttachmentSpecifications.size());
            Utils::CreateTextures(multisample, mColorAttachment.data(), mColorAttachment.size());

            for (size_t i = 0; i < mColorAttachment.size(); i++)
            {
                Utils::BindTexture(multisample, mColorAttachment[i]);
                switch (mColorAttachmentSpecifications[i].TextureFormat)
                {
                case FramebufferTextureFormat::RGBA8:
                    Utils::AttachColorTexture(mColorAttachment[i], mSpecification.samples, GL_RGBA8, GL_RGBA, mSpecification.width, mSpecification.height, i);
                    break;
                case FramebufferTextureFormat::RED_INTEGER:
                    Utils::AttachColorTexture(mColorAttachment[i], mSpecification.samples, GL_R32I, GL_RED_INTEGER, mSpecification.width, mSpecification.height, i);
                    break;
                }
            }
        }

        if (mDepthAttachmentSpecification.TextureFormat != FramebufferTextureFormat::None)
        {
            Utils::CreateTextures(multisample, &mDepthAttachment, 1);
            Utils::BindTexture(multisample, mDepthAttachment);
            switch (mDepthAttachmentSpecification.TextureFormat)
            {
            case FramebufferTextureFormat::DEPTH24STENCIL8:
                Utils::AttachDepthTexture(mDepthAttachment, mSpecification.samples, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL_ATTACHMENT, mSpecification.width, mSpecification.height);
                break;
            }
        }

        if (mColorAttachment.size() > 1)
        {
            GLenum buffers[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
            glDrawBuffers(mColorAttachment.size(), buffers);
        }
        else if (mColorAttachment.empty())
        {
            // Only depth-pass
            glDrawBuffer(GL_NONE);
        }
    }

    void OpenGLFrameBuffer::Bind()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, mRendererID);
        glViewport(0, 0, mSpecification.width, mSpecification.height);
    }

    void OpenGLFrameBuffer::Unbind()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // note : avoid calling this function frequently, it is expensive
    void OpenGLFrameBuffer::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0 || width > sMaxFramebufferSize || height > sMaxFramebufferSize)
        {
            ORANGE_LOG_WARN("Attempted to resize framebuffer to {0}, {1}", width, height);
            return;
        }
        mSpecification.width = width;
        mSpecification.height = height;
        Invalidate();
    }

    const uint32_t OpenGLFrameBuffer::GetColorAttachmentRendererID(uint32_t index) const
    {
        ORANGE_CORE_ASSERT(index < mColorAttachment.size(), "Index {0} is out of range", index);
        return mColorAttachment[index];
    }

    int OpenGLFrameBuffer::ReadPixel(uint32_t attachmentIndex, int x, int y) const
    {
        ORANGE_CORE_ASSERT(attachmentIndex < mColorAttachment.size(), "Index {0} is out of range", attachmentIndex);

        glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
        int pixelData;
        glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixelData);
        return pixelData;
    }

}