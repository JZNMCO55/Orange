/**
 * @file VulkanPipelineExample.cpp
 * @brief Vulkan渲染管线使用示例实现
 */

#ifdef ORANGE_VULKAN_ENABLED

#include "VulkanPipelineExample.h"
#include "../VulkanResources/VulkanShaderUtils.h"
#include <iostream>

namespace Orange
{
    namespace Graphics
    {
        namespace Vulkan
        {

            VulkanPipelineExample::VulkanPipelineExample(VulkanDevice *device)
                : m_device(device)
            {
            }

            VulkanPipelineExample::~VulkanPipelineExample()
            {
                Cleanup();
            }

            bool VulkanPipelineExample::Initialize()
            {
                if (!m_device)
                {
                    std::cerr << "VulkanPipelineExample: 设备为空" << std::endl;
                    return false;
                }

                std::cout << "VulkanPipelineExample: 开始初始化..." << std::endl;

                // 创建测试用的渲染通道
                if (!CreateTestRenderPass())
                {
                    std::cerr << "VulkanPipelineExample: 创建测试渲染通道失败" << std::endl;
                    return false;
                }

                // 创建测试用的着色器
                if (!CreateTestShaders())
                {
                    std::cerr << "VulkanPipelineExample: 创建测试着色器失败" << std::endl;
                    return false;
                }

                // 创建管线布局
                if (!DemoCreatePipelineLayout())
                {
                    std::cerr << "VulkanPipelineExample: 创建管线布局失败" << std::endl;
                    return false;
                }

                // 创建图形管线
                if (!DemoCreateTrianglePipeline())
                {
                    std::cerr << "VulkanPipelineExample: 创建图形管线失败" << std::endl;
                    return false;
                }

                // 创建计算管线
                if (!DemoCreateComputePipeline())
                {
                    std::cout << "VulkanPipelineExample: 计算管线创建跳过（没有计算着色器）" << std::endl;
                }

                std::cout << "VulkanPipelineExample: 初始化成功" << std::endl;
                return true;
            }

            void VulkanPipelineExample::Cleanup()
            {
                m_graphicsPipeline.reset();
                m_computePipeline.reset();
                m_pipelineLayout.reset();
                m_renderPass.reset();
                m_vertexShader.reset();
                m_fragmentShader.reset();
                m_computeShader.reset();
                std::cout << "VulkanPipelineExample: 清理完成" << std::endl;
            }

            bool VulkanPipelineExample::DemoCreateTrianglePipeline()
            {
                std::cout << "VulkanPipelineExample: 创建三角形渲染管线..." << std::endl;

                if (!m_pipelineLayout || !m_renderPass || !m_vertexShader || !m_fragmentShader)
                {
                    std::cerr << "VulkanPipelineExample: 缺少必要的资源" << std::endl;
                    return false;
                }

                // 创建图形管线
                m_graphicsPipeline = std::make_shared<VulkanPipeline>(m_device);

                // 设置着色器阶段
                std::vector<ShaderModuleInfo> shaderStages;

                ShaderModuleInfo vertexStage;
                vertexStage.type = ShaderType::Vertex;
                vertexStage.shaderModule = m_vertexShader.get();
                vertexStage.entryPoint = "main";
                shaderStages.push_back(vertexStage);

                ShaderModuleInfo fragmentStage;
                fragmentStage.type = ShaderType::Fragment;
                fragmentStage.shaderModule = m_fragmentShader.get();
                fragmentStage.entryPoint = "main";
                shaderStages.push_back(fragmentStage);

                // 设置顶点输入（三角形顶点：位置 + 颜色）
                std::vector<VertexInputDescription> vertexInputs;
                VertexInputDescription vertexInput;
                vertexInput.binding = 0;
                vertexInput.stride = sizeof(float) * 6; // vec3 position + vec3 color
                vertexInput.perInstance = false;

                // 位置属性
                VertexAttribute positionAttr;
                positionAttr.location = 0;
                positionAttr.binding = 0;
                positionAttr.offset = 0;
                positionAttr.stride = sizeof(float) * 3;
                vertexInput.attributes.push_back(positionAttr);

                // 颜色属性
                VertexAttribute colorAttr;
                colorAttr.location = 1;
                colorAttr.binding = 0;
                colorAttr.offset = sizeof(float) * 3;
                colorAttr.stride = sizeof(float) * 3;
                vertexInput.attributes.push_back(colorAttr);

                vertexInputs.push_back(vertexInput);

                // 设置输入装配状态
                InputAssemblyState inputAssembly;
                inputAssembly.topology = PrimitiveTopology::TriangleList;
                inputAssembly.primitiveRestart = false;

                // 设置光栅化状态
                RasterizationState rasterization;
                rasterization.depthClampEnable = false;
                rasterization.rasterizerDiscardEnable = false;
                rasterization.polygonMode = PolygonMode::Fill;
                rasterization.cullMode = CullMode::Back;
                rasterization.frontFace = FrontFace::CounterClockwise;
                rasterization.depthBiasEnable = false;
                rasterization.lineWidth = 1.0f;

                // 设置多重采样状态
                MultisampleState multisample;
                multisample.rasterizationSamples = 1;
                multisample.sampleShadingEnable = false;

                // 设置深度模板状态
                DepthStencilState depthStencil;
                depthStencil.depthTestEnable = true;
                depthStencil.depthWriteEnable = true;
                depthStencil.depthCompareOp = CompareOp::Less;

                // 设置颜色混合状态
                ColorBlendState colorBlend;
                ColorBlendAttachmentState colorBlendAttachment;
                colorBlendAttachment.blendEnable = false;
                colorBlendAttachment.colorWriteMask = static_cast<uint8_t>(ColorComponentFlag::All);
                colorBlend.attachments.push_back(colorBlendAttachment);

                // 创建图形管线信息
                GraphicsPipelineCreateInfo pipelineInfo;
                pipelineInfo.shaderStages = shaderStages;
                pipelineInfo.vertexInputs = vertexInputs;
                pipelineInfo.inputAssembly = inputAssembly;
                pipelineInfo.rasterization = rasterization;
                pipelineInfo.multisample = multisample;
                pipelineInfo.depthStencil = depthStencil;
                pipelineInfo.colorBlend = colorBlend;
                pipelineInfo.pipelineLayout = m_pipelineLayout.get();
                pipelineInfo.renderPass = m_renderPass.get();
                pipelineInfo.subpass = 0;
                pipelineInfo.name = "TrianglePipeline";

                // 初始化管线
                if (!m_graphicsPipeline->InitializeGraphics(pipelineInfo))
                {
                    std::cerr << "VulkanPipelineExample: 图形管线初始化失败" << std::endl;
                    return false;
                }

                return ValidatePipeline(m_graphicsPipeline, "图形管线");
            }

