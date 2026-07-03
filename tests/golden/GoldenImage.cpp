#include "GoldenImage.h"

#include <cstdint>
#include <fstream>
#include <istream>

namespace Orange::Golden
{

    Image MakeRgbFromRgba(const uint8_t* pRgba, uint32_t w, uint32_t h)
    {
        Image img;
        img.mWidth                = w;
        img.mHeight               = h;
        const uint64_t pixelCount = static_cast<uint64_t>(w) * h;
        img.mRgb.resize(pixelCount * 3);
        if (pRgba == nullptr)
        {
            return img; // 防御：源指针为空时返回全零图像（尺寸仍正确）。
        }
        for (uint64_t i = 0; i < pixelCount; ++i)
        {
            // RGBA8 紧凑布局：每像素 4 字节，只取前 3 个通道。
            img.mRgb[i * 3 + 0] = pRgba[i * 4 + 0];
            img.mRgb[i * 3 + 1] = pRgba[i * 4 + 1];
            img.mRgb[i * 3 + 2] = pRgba[i * 4 + 2];
        }
        return img;
    }

    bool WritePpm(const std::string& path, const Image& img)
    {
        // 尺寸与缓冲一致性校验：避免写出截断 / 越界的 PPM。
        const uint64_t expected = static_cast<uint64_t>(img.mWidth) * img.mHeight * 3;
        if (img.mRgb.size() != expected)
        {
            return false;
        }

        // 用 ofstream 而非 fopen，避开 MSVC C4996（fopen 弃用告警被当 error）。
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            return false;
        }

        // P6 header："P6\n<w> <h>\n255\n"（纯 ASCII，二进制流照写无 CRLF 转换问题）。
        file << "P6\n"
             << img.mWidth << ' ' << img.mHeight << "\n255\n";
        if (!file)
        {
            return false;
        }

        if (!img.mRgb.empty())
        {
            file.write(reinterpret_cast<const char*>(img.mRgb.data()),
                       static_cast<std::streamsize>(img.mRgb.size()));
        }

