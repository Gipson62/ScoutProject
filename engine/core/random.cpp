#include "engine/core/random.hpp"

#include <cstdint>
#include <limits>

#include "engine/core/panic.hpp"

namespace sg
{
namespace
{

constexpr std::uint64_t kGamma = 0x9E3779B97F4A7C15ULL;
constexpr std::uint64_t kMix1 = 0xBF58476D1CE4E5B9ULL;
constexpr std::uint64_t kMix2 = 0x94D049BB133111EBULL;

}  // namespace

auto Random::NextU64() -> std::uint64_t
{
  m_state += kGamma;
  std::uint64_t z = m_state;
  z = (z ^ (z >> 30U)) * kMix1;
  z = (z ^ (z >> 27U)) * kMix2;
  return z ^ (z >> 31U);
}

auto Random::NextBelow(std::uint64_t bound) -> std::uint64_t
{
  SG_ASSERT(bound > 0, "Random::NextBelow needs a bound above zero");

  // The largest multiple of `bound` that fits in 64 bits; anything at or above
  // it is thrown away, which costs on average well under one extra draw.
  const std::uint64_t limit =
      std::numeric_limits<std::uint64_t>::max() -
      (std::numeric_limits<std::uint64_t>::max() % bound);
  std::uint64_t draw = NextU64();
  while (draw >= limit)
  {
    draw = NextU64();
  }
  return draw % bound;
}

auto Random::NextInRange(std::int64_t low, std::int64_t high) -> std::int64_t
{
  SG_ASSERT(low <= high, "Random::NextInRange was given an empty range");

  // Computed on unsigned values, so a range that spans the whole int64 width
  // cannot overflow on the way.
  const std::uint64_t span =
      static_cast<std::uint64_t>(high) - static_cast<std::uint64_t>(low);
  if (span == std::numeric_limits<std::uint64_t>::max())
  {
    return static_cast<std::int64_t>(NextU64());
  }
  return static_cast<std::int64_t>(static_cast<std::uint64_t>(low) +
                                   NextBelow(span + 1));
}

auto Random::NextDouble() -> double
{
  constexpr double kScale = 1.0 / 9007199254740992.0;  // 2^-53
  return static_cast<double>(NextU64() >> 11U) * kScale;
}

auto Random::NextUniform(double low, double high) -> double
{
  return low + ((high - low) * NextDouble());
}

}  // namespace sg