            bool VulkanPipelineExample::DemoCreateComputePipeline()
            {
                std::cout << "VulkanPipelineExample: 创建计算管线..." << std::endl;

                if (!m_pipelineLayout)
                {
                    std::cerr << "VulkanPipelineExample: 管线布局为空" << std::endl;
                    return false;
                }

                // 由于我们没有计算着色器，跳过计算管线创建
                if (!m_computeShader)
                {
                    std::cout << "VulkanPipelineExample: 没有计算着色器，跳过计算管线创建" << std::endl;
                    return true;
                }

                // 创建计算管线
                m_computePipeline = std::make_shared<VulkanPipeline>(m_device);

                // 设置计算着色器信息
                ShaderModuleInfo computeStage;
                computeStage.type = ShaderType::Compute;
                computeStage.shaderModule = m_computeShader.get();
                computeStage.entryPoint = "main";

                // 创建计算管线信息
                ComputePipelineCreateInfo pipelineInfo;
                pipelineInfo.computeShader = computeStage;
                pipelineInfo.pipelineLayout = m_pipelineLayout.get();
                pipelineInfo.name = "ComputePipeline";

                // 初始化管线
                if (!m_computePipeline->InitializeCompute(pipelineInfo))
                {
                    std::cerr << "VulkanPipelineExample: 计算管线初始化失败" << std::endl;
                    return false;
                }

                return ValidatePipeline(m_computePipeline, "计算管线");
            }

            bool VulkanPipelineExample::DemoCreatePipelineLayout()
            {
                std::cout << "VulkanPipelineExample: 创建管线布局..." << std::endl;

                // 创建管线布局
                m_pipelineLayout = std::make_shared<VulkanPipelineLayout>(m_device);

                // 设置简单的管线布局（无描述符集，无推送常量）
                PipelineLayoutCreateInfo layoutInfo;
                layoutInfo.name = "SimplePipelineLayout";

                // 初始化布局
                if (!m_pipelineLayout->Initialize(layoutInfo))
                {
                    std::cerr << "VulkanPipelineExample: 管线布局初始化失败" << std::endl;
                    return false;
                }

                std::cout << "VulkanPipelineExample: 管线布局创建成功" << std::endl;
                return true;
            }

            bool VulkanPipelineExample::CreateTestRenderPass()
            {
                std::cout << "VulkanPipelineExample: 创建测试渲染通道..." << std::endl;

                // 创建简单的渲染通道（颜色附件）
                RenderPassCreateInfo renderPassInfo;

                // 颜色附件
                AttachmentDescription colorAttachment;
                colorAttachment.format = PixelFormat::RGBA8_UNORM;
                colorAttachment.samples = 1;
                colorAttachment.loadOp = true;  // 清除
                colorAttachment.storeOp = true; // 存储
                colorAttachment.initialLayout = ResourceState::Undefined;
                colorAttachment.finalLayout = ResourceState::Present;
                colorAttachment.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};

