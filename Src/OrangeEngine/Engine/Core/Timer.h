#ifndef TIMER_H
#define TIMER_H

#include "OrangeExport.h"
#include <chrono>

namespace Orange
{
    class ORANGE_API Timer
    {
    public:
        Timer() { Reset(); }

        void Timer::Reset()
        {
            mStart = std::chrono::high_resolution_clock::now();
        }

        float Timer::Elapsed()
        {
            return  std::chrono::duration_cast<std::chrono::nanoseconds>
                (std::chrono::high_resolution_clock::now() - mStart).count() * 0.001f * 0.001f * 0.001f;
        }

        float Timer::EplapsedMillis()
        {
            return Elapsed() * 1000.0f;
        }

    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> mStart;
    };
}

#endif // TIMER_H