#include "Random.h"
#include <chrono>

namespace Orange
{
    namespace Core
    {
        Random &Random::GetInstance()
        {
            static Random instance;
            return instance;
        }

        void Random::Initialize(uint64_t seed)
        {
            if (seed == 0)
            {
                // Use current time as seed if no seed is provided
                seed = static_cast<uint64_t>(
                    std::chrono::high_resolution_clock::now().time_since_epoch().count());
            }
            m_seed = seed;
            m_engine.seed(seed);
        }

        int32_t Random::GetInt(int32_t min, int32_t max)
        {
            std::uniform_int_distribution<int32_t> dist(min, max);
            return dist(m_engine);
        }

        float Random::GetFloat(float min, float max)
        {
            std::uniform_real_distribution<float> dist(min, max);
            return dist(m_engine);
        }

        double Random::GetDouble(double min, double max)
        {
            std::uniform_real_distribution<double> dist(min, max);
            return dist(m_engine);
        }

        bool Random::GetBool()
        {
            std::uniform_int_distribution<int32_t> dist(0, 1);
            return dist(m_engine) == 1;
        }

        float Random::GetNormal(float mean, float stddev)
        {
            std::normal_distribution<float> dist(mean, stddev);
            return dist(m_engine);
        }

        float Random::GetUniform(float min, float max)
        {
            std::uniform_real_distribution<float> dist(min, max);
            return dist(m_engine);
        }

        uint64_t Random::GetSeed() const
        {
            return m_seed;
        }

    } // namespace Core
} // namespace Orange