#ifndef ORANGE_ENGINE_SAMPLES_COMMON_CAPTURE_LAYER_H
#define ORANGE_ENGINE_SAMPLES_COMMON_CAPTURE_LAYER_H

// CaptureLayer —— 共享的"跑 N 帧 → 截图 → 退出"工具 layer。
// sample 用 `--capture <path> [--frames N]` CLI 启用：CI / 验收阶段免去
// 手按截图键。Pipeline::RequestCapture 在本帧 Render 时把 GPU 帧 readback
// 到 png；CaptureLayer 在请求帧后再多跑一帧再 RequestExit，给 capture
// 一个完整 Render 来落盘。
//
// 必须在 RenderLayer **之前** push——本 layer 的 OnUpdate 调
// Pipeline::RequestCapture 后，同帧的 RenderLayer 会触发实际 readback。

#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/render/Pipeline.h>

#include <filesystem>

namespace OrangeSamples
{

    class CaptureLayer final : public Orange::Engine::Layer
    {
    public:
        CaptureLayer(Orange::Engine::Render::Pipeline& pipeline,
                     Orange::Engine::AppHost&          host,
                     std::filesystem::path             outPath,
                     int                               captureFrame = 60)
            : Orange::Engine::Layer("CaptureLayer"), mpPipeline(&pipeline), mpHost(&host), mOutPath(std::move(outPath)), mCaptureFrame(captureFrame)
        {
        }

        void OnUpdate(const Orange::Engine::FrameContext& /*frame*/) override
        {
            ++mCurrentFrame;
            if (mCurrentFrame == mCaptureFrame)
            {
                mpPipeline->RequestCapture(mOutPath);
            }
            // 多等几帧让 capture 真正落盘——Pipeline 内部 readback + stb_image_write
            // 编 png 在 ~30ms 量级，给 60Hz 多 2 帧足够。
            else if (mCurrentFrame == mCaptureFrame + 3)
            {
                mpHost->RequestExit();
            }
        }

    private:
        Orange::Engine::Render::Pipeline* mpPipeline{nullptr};
        Orange::Engine::AppHost*          mpHost{nullptr};
        std::filesystem::path             mOutPath;
        int                               mCaptureFrame{60};
        int                               mCurrentFrame{0};
    };

    // CLI helper：从 argv 解析 capture 相关 flag。
    //   --capture <path>     截图落点；空字符串 = 不截图（交互模式）；
    //   --frames N           截图发生在第 N 帧（默认 60）；
    //   --scenario <name>    脚本化场景：idle / walk / jump。具体含义由
    //                        sample 端解读——CaptureLayer 自身不消费 scenario，
    //                        只把它存下来给 sample 内部的 ScenarioLayer 用。
    struct CaptureCliOptions
    {
        std::filesystem::path outPath;
        int                   captureFrame{60};
        std::string           scenario;
    };

    inline CaptureCliOptions ParseCaptureCli(int argc, char** argv)
    {
        CaptureCliOptions opts;
        for (int i = 1; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--capture" && i + 1 < argc)
            {
                opts.outPath = argv[++i];
            }
            else if (a == "--frames" && i + 1 < argc)
            {
                try
                {
                    opts.captureFrame = std::stoi(argv[++i]);
                }
                catch (...)
                {
                    opts.captureFrame = 60;
                }
            }
            else if (a == "--scenario" && i + 1 < argc)
            {
                opts.scenario = argv[++i];
            }
        }
        return opts;
    }

} // namespace OrangeSamples

#endif // ORANGE_ENGINE_SAMPLES_COMMON_CAPTURE_LAYER_H
