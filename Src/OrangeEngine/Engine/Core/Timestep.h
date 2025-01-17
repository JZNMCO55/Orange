#ifndef TIME_STEP_H
#define TIME_STEP_H

#include "OrangeExport.h"

namespace Orange
{
    class ORANGE_API Timestep
    {
    public:
        Timestep(float time = 0.0f)
            : mTime(time)
        {}

        operator float() const { return mTime; }

        float GetSeconds() const { return mTime; }
        float GetMilliseconds() const { return mTime * 1000.0f; }

    private:
        float mTime;
    };
}

#endif // TIME_STEP_H