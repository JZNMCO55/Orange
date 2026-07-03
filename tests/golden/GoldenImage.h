#ifndef ORANGE_TESTS_GOLDEN_GOLDEN_IMAGE_H
#define ORANGE_TESTS_GOLDEN_GOLDEN_IMAGE_H

// Golden image 测试专用的 CPU 侧图像 codec + 比较 helper。
//
// 这一组工具只处理主存里的像素数组——**不 include 任何 Vulkan/volk 头**，与
// RHI/后端解耦，便于纯 CPU 单元测试直接调用。命名空间 Orange::Golden 仅供
// tests 使用，不进入公共库 include/。
//
// 图像统一以 RGB 三通道、行主序、每像素 3 字节存放（readback 的 RGBA8 由
// MakeRgbFromRgba 提取 RGB、丢弃 alpha）。磁盘格式用 PPM P6 binary：自包含、
// 无需第三方库、二进制可逐字节稳定 round-trip。
//
// 本组文件逐字搬自 OrangeRender/tests/golden/（零 Vulkan 依赖、自包含），供
// OrangeEngine 侧视觉回归复用，不改一行实现。

#include <cstdint>
#include <string>
#include <vector>

namespace Orange::Golden
{

    // 行主序 RGB8 图像。mRgb.size() 应等于 mWidth * mHeight * 3。
    struct Image
    {
        uint32_t             mWidth  = 0;
        uint32_t             mHeight = 0;
        std::vector<uint8_t> mRgb;
    };

    // 从 readback 得到的紧凑 RGBA8 缓冲提取 RGB（丢弃 alpha）。
    Image MakeRgbFromRgba(const uint8_t* pRgba, uint32_t w, uint32_t h);

    // 写出 PPM P6（header "P6\n<w> <h>\n255\n" + raw RGB 字节）。成功返回 true。
    bool WritePpm(const std::string& path, const Image& img);

    // 解析 PPM P6 到 out。文件不存在 / 格式或尺寸头不合法均返回 false。
    bool ReadPpm(const std::string& path, Image& out);

    // 逐像素逐通道比较结果。
    struct CompareResult
    {
        bool     mPassed            = false;
        uint64_t mFailedPixels      = 0;   // 任一通道超容差的像素数
        uint32_t mMaxChannelDiff    = 0;   // 全局最大单通道差
        double   mFailedPercent     = 0.0; // 100 * mFailedPixels / 总像素数
        bool     mDimensionMismatch = false;
    };

    // 比较 actual 与 expected。
    //   - 尺寸不符：mDimensionMismatch=true、mPassed=false 直接返回；
    //   - 否则逐像素逐通道算 abs(int(a)-int(e))，任一通道差 > perChannelTol 则该像素
    //     计入 mFailedPixels；mMaxChannelDiff 记录全局最大通道差；
    //   - mFailedPercent = 100 * mFailedPixels / 总像素数；
    //   - mPassed = (mFailedPercent <= maxFailedPercent)。
    CompareResult Compare(const Image& actual,
                          const Image& expected,
                          uint32_t     perChannelTol,
                          double       maxFailedPercent);

    // 写 diff 图：超容差像素标红 (255,0,0)，其余像素输出灰度（原亮度 ×0.3 压暗作
    // 背景）。尺寸不一致则跳过返回 false；成功写出 PPM 返回 true。
    bool WriteDiffImage(const std::string& path,
                        const Image&       actual,
                        const Image&       expected,
                        uint32_t           perChannelTol);

} // namespace Orange::Golden

#endif // ORANGE_TESTS_GOLDEN_GOLDEN_IMAGE_H