                renderPassInfo.attachments.push_back(colorAttachment);

                // 子通道
                SubpassDescription subpass;
                AttachmentReference colorRef;
                colorRef.attachment = 0;
                colorRef.layout = ResourceState::RenderTarget;
                subpass.colorAttachments.push_back(colorRef);

                renderPassInfo.subpasses.push_back(subpass);
                renderPassInfo.name = "TestRenderPass";

                // 创建渲染通道
                m_renderPass = std::make_shared<VulkanRenderPass>(m_device);
                if (!m_renderPass->Initialize(renderPassInfo))
                {
                    std::cerr << "VulkanPipelineExample: 渲染通道初始化失败" << std::endl;
                    return false;
                }

                std::cout << "VulkanPipelineExample: 测试渲染通道创建成功" << std::endl;
                return true;
            }

            bool VulkanPipelineExample::CreateTestShaders()
            {
                std::cout << "VulkanPipelineExample: 创建测试着色器..." << std::endl;

                // 创建顶点着色器
                auto vertexSpirv = VulkanShaderUtils::GetTriangleVertexShaderSPIRV();
                if (!VulkanShaderUtils::ValidateSPIRVCode(vertexSpirv))
                {
                    std::cerr << "VulkanPipelineExample: 顶点着色器SPIR-V代码无效" << std::endl;
                    return false;
                }

                m_vertexShader = VulkanShader::CreateFromSPIRV(m_device, vertexSpirv, ShaderType::Vertex);
                if (!m_vertexShader)
                {
                    std::cerr << "VulkanPipelineExample: 顶点着色器创建失败" << std::endl;
                    return false;
                }

                // 创建片段着色器
                auto fragmentSpirv = VulkanShaderUtils::GetTriangleFragmentShaderSPIRV();
                if (!VulkanShaderUtils::ValidateSPIRVCode(fragmentSpirv))
                {
                    std::cerr << "VulkanPipelineExample: 片段着色器SPIR-V代码无效" << std::endl;
                    return false;
                }

                m_fragmentShader = VulkanShader::CreateFromSPIRV(m_device, fragmentSpirv, ShaderType::Fragment);
                if (!m_fragmentShader)
                {
                    std::cerr << "VulkanPipelineExample: 片段着色器创建失败" << std::endl;
                    return false;
                }

                std::cout << "VulkanPipelineExample: 测试着色器创建成功" << std::endl;
                return true;
            }

            bool VulkanPipelineExample::ValidatePipeline(std::shared_ptr<VulkanPipeline> pipeline, const std::string &pipelineName)
            {
                if (!pipeline)
                {
                    std::cerr << "VulkanPipelineExample: " << pipelineName << " 为空" << std::endl;
                    return false;
                }

                if (pipeline->GetVkPipeline() == VK_NULL_HANDLE)
                {
                    std::cerr << "VulkanPipelineExample: " << pipelineName << " Vulkan管线无效" << std::endl;
                    return false;
                }

                std::cout << "VulkanPipelineExample: " << pipelineName << " 验证成功" << std::endl;
                std::cout << "  类型: " << (pipeline->GetType() == PipelineType::Graphics ? "图形管线" : "计算管线") << std::endl;
                std::cout << "  子通道: " << pipeline->GetSubpass() << std::endl;

                return true;
            }

            // 全局示例函数，方便直接调用
            bool RunVulkanPipelineExample(VulkanDevice *device)
            {
                if (!device)
                {
                    std::cerr << "RunVulkanPipelineExample: 设备为空" << std::endl;
                    return false;
                }

                std::cout << "========== Vulkan管线示例开始 ==========" << std::endl;

                VulkanPipelineExample example(device);
                bool success = example.Initialize();

                if (success)
                {
                    std::cout << "VulkanPipelineExample: 示例运行成功！" << std::endl;

                    // 演示获取管线信息
                    auto graphicsPipeline = example.GetGraphicsPipeline();
                    auto pipelineLayout = example.GetPipelineLayout();

                    if (graphicsPipeline)
                    {
                        std::cout << "VulkanPipelineExample: 图形管线有效，类型: "
                                  << (graphicsPipeline->GetType() == PipelineType::Graphics ? "图形" : "计算") << std::endl;
                    }

                    if (pipelineLayout)
                    {
                        std::cout << "VulkanPipelineExample: 管线布局有效，描述符集数量: "
                                  << pipelineLayout->GetDescriptorSetLayoutCount() << std::endl;
                    }
                }
                else
                {
                    std::cerr << "VulkanPipelineExample: 示例运行失败" << std::endl;
                }

                std::cout << "========== Vulkan管线示例结束 ==========" << std::endl;
                return success;
            }

        } // namespace Vulkan
    } // namespace Graphics
} // namespace Orange

#endif // ORANGE_VULKAN_ENABLED