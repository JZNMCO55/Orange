#ifndef ORANGE_TESTS_GOLDEN_GOLDEN_TEST_HARNESS_H
#define ORANGE_TESTS_GOLDEN_GOLDEN_TEST_HARNESS_H

// Golden image 测试 harness（header-only）。
//
// 调用方负责渲染 + readback 出紧凑 RGBA8 像素，把指针 + 尺寸 + caseName 交给
// RunGoldenComparison；harness 完成「提取 RGB → 读参考图 → 比对 / 更新」三种
// 模式，返回 EXIT_SUCCESS / EXIT_FAILURE 供测试 main 直接 return。
//
// 这一层只依赖 CPU 侧的 GoldenImage helper（不碰 Vulkan），保证渲染逻辑与比对
// 逻辑解耦。本文件逐字搬自 OrangeRender/tests/golden/，不改一行实现。

#include "GoldenImage.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace Orange::Golden
{

    // 解析后的命令行参数。argv[1]（goldenDir 位置参数）由调用方传入，不在此解析。
    struct GoldenArgs
    {
        bool        mUpdateGolden     = false;
        uint32_t    mTolerance        = 2;   // perChannelTol 默认值
        double      mMaxFailedPercent = 0.1; // 默认允许 0.1% 像素超容差
        std::string mDiffDir          = "."; // diff / actual 图输出目录
    };

    // 从 argv 解析 --update-golden / --tolerance <n> / --max-failed-percent <f> /
    // --diff-dir <path>。位置参数（argv[1] = goldenDir）由调用方处理，这里跳过任何
    // 不以 "--" 起始的 token。
    inline GoldenArgs ParseGoldenArgs(int argc, char** argv)
    {
        GoldenArgs args;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view a(argv[i]);
            if (a == "--update-golden")
            {
                args.mUpdateGolden = true;
            }
            else if (a == "--tolerance" && (i + 1) < argc)
            {
                args.mTolerance = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            }
            else if (a == "--max-failed-percent" && (i + 1) < argc)
            {
                args.mMaxFailedPercent = std::strtod(argv[++i], nullptr);
            }
            else if (a == "--diff-dir" && (i + 1) < argc)
            {
                args.mDiffDir = argv[++i];
            }
            // 其余 token（含位置参数 goldenDir）忽略。
        }
        return args;
    }

    // 运行一次 golden 比对。返回 EXIT_SUCCESS / EXIT_FAILURE。
    //   pRgbaPixels / w / h —— 调用方 readback 出的紧凑 RGBA8 像素；
    //   caseName            —— 用例名，参考图 = goldenDir/<caseName>.ppm；
    //   goldenDir           —— 参考图所在源码目录（ctest 注册时作为 argv[1] 传入）；
    //   argc / argv         —— 透传给 ParseGoldenArgs。
    inline int RunGoldenComparison(const uint8_t*     pRgbaPixels,
                                   uint32_t           w,
                                   uint32_t           h,
                                   const std::string& caseName,
                                   const std::string& goldenDir,
                                   int                argc,
                                   char**             argv)
    {
        const GoldenArgs args = ParseGoldenArgs(argc, argv);

        const Image actual = MakeRgbFromRgba(pRgbaPixels, w, h);

        const std::string refPath  = goldenDir + "/" + caseName + ".ppm";
        const std::string diffPath = args.mDiffDir + "/" + caseName + "_diff.ppm";
        const std::string actPath  = args.mDiffDir + "/" + caseName + "_actual.ppm";

        // ---- 更新模式：把当前渲染写成参考图 ------------------------------------
        if (args.mUpdateGolden)
        {
            if (!WritePpm(refPath, actual))
            {
                std::fprintf(stderr, "[golden] FAILED to write reference: %s\n",
                             refPath.c_str());
                return EXIT_FAILURE;
            }
            std::fprintf(stdout, "[golden] updated reference: %s\n", refPath.c_str());
            return EXIT_SUCCESS;
        }

        // ---- 比对模式 -----------------------------------------------------------
        Image expected;
        if (!ReadPpm(refPath, expected))
        {
            // 参考图缺失：不静默生成（坏图不能被默认接受），写出 actual 供查看。
            std::fprintf(stderr,
                         "[golden] MISSING reference: %s (run test with "
                         "--update-golden to create it)\n",
                         refPath.c_str());
            WritePpm(actPath, actual);
            return EXIT_FAILURE;
        }

        const CompareResult cmp =
            Compare(actual, expected, args.mTolerance, args.mMaxFailedPercent);

        if (!cmp.mPassed)
        {
            const uint64_t totalPixels = static_cast<uint64_t>(w) * h;
            std::fprintf(stderr,
                         "[golden] FAIL %s: failedPixels=%llu/%llu failedPercent=%.4f%% "
                         "maxChannelDiff=%u dimensionMismatch=%d\n",
                         caseName.c_str(),
                         static_cast<unsigned long long>(cmp.mFailedPixels),
                         static_cast<unsigned long long>(totalPixels),
                         cmp.mFailedPercent,
                         cmp.mMaxChannelDiff,
                         cmp.mDimensionMismatch ? 1 : 0);
            WritePpm(actPath, actual);
            WriteDiffImage(diffPath, actual, expected, args.mTolerance);
            std::fprintf(stderr, "[golden] wrote actual=%s diff=%s\n",
                         actPath.c_str(), diffPath.c_str());
            return EXIT_FAILURE;
        }

        std::fprintf(stdout, "[golden] PASS %s (maxDiff=%u, failed=%.4f%%)\n",
                     caseName.c_str(), cmp.mMaxChannelDiff, cmp.mFailedPercent);
        return EXIT_SUCCESS;
    }

} // namespace Orange::Golden

#endif // ORANGE_TESTS_GOLDEN_GOLDEN_TEST_HARNESS_H
