#include "orgpch.h"
#include "Pipeline.h"

#include "RendererAPI.h"

namespace Orange
{
    Ref<Pipeline> Pipeline::Create(const PipelineSpecification &spec)
    {
        Ref<Pipeline> pipeline = nullptr;
        switch (RendererAPI::Current())
        {
        case RendererAPIType::None:
        {
            return nullptr;
        }
        case RendererAPIType::Vulkan:
        {
            // TODO: Vulkan
            // pipeline = CreateRef<VulkanPipeline>(spec);
        }
        }
        return pipeline;
    }
}