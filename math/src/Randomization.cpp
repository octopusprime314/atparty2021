#include "Randomization.h"
#include <chrono>
#include <random>

namespace
{
std::mt19937_64 _rng;
}

namespace Randomization
{
void seed()
{
    uint64_t      timeSeed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::seed_seq ss{uint32_t(timeSeed & 0xffffffff), uint32_t(timeSeed >> 32)};
    _rng.seed(ss);
}

Vector4 pointInSphere()
{
    std::uniform_real_distribution<double> unif{-1, 1};
    float                                  d, x, y, z;
    do
    {
        x = unif(_rng);
        y = unif(_rng);
        z = unif(_rng);
        d = x * x + y * y + z * z;
    } while (d > 1.0f);

    return Vector4(x, y, z, 0.0);
}
} // namespace Randomization