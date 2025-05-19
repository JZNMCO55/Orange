#ifndef RANDOM_H
#define RANDOM_H

#include <cstdint>
#include <random>
#include <memory>

namespace Orange
{
    namespace Core
    {

        class Random
        {
        public:
            // Singleton instance
            static Random &GetInstance();

            // Delete copy constructor and assignment operator
            Random(const Random &) = delete;
            Random &operator=(const Random &) = delete;

            // Initialize with a specific seed
            void Initialize(uint64_t seed = 0);

            // Get a random integer in range [min, max]
            int32_t GetInt(int32_t min, int32_t max);

            // Get a random float in range [min, max]
            float GetFloat(float min, float max);

            // Get a random double in range [min, max]
            double GetDouble(double min, double max);

            // Get a random boolean
            bool GetBool();

            // Get a random number from normal distribution
            float GetNormal(float mean = 0.0f, float stddev = 1.0f);

            // Get a random number from uniform distribution
            float GetUniform(float min = 0.0f, float max = 1.0f);

            // Get current seed
            uint64_t GetSeed() const;

        private:
            Random() = default;
            ~Random() = default;

            std::mt19937_64 m_engine;
            uint64_t m_seed = 0;
        };

    } // namespace Core
} // namespace Orange

#endif // RANDOM_H