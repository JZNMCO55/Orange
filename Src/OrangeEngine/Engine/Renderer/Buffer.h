#ifndef BUFFER_H
#define BUFFER_H

#include "OrangeExport.h"
namespace Orange
{
    enum class EShaderDataType
    {
        None = 0,
        Float, Float2, Float3, Float4,
        Mat3, Mat4,
        Int, Int2, Int3, Int4,
        Bool
    };

    static uint32_t ShaderDataTypeSize(EShaderDataType type)
    {
        switch (type)
        {
            case EShaderDataType::Float:    return 4;
            case EShaderDataType::Float2:   return 4 * 2;
            case EShaderDataType::Float3:   return 4 * 3;
            case EShaderDataType::Float4:   return 4 * 4;
            case EShaderDataType::Mat3:     return 4 * 3 * 3;
            case EShaderDataType::Mat4:     return 4 * 4 * 4;
            case EShaderDataType::Int:      return 4;
            case EShaderDataType::Int2:     return 4 * 2;
            case EShaderDataType::Int3:     return 4 * 3;
            case EShaderDataType::Int4:     return 4 * 4;
            case EShaderDataType::Bool:     return 1;
        }

        ORANGE_CORE_ASSERT(false, "Unknown ShaderDataType!");

        return 0;
    }

    struct BufferElement
    {
        std::string Name;
        EShaderDataType Type;
        uint32_t Size;
        uint32_t Offset;
        bool Normalized;

        BufferElement() {}

        BufferElement(EShaderDataType type, const std::string& name, bool normalized = false)
            : Name(name), Type(type), Size(ShaderDataTypeSize(type)), Offset(0), Normalized(normalized)
        {
        }

        uint32_t GetComponentCount() const
        {
            switch (Type)
            {
                case EShaderDataType::Float:    return 1;
                case EShaderDataType::Float2:   return 2;
                case EShaderDataType::Float3:   return 3;
                case EShaderDataType::Float4:   return 4;
                case EShaderDataType::Mat3:     return 3 * 3;
                case EShaderDataType::Mat4:     return 4 * 4;
                case EShaderDataType::Int:      return 1;
                case EShaderDataType::Int2:     return 2;
                case EShaderDataType::Int3:     return 3;
                case EShaderDataType::Int4:     return 4;
                case EShaderDataType::Bool:     return 1;
            }

            ORANGE_CORE_ASSERT(false, "Unknown ShaderDataType!");
            return 0;
        }
    };

    class ORANGE_API BufferLayout
    {
    public:
        BufferLayout() {}
        BufferLayout(const std::initializer_list<BufferElement>& elements)
            : mElements(elements)
        {
            CalculateOffsetsAndStride();
        }

        std::vector<BufferElement>::iterator begin() { return mElements.begin(); }
        std::vector<BufferElement>::iterator end() { return mElements.end(); }
        std::vector<BufferElement>::const_iterator begin() const { return mElements.begin(); }
        std::vector<BufferElement>::const_iterator end() const { return mElements.end(); }

        uint32_t GetStride() const { return mStride; }
    private:
        void CalculateOffsetsAndStride()
        {
            uint32_t offset = 0;
            mStride = 0;
            for (auto& element : mElements)
            {
                element.Offset = offset;
                offset += element.Size;
                mStride += element.Size;
            }
        }

    private:
        std::vector<BufferElement> mElements;
        uint32_t mStride{ 0 };
    };

    class ORANGE_API VertexBuffer
    {
    public:
        virtual ~VertexBuffer() {};

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        virtual const BufferLayout& GetLayout() const = 0;
        virtual void SetLayout(const BufferLayout& layout) = 0;

        static VertexBuffer* Create(float* vertices, uint32_t size);
    }; 


    class ORANGE_API IndexBuffer
    {
    public:
        virtual ~IndexBuffer() {};

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;
        virtual uint32_t GetCount() const = 0;

        static IndexBuffer* Create(uint32_t* indices, uint32_t count);
    };
}

#endif // BUFFER_H