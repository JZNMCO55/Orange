#include <gtest/gtest.h>
#include "Layer1/Core/RandomNum/Random.h"

namespace Orange
{
    namespace Test
    {

        class RandomTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                // Initialize with a fixed seed for reproducibility
                Core::Random::GetInstance().Initialize(12345);
            }
        };

        TEST_F(RandomTest, IntRangeTest)
        {
            auto &random = Core::Random::GetInstance();

            // Test multiple times
            for (int i = 0; i < 100; ++i)
            {
                int value = random.GetInt(10, 20);
                EXPECT_GE(value, 10);
                EXPECT_LE(value, 20);
            }
        }

        TEST_F(RandomTest, FloatRangeTest)
        {
            auto &random = Core::Random::GetInstance();

            for (int i = 0; i < 100; ++i)
            {
                float value = random.GetFloat(0.5f, 1.5f);
                EXPECT_GE(value, 0.5f);
                EXPECT_LE(value, 1.5f);
            }
        }

        TEST_F(RandomTest, BoolTest)
        {
            auto &random = Core::Random::GetInstance();

            int trueCount = 0;
            for (int i = 0; i < 1000; ++i)
            {
                if (random.GetBool())
                {
                    trueCount++;
                }
            }

            // Check if distribution is roughly 50/50
            EXPECT_GT(trueCount, 400);
            EXPECT_LT(trueCount, 600);
        }

        TEST_F(RandomTest, NormalDistributionTest)
        {
            auto &random = Core::Random::GetInstance();

            float sum = 0.0f;
            int count = 10000;

            for (int i = 0; i < count; ++i)
            {
                sum += random.GetNormal(0.0f, 1.0f);
            }

            float mean = sum / count;
            // Check if mean is close to 0
            EXPECT_NEAR(mean, 0.0f, 0.1f);
        }

        TEST_F(RandomTest, SeedReproducibility)
        {
            // Test with same seed produces same sequence
            Core::Random::GetInstance().Initialize(42);
            int firstValue = Core::Random::GetInstance().GetInt(0, 100);

            Core::Random::GetInstance().Initialize(42);
            int secondValue = Core::Random::GetInstance().GetInt(0, 100);

            EXPECT_EQ(firstValue, secondValue);
        }

    } // namespace Test
} // namespace Orange
