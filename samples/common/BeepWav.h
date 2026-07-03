#ifndef ORANGE_ENGINE_SAMPLES_COMMON_BEEP_WAV_H
#define ORANGE_ENGINE_SAMPLES_COMMON_BEEP_WAV_H

// 在内存里凑一个最简单的 16-bit PCM WAV 字节缓冲，方便 sample 在不
// 引入磁盘音频资源的前提下演示 AudioEngine。生成的字节直接灌进
// Asset::SoundAsset，miniaudio 内部走 ma_decoder_init_memory 解出来。

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace OrangeSamples
{

    // 写一个 little-endian uint32 / uint16 到字节流。
    inline void AppendU32LE(std::vector<std::uint8_t>& buf, std::uint32_t v)
    {
        buf.push_back(static_cast<std::uint8_t>(v & 0xFF));
        buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    }
    inline void AppendU16LE(std::vector<std::uint8_t>& buf, std::uint16_t v)
    {
        buf.push_back(static_cast<std::uint8_t>(v & 0xFF));
        buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    }
    inline void AppendBytes(std::vector<std::uint8_t>& buf, const char* s, std::size_t n)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            buf.push_back(static_cast<std::uint8_t>(s[i]));
        }
    }

    // 生成一个 mono 16-bit PCM 的"叮"声 —— 简单正弦 + 线性 fade-out 包络。
    //   * frequencyHz：基频，跳跃声用 880Hz 高一点更"清脆"；
    //   * durationMs： 100~200ms 量级；
    //   * sampleRate：44100；
    //   * volume：    0..1。
    inline std::vector<std::uint8_t> MakeBeepWav(float         frequencyHz = 880.0f,
                                                 int           durationMs  = 120,
                                                 std::uint32_t sampleRate  = 44100,
                                                 float         volume      = 0.5f)
    {
        const std::uint16_t channels      = 1;
        const std::uint16_t bitsPerSample = 16;
        const std::uint16_t blockAlign    = channels * (bitsPerSample / 8);
        const std::uint32_t byteRate      = sampleRate * blockAlign;
        const std::uint32_t numSamples =
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(sampleRate) *
                                       static_cast<std::uint64_t>(durationMs) / 1000ULL);
        const std::uint32_t dataSize     = numSamples * blockAlign;
        const std::uint32_t fmtChunkSize = 16;
        const std::uint32_t riffSize     = 4 + (8 + fmtChunkSize) + (8 + dataSize);

        std::vector<std::uint8_t> buf;
        buf.reserve(8 + riffSize);

        AppendBytes(buf, "RIFF", 4);
        AppendU32LE(buf, riffSize);
        AppendBytes(buf, "WAVE", 4);

        AppendBytes(buf, "fmt ", 4);
        AppendU32LE(buf, fmtChunkSize);
        AppendU16LE(buf, 1); // PCM
        AppendU16LE(buf, channels);
        AppendU32LE(buf, sampleRate);
        AppendU32LE(buf, byteRate);
        AppendU16LE(buf, blockAlign);
        AppendU16LE(buf, bitsPerSample);

        AppendBytes(buf, "data", 4);
        AppendU32LE(buf, dataSize);

        const float twoPi = 6.28318530717958647692f;
        for (std::uint32_t i = 0; i < numSamples; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
            // 线性 fade-out：开头满音量、结尾收到 0，避免"啪"的爆音。
            const float        envelope = 1.0f - static_cast<float>(i) / static_cast<float>(numSamples);
            const float        sample   = std::sin(t * twoPi * frequencyHz) * volume * envelope;
            const std::int16_t s        = static_cast<std::int16_t>(sample * 32767.0f);
            AppendU16LE(buf, static_cast<std::uint16_t>(s));
        }
        return buf;
    }

} // namespace OrangeSamples

#endif // ORANGE_ENGINE_SAMPLES_COMMON_BEEP_WAV_H