        // 析构会 flush；返回前显式检查流状态（写失败 / flush 失败）。
        file.flush();
        return static_cast<bool>(file);
    }

    namespace
    {

        // 跳过 PPM header 里的空白与以 '#' 起始的注释行。读取下一个 token 之前调用。
        // 返回 false 表示提前 EOF / 读错误。
        bool SkipWhitespaceAndComments(std::istream& in)
        {
            for (;;)
            {
                const int c = in.get();
                if (c == std::char_traits<char>::eof())
                {
                    return false;
                }
                if (c == '#')
                {
                    // 注释行：吃到行尾。
                    int cc = c;
                    do
                    {
                        cc = in.get();
                    } while (cc != std::char_traits<char>::eof() && cc != '\n');
                    if (cc == std::char_traits<char>::eof())
                    {
                        return false;
                    }
                    continue;
                }
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                {
                    continue;
                }
                // 命中非空白非注释字符：回退一格供后续读取。
                in.unget();
                return true;
            }
        }

        // 读取一个无符号十进制整数 token。返回 false 表示解析失败。
        bool ReadUint(std::istream& in, uint32_t& out)
        {
            if (!SkipWhitespaceAndComments(in))
            {
                return false;
            }
            uint64_t value    = 0;
            bool     anyDigit = false;
            for (;;)
            {
                const int c = in.get();
                if (c == std::char_traits<char>::eof())
                {
                    break;
                }
                if (c < '0' || c > '9')
                {
                    in.unget();
                    break;
                }
                anyDigit = true;
                value    = value * 10 + static_cast<uint64_t>(c - '0');
                if (value > 0xFFFFFFFFull)
                {
                    return false; // 溢出 uint32_t：拒绝。
                }
            }
            if (!anyDigit)
            {
                return false;
            }
            out = static_cast<uint32_t>(value);
            return true;
        }

    } // namespace

    bool ReadPpm(const std::string& path, Image& out)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return false; // 文件不存在。
        }

        // magic 必须是 "P6"。
        const int m0 = file.get();
        const int m1 = file.get();
        if (m0 != 'P' || m1 != '6')
        {
            return false;
        }

        uint32_t w = 0, h = 0, maxVal = 0;
        if (!ReadUint(file, w) || !ReadUint(file, h) || !ReadUint(file, maxVal))
        {
            return false;
        }
        if (w == 0 || h == 0 || maxVal != 255)
        {
            return false; // 只支持 8-bit/通道（maxVal == 255）。
        }

        // maxVal 之后恰好一个空白分隔符（按 PPM 规范），之后即 raw 像素字节。
        const int sep = file.get();
        if (sep == std::char_traits<char>::eof())
        {
            return false;
        }

        const uint64_t       byteCount = static_cast<uint64_t>(w) * h * 3;
        std::vector<uint8_t> rgb(byteCount);
        if (byteCount > 0)
        {
            file.read(reinterpret_cast<char*>(rgb.data()),
                      static_cast<std::streamsize>(byteCount));
            if (static_cast<uint64_t>(file.gcount()) != byteCount)
            {
                return false; // 像素数据截断 / 尺寸头与实际字节不符。
            }
        }

        out.mWidth  = w;
        out.mHeight = h;
        out.mRgb    = std::move(rgb);
        return true;
    }

    CompareResult Compare(const Image& actual,
                          const Image& expected,
                          uint32_t     perChannelTol,
                          double       maxFailedPercent)
    {
        CompareResult result;

        if (actual.mWidth != expected.mWidth ||
            actual.mHeight != expected.mHeight ||
            actual.mRgb.size() != expected.mRgb.size())
        {
            result.mDimensionMismatch = true;
            result.mPassed            = false;
            return result;
        }

        const uint64_t pixelCount =
            static_cast<uint64_t>(actual.mWidth) * actual.mHeight;
        const int tol = static_cast<int>(perChannelTol);

        for (uint64_t p = 0; p < pixelCount; ++p)
        {
            bool pixelFailed = false;
            for (int ch = 0; ch < 3; ++ch)
            {
                const int a = static_cast<int>(actual.mRgb[p * 3 + ch]);
                const int e = static_cast<int>(expected.mRgb[p * 3 + ch]);
                int       d = a - e;
                if (d < 0)
                {
                    d = -d;
                }
                if (static_cast<uint32_t>(d) > result.mMaxChannelDiff)
                {
                    result.mMaxChannelDiff = static_cast<uint32_t>(d);
                }
                if (d > tol)
                {
                    pixelFailed = true;
                }
            }
            if (pixelFailed)
            {
                ++result.mFailedPixels;
            }
        }

        result.mFailedPercent =
            pixelCount > 0
                ? 100.0 * static_cast<double>(result.mFailedPixels) /
                      static_cast<double>(pixelCount)
                : 0.0;
        result.mPassed = (result.mFailedPercent <= maxFailedPercent);
        return result;
    }

    bool WriteDiffImage(const std::string& path,
                        const Image&       actual,
                        const Image&       expected,
                        uint32_t           perChannelTol)
    {
        if (actual.mWidth != expected.mWidth ||
            actual.mHeight != expected.mHeight ||
            actual.mRgb.size() != expected.mRgb.size())
        {
            return false; // 尺寸不一致无法逐像素生成 diff。
        }

        const uint64_t pixelCount =
            static_cast<uint64_t>(actual.mWidth) * actual.mHeight;
        const int tol = static_cast<int>(perChannelTol);

        Image diff;
        diff.mWidth  = actual.mWidth;
        diff.mHeight = actual.mHeight;
        diff.mRgb.resize(pixelCount * 3);

        for (uint64_t p = 0; p < pixelCount; ++p)
        {
            bool pixelFailed = false;
            for (int ch = 0; ch < 3; ++ch)
            {
                const int a = static_cast<int>(actual.mRgb[p * 3 + ch]);
                const int e = static_cast<int>(expected.mRgb[p * 3 + ch]);
                int       d = a - e;
                if (d < 0)
                {
                    d = -d;
                }
                if (d > tol)
                {
                    pixelFailed = true;
                }
            }
            if (pixelFailed)
            {
                // 超容差像素标红。
                diff.mRgb[p * 3 + 0] = 255;
                diff.mRgb[p * 3 + 1] = 0;
                diff.mRgb[p * 3 + 2] = 0;
            }
            else
            {
                // 背景：原图灰度 ×0.3 压暗，方便红色 diff 点凸显。
                const int r = actual.mRgb[p * 3 + 0];
                const int g = actual.mRgb[p * 3 + 1];
                const int b = actual.mRgb[p * 3 + 2];
                // Rec.601 亮度近似（整数权重 / 256）。
                const int     luma = (77 * r + 150 * g + 29 * b) >> 8;
                const uint8_t dim =
                    static_cast<uint8_t>(static_cast<double>(luma) * 0.3);
                diff.mRgb[p * 3 + 0] = dim;
                diff.mRgb[p * 3 + 1] = dim;
                diff.mRgb[p * 3 + 2] = dim;
            }
        }

        return WritePpm(path, diff);
    }

} // namespace Orange::Golden
